#include <cerrno>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include "vpp/bytecode/intrinsic.h"
#include "vpp/runtime/directory_machine.h"
#include "vpp/runtime/error.h"
#include "vpp/native/aot_runtime.h"

using namespace vietvm;
namespace fs = std::filesystem;

namespace {
int item(const StackValue &value, std::size_t index) {
    return std::get<int>(std::get<ListHandle>(value)->elements.at(index));
}
std::string name(const StackValue &value) {
    return std::get<std::string>(std::get<ListHandle>(value)->elements.at(1));
}
bool rejects(Opcode opcode, std::vector<StackValue> args,
             const std::unordered_set<std::string> &cap,
             runtime::DirectoryMachineState &state) {
    try { (void)runtime::executeDirectoryMachinePrimitive(opcode, args, cap, state); }
    catch (const runtime::RuntimeError &) { return true; }
    return false;
}
} // namespace

int main() {
    int failures = 0;
    const auto check = [&](bool ok, const char *reason) {
        if (!ok) { std::cerr << reason << '\n'; ++failures; }
    };
    const std::unordered_set<std::string> cap{
        "machine.dir.mkdir", "machine.dir.remove", "machine.dir.open",
        "machine.dir.next", "machine.dir.close"};
    runtime::DirectoryMachineState state, other;
    const auto call = [&](Opcode op, std::vector<StackValue> args) {
        return runtime::executeDirectoryMachinePrimitive(op, args, cap, state);
    };
    for (int opcode = OP_VM_MACHINE_DIR_MKDIR; opcode <= OP_VM_MACHINE_DIR_CLOSE; ++opcode) {
        const auto *descriptor = bytecode::intrinsicByOpcode(opcode);
        check(runtime::isDirectoryMachinePrimitive(opcode) && descriptor && descriptor->arity == 1,
              "directory descriptor missing");
        check(rejects(static_cast<Opcode>(opcode), {"."}, {}, state), "capability bypass");
        check(rejects(static_cast<Opcode>(opcode), {"."}, {"system.fs.dir.open"}, state),
              "capability isolation failed");
        check(rejects(static_cast<Opcode>(opcode), {}, cap, state), "missing argument accepted");
        check(rejects(static_cast<Opcode>(opcode), {".", "."}, cap, state), "extra argument accepted");
    }
    check(!runtime::isDirectoryMachinePrimitive(OP_VM_MACHINE_TIME_LOCAL), "opcode collision");
    check(rejects(OP_VM_MACHINE_DIR_MKDIR, {42}, cap, state), "non-string path accepted");
    check(rejects(OP_VM_MACHINE_DIR_OPEN, {""}, cap, state), "empty path accepted");
    check(rejects(OP_VM_MACHINE_DIR_REMOVE, {std::string("abc\0def", 7)}, cap, state),
          "NUL path accepted");
    check(rejects(OP_VM_MACHINE_DIR_OPEN, {std::string("\xff", 1)}, cap, state),
          "invalid UTF-8 path accepted");

    const fs::path base = fs::temp_directory_path() /
        ("vpp-machine-directory-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const std::string root = base.u8string();
    // On Windows, constructing fs::path from narrow UTF-8 bytes can use the
    // active ANSI code page. u8path preserves the intended Unicode filename.
    const std::string child = (base / fs::u8path(u8"cà phê")).u8string();
    try {
        check(item(call(OP_VM_MACHINE_DIR_MKDIR, {root}), 0) == 0, "mkdir failed");
        auto duplicate = call(OP_VM_MACHINE_DIR_MKDIR, {root});
        check(item(duplicate, 0) == -1 && item(duplicate, 1) == 1,
              "EEXIST normalization failed");
        check(item(call(OP_VM_MACHINE_DIR_MKDIR, {child}), 0) == 0, "Unicode mkdir failed");
        auto missing = call(OP_VM_MACHINE_DIR_REMOVE, {(base / "missing").u8string()});
        check(item(missing, 0) == -1 && item(missing, 1) == 2,
              "ENOENT normalization failed");
        auto open = call(OP_VM_MACHINE_DIR_OPEN, {root});
        const int token = item(open, 0);
        check(token > 0 && item(open, 1) == 0, "directory open failed");
        check(rejects(OP_VM_MACHINE_DIR_NEXT, {token}, cap, other), "cross-VM token accepted");
        check(rejects(OP_VM_MACHINE_DIR_NEXT, {0}, cap, state), "invalid token accepted");
        std::set<std::string> entries;
        for (int i = 0; i < 128; ++i) {
            const auto next = call(OP_VM_MACHINE_DIR_NEXT, {token});
            if (item(next, 0) == 0) break;
            check(item(next, 0) == 1, "directory iteration failed");
            if (item(next, 0) == 1) entries.insert(name(next));
        }
        check(entries.count(u8"cà phê") == 1, "Unicode directory entry missing");
        check(item(call(OP_VM_MACHINE_DIR_NEXT, {token}), 0) == 0, "EOF not stable");
        check(item(call(OP_VM_MACHINE_DIR_CLOSE, {token}), 0) == 0, "directory close failed");
        check(rejects(OP_VM_MACHINE_DIR_NEXT, {token}, cap, state), "closed token accepted");
        check(rejects(OP_VM_MACHINE_DIR_CLOSE, {token}, cap, state), "double close accepted");
        auto noSuch = call(OP_VM_MACHINE_DIR_OPEN, {(base / "missing").u8string()});
        check(item(noSuch, 0) == 0 && item(noSuch, 1) == ENOENT,
              "missing directory open status wrong");
        auto handle = call(OP_VM_MACHINE_DIR_OPEN, {root});
        check(item(handle, 0) > 0, "second directory open failed");
        // VM termination must close outstanding native directory handles.
        state.closeAll();
        check(rejects(OP_VM_MACHINE_DIR_NEXT, {item(handle, 0)}, cap, state),
              "VM shutdown retained handle");
        check(item(call(OP_VM_MACHINE_DIR_REMOVE, {child}), 0) == 0, "rmdir child failed");
        // Removing a symlink to a directory must never remove the target.
        const fs::path target = base / "target";
        const fs::path link = base / "link";
        fs::create_directory(target);
        fs::create_directory_symlink(target, link);
        check(item(call(OP_VM_MACHINE_DIR_REMOVE, {link.u8string()}), 0) == 0,
              "directory symlink unlink failed");
        check(fs::is_directory(target) && !fs::exists(link), "symlink target damaged");
        fs::remove(target);
        check(item(call(OP_VM_MACHINE_DIR_REMOVE, {root}), 0) == 0, "rmdir root failed");

        native::aot::Program program;
        program.capabilities = cap;
        native::aot::Frame frame(program);
        frame.push(root);
        frame.intrinsic(OP_VM_MACHINE_DIR_MKDIR);
        check(item(frame.pop(), 0) == 0, "Native AOT mkdir failed");
        frame.push(root);
        frame.intrinsic(OP_VM_MACHINE_DIR_OPEN);
        auto aotHandle = frame.pop();
        check(item(aotHandle, 0) > 0, "Native AOT directory open failed");
        frame.push(item(aotHandle, 0));
        frame.intrinsic(OP_VM_MACHINE_DIR_CLOSE);
        check(item(frame.pop(), 0) == 0, "Native AOT directory close failed");
        program.capabilities = {};
        bool denied = false;
        try { frame.push(root); frame.intrinsic(OP_VM_MACHINE_DIR_REMOVE); }
        catch (const runtime::RuntimeError &) { denied = true; }
        check(denied, "Native AOT bypassed directory capability");
        check(item(call(OP_VM_MACHINE_DIR_REMOVE, {root}), 0) == 0, "AOT cleanup failed");
    } catch (const std::exception &e) {
        std::cerr << "directory hardening exception: " << e.what() << '\n';
        ++failures;
    }
    std::error_code ignored;
    fs::remove_all(base, ignored);
    if (failures == 0) std::cout << "machine directory hardening passed\n";
    return failures == 0 ? 0 : 1;
}

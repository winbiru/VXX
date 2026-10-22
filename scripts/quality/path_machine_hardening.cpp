#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>
#include "vpp/bytecode/intrinsic.h"
#include "vpp/runtime/path_machine.h"
#include "vpp/runtime/error.h"
#include "vpp/native/aot_runtime.h"

using namespace vietvm;
namespace fs = std::filesystem;

namespace {
bool rejects(Opcode op, std::vector<StackValue> args,
             const std::unordered_set<std::string> &cap) {
    try { (void)runtime::executePathMachinePrimitive(op, args, cap); }
    catch (const runtime::RuntimeError &) { return true; }
    return false;
}
} // namespace

int main() {
    int failures = 0;
    const auto check = [&](bool ok, const char *label) {
        if (!ok) { std::cerr << label << '\n'; ++failures; }
    };
    const std::unordered_set<std::string> cap{"machine.path.kind"};
    const auto kind = [&](const std::string &path, int follow) {
        return std::get<int>(runtime::executePathMachinePrimitive(
            OP_VM_MACHINE_PATH_KIND, {path, follow}, cap));
    };
    const auto *descriptor = bytecode::intrinsicByOpcode(OP_VM_MACHINE_PATH_KIND);
    check(runtime::isPathMachinePrimitive(OP_VM_MACHINE_PATH_KIND) &&
          descriptor && descriptor->arity == 2 && descriptor->name == "máy_đường_loại",
          "path intrinsic descriptor mismatch");
    check(!runtime::isPathMachinePrimitive(OP_VM_MACHINE_DIR_OPEN), "path opcode collision");
    check(rejects(OP_VM_MACHINE_PATH_KIND, {".", 1}, {}), "capability bypass");
    check(rejects(OP_VM_MACHINE_PATH_KIND, {".", 1}, {"system.fs.stat"}),
          "capability isolation failed");
    check(rejects(OP_VM_MACHINE_PATH_KIND, {"."}, cap), "missing argument accepted");
    check(rejects(OP_VM_MACHINE_PATH_KIND, {".", 1, 1}, cap), "extra argument accepted");
    check(rejects(OP_VM_MACHINE_PATH_KIND, {13, 1}, cap), "non-string accepted");
    check(rejects(OP_VM_MACHINE_PATH_KIND, {".", "true"}, cap), "non-i32 mode accepted");
    check(rejects(OP_VM_MACHINE_PATH_KIND, {".", 2}, cap), "invalid mode accepted");
    check(rejects(OP_VM_MACHINE_PATH_KIND, {std::string("bad\0path", 8), 1}, cap),
          "NUL path accepted");
    check(rejects(OP_VM_MACHINE_PATH_KIND, {std::string("\xff", 1), 1}, cap),
          "invalid UTF-8 accepted");
    check(kind("", 1) == -1, "empty path should be absent");

    const fs::path base = fs::temp_directory_path() /
        ("vpp-machine-path-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directory(base);
        const fs::path file = base / u8"tệp Việt.txt";
        const fs::path link = base / "link";
        { std::ofstream out(file); out << "data"; }
        std::error_code linkError;
        fs::create_symlink(file, link, linkError);
        check(kind((base / "missing").u8string(), 1) == -1, "missing path not rejected");
#if defined(_WIN32)
        constexpr int regular = 1, directory = 2, symbolic = 0;
#else
        constexpr int regular = 8, directory = 4, symbolic = 10;
#endif
        check(kind(base.u8string(), 1) == directory, "directory kind wrong");
        check(kind(file.u8string(), 1) == regular, "Unicode file kind wrong");
        if (!linkError) {
            check(kind(link.u8string(), 1) == regular, "follow symlink kind wrong");
            check(kind(link.u8string(), 0) == symbolic, "no-follow symlink kind wrong");
            fs::remove(file);
            check(kind(link.u8string(), 1) == -1,
                  "dangling symlink should be absent when following");
            check(kind(link.u8string(), 0) == symbolic,
                  "dangling symlink should remain symbolic when not following");
        } else {
#if !defined(_WIN32)
            check(false, "POSIX symlink creation failed");
#endif
        }
        native::aot::Program program;
        program.capabilities = cap;
        native::aot::Frame frame(program);
        frame.push(base.u8string()); frame.push(1);
        frame.intrinsic(OP_VM_MACHINE_PATH_KIND);
        check(std::get<int>(frame.pop()) == directory, "Native AOT path kind wrong");
        program.capabilities.clear();
        bool denied = false;
        try { frame.push(base.u8string()); frame.push(1); frame.intrinsic(OP_VM_MACHINE_PATH_KIND); }
        catch (const runtime::RuntimeError &) { denied = true; }
        check(denied, "Native AOT bypassed path capability");
    } catch (const std::exception &e) {
        std::cerr << "path hardening exception: " << e.what() << '\n'; ++failures;
    }
    std::error_code ignored;
    fs::remove_all(base, ignored);
    if (failures == 0) std::cout << "machine path hardening passed\n";
    return failures == 0 ? 0 : 1;
}

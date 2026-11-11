#include <chrono>
#include <cerrno>
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
    const auto metadata = [&](const std::string &path) {
        const auto value = runtime::executePathMachinePrimitive(
            OP_VM_MACHINE_PATH_STAT, {path}, cap);
        std::vector<std::string> fields;
        for (const auto &item : std::get<ListHandle>(value)->elements)
            fields.push_back(std::get<std::string>(item));
        return fields;
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
    check(rejects(OP_VM_MACHINE_PATH_CURRENT, {"unexpected"}, cap), "cwd arity bypass");
    check(rejects(OP_VM_MACHINE_PATH_CANONICAL, {std::string("bad\0path", 8)}, cap),
          "canonical path accepted NUL");
    check(rejects(OP_VM_MACHINE_PATH_SET_CURRENT, {"."}, cap), "chdir capability bypass");
    const auto query = [&](Opcode op, std::vector<StackValue> args) {
        const auto value = runtime::executePathMachinePrimitive(op, args, cap);
        const auto &fields = std::get<ListHandle>(value)->elements;
        if (fields.size() != 2 || std::get<std::string>(fields[0]) != "ok")
            throw std::runtime_error("unexpected path query error");
        return fields[1];
    };
    check(fs::path(std::get<std::string>(query(OP_VM_MACHINE_PATH_CURRENT, {}))).is_absolute(),
          "cwd must be absolute");
    check(fs::is_directory(fs::u8path(std::get<std::string>(query(OP_VM_MACHINE_PATH_TEMP, {})))),
          "temporary root must exist");

    const fs::path base = fs::temp_directory_path() /
        ("vpp-machine-path-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directory(base);
        const fs::path file = base / u8"tệp Việt.txt";
        const fs::path link = base / "link";
        { std::ofstream out(file); out << "data"; }
        const auto existingMetadata = metadata(file.u8string());
        check(existingMetadata.size() == 4 && existingMetadata[0] == "present" &&
              existingMetadata[1] == "4", "existing file metadata incorrect");
        check(metadata(base.u8string()) == std::vector<std::string>{"missing"},
              "directory must not be reported as a regular file");
        const auto absent = (base / "not-created").u8string();
        const auto absentChild = (base / "not-created" / "leaf").u8string();
#if !defined(_WIN32)
        // Raw Linux syscalls do not set errno, unlike libc stat/lstat. Force
        // a conflicting errno so a missing path cannot accidentally inherit it.
        errno = EINVAL;
#endif
        check(metadata(absent) == std::vector<std::string>{"missing"},
              "missing file inherited a stale OS error");
#if !defined(_WIN32)
        errno = EINVAL;
#endif
        check(metadata(absentChild) == std::vector<std::string>{"missing"},
              "missing parent inherited a stale OS error");
#if defined(_WIN32)
        // Win32 rejects '?' in a filename with an OS error. Excessive path
        // lengths can instead surface as PATH_NOT_FOUND under Win32.
        const auto invalidName = metadata((base / "bad?name").u8string());
        check(invalidName.size() == 2 && invalidName[0] == "error",
              "invalid Windows filename must preserve actual OS error");
#else
        const auto tooLong = (base / std::string(300, 'x')).u8string();
        errno = ENOENT;
        check(metadata(tooLong) == std::vector<std::string>{"error", std::to_string(ENAMETOOLONG)},
              "overlong path must preserve actual OS error");
#endif
        check(std::get<std::string>(query(OP_VM_MACHINE_PATH_ABSOLUTE, {"."})) == fs::absolute(".").u8string(),
              "absolute path mismatch");
        const auto missingSuffix = base / "not-created" / "leaf";
        check(std::get<std::string>(query(OP_VM_MACHINE_PATH_CANONICAL, {missingSuffix.u8string()})) ==
                  fs::weakly_canonical(missingSuffix).u8string(), "canonical missing suffix mismatch");
        const auto hardLink = base / "hard link";
        fs::create_hard_link(file, hardLink);
        check(std::get<int>(query(OP_VM_MACHINE_PATH_EQUIVALENT, {file.u8string(), hardLink.u8string()})) == 1,
              "hard-link identity was lost");
#if !defined(_WIN32)
        fs::permissions(file, fs::perms::owner_read | fs::perms::owner_write);
        check(std::get<int>(query(OP_VM_MACHINE_PATH_EXECUTABLE, {file.u8string()})) == 0,
              "non-executable file accepted");
        fs::permissions(file, fs::perms::owner_exec, fs::perm_options::add);
#endif
        check(std::get<int>(query(OP_VM_MACHINE_PATH_EXECUTABLE, {file.u8string()})) == 1,
              "executable permission observation failed");
        const auto original = fs::current_path();
        auto changing = cap;
        changing.insert("machine.path.chdir");
        const auto moved = runtime::executePathMachinePrimitive(OP_VM_MACHINE_PATH_SET_CURRENT,
            {base.u8string()}, changing);
        const bool correctCwd = fs::equivalent(fs::current_path(), base);
        fs::current_path(original);
        check(std::get<std::string>(std::get<ListHandle>(moved)->elements[0]) == "ok" && correctCwd,
              "chdir did not change cwd");
        const auto missingCwd = runtime::executePathMachinePrimitive(OP_VM_MACHINE_PATH_SET_CURRENT,
            {missingSuffix.u8string()}, changing);
        check(std::get<std::string>(std::get<ListHandle>(missingCwd)->elements[0]) == "error" &&
                  fs::current_path() == original, "failed chdir changed cwd or lost OS error");
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
        frame.intrinsic(OP_VM_MACHINE_PATH_TEMP);
        check(std::get<std::string>(std::get<ListHandle>(frame.pop())->elements[0]) == "ok",
              "Native AOT temporary path query failed");
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

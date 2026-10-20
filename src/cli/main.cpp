#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>
#include <optional>
#include <regex>
#include <set>
#include <unordered_map>
#include <filesystem>
#include <cstdlib>
#include <atomic>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <climits>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#endif
#include "vm/vm.h"
#include "frontend/keywords.h"
#include "frontend/lexer.h"
#include "vpp/compiler/pipeline.h"
#include "vpp/native/target.h"
#include "vpp/native/raw_codegen.h"
#include "vpp/native/vpp_machine_bridge.h"
#include "vpp/native/aot.h"
#include "vpp/runtime/error.h"
#include "vpp/tooling/tooling.h"
#include "vpp/core/message_constants.h"
#include "vpp/core/cli_constants.h"
#include "vpp/core/package_cache.h"
#include "vpp/core/package_installer.h"
#include "vpp/core/package_manifest.h"
#include "vpp/core/package_source.h"
#include "vpp/core/package_solver.h"
#include "vpp/core/project_layout.h"
#include "vpp/core/semver.h"
#include "vpp/core/text.h"

namespace fs = std::filesystem;
namespace messages = vietvm::messages;

// Giữ lại thư mục làm việc hiện tại theo RAII; constructor chụp `current_path()` và destructor tự khôi phục khi rời scope để lệnh CLI tạm đổi thư mục không làm ảnh hưởng caller.
class RuntimeCwdGuard {
public:
    // Ghi nhớ thư mục làm việc tại thời điểm guard được tạo; giá trị này sẽ được dùng để khôi phục ở destructor.
    RuntimeCwdGuard() : saved_(fs::current_path()) {}
    // Khôi phục thư mục làm việc đã lưu khi guard hết vòng đời; lỗi phục hồi được nuốt để destructor không ném exception trong quá trình unwind.
    ~RuntimeCwdGuard() {
        try {
            fs::current_path(saved_);
        } catch (...) {
        }
    }

    // Cấm sao chép guard để hai object không cùng cố khôi phục một trạng thái thư mục đã chụp ở thời điểm khác nhau.
    RuntimeCwdGuard(const RuntimeCwdGuard &) = delete;
    // Cấm phép gán để ownership của trạng thái thư mục đã lưu luôn gắn với đúng một guard.
    RuntimeCwdGuard &operator=(const RuntimeCwdGuard &) = delete;

private:
    fs::path saved_;
};

// In lỗi thông báo; hàm chuyển dữ liệu thành chuỗi và gửi tới luồng đầu ra theo định dạng quy định.
static void printErrorMessage(std::string_view fallback,
                              const std::string &detail) {
    std::cerr << messages::formatMessage(fallback, {detail}) << std::endl;
}

// Nối output sink của VM với stdout/collector của CLI; mọi opcode `in` sau đó đi qua callback này thay vì ghi trực tiếp trong VM.
static void connectVmOutput(VM &vm) {
    vm.setOutputSink([](const std::string &text) { std::cout << text; });
}

// Đọc tệp; hàm lấy nội dung từ nguồn tương ứng, kiểm tra lỗi cần thiết rồi trả dữ liệu đã đọc.
std::string readFile(const std::string &filename) {
    std::ifstream fileStream(vietvm::core::utf8Path(filename));
    if (!fileStream.is_open()) {
        throw std::runtime_error(
            messages::formatMessage(messages::kCliFileOpenFailed, {filename}));
    }
    std::stringstream buffer;
    buffer << fileStream.rdbuf();
    return buffer.str();
}

// In usage; hàm chuyển dữ liệu thành chuỗi và gửi tới luồng đầu ra theo định dạng quy định.
static void printUsage() {
    std::cout << messages::messageText(messages::kCliUsage);
}

// In version; hàm chuyển dữ liệu thành chuỗi và gửi tới luồng đầu ra theo định dạng quy định.
static void printVersion() {
    std::cout << messages::messageText(messages::kCliVersion,
                                       {vietvm::core::kCliVersion});
}

// Raw i32 function bodies now have encoders for all registered CPU families.
// This is NOT an ELF/Mach-O/COFF writer or a native executable builder.
static void printTargets() {
    const auto host = vietvm::native::hostTarget();
    std::cout << "Host: " << vietvm::native::canonicalTriple(host) << '\n';
    std::cout << "Target registry (raw i32 leaf codegen; AOT self-host thử nghiệm "
                 "cho x86_64/AArch64 trên host):\n";
    for (const auto &arch : vietvm::native::registeredArchitectures()) {
        const auto parsed = vietvm::native::parseTarget(std::string(arch.name) + "-linux-gnu");
        if (!parsed.target) continue;
        std::cout << "  " << vietvm::native::canonicalTriple(*parsed.target)
                  << " [" << arch.bits << "-bit, "
                  << (arch.littleEndian ? "little" : "big") << "-endian, "
                  << vietvm::native::objectFormatName(parsed.target->objectFormat) << "] raw i32 leaf"
                  << (vietvm::native::hasExperimentalMachineJit(*parsed.target)
                          ? ", JIT thử nghiệm (host-only)" : "") << '\n';
    }
    for (const char *triple : {"aarch64-apple-darwin", "x86_64-apple-darwin",
                               "x86_64-pc-windows-msvc", "aarch64-pc-windows-msvc",
                               "i386-pc-windows-msvc"}) {
        const auto parsed = vietvm::native::parseTarget(triple);
        if (!parsed.target) continue;
        std::cout << "  " << vietvm::native::canonicalTriple(*parsed.target)
                  << " [" << vietvm::native::objectFormatName(parsed.target->objectFormat) << "] raw i32 leaf"
                  << (vietvm::native::hasExperimentalMachineJit(*parsed.target)
                          ? ", JIT thử nghiệm (host-only)" : "") << '\n';
    }
}

enum class SnippetMode {
    Execute,
    Disassemble,
    DumpAst,
    DumpIr,
};

static void verifyLockedProjectForSource(const fs::path &sourcePath);

// Chạy snippet; hàm điều phối toàn bộ luồng xử lý của tác vụ, gọi các bước con theo thứ tự và trả mã/kết quả cuối cùng.
static int runSnippet(const std::string &source,
                      const fs::path &resolutionBase,
                      SnippetMode mode,
                      const fs::path &sourceIdentity = {},
                      VM::OutputSink outputSink = {},
                      const fs::path &runtimeBase = {}) {
    const bool emitEntryPointCall = mode == SnippetMode::Execute ||
                              mode == SnippetMode::Disassemble;
    vietvm::compiler::CompilationContext compilationContext;
    compilationContext.importResolutionBase = resolutionBase;
    if (!sourceIdentity.empty()) {
        compilationContext.currentSourceIdentity =
            sourceIdentity.lexically_normal().generic_u8string();
    } else {
        compilationContext.currentSourceIdentity = "<memory>";
    }
    vietvm::compiler::CompilationArtifacts artifacts =
        vietvm::compiler::compilePipeline(
            compilationContext, source, keywordMap, emitEntryPointCall);
    const auto &stringPool = compilationContext.stringPool;

    switch (mode) {
        case SnippetMode::DumpAst:
            std::cout << vietvm::tooling::dumpAst(artifacts.ast);
            return EXIT_SUCCESS;
        case SnippetMode::DumpIr:
            // compilePipeline returns the post-optimizer IR consumed by the
            // direct emitter. Unsupported regions are rejected instead of
            // falling back to a token backend.
            std::cout << "backend=direct-ir codegen-unsupported-regions="
                      << artifacts.unsupportedDirectIrRegions << '\n';
            std::cout << vietvm::tooling::dumpIr(artifacts.ir);
            return EXIT_SUCCESS;
        case SnippetMode::Disassemble:
            std::cout << vietvm::tooling::disassembleBytecode(artifacts.bytecode, stringPool);
            return EXIT_SUCCESS;
        case SnippetMode::Execute:
            break;
    }

    VM vm(artifacts.bytecode, stringPool);
    if (outputSink) vm.setOutputSink(std::move(outputSink));
    else connectVmOutput(vm);
    std::unordered_map<int, int> functionNames;
    for (const auto &entry : compilationContext.functionNameIndices) {
        functionNames[entry.second] = entry.first;
    }
    vm.setFunctions(compilationContext.functionBytecode, std::move(functionNames));
    vm.setForeignFunctions(compilationContext.foreignFunctions);
    // Grant the exact OS contracts used by the bundled System library.
    vm.setForeignCapabilities({"system.env.read", "system.process.id",
                               "system.process.parent_id", "system.time.sleep",
                               "system.process.new", "system.process.arg",
                               "system.process.command_line",
                               "system.process.env", "system.process.start",
                               "system.process.poll", "system.process.read",
                               "system.process.wait", "system.process.error",
                               "system.process.close",
                               "system.time.monotonic",
                               "system.time.realtime",
                               "system.time.local",
                               "system.entropy.read",
                               "system.fs.mkdir", "system.fs.remove",
                               "system.fs.exists",
                               "system.fs.stat", "system.fs.lstat",
                               "system.fs.path.kind",
                               "system.fs.dir.open", "system.fs.dir.read",
                               "system.fs.dir.close",
                               "system.net.resolve.open", "system.net.resolve.next",
                               "system.net.resolve.close",
                               "system.net.socket.connect", "system.net.socket.listen",
                               "system.net.socket.accept", "system.net.socket.timeout",
                               "system.net.socket.kind", "system.net.socket.send",
                               "system.net.socket.recv", "system.net.socket.close",
                               "system.file.open", "system.file.read",
                               "system.file.write", "system.file.close",
                               "system.file.error",
                               "system.file.seek", "system.file.tell", "system.file.flush",
                               "system.ffi.error",
                               "system.ffi.layout",
                               "system.user.id", "system.user.effective_id",
                               "system.group.id", "system.group.effective_id"});
    vm.setDebugInfo(artifacts.bytecodeDebugInfo,
                    compilationContext.functionDebugInfo);
    for (const auto &module : compilationContext.moduleInitializers) {
        (void)vm.addModuleInitializer(module.identity, module.bytecode,
                                      module.debugInfo);
    }

    // Relative runtime file/database paths historically resolve beside the
    // entry source file. Import resolution above no longer depends on process
    // cwd, so keep this compatibility scope limited to VM execution.
    RuntimeCwdGuard runtimeCwdGuard;
    const auto &executionBase = runtimeBase.empty() ? resolutionBase : runtimeBase;
    if (!executionBase.empty()) {
        fs::current_path(executionBase);
    }
    vm.run();
    return EXIT_SUCCESS;
}

// Chạy tệp; hàm điều phối toàn bộ luồng xử lý của tác vụ, gọi các bước con theo thứ tự và trả mã/kết quả cuối cùng.
static int runFile(const std::string &filename,
                   SnippetMode mode,
                   bool lintOnly = false) {
    std::string source = readFile(filename);
    fs::path filePath = vietvm::core::utf8Path(filename);
    fs::path fileDir = filePath.parent_path();

    if (lintOnly) {
        std::string errorMessage;
        if (vietvm::tooling::lintSource(source, errorMessage)) {
            std::cout << messages::messageText(messages::kToolLintPassed, {filename});
            return EXIT_SUCCESS;
        }
        std::cerr << messages::formatMessage(messages::kToolLintFailed,
                                             {filename, errorMessage}) << std::endl;
        return EXIT_FAILURE;
    }

    verifyLockedProjectForSource(filePath);

    return runSnippet(source,
                      fileDir.empty() ? fs::current_path() : fileDir,
                      mode,
                      filePath);
}

// Internal regression-test worker. Chạy nhiều source file tuần tự trong cùng
// process để compiler có thể tái sử dụng cache AST giữa các lượt, nhưng vẫn ghi
// output/status riêng cho từng file để run_tests.sh giữ nguyên golden semantics.
static int runBatchTestFiles(const fs::path &outputDir,
                             const std::vector<fs::path> &testFiles) {
    fs::create_directories(outputDir);
    for (const fs::path &testFile : testFiles) {
        std::ostringstream captured;
        std::streambuf *oldOut = std::cout.rdbuf(captured.rdbuf());
        std::streambuf *oldErr = std::cerr.rdbuf(captured.rdbuf());
        int status = EXIT_FAILURE;
        try {
            status = runFile(testFile.u8string(), SnippetMode::Execute);
        } catch (const vietvm::runtime::RuntimeError &error) {
            printErrorMessage(messages::kCliUnhandledException,
                              vietvm::runtime::formatRuntimeError(error));
            status = EXIT_FAILURE;
        } catch (const std::exception &error) {
            printErrorMessage(messages::kCliUnhandledException, error.what());
            status = EXIT_FAILURE;
        }
        std::cout.flush();
        std::cerr.flush();
        std::cout.rdbuf(oldOut);
        std::cerr.rdbuf(oldErr);

        const std::string base = testFile.stem().u8string();
        std::ofstream output(outputDir / vietvm::core::utf8Path(base + ".output"),
                             std::ios::binary | std::ios::trunc);
        output << captured.str();
        std::ofstream statusFile(outputDir / vietvm::core::utf8Path(base + ".status"),
                                 std::ios::trunc);
        statusFile << status << '\n';
        if (!output || !statusFile) return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

// Biên dịch một tệp V++ mà không chạy VM. Lệnh `dựng` dùng contract này trong
// giai đoạn bytecode 1.0 chưa đóng format artifact trên đĩa: compile pipeline
// phải thành công và metadata/function registry phải hoàn chỉnh trong bộ nhớ.
static int buildFile(const std::string &filename) {
    const std::string source = readFile(filename);
    const fs::path filePath = vietvm::core::utf8Path(filename);
    const fs::path fileDir = filePath.parent_path();
    verifyLockedProjectForSource(filePath);

    vietvm::compiler::CompilationContext context;
    context.importResolutionBase =
        fileDir.empty() ? fs::current_path() : fileDir;
    context.currentSourceIdentity = filePath.lexically_normal().generic_u8string();
    const auto artifacts = vietvm::compiler::compilePipeline(
        context, source, keywordMap, true);

    std::cout << messages::messageText(
        messages::kCliBuildSucceeded,
        {filename,
         std::to_string(artifacts.bytecode.size()),
         std::to_string(context.functionBytecode.size())});
    return EXIT_SUCCESS;
}

// Quote only bootstrap arguments. The target source is read and compiled by
// trình điều khiển.vi, never inserted as executable code in this wrapper.
static int buildFullNative(const std::string &filename, const std::string &outputName,
                           bool compiler) {
    const auto input = fs::weakly_canonical(fs::absolute(vietvm::core::utf8Path(filename)));
    const auto output = fs::absolute(vietvm::core::utf8Path(outputName));
    if (input == fs::weakly_canonical(output) ||
        (fs::exists(output) && fs::equivalent(input, output)))
        throw std::runtime_error("Native AOT không được ghi đè mã nguồn");
    verifyLockedProjectForSource(input);
    vietvm::compiler::CompilationContext context;
    context.importResolutionBase = input.parent_path();
    context.currentSourceIdentity = input.generic_u8string();
    const auto artifacts = vietvm::compiler::compilePipeline(context, readFile(input.u8string()), keywordMap, true);
    vietvm::native::aot::Snapshot snapshot;
    snapshot.strings = context.stringPool;
    snapshot.entry = artifacts.bytecode;
    snapshot.foreignFunctions = context.foreignFunctions;
    for (const auto &[id, code] : context.functionBytecode) {
        // Lambdas have an ID and executable body, but no StringPool name.
        const auto name = context.functionNameIndices.find(id);
        snapshot.functions.emplace(id, vietvm::native::aot::FunctionCode{
            name == context.functionNameIndices.end() ? -1 : name->second, code});
    }
    for (const auto &module : context.moduleInitializers) snapshot.initializers.push_back(module.bytecode);
    const char *root = std::getenv("VPP_AOT_ROOT");
    if (!root || !*root) root = std::getenv("VPP_HOME");
    if (!root || !*root) throw std::runtime_error("Native AOT cần VPP_HOME trỏ tới bộ cài toolchain");
    vietvm::native::aot::compileExecutable(snapshot, output, fs::absolute(vietvm::core::utf8Path(root)), compiler);
    return EXIT_SUCCESS;
}

static std::string vppStringLiteral(const std::string &value) {
    std::string result = "\"";
    for (const unsigned char c : value) {
        switch (c) {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (c < 32 || c == 127)
                    throw std::runtime_error("đối số compiler chứa ký tự điều khiển chưa hỗ trợ");
                result += static_cast<char>(c);
        }
    }
    return result + '"';
}

// Only Stage 0 uses the C++ bootstrap. --compiler=vpp must execute an already
// bootstrapped native V++ compiler, with no implicit VM fallback.
static fs::path nativeVppCompiler() {
    if (const char *value = std::getenv("VPP_NATIVE_COMPILER"); value && *value) {
        const fs::path configured = fs::absolute(vietvm::core::utf8Path(value));
        if (!fs::is_regular_file(configured))
            throw std::runtime_error("VPP_NATIVE_COMPILER không tồn tại: " + configured.u8string());
        return configured;
    }
    for (const char *env : {"VPP_HOME", "VPP_AOT_ROOT"}) {
        if (const char *value = std::getenv(env); value && *value) {
            const fs::path candidate = fs::absolute(vietvm::core::utf8Path(value)) / "bin" /
#ifdef _WIN32
                "vpp-native.exe";
#else
                "vpp-native";
#endif
            if (fs::is_regular_file(candidate)) return candidate;
        }
    }
    throw std::runtime_error(
        "Compiler V++ native chưa được bootstrap. Chạy 'vpp --bootstrap-native "
        "gói/biên dịch/native_cli.vi <VPP_HOME>/bin/vpp-native' ở Stage 0 "
        "hoặc đặt VPP_NATIVE_COMPILER tới compiler native đã xây dựng.");
}

class NativeCompilerTemporaryDirectory {
public:
    NativeCompilerTemporaryDirectory() {
        static std::atomic<unsigned long> sequence{0};
        for (int i = 0; i < 100; ++i) {
            path = fs::temp_directory_path() /
                ("vpp-native-cli-" + std::to_string(++sequence) + "-" +
#ifdef _WIN32
                 std::to_string(GetCurrentProcessId())
#else
                 std::to_string(getpid())
#endif
                );
            std::error_code error;
            if (fs::create_directory(path, error)) return;
        }
        throw std::runtime_error("Compiler V++: không tạo được thư mục tạm");
    }
    ~NativeCompilerTemporaryDirectory() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
    fs::path path;
};

static int runVppCompiler(const std::string &filename, const std::string &mode,
                          const std::string &outputName = {}) {
    const fs::path sourcePath = fs::weakly_canonical(
        fs::absolute(vietvm::core::utf8Path(filename)));
    if (mode != "artifact") verifyLockedProjectForSource(sourcePath);
    const fs::path outputPath = outputName.empty() ? fs::path{} :
        fs::absolute(vietvm::core::utf8Path(outputName)).lexically_normal();
    if (!outputPath.empty() && fs::weakly_canonical(outputPath) == sourcePath)
        throw std::runtime_error("compiler V++: đầu ra không được ghi đè nguồn");

    const fs::path compiler = nativeVppCompiler();
    NativeCompilerTemporaryDirectory temporary;
    fs::path input = sourcePath;
    std::vector<std::string> arguments{compiler.u8string()};
    if (mode == "artifact") {
        arguments.insert(arguments.end(), {"artifact", input.u8string()});
    } else if (mode == "run") {
        arguments.insert(arguments.end(), {"run", input.u8string()});
    } else if (mode == "build") {
        arguments.insert(arguments.end(), {"build", input.u8string(), "-o",
            (outputPath.empty() ? temporary.path / "output.vbc" : outputPath).u8string(), "--bytecode"});
    } else {
        throw std::invalid_argument("Compiler V++: chế độ chưa hỗ trợ: " + mode);
    }
    const auto result = vietvm::native::aot::runProcess(arguments);
    if (result.status != 0) return result.status;
    std::cout << result.output;
    return EXIT_SUCCESS;
}

// Opt-in self-host native: frontend AST/IR, Machine IR, ISA và object đều
// thực thi bằng thư viện V++; CLI chỉ chọn target và ghi artifact.
static int buildSelfHostedNativeObject(const vietvm::native::Target &target,
                                       const std::string &functionName,
                                       const std::string &filename,
                                       const std::string &outputName) {
    const fs::path sourcePath = vietvm::core::utf8Path(filename);
    verifyLockedProjectForSource(sourcePath);
    const fs::path sourceBase = sourcePath.parent_path().empty()
        ? fs::current_path() : sourcePath.parent_path();
    const auto compiled = vietvm::native::compileSourceMachineFunctionVpp(
        target, readFile(filename), functionName, sourceBase);
    if (!compiled) {
        std::cerr << "Native self-host V++ lỗi: " << compiled.error << '\n';
        return EXIT_FAILURE;
    }
    if (!compiled.object || compiled.object.bytes.empty()) {
        std::cerr << "Native self-host V++: object rỗng hoặc không hợp lệ\n";
        return EXIT_FAILURE;
    }
    std::ofstream output(vietvm::core::utf8Path(outputName),
                         std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("không thể mở tệp object: " + outputName);
    output.write(reinterpret_cast<const char *>(compiled.object.bytes.data()),
                 static_cast<std::streamsize>(compiled.object.bytes.size()));
    if (!output) throw std::runtime_error("ghi object native thất bại: " + outputName);
    std::cout << "Native self-host V++ object: " << functionName << " -> " << outputName
              << " (" << vietvm::native::canonicalTriple(target) << ", "
              << compiled.emission.bytes.size() << " machine bytes, "
              << compiled.emission.relocations.size() << " relocations)\n";
    return EXIT_SUCCESS;
}

// Native executable for the host architecture. V++ compiles all functions;
// the system C linker resolves cross-function relocations and produces the
// platform executable. The source must export a zero-argument i32 main().
static int buildSelfHostedNativeExecutable(const vietvm::native::Target &target,
                                           const std::string &filename,
                                           const std::string &outputName) {
#if defined(_WIN32)
    (void)target;
    (void)filename;
    (void)outputName;
    std::cerr << "Native self-host executable hiện chỉ hỗ trợ host macOS/Linux\n";
    return EXIT_FAILURE;
#else
    if (vietvm::native::canonicalTriple(target) !=
        vietvm::native::canonicalTriple(vietvm::native::hostTarget())) {
        std::cerr << "Native executable cần target khớp host; cross-link chưa hỗ trợ\n";
        return EXIT_FAILURE;
    }
    const fs::path input = fs::absolute(vietvm::core::utf8Path(filename)).lexically_normal();
    const fs::path destination = fs::absolute(vietvm::core::utf8Path(outputName)).lexically_normal();
    // Resolve links before publishing: a differently spelled output path may
    // still designate the input source through symlinks or hard links.
    if (input == destination ||
        fs::weakly_canonical(input) == fs::weakly_canonical(destination) ||
        (fs::exists(destination) && fs::equivalent(input, destination))) {
        std::cerr << "Native executable không được ghi đè mã nguồn\n";
        return EXIT_FAILURE;
    }
    verifyLockedProjectForSource(input);
    // Compile the V++ runtime and link its OS/machine assembly boundary.
    fs::path nativeRuntimeRoot;
    std::string nativeSource = readFile(filename);
    std::string primitiveRelative;
#if defined(__APPLE__) && defined(__aarch64__)
    primitiveRelative = "scripts/ASM/vpp_aot_primitives_aarch64.S";
#elif defined(__linux__) && defined(__x86_64__)
    primitiveRelative = "scripts/ASM/vpp_aot_primitives_linux_x86_64.S";
#elif defined(__linux__) && defined(__aarch64__)
    primitiveRelative = "scripts/ASM/vpp_aot_primitives_linux_aarch64.S";
#else
    std::cerr << "Native self-host chưa có runtime V++/assembly cho host này\n";
    return EXIT_FAILURE;
#endif
    {
        std::vector<fs::path> roots;
        if (const char *home = std::getenv("VPP_HOME"); home && *home)
            roots.emplace_back(vietvm::core::utf8Path(home));
        roots.push_back(fs::current_path());
        for (auto p = input.parent_path(); !p.empty(); p = p.parent_path()) {
            roots.push_back(p);
            if (p == p.root_path()) break;
        }
        for (const auto &root : roots) {
            if (fs::is_regular_file(root / primitiveRelative) &&
                fs::is_regular_file(root / vietvm::core::utf8Path("gói/biên dịch/native/runtime.vi"))) {
                nativeRuntimeRoot = fs::absolute(root);
                break;
            }
        }
        if (nativeRuntimeRoot.empty())
            throw std::runtime_error("thiếu runtime V++/primitive assembly; đặt VPP_HOME tới bộ cài V++");
        nativeSource = readFile((nativeRuntimeRoot /
            vietvm::core::utf8Path("gói/biên dịch/native/runtime.vi")).string()) + "\n" + nativeSource;
    }
    const auto compiled = vietvm::native::compileSourceProgramVpp(
        target, nativeSource, input.parent_path());
    if (!compiled) {
        std::cerr << "Native self-host V++ lỗi: " << compiled.error << '\n';
        return EXIT_FAILURE;
    }
    bool foundMain = false;
    for (const auto &entry : compiled.functions) {
        if (entry.first == "main") {
            foundMain = true;
            if (entry.second.function.parameterCount != 0) {
                std::cerr << "Native AOT yêu cầu hàm main() không có tham số\n";
                return EXIT_FAILURE;
            }
        }
    }
    if (!foundMain) {
        std::cerr << "Native AOT yêu cầu khai báo hàm main() trả về i32\n";
        return EXIT_FAILURE;
    }
    // Link into a unique sibling directory. Do not replace an existing
    // executable until compilation and linking have both succeeded.
    const auto parent = destination.parent_path();
    if (!fs::is_directory(parent)) {
        std::cerr << "Thư mục đầu ra native không tồn tại: " << parent << '\n';
        return EXIT_FAILURE;
    }
    std::string temporaryName = (parent / ".vpp-native-XXXXXX").string();
    std::vector<char> mutableName(temporaryName.begin(), temporaryName.end());
    mutableName.push_back('\0');
    const char *created = mkdtemp(mutableName.data());
    if (!created) throw std::runtime_error("không thể tạo thư mục tạm native AOT");
    const fs::path temporaryDirectory(created);
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code ignored; fs::remove_all(path, ignored); }
    } cleanup{temporaryDirectory};

    std::vector<std::string> arguments;
    const char *configuredCompiler = std::getenv("VPP_NATIVE_CC");
    arguments.emplace_back(configuredCompiler && *configuredCompiler ? configuredCompiler : "cc");
    for (std::size_t i = 0; i < compiled.functions.size(); ++i) {
        const auto object = temporaryDirectory / ("function-" + std::to_string(i) + ".o");
        std::ofstream out(object, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("không thể tạo native object: " + object.string());
        const auto &bytes = compiled.functions[i].second.object.bytes;
        out.write(reinterpret_cast<const char *>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        if (!out) throw std::runtime_error("không thể ghi native object: " + object.string());
        out.close();
        arguments.push_back(object.string());
    }
    arguments.push_back("-DVPP_NATIVE_VPP_RUNTIME=1");
    arguments.push_back((nativeRuntimeRoot / primitiveRelative).string());
#if defined(__linux__)
    // Assembly calls libcrypto for the SHA-256 primitive.
    arguments.push_back("-lcrypto");
#endif
    const auto temporaryExecutable = temporaryDirectory / "program";
    arguments.push_back("-o");
    arguments.push_back(temporaryExecutable.string());
    const pid_t child = fork();
    if (child < 0) throw std::runtime_error("không thể khởi chạy linker native");
    if (child == 0) {
        std::vector<char *> argv;
        for (auto &argument : arguments) argv.push_back(argument.data());
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);
    }
    int status = 0;
    if (waitpid(child, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        std::cerr << "Native host linker thất bại (mã "
                  << (WIFEXITED(status) ? WEXITSTATUS(status) : -1) << ")\n";
        return EXIT_FAILURE;
    }
    fs::rename(temporaryExecutable, destination);
    std::cout << "Native self-host V++ executable: " << destination.string()
              << " (" << compiled.functions.size() << " functions, "
              << vietvm::native::canonicalTriple(target) << ")\n";
    return EXIT_SUCCESS;
#endif
}

// Biên dịch một hàm IR tối giản thành byte mã máy thuần cho target tùy chọn.
// Artifact .bin chỉ chứa function body, không có header/linker/relocations.
static int buildRawLeafFile(const vietvm::native::Target &target,
                            const std::string &functionName,
                            const std::string &filename,
                            const std::string &outputName,
                            const std::string &mode) {
    const fs::path sourcePath = vietvm::core::utf8Path(filename);
    verifyLockedProjectForSource(sourcePath);
    vietvm::compiler::CompilationContext context;
    context.importResolutionBase = sourcePath.parent_path().empty()
        ? fs::current_path() : sourcePath.parent_path();
    context.currentSourceIdentity = sourcePath.lexically_normal().generic_u8string();
    const auto artifacts = vietvm::compiler::compilePipeline(
        context, readFile(filename), keywordMap, false);
    const bool assembly = mode == "--emit-asm";
    const bool object = mode == "--emit-obj";
    const auto compiledLeaf = vietvm::native::compileRawLeafVpp(
        target, artifacts.ir, functionName, context.importResolutionBase, object);
    const bool generalized = !compiledLeaf && !compiledLeaf.lowered &&
        (target.arch == vietvm::native::Architecture::X86_64 ||
         target.arch == vietvm::native::Architecture::AArch64);
    const auto compiledMachine = generalized
        ? vietvm::native::compileMachineFunctionVpp(
              target, artifacts.ir, functionName, context.importResolutionBase, object)
        : vietvm::native::VppCompiledMachine{};
    if (!compiledLeaf && (!generalized || !compiledMachine)) {
        std::cerr << "Native V++ compile lỗi: "
                  << (generalized ? compiledMachine.error : compiledLeaf.error) << '\n';
        return EXIT_FAILURE;
    }
    // One V++ VM run owns lowering, encoding and optional object packing.
    // C++ only prints ISA assembly; the object writer itself lives in V++.
    auto machineEmission = generalized
        ? compiledMachine.emission
        : vietvm::native::MachineEmission{};
    const auto leafEmission = !generalized
        ? compiledLeaf.emission
        : vietvm::native::RawLeafResult{};
    if (generalized ? !machineEmission : !leafEmission) {
        std::cerr << "Native ISA lỗi: "
                  << (generalized ? machineEmission.error : leafEmission.error) << '\n';
        return EXIT_FAILURE;
    }
    const auto &bytes = generalized ? machineEmission.bytes : leafEmission.bytes;
    if (!assembly && !object && generalized && !machineEmission.relocations.empty()) {
        std::cerr << "Raw machine code không thể chứa CALL chưa liên kết; dùng --emit-obj\n";
        return EXIT_FAILURE;
    }
    if (assembly && generalized) {
        const auto cppListing = vietvm::native::emitMachineFunction(target, compiledMachine.function);
        if (!cppListing) {
            std::cerr << "C++ assembly printer lỗi: " << cppListing.error << '\n';
            return EXIT_FAILURE;
        }
        if (cppListing.bytes != machineEmission.bytes ||
            cppListing.relocations.size() != machineEmission.relocations.size()) {
            std::cerr << "V++ và C++ assembly printer không khớp mã máy/relocation\n";
            return EXIT_FAILURE;
        }
        for (std::size_t i = 0; i < cppListing.relocations.size(); ++i) {
            if (cppListing.relocations[i].offset != machineEmission.relocations[i].offset ||
                cppListing.relocations[i].symbol != machineEmission.relocations[i].symbol) {
                std::cerr << "V++ và C++ assembly printer không khớp CALL relocation\n";
                return EXIT_FAILURE;
            }
        }
        machineEmission.assembly = cppListing.assembly;
    }
    if (assembly && !generalized) {
        // C++ remains the textual ISA printer; check its independently
        // encoded bytes against V++ before exposing an assembly listing.
        const auto reference = vietvm::native::emitRawLeaf(target, compiledLeaf.leaf);
        if (!reference || reference.bytes != leafEmission.bytes) {
            std::cerr << "V++ và C++ leaf encoder không khớp byte mã máy\n";
            return EXIT_FAILURE;
        }
    }
    const auto listing = assembly
        ? (generalized
            ? vietvm::native::emitMachineAssembly(target, machineEmission, functionName)
            : vietvm::native::emitRawAssembly(target, compiledLeaf.leaf, functionName))
        : vietvm::native::RawAssemblyResult{};
    if (assembly && !listing) {
        std::cerr << "Native assembly lỗi: " << listing.error << '\n';
        return EXIT_FAILURE;
    }
    const auto objectFile = object
        ? (generalized ? compiledMachine.object : compiledLeaf.object)
        : vietvm::native::RawObjectResult{};
    if (object && !objectFile) {
        std::cerr << "Native object lỗi: " << objectFile.error << '\n';
        return EXIT_FAILURE;
    }
    std::ofstream output(vietvm::core::utf8Path(outputName),
                         std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("không thể mở tệp mã máy: " + outputName);
    if (assembly) output << listing.text;
    else if (object) output.write(reinterpret_cast<const char *>(objectFile.bytes.data()),
                                 static_cast<std::streamsize>(objectFile.bytes.size()));
    else output.write(reinterpret_cast<const char *>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
    if (!output) throw std::runtime_error("ghi tệp mã máy thất bại: " + outputName);
    std::cout << (assembly ? "Assembly source: " : (object ? "Native object: " : "Raw machine code: "))
              << functionName << " -> " << outputName
              << " (" << vietvm::native::canonicalTriple(target) << ", "
              << bytes.size() << " machine bytes; "
              << (assembly ? "ISA instructions, not linked" : (object ? "relocatable object, not executable" : "function body, not executable"))
              << ")\n";
    return EXIT_SUCCESS;
}

// Thu thập toàn bộ tệp `.vi` dưới một tệp/thư mục theo thứ tự xác định để các
// lệnh kiểm thử, soát lỗi và định dạng có cùng quy tắc discovery.
static std::vector<fs::path> collectSourceFiles(const fs::path &path) {
    std::vector<fs::path> sourceFiles;
    if (fs::is_regular_file(path)) {
        if (path.extension() == ".vi") sourceFiles.push_back(path);
    } else if (fs::is_directory(path)) {
        for (const auto &entry : fs::recursive_directory_iterator(path)) {
            if (entry.is_regular_file() && entry.path().extension() == ".vi") {
                sourceFiles.push_back(entry.path());
            }
        }
    }
    std::sort(sourceFiles.begin(), sourceFiles.end());
    return sourceFiles;
}

// Chạy toàn bộ tệp `.vi` trong một đường dẫn kiểm thử theo thứ tự xác định.
// Nếu đầu vào là một tệp thì chỉ chạy tệp đó; nếu là thư mục thì quét đệ quy.
static int runTests(const fs::path &requestedPath) {
    fs::path path = requestedPath;
    if (path.empty()) path = vietvm::core::utf8Path("tests");
    if (!fs::exists(path)) {
        std::cerr << messages::formatMessage(
            messages::kCliTestPathMissing, {path.u8string()}) << '\n';
        return EXIT_FAILURE;
    }

    const std::vector<fs::path> testFiles = collectSourceFiles(path);

    if (testFiles.empty()) {
        std::cerr << messages::formatMessage(
            messages::kCliTestNoFiles, {path.u8string()}) << '\n';
        return EXIT_FAILURE;
    }

    std::size_t passed = 0;
    for (const fs::path &testFile : testFiles) {
        std::cout << messages::messageText(
            messages::kCliTestRunning, {testFile.u8string()}) << std::endl;
        try {
            if (runFile(testFile.u8string(), SnippetMode::Execute) == EXIT_SUCCESS) {
                ++passed;
            }
        } catch (const vietvm::runtime::RuntimeError &error) {
            printErrorMessage(messages::kCliUnhandledException,
                              vietvm::runtime::formatRuntimeError(error));
        } catch (const std::exception &error) {
            printErrorMessage(messages::kCliUnhandledException, error.what());
        }
    }

    std::cout << messages::messageText(
        messages::kCliTestSummary,
        {std::to_string(passed), std::to_string(testFiles.size())});
    return passed == testFiles.size() ? EXIT_SUCCESS : EXIT_FAILURE;
}

static int runLintPath(const fs::path &path) {
    if (!fs::exists(path)) {
        std::cerr << messages::formatMessage(
            messages::kCliToolPathMissing, {path.u8string()}) << '\n';
        return EXIT_FAILURE;
    }
    const auto sourceFiles = collectSourceFiles(path);
    if (sourceFiles.empty()) {
        std::cerr << messages::formatMessage(
            messages::kCliToolNoSourceFiles, {path.u8string()}) << '\n';
        return EXIT_FAILURE;
    }

    std::size_t errors = 0;
    std::size_t warnings = 0;
    for (const fs::path &sourceFile : sourceFiles) {
        const std::string source = readFile(sourceFile.u8string());
        const fs::path resolutionBase = sourceFile.parent_path().empty()
            ? fs::current_path()
            : sourceFile.parent_path();
        const auto diagnostics = vietvm::tooling::lintDiagnostics(source, resolutionBase);
        for (const auto &diagnostic : diagnostics) {
            const bool isError =
                diagnostic.severity == vietvm::tooling::DiagnosticSeverity::Error;
            if (isError) ++errors;
            else ++warnings;
            std::cerr << messages::messageText(
                messages::kCliLintDiagnostic,
                {sourceFile.u8string(),
                 std::to_string(diagnostic.span.begin.line),
                 std::to_string(diagnostic.span.begin.column),
                 messages::messageText(isError ? messages::kToolSeverityError
                                                : messages::kToolSeverityWarning),
                 diagnostic.message});
        }
    }
    std::cout << messages::messageText(
        messages::kCliLintSummary,
        {std::to_string(sourceFiles.size()),
         std::to_string(errors),
         std::to_string(warnings)});
    return errors == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

static int runFormatPath(const fs::path &path,
                         bool writeFiles,
                         bool checkOnly) {
    if (writeFiles && checkOnly) {
        std::cerr << messages::formatMessage(messages::kCliFormatConflictingModes) << '\n';
        return EXIT_FAILURE;
    }
    if (!fs::exists(path)) {
        std::cerr << messages::formatMessage(
            messages::kCliToolPathMissing, {path.u8string()}) << '\n';
        return EXIT_FAILURE;
    }
    const auto sourceFiles = collectSourceFiles(path);
    if (sourceFiles.empty()) {
        std::cerr << messages::formatMessage(
            messages::kCliToolNoSourceFiles, {path.u8string()}) << '\n';
        return EXIT_FAILURE;
    }
    if (fs::is_directory(path) && !writeFiles && !checkOnly) {
        std::cerr << messages::formatMessage(messages::kCliFormatDirectoryNeedsMode) << '\n';
        return EXIT_FAILURE;
    }

    std::size_t changed = 0;
    for (const fs::path &sourceFile : sourceFiles) {
        const std::string source = readFile(sourceFile.u8string());
        const std::string formatted = vietvm::tooling::formatSource(source);
        if (formatted != source) {
            ++changed;
            if (checkOnly) {
                std::cerr << messages::messageText(
                    messages::kCliFormatChanged, {sourceFile.u8string()});
            } else if (writeFiles) {
                std::ofstream output(sourceFile, std::ios::binary | std::ios::trunc);
                if (!output.is_open()) {
                    std::cerr << messages::formatMessage(
                        messages::kCliFormatWriteFailed, {sourceFile.u8string()}) << '\n';
                    return EXIT_FAILURE;
                }
                output << formatted;
            }
        }
        if (!writeFiles && !checkOnly) std::cout << formatted;
    }

    if (writeFiles || checkOnly) {
        std::cout << messages::messageText(
            messages::kCliFormatSummary,
            {std::to_string(sourceFiles.size()), std::to_string(changed)});
    }
    return checkOnly && changed != 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}

// Chạy repl; hàm điều phối toàn bộ luồng xử lý của tác vụ, gọi các bước con theo thứ tự và trả mã/kết quả cuối cùng.
static int runRepl() {
    std::cout << messages::messageText(messages::kReplWelcome);
    std::string line;
    while (true) {
        std::cout << messages::messageText(messages::kReplPrompt);
        if (!std::getline(std::cin, line)) break;

        const std::string trimmed = vietvm::core::trim(line);
        if (trimmed.empty()) continue;
        if (trimmed == ":thoát" || trimmed == ":quit" || trimmed == ":exit") break;
        if (trimmed == ":giúp" || trimmed == ":help") {
            std::cout << messages::messageText(messages::kReplHelp);
            continue;
        }

        std::string source = "nhập \"stdlib\";\n" + line;
        try {
            (void)runSnippet(source, fs::current_path(), SnippetMode::Execute,
                             fs::path("<repl>"));
        } catch (const vietvm::runtime::RuntimeError &error) {
            printErrorMessage(messages::kReplExecutionFailed,
                              vietvm::runtime::formatRuntimeError(error));
        } catch (const std::exception &ex) {
            printErrorMessage(messages::kReplExecutionFailed, ex.what());
        }
    }
    return EXIT_SUCCESS;
}

// Tạo đường dẫn tới manifest của package từ thư mục package; hàm ghép root với tên file manifest theo layout chuẩn.
static fs::path packageManifestPath(const fs::path &root) {
    return root / vietvm::core::utf8Path(vietvm::core::kProjectManifestFile);
}

static fs::path packageLockPath(const fs::path &root) {
    return root / vietvm::core::utf8Path(vietvm::core::kPackageLockFile);
}

// Xác định thư mục gốc package từ working directory hiện tại; hàm chuẩn hóa path trước khi các lệnh `pkg` đọc/ghi metadata.
static fs::path packageRootPath(const fs::path &root) {
    for (const char *directoryName : vietvm::core::kPackageDirectoryNames) {
        const fs::path candidate = root / vietvm::core::utf8Path(directoryName);
        if (fs::exists(candidate)) return candidate;
    }
    return root / vietvm::core::utf8Path(vietvm::core::kPrimaryPackageDirectory);
}

static void verifyLockedProjectForSource(const fs::path &sourcePath) {
    fs::path start = sourcePath.parent_path();
    if (start.empty()) start = fs::current_path();
    try {
        start = fs::absolute(start).lexically_normal();
    } catch (...) {
        start = start.lexically_normal();
    }

    std::optional<fs::path> projectRoot;
    for (fs::path dir = start;; dir = dir.parent_path()) {
        if (fs::exists(packageLockPath(dir))) {
            projectRoot = dir;
            break;
        }
        if (dir == dir.parent_path()) break;
    }
    if (!projectRoot.has_value()) return;

    const auto lockfile =
        vietvm::core::readPackageLockfile(packageLockPath(*projectRoot));
    const fs::path installedRoot = packageRootPath(*projectRoot);
    for (const auto &entry : lockfile.packages) {
        const fs::path installed =
            installedRoot / vietvm::core::utf8Path(entry.name);
        if (!fs::exists(installed) || !fs::is_directory(installed)) {
            throw std::runtime_error(messages::formatMessage(
                messages::kPkgLockedPackageMissing,
                {entry.name, installed.u8string()}));
        }
        const std::string actual =
            vietvm::core::fingerprintPackageTree(installed);
        if (actual != entry.fingerprint) {
            throw std::runtime_error(messages::formatMessage(
                messages::kPkgLockedPackageChanged,
                {entry.name, entry.fingerprint, actual}));
        }
    }
}

// Quét package đã có trên đĩa để tương thích project cũ chưa có manifest schema 1.
static std::vector<std::string> scanInstalledPackages(const fs::path &root) {
    std::vector<std::string> packages;
    fs::path packagesDir = packageRootPath(root);
    if (!fs::exists(packagesDir)) return packages;
    for (const auto &entry : fs::directory_iterator(packagesDir)) {
        if (entry.is_directory()) {
            fs::path mainFile = vietvm::core::packageEntryPath(entry.path());
            if (fs::exists(mainFile)) {
                packages.push_back(entry.path().filename().u8string());
            }
        }
    }
    std::sort(packages.begin(), packages.end());
    return packages;
}

// Tạo manifest mặc định từ project root; package có sẵn được ghi thành path
// dependency để `vpp gói khởi tạo` có thể nâng project legacy mà không mất state.
static vietvm::core::ProjectManifest createProjectManifest(
    const fs::path &root,
    const std::string &name) {
    vietvm::core::ProjectManifest manifest;
    manifest.name = name.empty() ? root.filename().u8string() : name;
    manifest.version = vietvm::core::kCliVersion;
    for (const std::string &packageName : scanInstalledPackages(root)) {
        vietvm::core::PackageDependencySpec dependency;
        dependency.name = packageName;
        dependency.versionRange = "*";
        dependency.sourceKind = vietvm::core::PackageSourceKind::Path;
        dependency.location =
            (vietvm::core::utf8Path(vietvm::core::kPrimaryPackageDirectory) /
             vietvm::core::utf8Path(packageName)).generic_u8string();
        manifest.dependencies.push_back(std::move(dependency));
    }
    return manifest;
}

static vietvm::core::ProjectManifest loadOrCreateProjectManifest(
    const fs::path &root) {
    const fs::path manifestPath = packageManifestPath(root);
    if (fs::exists(manifestPath)) {
        return vietvm::core::readProjectManifest(manifestPath);
    }
    return createProjectManifest(root, root.filename().u8string());
}

// Đọc dependency theo manifest. Project chưa có manifest vẫn dùng scan legacy
// để các lệnh list/doctor không đổi hành vi trước khi người dùng chạy init.
static std::vector<std::string> listPackages(const fs::path &root) {
    const fs::path manifestPath = packageManifestPath(root);
    if (!fs::exists(manifestPath)) return scanInstalledPackages(root);

    const auto manifest = vietvm::core::readProjectManifest(manifestPath);
    std::vector<std::string> packages;
    packages.reserve(manifest.dependencies.size());
    for (const auto &dependency : manifest.dependencies) {
        packages.push_back(dependency.name);
    }
    std::sort(packages.begin(), packages.end());
    return packages;
}

static std::string relativePackageLocation(const fs::path &path,
                                           const fs::path &projectRoot) {
    try {
        const fs::path relative = fs::relative(path, projectRoot);
        if (!relative.empty()) return relative.generic_u8string();
    } catch (...) {
    }
    return path.lexically_normal().generic_u8string();
}

static std::string packageSourceVersion(const fs::path &sourcePath) {
    if (fs::is_directory(sourcePath)) {
        const fs::path manifestPath = packageManifestPath(sourcePath);
        if (fs::exists(manifestPath)) {
            return vietvm::core::readProjectManifest(manifestPath).version;
        }
    }
    return "0.0.0+local";
}

static vietvm::core::ResolvedPackageGraph resolveProjectPackages(
    const fs::path &root,
    const vietvm::core::ProjectManifest &manifest) {
    const fs::path sourceStateRoot =
        root / vietvm::core::utf8Path(vietvm::core::kPackageStateDirectory) /
        "sources";
    return vietvm::core::resolvePackageDependencyGraph(
        manifest, root,
        [sourceStateRoot](const vietvm::core::PackageDependencySpec &dependency,
                          const fs::path &declaringRoot) {
            return vietvm::core::materializePackageSource(
                dependency, declaringRoot, sourceStateRoot);
        });
}

static void materializeResolvedPackages(
    const fs::path &root,
    const vietvm::core::ResolvedPackageGraph &graph) {
    const fs::path packageRoot = packageRootPath(root);
    const fs::path cacheRoot = vietvm::core::packageCacheRoot(root);
    fs::create_directories(packageRoot);
    for (const auto &package : graph.packages) {
        const fs::path target =
            packageRoot / vietvm::core::utf8Path(package.name);
        const std::string fingerprint =
            vietvm::core::materializePathPackage(package.sourcePath, target);
        (void)vietvm::core::cachePackageTree(target, cacheRoot, fingerprint);
    }
}

static int writeResolvedPackageLock(
    const fs::path &root,
    const vietvm::core::ResolvedPackageGraph &graph,
    bool announce) {
    vietvm::core::PackageLockfile lockfile;
    const fs::path packageRoot = packageRootPath(root);
    const fs::path cacheRoot = vietvm::core::packageCacheRoot(root);

    for (const auto &dependency : graph.packages) {
        const fs::path installed =
            packageRoot / vietvm::core::utf8Path(dependency.name);
        if (!fs::exists(installed) || !fs::is_directory(installed)) {
            std::cerr << messages::formatMessage(
                messages::kPkgLockDependencyMissing, {dependency.name}) << '\n';
            return EXIT_FAILURE;
        }

        const std::string installedVersion = packageSourceVersion(installed);
        if (installedVersion != dependency.version) {
            std::cerr << messages::formatMessage(
                messages::kPkgLockInstalledVersionMismatch,
                {dependency.name, installedVersion, dependency.version}) << '\n';
            return EXIT_FAILURE;
        }

        // A source can move without changing its package version (notably a Git
        // branch/tag). Locking the newly resolved source identity together with
        // stale vendored bytes would produce an impossible lock entry. Reuse the
        // real installer staging/filtering path to verify that the installed
        // artifact is exactly what the current resolved source materializes to.
        const std::string installedFingerprint =
            vietvm::core::fingerprintPackageTree(installed);
        const fs::path verificationTarget =
            root / vietvm::core::utf8Path(vietvm::core::kPackageStateDirectory) /
            "lock-verify" / vietvm::core::utf8Path(dependency.name);
        std::error_code cleanupError;
        fs::remove_all(verificationTarget, cleanupError);
        try {
            (void)vietvm::core::materializePathPackage(
                dependency.sourcePath, verificationTarget, installedFingerprint);
        } catch (const std::exception &error) {
            fs::remove_all(verificationTarget, cleanupError);
            throw std::runtime_error(messages::formatMessage(
                messages::kCliPackageLockSourceMismatch,
                {dependency.name, error.what()}));
        }
        fs::remove_all(verificationTarget, cleanupError);

        vietvm::core::PackageLockEntry entry;
        entry.name = dependency.name;
        entry.version = dependency.version;
        entry.sourceKind = dependency.sourceKind;
        entry.location =
            (dependency.sourceKind == vietvm::core::PackageSourceKind::Git ||
             dependency.sourceKind == vietvm::core::PackageSourceKind::Registry)
                ? dependency.resolvedLocation
                : relativePackageLocation(dependency.sourcePath, root);
        try {
            entry.resolvedPath = fs::relative(installed, root).generic_u8string();
        } catch (...) {
            entry.resolvedPath = installed.lexically_normal().generic_u8string();
        }
        entry.fingerprint = installedFingerprint;
        entry.revision = dependency.sourceRevision;
        (void)vietvm::core::cachePackageTree(
            installed, cacheRoot, entry.fingerprint);
        lockfile.packages.push_back(std::move(entry));
    }

    const fs::path lockPath = packageLockPath(root);
    vietvm::core::writePackageLockfile(lockPath, lockfile);
    if (announce) {
        std::cout << messages::messageText(messages::kPkgLocked, {lockPath.u8string()});
    }
    return EXIT_SUCCESS;
}

// Ghi manifest schema 1 theo model package chung thay vì tự ghép JSON trong CLI.
static void writePackageManifest(const fs::path &root, const std::string &name) {
    const auto manifest = createProjectManifest(root, name);
    vietvm::core::writeProjectManifest(packageManifestPath(root), manifest);
}

// Khởi tạo package V++ mới; lệnh tạo cấu trúc thư mục/manifest mặc định sau khi kiểm tra đích chưa xung đột.
static int pkgInit(const std::string &name) {
    fs::path root = fs::current_path();
    fs::create_directories(root / vietvm::core::utf8Path(vietvm::core::kPrimaryPackageDirectory));
    if (name.empty()) {
        writePackageManifest(root, root.filename().u8string());
    } else {
        writePackageManifest(root, name);
    }
    std::cout << messages::messageText(messages::kPkgManifestCreated,
                                       {packageManifestPath(root).u8string()});
    return EXIT_SUCCESS;
}

// Tìm thư mục template theo VPP_HOME trước, sau đó đi ngược từ cwd để hỗ trợ
// cả release đã cài và chạy trực tiếp trong source tree.
static fs::path findProjectTemplate(const std::string &templateName) {
    if (const char *vppHome = std::getenv(vietvm::core::kEnvVppHome)) {
        const fs::path candidate =
            fs::u8path(vppHome) / "templates" / vietvm::core::utf8Path(templateName);
        if (fs::exists(candidate) && fs::is_directory(candidate)) return candidate;
    }
    for (fs::path dir = fs::current_path(); ; dir = dir.parent_path()) {
        const fs::path candidate =
            dir / "templates" / vietvm::core::utf8Path(templateName);
        if (fs::exists(candidate) && fs::is_directory(candidate)) return candidate;
        if (dir == dir.parent_path()) break;
    }
    return {};
}

static bool copyProjectTemplate(const fs::path &templateRoot,
                                const fs::path &projectRoot,
                                std::string &failedPath,
                                std::string &errorMessage) {
    std::error_code ec;
    for (const auto &entry : fs::recursive_directory_iterator(templateRoot, ec)) {
        if (ec) {
            failedPath = templateRoot.u8string();
            errorMessage = ec.message();
            return false;
        }
        const fs::path relative = fs::relative(entry.path(), templateRoot, ec);
        if (ec) {
            failedPath = entry.path().u8string();
            errorMessage = ec.message();
            return false;
        }
        const fs::path target = projectRoot / relative;
        if (entry.is_directory()) {
            fs::create_directories(target, ec);
        } else if (entry.is_regular_file()) {
            fs::create_directories(target.parent_path(), ec);
            if (!ec) fs::copy_file(entry.path(), target, fs::copy_options::none, ec);
        }
        if (ec) {
            failedPath = relative.generic_u8string();
            errorMessage = ec.message();
            return false;
        }
    }
    return true;
}

// Khởi tạo application project theo layout 1.0: manifest ở root, source trong
// `src/`, test trong `tests/` và `gói/` dành cho dependency đã materialize.
static int applicationInit(const std::string &name) {
    if (name.empty()) {
        std::cerr << messages::formatMessage(messages::kPkgApplicationNameMissing) << '\n';
        return EXIT_FAILURE;
    }

    const fs::path root = fs::current_path() / vietvm::core::utf8Path(name);
    if (fs::exists(root)) {
        std::cerr << messages::formatMessage(messages::kPkgApplicationDirectoryExists,
                                             {root.u8string()}) << '\n';
        return EXIT_FAILURE;
    }

    const fs::path templateRoot = findProjectTemplate("application");
    if (templateRoot.empty()) {
        std::cerr << messages::formatMessage(messages::kPkgApplicationTemplatesMissing) << '\n';
        return EXIT_FAILURE;
    }

    fs::create_directories(root / vietvm::core::utf8Path(vietvm::core::kPrimaryPackageDirectory));
    writePackageManifest(root, name);
    std::string failedPath;
    std::string errorMessage;
    if (!copyProjectTemplate(templateRoot, root, failedPath, errorMessage)) {
        std::error_code ignored;
        fs::remove_all(root, ignored);
        std::cerr << messages::formatMessage(messages::kPkgApplicationTemplateCopyFailed,
                                             {failedPath, errorMessage}) << '\n';
        return EXIT_FAILURE;
    }

    std::cout << messages::messageText(messages::kPkgApplicationCreated, {root.u8string()});
    return EXIT_SUCCESS;
}

// Khởi tạo backend/config dự án theo tùy chọn CLI; hàm tạo các file nền cần thiết để compiler/runtime nhận diện dự án.
static int backendInit(const std::string &name) {
    if (name.empty()) {
        std::cerr << messages::formatMessage(messages::kPkgBackendNameMissing) << '\n';
        return EXIT_FAILURE;
    }

    fs::path root = fs::current_path() / vietvm::core::utf8Path(name);
    if (fs::exists(root)) {
        std::cerr << messages::formatMessage(messages::kPkgBackendDirectoryExists,
                                             {root.u8string()}) << '\n';
        return EXIT_FAILURE;
    }

    const fs::path templateRoot = findProjectTemplate("backend");
    if (templateRoot.empty()) {
        std::cerr << messages::formatMessage(messages::kPkgBackendTemplatesMissing) << '\n';
        return EXIT_FAILURE;
    }

    fs::create_directories(root / vietvm::core::utf8Path(vietvm::core::kPrimaryPackageDirectory));
    writePackageManifest(root, name);
    for (const char *filename : {
             "application.vi",
             "config.vi",
             "controller.vi",
             "router.vi",
             "server.vi",
             "application.properties",
             "README.md",
             ".gitignore",
         }) {
        std::error_code ec;
        fs::copy_file(templateRoot / filename, root / filename, fs::copy_options::none, ec);
        if (ec) {
            std::cerr << messages::formatMessage(messages::kPkgBackendTemplateCopyFailed,
                                                 {filename, ec.message()}) << '\n';
            return EXIT_FAILURE;
        }
    }

    std::cout << messages::messageText(messages::kPkgBackendCreated, {root.u8string()});
    return EXIT_SUCCESS;
}

// Thêm dependency/package vào manifest; hàm kiểm tra trùng, cập nhật metadata rồi ghi lại file cấu hình.
struct ParsedPackageSourceArgument {
    vietvm::core::PackageSourceKind kind = vietvm::core::PackageSourceKind::Path;
    std::string location;
    std::string reference;
    std::string packageName;
    std::string versionRange = "*";
};

static void parseRegistryPackageSelector(
    const std::string &selector,
    ParsedPackageSourceArgument &parsed) {
    if (selector.empty()) {
        throw std::runtime_error(std::string(messages::kCliRegistrySelectorRequired));
    }
    const std::size_t at = selector.rfind('@');
    if (at == std::string::npos) {
        parsed.packageName = selector;
        parsed.versionRange = "*";
    } else {
        parsed.packageName = selector.substr(0, at);
        parsed.versionRange = selector.substr(at + 1);
    }
    if (!vietvm::core::isValidPackageName(parsed.packageName) ||
        parsed.versionRange.empty()) {
        throw std::runtime_error(messages::formatMessage(
            messages::kCliRegistrySelectorInvalid, {selector}));
    }
}

static ParsedPackageSourceArgument parsePackageSourceArgument(
    const std::string &sourceArg) {
    ParsedPackageSourceArgument parsed;
    parsed.location = sourceArg;
    if (sourceArg.rfind("registry:", 0) == 0) {
        parsed.kind = vietvm::core::PackageSourceKind::Registry;
        parsed.location.clear();
        parseRegistryPackageSelector(sourceArg.substr(9), parsed);
        return parsed;
    }
    if (sourceArg.rfind("registry+", 0) == 0) {
        parsed.kind = vietvm::core::PackageSourceKind::Registry;
        std::string body = sourceArg.substr(9);
        const std::size_t fragment = body.rfind('#');
        if (fragment == std::string::npos) {
            throw std::runtime_error(std::string(messages::kCliRegistrySourceInvalid));
        }
        parsed.location = body.substr(0, fragment);
        if (parsed.location.empty()) {
            throw std::runtime_error(std::string(messages::kCliRegistryRootEmpty));
        }
        parseRegistryPackageSelector(body.substr(fragment + 1), parsed);
        return parsed;
    }
    if (sourceArg.rfind("git+", 0) != 0) return parsed;

    parsed.kind = vietvm::core::PackageSourceKind::Git;
    parsed.location = sourceArg.substr(4);
    parsed.reference = "HEAD";
    const std::size_t fragment = parsed.location.rfind('#');
    if (fragment != std::string::npos) {
        parsed.reference = parsed.location.substr(fragment + 1);
        parsed.location.resize(fragment);
    }
    if (parsed.location.empty() || parsed.reference.empty()) {
        throw std::runtime_error(std::string(messages::kCliGitSourceInvalid));
    }
    return parsed;
}

static std::string inferGitPackageName(const std::string &location) {
    std::string normalized = location;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    while (!normalized.empty() && normalized.back() == '/') normalized.pop_back();
    const std::size_t slash = normalized.find_last_of('/');
    const std::size_t colon = normalized.find_last_of(':');
    const std::size_t separator =
        slash == std::string::npos ? colon
                                   : (colon == std::string::npos ? slash
                                                                 : std::max(slash, colon));
    std::string name = separator == std::string::npos
                           ? normalized
                           : normalized.substr(separator + 1);
    if (name.size() > 4 && name.substr(name.size() - 4) == ".git") {
        name.resize(name.size() - 4);
    }
    return name;
}

static int pkgAdd(const std::string &sourceArg, const std::string &packageNameArg) {
    const ParsedPackageSourceArgument parsedSource =
        parsePackageSourceArgument(sourceArg);
    fs::path sourcePath;
    if (parsedSource.kind == vietvm::core::PackageSourceKind::Path) {
        sourcePath = vietvm::core::utf8Path(parsedSource.location);
        if (!fs::exists(sourcePath)) {
            std::cerr << messages::formatMessage(
                messages::kPkgSourceNotFound, {parsedSource.location}) << std::endl;
            return EXIT_FAILURE;
        }
    }

    std::optional<vietvm::core::ProjectManifest> sourceProject;
    if (parsedSource.kind == vietvm::core::PackageSourceKind::Path &&
        fs::is_directory(sourcePath)) {
        const fs::path sourceManifest = packageManifestPath(sourcePath);
        if (fs::exists(sourceManifest)) {
            sourceProject = vietvm::core::readProjectManifest(sourceManifest);
        }
    }

    std::string packageName = packageNameArg;
    if (parsedSource.kind == vietvm::core::PackageSourceKind::Registry) {
        if (!packageName.empty() && packageName != parsedSource.packageName) {
            throw std::runtime_error(messages::formatMessage(
                messages::kCliRegistryPackageNameMismatch,
                {parsedSource.packageName, packageName}));
        }
        packageName = parsedSource.packageName;
    }
    if (packageName.empty()) {
        if (sourceProject.has_value() && !sourceProject->name.empty()) {
            packageName = sourceProject->name;
        } else if (parsedSource.kind == vietvm::core::PackageSourceKind::Git) {
            packageName = inferGitPackageName(parsedSource.location);
        } else {
            packageName = sourcePath.filename().u8string();
            if (sourcePath.has_extension()) {
                packageName = sourcePath.stem().u8string();
            }
        }
    }

    const fs::path projectRoot = fs::current_path();
    auto manifest = loadOrCreateProjectManifest(projectRoot);
    auto dependency = std::find_if(
        manifest.dependencies.begin(), manifest.dependencies.end(),
        [&](const vietvm::core::PackageDependencySpec &item) {
            return item.name == packageName;
        });
    vietvm::core::PackageDependencySpec updated;
    updated.name = packageName;
    updated.versionRange = parsedSource.versionRange;
    updated.sourceKind = parsedSource.kind;
    updated.location = parsedSource.location;
    updated.reference = parsedSource.reference;
    if (sourceProject.has_value()) {
        updated.versionRange = sourceProject->version;
    }

    if (dependency == manifest.dependencies.end()) {
        manifest.dependencies.push_back(std::move(updated));
    } else {
        *dependency = std::move(updated);
    }

    // Resolve toàn graph trước khi thay package trên đĩa. Version conflict,
    // dependency cycle hoặc source transport chưa hỗ trợ vì vậy thất bại trước
    // bước materialize thay vì để lại dependency graph nửa vời.
    const auto graph = resolveProjectPackages(projectRoot, manifest);
    if (parsedSource.kind == vietvm::core::PackageSourceKind::Git) {
        const auto selected = std::find_if(
            graph.packages.begin(), graph.packages.end(),
            [&](const vietvm::core::ResolvedPackageDependency &item) {
                return item.name == packageName;
            });
        if (selected == graph.packages.end()) {
            throw std::runtime_error(messages::formatMessage(
                messages::kCliGitResolvedDependencyMissing, {packageName}));
        }
        auto direct = std::find_if(
            manifest.dependencies.begin(), manifest.dependencies.end(),
            [&](const vietvm::core::PackageDependencySpec &item) {
                return item.name == packageName;
            });
        if (direct != manifest.dependencies.end()) {
            direct->versionRange = selected->version;
        }
    }
    materializeResolvedPackages(projectRoot, graph);
    vietvm::core::writeProjectManifest(packageManifestPath(projectRoot), manifest);
    if (writeResolvedPackageLock(projectRoot, graph, false) != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    std::cout << messages::messageText(messages::kPkgInstalled, {packageName});
    return EXIT_SUCCESS;
}

static int pkgPublish(const std::string &registryArg) {
    if (registryArg.empty()) {
        std::cerr << messages::formatMessage(messages::kPkgPublishRegistryMissing) << '\n';
        return EXIT_FAILURE;
    }
    const auto published = vietvm::core::publishPackageToRegistry(
        fs::current_path(), vietvm::core::utf8Path(registryArg));
    std::cout << messages::messageText(
        messages::kPkgPublished,
        {published.name, published.version, published.path.u8string()});
    return EXIT_SUCCESS;
}

static int pkgSync() {
    const fs::path root = fs::current_path();
    const fs::path manifestPath = packageManifestPath(root);
    if (!fs::exists(manifestPath)) {
        std::cerr << messages::formatMessage(messages::kPkgSyncManifestMissing) << '\n';
        return EXIT_FAILURE;
    }
    const auto manifest = vietvm::core::readProjectManifest(manifestPath);
    const auto graph = resolveProjectPackages(root, manifest);
    materializeResolvedPackages(root, graph);
    if (writeResolvedPackageLock(root, graph, false) != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    std::cout << messages::messageText(
        messages::kPkgSynced, {std::to_string(graph.packages.size())});
    return EXIT_SUCCESS;
}

// In các package/dependency đang khai báo; hàm đọc manifest và định dạng từng entry cho CLI.
static int pkgList() {
    auto packages = listPackages(fs::current_path());
    if (packages.empty()) {
        std::cout << messages::messageText(messages::kPkgListEmpty);
        return EXIT_SUCCESS;
    }
    for (const auto &pkg : packages) {
        std::cout << pkg << '\n';
    }
    return EXIT_SUCCESS;
}

// Xóa package khỏi manifest; hàm tìm entry theo tên, loại bỏ rồi ghi lại cấu hình khi có thay đổi.
static int pkgRemove(const std::string &packageName) {
    if (packageName.empty()) {
        std::cerr << messages::formatMessage(messages::kPkgRemoveNameMissing) << '\n';
        return EXIT_FAILURE;
    }
    if (!vietvm::core::isValidPackageName(packageName)) {
        std::cerr << messages::formatMessage(messages::kPkgNotFound, {packageName}) << std::endl;
        return EXIT_FAILURE;
    }

    const fs::path projectRoot = fs::current_path();
    fs::path targetDir = packageRootPath(projectRoot) /
                         vietvm::core::utf8Path(packageName);
    if (!fs::exists(targetDir)) {
        std::cerr << messages::formatMessage(messages::kPkgNotFound, {packageName}) << std::endl;
        return EXIT_FAILURE;
    }

    const fs::path manifestPath = packageManifestPath(projectRoot);
    std::optional<vietvm::core::ProjectManifest> updatedManifest;
    std::optional<vietvm::core::ResolvedPackageGraph> updatedGraph;
    if (fs::exists(manifestPath)) {
        updatedManifest = vietvm::core::readProjectManifest(manifestPath);
        updatedManifest->dependencies.erase(
            std::remove_if(
                updatedManifest->dependencies.begin(), updatedManifest->dependencies.end(),
                [&](const vietvm::core::PackageDependencySpec &dependency) {
                    return dependency.name == packageName;
                }),
            updatedManifest->dependencies.end());

        // Resolve trạng thái sau khi xóa trước khi thay đổi file trên đĩa. Nếu
        // graph còn lại có conflict/source lỗi, lệnh dừng mà package cũ vẫn còn.
        updatedGraph = resolveProjectPackages(projectRoot, *updatedManifest);
    }

    std::error_code ec;
    fs::remove_all(targetDir, ec);
    if (ec) {
        std::cerr << messages::formatMessage(messages::kPkgRemoveFailed,
                                             {packageName, ec.message()}) << '\n';
        return EXIT_FAILURE;
    }

    if (updatedManifest.has_value() && updatedGraph.has_value()) {
        materializeResolvedPackages(projectRoot, *updatedGraph);
        vietvm::core::writeProjectManifest(manifestPath, *updatedManifest);
        if (writeResolvedPackageLock(projectRoot, *updatedGraph, false) != EXIT_SUCCESS) {
            return EXIT_FAILURE;
        }
    }
    std::cout << messages::messageText(messages::kPkgRemoved, {packageName});
    return EXIT_SUCCESS;
}

// Kiểm tra package đã tồn tại trong manifest hay chưa; hàm duyệt danh sách package và so tên chuẩn hóa.
static bool packageExists(const fs::path &root, const std::string &packageName) {
    if (packageName.empty()) return false;
    fs::path packageMain = vietvm::core::packageEntryPath(
        packageRootPath(root) / vietvm::core::utf8Path(packageName));
    return fs::exists(packageMain);
}

// In thông tin chi tiết của một package; hàm tra manifest theo tên rồi hiển thị metadata và trạng thái liên quan.
static int pkgInfo(const std::string &packageName) {
    if (packageName.empty()) {
        std::cerr << messages::formatMessage(messages::kPkgInfoNameMissing) << '\n';
        return EXIT_FAILURE;
    }

    fs::path root = fs::current_path();
    fs::path packageDir = packageRootPath(root) / vietvm::core::utf8Path(packageName);
    fs::path mainFile = vietvm::core::packageEntryPath(packageDir);

    if (!fs::exists(packageDir)) {
        std::cerr << messages::formatMessage(messages::kPkgNotFound, {packageName}) << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << messages::messageText(messages::kPkgInfoName, {packageName});
    std::cout << messages::messageText(messages::kPkgInfoPath, {packageDir.u8string()});
    std::cout << messages::messageText(
        messages::kPkgInfoMainFile,
        {fs::exists(mainFile) ? mainFile.u8string()
                              : messages::messageText(messages::kPkgValueAbsent)});
    return EXIT_SUCCESS;
}

// Trả kết quả CLI cho việc package có tồn tại hay không; hàm dùng `packageExists` và chuyển boolean thành exit/output phù hợp.
static int pkgHas(const std::string &packageName) {
    bool exists = packageExists(fs::current_path(), packageName);
    std::cout << messages::messageText(exists ? messages::kPkgValuePresent
                                               : messages::kPkgValueAbsent) << std::endl;
    return exists ? EXIT_SUCCESS : EXIT_FAILURE;
}

// Tính và in thống kê package hiện tại; hàm tổng hợp số package cùng metadata cần thiết từ manifest.
static int pkgStats() {
    fs::path root = fs::current_path();
    auto packages = listPackages(root);
    fs::path manifestPath = root / vietvm::core::utf8Path(vietvm::core::kProjectManifestFile);

    std::cout << messages::messageText(messages::kPkgStatsProject, {root.filename().u8string()});
    std::cout << messages::messageText(
        messages::kPkgStatsManifest,
        {messages::messageText(fs::exists(manifestPath) ? messages::kPkgValuePresent
                                                         : messages::kPkgValueAbsent)});
    std::cout << messages::messageText(messages::kPkgStatsCount,
                                       {std::to_string(packages.size())});
    return EXIT_SUCCESS;
}

// Khóa toàn dependency graph đã resolve thành exact version/content. Graph dùng
// cùng solver với `đồng bộ`, nên transitive dependency và version conflict được
// xử lý nhất quán giữa install và lock.
static int pkgLock() {
    const fs::path root = fs::current_path();
    const fs::path manifestPath = packageManifestPath(root);
    if (!fs::exists(manifestPath)) {
        std::cerr << messages::formatMessage(messages::kPkgLockManifestMissing) << '\n';
        return EXIT_FAILURE;
    }

    const auto manifest = vietvm::core::readProjectManifest(manifestPath);
    const auto graph = resolveProjectPackages(root, manifest);
    return writeResolvedPackageLock(root, graph, true);
}

static bool isOfflineFlag(const std::string &argument) {
    return argument == vietvm::cli::kOfflineFlag || argument == vietvm::cli::kOfflineFlagVi;
}

static int pkgRestore(bool offlineOnly = false) {
    const fs::path root = fs::current_path();
    const fs::path lockPath = packageLockPath(root);
    if (!fs::exists(lockPath)) {
        std::cerr << messages::formatMessage(messages::kPkgRestoreLockMissing) << '\n';
        return EXIT_FAILURE;
    }

    const auto lockfile = vietvm::core::readPackageLockfile(lockPath);
    const fs::path packageRoot = packageRootPath(root);
    const fs::path cacheRoot = vietvm::core::packageCacheRoot(root);
    fs::create_directories(packageRoot);

    for (const auto &entry : lockfile.packages) {
        const fs::path target =
            packageRoot / vietvm::core::utf8Path(entry.name);

        if (fs::exists(target) && fs::is_directory(target)) {
            const std::string installedFingerprint =
                vietvm::core::fingerprintPackageTree(target);
            if (installedFingerprint == entry.fingerprint) {
                (void)vietvm::core::cachePackageTree(
                    target, cacheRoot, entry.fingerprint);
                continue;
            }
        }

        const auto cached =
            vietvm::core::findCachedPackage(cacheRoot, entry.fingerprint);
        if (cached.has_value()) {
            (void)vietvm::core::materializePathPackage(
                *cached, target, entry.fingerprint);
            continue;
        }

        if (offlineOnly) {
            std::cerr << messages::formatMessage(
                messages::kPkgOfflineCacheMissing,
                {entry.name, entry.fingerprint}) << '\n';
            return EXIT_FAILURE;
        }

        fs::path sourcePath;
        if (entry.sourceKind == vietvm::core::PackageSourceKind::Git) {
            vietvm::core::PackageDependencySpec lockedSource;
            lockedSource.name = entry.name;
            lockedSource.versionRange = entry.version;
            lockedSource.sourceKind = vietvm::core::PackageSourceKind::Git;
            lockedSource.location = entry.location;
            lockedSource.reference = entry.revision;
            const fs::path sourceStateRoot =
                root / vietvm::core::utf8Path(vietvm::core::kPackageStateDirectory) /
                "sources";
            const auto materialized = vietvm::core::materializePackageSource(
                lockedSource, root, sourceStateRoot);
            if (materialized.revision != entry.revision) {
                throw std::runtime_error(messages::formatMessage(
                    messages::kCliGitRestoreRevisionMismatch,
                    {entry.name, entry.revision, materialized.revision}));
            }
            sourcePath = materialized.path;
        } else if (entry.sourceKind == vietvm::core::PackageSourceKind::Registry) {
            vietvm::core::PackageDependencySpec lockedSource;
            lockedSource.name = entry.name;
            lockedSource.versionRange = entry.version;
            lockedSource.sourceKind = vietvm::core::PackageSourceKind::Registry;
            lockedSource.location = entry.location;
            const fs::path sourceStateRoot =
                root / vietvm::core::utf8Path(vietvm::core::kPackageStateDirectory) /
                "sources";
            sourcePath = vietvm::core::materializePackageSource(
                lockedSource, root, sourceStateRoot).path;
        } else {
            sourcePath = vietvm::core::utf8Path(entry.location);
            if (!sourcePath.is_absolute()) sourcePath = root / sourcePath;
            try {
                sourcePath = fs::absolute(sourcePath).lexically_normal();
            } catch (...) {
                sourcePath = sourcePath.lexically_normal();
            }
            if (!fs::exists(sourcePath)) {
                std::cerr << messages::formatMessage(
                    messages::kPkgSourceNotFound, {sourcePath.u8string()}) << '\n';
                return EXIT_FAILURE;
            }
        }

        const std::string sourceVersion = packageSourceVersion(sourcePath);
        if (sourceVersion != entry.version) {
            std::cerr << messages::formatMessage(
                messages::kPkgRestoreVersionMismatch,
                {entry.name, sourceVersion, entry.version}) << '\n';
            return EXIT_FAILURE;
        }

        (void)vietvm::core::materializePathPackage(
            sourcePath, target, entry.fingerprint);
        (void)vietvm::core::cachePackageTree(
            target, cacheRoot, entry.fingerprint);
    }

    std::cout << messages::messageText(
        messages::kPkgRestored, {std::to_string(lockfile.packages.size())});
    return EXIT_SUCCESS;
}

// Chạy gói lệnh; hàm điều phối toàn bộ luồng xử lý của tác vụ, gọi các bước con theo thứ tự và trả mã/kết quả cuối cùng.
static int runPackageCommand(int argc, char *argv[]) {
    if (argc < 3) {
        std::cerr << messages::formatMessage(messages::kPkgSubcommandMissing) << '\n';
        return EXIT_FAILURE;
    }
    std::string sub = argv[2];
    int subWordOffset = 0;
    if (argc >= 4) {
        std::string twoWordSub = sub + " " + std::string(argv[3]);
        if (twoWordSub == vietvm::cli::kPkgInitVi || twoWordSub == vietvm::cli::kPkgListVi ||
            twoWordSub == vietvm::cli::kPkgInfoVi || twoWordSub == vietvm::cli::kPkgHasVi ||
            twoWordSub == vietvm::cli::kPkgInstallVi || twoWordSub == vietvm::cli::kPkgPublishVi ||
            twoWordSub == vietvm::cli::kPkgSyncVi || twoWordSub == vietvm::cli::kPkgUpdateVi ||
            twoWordSub == vietvm::cli::kPkgRestoreVi || twoWordSub == vietvm::cli::kPkgStatsVi) {
            sub = twoWordSub;
            subWordOffset = 1;
        }
    }
    if (sub == vietvm::cli::kPkgInit || sub == vietvm::cli::kPkgInitVi) {
        std::string name = (argc >= (4 + subWordOffset)) ? argv[3 + subWordOffset] : "";
        return pkgInit(name);
    }
    if (sub == vietvm::cli::kPkgAdd || sub == vietvm::cli::kPkgAddVi) {
        if (argc < (4 + subWordOffset)) {
            std::cerr << messages::formatMessage(messages::kPkgAddSourceMissing) << '\n';
            return EXIT_FAILURE;
        }
        std::string sourceArg = argv[3 + subWordOffset];
        std::string name = (argc >= (5 + subWordOffset)) ? argv[4 + subWordOffset] : "";
        return pkgAdd(sourceArg, name);
    }
    if (sub == vietvm::cli::kPkgInstall || sub == vietvm::cli::kPkgInstallVi) {
        if (argc < (4 + subWordOffset)) {
            return pkgRestore(false);
        }
        std::string sourceArg = argv[3 + subWordOffset];
        if (isOfflineFlag(sourceArg)) return pkgRestore(true);
        std::string name = (argc >= (5 + subWordOffset)) ? argv[4 + subWordOffset] : "";
        return pkgAdd(sourceArg, name);
    }
    if (sub == vietvm::cli::kPkgPublish || sub == vietvm::cli::kPkgPublishViHyphen || sub == vietvm::cli::kPkgPublishVi) {
        if (argc < (4 + subWordOffset)) {
            std::cerr << messages::formatMessage(messages::kPkgPublishRegistryMissing) << '\n';
            return EXIT_FAILURE;
        }
        return pkgPublish(argv[3 + subWordOffset]);
    }
    if (sub == vietvm::cli::kPkgList || sub == vietvm::cli::kPkgListVi) {
        return pkgList();
    }
    if (sub == vietvm::cli::kPkgSync || sub == vietvm::cli::kPkgSyncVi) {
        return pkgSync();
    }
    if (sub == vietvm::cli::kPkgUpdate || sub == vietvm::cli::kPkgUpdateVi) {
        return pkgSync();
    }
    if (sub == vietvm::cli::kPkgLock || sub == vietvm::cli::kPkgLockVi) {
        return pkgLock();
    }
    if (sub == vietvm::cli::kPkgRestore || sub == vietvm::cli::kPkgRestoreVi) {
        const bool offlineOnly =
            argc >= (4 + subWordOffset) &&
            isOfflineFlag(argv[3 + subWordOffset]);
        return pkgRestore(offlineOnly);
    }
    if (sub == vietvm::cli::kPkgRemove || sub == vietvm::cli::kPkgRemoveVi) {
        if (argc < (4 + subWordOffset)) {
            std::cerr << messages::formatMessage(messages::kPkgRemoveNameMissing) << '\n';
            return EXIT_FAILURE;
        }
        return pkgRemove(argv[3 + subWordOffset]);
    }
    if (sub == vietvm::cli::kPkgInfo || sub == vietvm::cli::kPkgInfoVi) {
        if (argc < (4 + subWordOffset)) {
            std::cerr << messages::formatMessage(messages::kPkgInfoNameMissing) << '\n';
            return EXIT_FAILURE;
        }
        return pkgInfo(argv[3 + subWordOffset]);
    }
    if (sub == vietvm::cli::kPkgHas || sub == vietvm::cli::kPkgHasVi) {
        if (argc < (4 + subWordOffset)) {
            std::cerr << messages::formatMessage(messages::kPkgHasNameMissing) << '\n';
            return EXIT_FAILURE;
        }
        return pkgHas(argv[3 + subWordOffset]);
    }
    if (sub == vietvm::cli::kPkgStats || sub == vietvm::cli::kPkgStatsVi) {
        return pkgStats();
    }
    std::cerr << messages::formatMessage(messages::kPkgInvalidSubcommand, {sub}) << std::endl;
    return EXIT_FAILURE;
}

// Chạy doctor; hàm điều phối toàn bộ luồng xử lý của tác vụ, gọi các bước con theo thứ tự và trả mã/kết quả cuối cùng.
static int runDoctor(const std::string &execPath) {
    std::cout << messages::messageText(messages::kCliDoctorHeading);
    std::cout << messages::messageText(messages::kCliDoctorVersion,
                                       {vietvm::core::kCliVersion});
    std::cout << messages::messageText(messages::kCliDoctorExecutable, {execPath});
    std::cout << messages::messageText(messages::kCliDoctorCurrentDirectory,
                                       {fs::current_path().u8string()});

    fs::path manifestPath = fs::current_path() /
                            vietvm::core::utf8Path(vietvm::core::kProjectManifestFile);
    std::cout << messages::messageText(
        messages::kCliDoctorManifest,
        {messages::messageText(fs::exists(manifestPath) ? messages::kPkgValuePresent
                                                         : messages::kPkgValueAbsent)});

    auto packages = listPackages(fs::current_path());
    std::cout << messages::messageText(messages::kCliDoctorPackageCount,
                                       {std::to_string(packages.size())});
    return EXIT_SUCCESS;
}

// Escape chuỗi để nhúng an toàn vào JSON của LSP/CLI; hàm thay dấu nháy, backslash và ký tự điều khiển bằng escape sequence.
static std::string jsonEscape(const std::string &s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

// Giải escape từ chuỗi JSON đơn giản; hàm đọc backslash sequence và khôi phục ký tự gốc cho parser LSP.
static std::string jsonUnescape(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char n = s[++i];
            switch (n) {
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case '\\': out.push_back('\\'); break;
                case '"': out.push_back('"'); break;
                default: out.push_back(n); break;
            }
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

// Đọc LSP thông báo; hàm lấy nội dung từ nguồn tương ứng, kiểm tra lỗi cần thiết rồi trả dữ liệu đã đọc.
static std::optional<std::string> readLspMessage() {
    std::string line;
    int contentLength = -1;
    while (std::getline(std::cin, line)) {
        if (line == "\r" || line.empty()) break;
        std::string normalized = vietvm::core::trim(line);
        const std::string prefix = "Content-Length:";
        if (normalized.rfind(prefix, 0) == 0) {
            contentLength = std::stoi(vietvm::core::trim(normalized.substr(prefix.size())));
        }
    }
    if (contentLength < 0) return std::nullopt;

    std::string body(contentLength, '\0');
    std::cin.read(body.data(), contentLength);
    if (!std::cin) return std::nullopt;
    return body;
}

// Trích xuất JSON chuỗi trường; hàm tìm phần dữ liệu cần thiết trong đầu vào và trả về lát cắt đã được chuẩn hóa.
static std::string extractJsonStringField(const std::string &body, const std::string &field) {
    std::regex rx("\"" + field + "\"\\s*:\\s*\"((?:\\\\.|[^\"\\\\])*)\"");
    std::smatch match;
    if (std::regex_search(body, match, rx) && match.size() > 1) {
        return jsonUnescape(match[1].str());
    }
    return "";
}

// Trích xuất JSON thô trường; hàm tìm phần dữ liệu cần thiết trong đầu vào và trả về lát cắt đã được chuẩn hóa.
static std::string extractJsonRawField(const std::string &body, const std::string &field) {
    std::regex rx("\"" + field + "\"\\s*:\\s*([^,}]+)");
    std::smatch match;
    if (std::regex_search(body, match, rx) && match.size() > 1) {
        return vietvm::core::trim(match[1].str());
    }
    return "";
}

struct LspPosition {
    std::size_t line = 0;
    std::size_t character = 0;
};

static bool extractLspPosition(const std::string &body, LspPosition &position) {
    const std::regex positionRx(
        "\\\"position\\\"\\s*:\\s*\\{[^}]*\\\"line\\\"\\s*:\\s*([0-9]+)[^}]*"
        "\\\"character\\\"\\s*:\\s*([0-9]+)[^}]*\\}");
    std::smatch match;
    if (!std::regex_search(body, match, positionRx) || match.size() < 3) return false;
    position.line = static_cast<std::size_t>(std::stoull(match[1].str()));
    position.character = static_cast<std::size_t>(std::stoull(match[2].str()));
    return true;
}

static std::size_t utf8SequenceLength(unsigned char lead) noexcept {
    if ((lead & 0x80u) == 0) return 1;
    if ((lead & 0xE0u) == 0xC0u) return 2;
    if ((lead & 0xF0u) == 0xE0u) return 3;
    if ((lead & 0xF8u) == 0xF0u) return 4;
    return 1;
}

static std::uint32_t decodeUtf8CodePoint(const std::string &text,
                                         std::size_t offset,
                                         std::size_t &length) noexcept {
    if (offset >= text.size()) {
        length = 0;
        return 0;
    }
    const unsigned char lead = static_cast<unsigned char>(text[offset]);
    length = std::min(utf8SequenceLength(lead), text.size() - offset);
    if (length == 1) return lead;
    std::uint32_t value = lead & ((1u << (7u - static_cast<unsigned int>(length))) - 1u);
    for (std::size_t index = 1; index < length; ++index) {
        const unsigned char next = static_cast<unsigned char>(text[offset + index]);
        if ((next & 0xC0u) != 0x80u) {
            length = 1;
            return lead;
        }
        value = (value << 6u) | (next & 0x3Fu);
    }
    return value;
}

static LspPosition lspPositionForOffset(const std::string &text,
                                        std::size_t targetOffset) noexcept {
    LspPosition position;
    const std::size_t limit = std::min(targetOffset, text.size());
    std::size_t offset = 0;
    while (offset < limit) {
        if (text[offset] == '\n') {
            ++position.line;
            position.character = 0;
            ++offset;
            continue;
        }
        std::size_t length = 1;
        const std::uint32_t codePoint = decodeUtf8CodePoint(text, offset, length);
        position.character += codePoint > 0xFFFFu ? 2u : 1u;
        offset += std::max<std::size_t>(length, 1);
    }
    return position;
}

static std::size_t sourceOffsetForLspPosition(const std::string &text,
                                              LspPosition position) noexcept {
    std::size_t offset = 0;
    std::size_t line = 0;
    while (offset < text.size() && line < position.line) {
        if (text[offset++] == '\n') ++line;
    }
    if (line != position.line) return text.size();

    std::size_t character = 0;
    while (offset < text.size() && text[offset] != '\n' && character < position.character) {
        std::size_t length = 1;
        const std::uint32_t codePoint = decodeUtf8CodePoint(text, offset, length);
        const std::size_t units = codePoint > 0xFFFFu ? 2u : 1u;
        if (character + units > position.character) break;
        character += units;
        offset += std::max<std::size_t>(length, 1);
    }
    return offset;
}

static std::string lspPositionJson(LspPosition position) {
    return "{\"line\":" + std::to_string(position.line) +
           ",\"character\":" + std::to_string(position.character) + "}";
}

static std::string lspRangeJson(const std::string &text,
                                const vietvm::frontend::SourceSpan &span) {
    return "{\"start\":" + lspPositionJson(lspPositionForOffset(text, span.begin.offset)) +
           ",\"end\":" + lspPositionJson(lspPositionForOffset(text, span.end.offset)) + "}";
}

static std::string percentDecode(std::string value) {
    std::string decoded;
    decoded.reserve(value.size());
    auto hexValue = [](char value) -> int {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        return -1;
    };
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] == '%' && index + 2 < value.size()) {
            const int high = hexValue(value[index + 1]);
            const int low = hexValue(value[index + 2]);
            if (high >= 0 && low >= 0) {
                decoded.push_back(static_cast<char>((high << 4) | low));
                index += 2;
                continue;
            }
        }
        decoded.push_back(value[index]);
    }
    return decoded;
}

static fs::path filePathFromUri(const std::string &uri) {
    constexpr std::string_view prefix = "file://";
    if (uri.rfind(prefix.data(), 0) != 0) return {};
    std::string path = percentDecode(uri.substr(prefix.size()));
#ifdef _WIN32
    if (path.size() >= 3 && path[0] == '/' && std::isalpha(static_cast<unsigned char>(path[1])) &&
        path[2] == ':') {
        path.erase(path.begin());
    }
#endif
    return vietvm::core::utf8Path(path);
}

static std::string fileUriFromPath(const fs::path &path) {
    fs::path absolute = path;
    try {
        absolute = fs::absolute(path).lexically_normal();
    } catch (...) {
        absolute = path.lexically_normal();
    }

    const std::string raw = absolute.generic_u8string();
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(raw.size() + 16);
    for (unsigned char value : raw) {
        const bool safe = std::isalnum(value) || value == '/' || value == ':' ||
                          value == '-' || value == '_' || value == '.' || value == '~';
        if (safe) {
            encoded.push_back(static_cast<char>(value));
        } else {
            encoded.push_back('%');
            encoded.push_back(hex[(value >> 4) & 0x0F]);
            encoded.push_back(hex[value & 0x0F]);
        }
    }
#ifdef _WIN32
    return "file:///" + encoded;
#else
    return "file://" + encoded;
#endif
}

static bool analyzeLspDocument(const std::string &uri,
                               const std::string &text,
                               vietvm::compiler::CompilationContext &context,
                               vietvm::compiler::CompilationArtifacts &artifacts) {
    const fs::path sourcePath = filePathFromUri(uri);
    context.importResolutionBase = sourcePath.empty() || sourcePath.parent_path().empty()
        ? fs::current_path()
        : sourcePath.parent_path();
    context.currentSourceIdentity = sourcePath.empty()
                                        ? uri
                                        : sourcePath.lexically_normal().generic_u8string();
    try {
        artifacts = vietvm::compiler::compilePipeline(context, text, keywordMap, false);
        return true;
    } catch (...) {
        return false;
    }
}

static bool spanContainsOffset(const vietvm::frontend::SourceSpan &span,
                               std::size_t offset) noexcept {
    return span.begin.offset <= offset && offset < span.end.offset;
}

static const vietvm::frontend::AstStatement *findStatementByTokenBegin(
    const std::vector<vietvm::frontend::AstStatement> &statements,
    std::size_t tokenBegin) {
    for (const auto &statement : statements) {
        if (statement.tokenBegin == tokenBegin) return &statement;
        if (const auto *nested = findStatementByTokenBegin(statement.children, tokenBegin)) {
            return nested;
        }
    }
    return nullptr;
}

static vietvm::frontend::SourceSpan lspDeclarationSpan(
    const vietvm::compiler::CompilationArtifacts &artifacts,
    const vietvm::compiler::SemanticSymbol &symbol) {
    for (const auto &entry : artifacts.semantic.declarationSymbols) {
        if (entry.second != static_cast<int>(symbol.id)) continue;
        const auto *statement = findStatementByTokenBegin(artifacts.ast.statements, entry.first);
        if (statement == nullptr) continue;
        const std::size_t end = std::min(statement->tokenEnd, artifacts.ast.tokens.size());
        std::size_t headerEnd = end;
        for (std::size_t index = statement->tokenBegin; index < end; ++index) {
            const auto &lexeme = artifacts.ast.tokens[index].lexeme;
            if (lexeme == "(" || lexeme == "{" || lexeme == ";") {
                headerEnd = index;
                break;
            }
        }
        for (std::size_t begin = statement->tokenBegin; begin < headerEnd; ++begin) {
            std::string candidate;
            for (std::size_t finish = begin; finish < headerEnd; ++finish) {
                if (!candidate.empty()) candidate.push_back(' ');
                candidate += artifacts.ast.tokens[finish].lexeme;
                if (candidate == symbol.lookupName) {
                    auto span = artifacts.ast.tokens[begin].span;
                    span.end = artifacts.ast.tokens[finish].span.end;
                    return span;
                }
                if (candidate.size() >= symbol.lookupName.size()) break;
            }
        }
    }
    return symbol.declaration;
}

static const vietvm::compiler::SemanticSymbol *symbolAtLspPosition(
    const vietvm::compiler::CompilationArtifacts &artifacts,
    std::size_t offset) {
    const auto &model = artifacts.semantic;
    for (const auto &symbol : model.symbols) {
        if (symbol.origin == vietvm::compiler::SymbolOrigin::Source &&
            spanContainsOffset(lspDeclarationSpan(artifacts, symbol), offset)) {
            return &symbol;
        }
    }

    for (const auto &reference : model.references) {
        if (!spanContainsOffset(reference.span, offset) || reference.resolvedSymbolId < 0) continue;
        const std::size_t symbolId = static_cast<std::size_t>(reference.resolvedSymbolId);
        if (symbolId < model.symbols.size()) return &model.symbols[symbolId];
    }

    const vietvm::frontend::AstExpression *bestExpression = nullptr;
    for (const auto &expression : artifacts.ast.expressions) {
        if (!spanContainsOffset(expression.span, offset)) continue;
        if (bestExpression == nullptr ||
            (expression.span.end.offset - expression.span.begin.offset) <
                (bestExpression->span.end.offset - bestExpression->span.begin.offset)) {
            bestExpression = &expression;
        }
    }
    if (bestExpression != nullptr) {
        const auto *binding = model.bindingForExpression(bestExpression->id);
        if (binding != nullptr && binding->kind == vietvm::compiler::BindingKind::Symbol &&
            binding->symbol < model.symbols.size()) {
            return &model.symbols[binding->symbol];
        }
    }
    return nullptr;
}

static vietvm::frontend::SourceSpan lspSymbolSpanAtPosition(
    const vietvm::compiler::CompilationArtifacts &artifacts,
    const vietvm::compiler::SemanticSymbol &symbol,
    std::size_t offset) {
    const auto declaration = lspDeclarationSpan(artifacts, symbol);
    if (spanContainsOffset(declaration, offset)) return declaration;

    for (const auto &reference : artifacts.semantic.references) {
        if (reference.resolvedSymbolId != static_cast<int>(symbol.id) ||
            !spanContainsOffset(reference.span, offset)) {
            continue;
        }
        return reference.span;
    }

    const vietvm::frontend::AstExpression *bestExpression = nullptr;
    for (const auto &expression : artifacts.ast.expressions) {
        if (!spanContainsOffset(expression.span, offset)) continue;
        const auto *binding = artifacts.semantic.bindingForExpression(expression.id);
        if (binding == nullptr || binding->kind != vietvm::compiler::BindingKind::Symbol ||
            binding->symbol != symbol.id) {
            continue;
        }
        if (bestExpression == nullptr ||
            (expression.span.end.offset - expression.span.begin.offset) <
                (bestExpression->span.end.offset - bestExpression->span.begin.offset)) {
            bestExpression = &expression;
        }
    }
    return bestExpression == nullptr ? declaration : bestExpression->span;
}

static const char *lspSymbolKindName(vietvm::compiler::SemanticSymbolKind kind) noexcept {
    using vietvm::compiler::SemanticSymbolKind;
    switch (kind) {
        case SemanticSymbolKind::Function: return "hàm";
        case SemanticSymbolKind::ForeignFunction: return "ngoại hàm";
        case SemanticSymbolKind::Method: return "phương thức";
        case SemanticSymbolKind::Class: return "lớp";
        case SemanticSymbolKind::Interface: return "giao diện";
        case SemanticSymbolKind::Parameter: return "tham số";
        case SemanticSymbolKind::GlobalVariable: return "biến toàn cục";
        case SemanticSymbolKind::LocalVariable: return "biến cục bộ";
        case SemanticSymbolKind::ImportAlias: return "bí danh mô-đun";
        case SemanticSymbolKind::CatchVariable: return "biến bắt lỗi";
    }
    return "ký hiệu";
}

static int lspCompletionKind(vietvm::compiler::SemanticSymbolKind kind) noexcept {
    using vietvm::compiler::SemanticSymbolKind;
    switch (kind) {
        case SemanticSymbolKind::Method: return 2;
        case SemanticSymbolKind::Function: return 3;
        case SemanticSymbolKind::ForeignFunction: return 3;
        case SemanticSymbolKind::Class: return 7;
        case SemanticSymbolKind::Interface: return 8;
        case SemanticSymbolKind::ImportAlias: return 9;
        case SemanticSymbolKind::Parameter:
        case SemanticSymbolKind::GlobalVariable:
        case SemanticSymbolKind::LocalVariable:
        case SemanticSymbolKind::CatchVariable:
            return 6;
    }
    return 1;
}

static vietvm::compiler::ScopeId lspScopeAtOffset(
    const vietvm::compiler::SemanticModel &model,
    std::size_t offset) noexcept {
    vietvm::compiler::ScopeId best = model.globalScope;
    std::size_t bestWidth = std::numeric_limits<std::size_t>::max();
    for (const auto &scope : model.scopes) {
        if (!spanContainsOffset(scope.span, offset)) continue;
        const std::size_t width = scope.span.end.offset - scope.span.begin.offset;
        if (width <= bestWidth) {
            best = scope.id;
            bestWidth = width;
        }
    }
    return best;
}

static bool lspScopeVisibleFrom(const vietvm::compiler::SemanticModel &model,
                                vietvm::compiler::ScopeId declarationScope,
                                vietvm::compiler::ScopeId currentScope) noexcept {
    while (currentScope != vietvm::compiler::kInvalidScopeId && currentScope < model.scopes.size()) {
        if (currentScope == declarationScope) return true;
        currentScope = model.scopes[currentScope].parent;
    }
    return false;
}

static std::string lspCompletionResult(
    const vietvm::compiler::CompilationArtifacts &artifacts,
    std::size_t offset) {
    const auto &model = artifacts.semantic;
    const auto currentScope = lspScopeAtOffset(model, offset);
    std::set<std::string> emitted;
    std::ostringstream result;
    result << "{\"isIncomplete\":false,\"items\":[";
    bool first = true;
    for (const auto &symbol : model.symbols) {
        if (!lspScopeVisibleFrom(model, symbol.declaringScope, currentScope)) continue;
        if (!emitted.insert(symbol.lookupName).second) continue;
        if (!first) result << ',';
        first = false;
        result << "{\"label\":\"" << jsonEscape(symbol.lookupName)
               << "\",\"kind\":" << lspCompletionKind(symbol.kind)
               << ",\"detail\":\"" << jsonEscape(lspSymbolKindName(symbol.kind));
        if ((symbol.kind == vietvm::compiler::SemanticSymbolKind::Function ||
             symbol.kind == vietvm::compiler::SemanticSymbolKind::ForeignFunction ||
             symbol.kind == vietvm::compiler::SemanticSymbolKind::Method) &&
            symbol.parameterCount > 0) {
            result << " · " << symbol.minimumArgumentCount << ".." << symbol.parameterCount
                   << " tham số";
        }
        result << "\"}";
    }
    result << "]}";
    return result.str();
}

static std::string lspHoverText(const vietvm::compiler::SemanticSymbol &symbol) {
    std::ostringstream text;
    text << lspSymbolKindName(symbol.kind) << ' ' << symbol.lookupName;
    if (symbol.kind == vietvm::compiler::SemanticSymbolKind::Function ||
        symbol.kind == vietvm::compiler::SemanticSymbolKind::ForeignFunction ||
        symbol.kind == vietvm::compiler::SemanticSymbolKind::Method) {
        text << " — " << symbol.minimumArgumentCount << ".." << symbol.parameterCount
             << " tham số";
    }
    if (symbol.origin == vietvm::compiler::SymbolOrigin::Imported) text << " — đã nhập";
    return text.str();
}

static bool validRenameIdentifier(const std::string &name) {
    if (name.empty()) return false;
    try {
        const auto tokens = vietvm::compiler::postProcessTokensWithSpans(
            vietvm::compiler::tokenizeWithSpans(name));
        return tokens.size() == 1 && tokens.front().lexeme == name &&
               tokens.front().kind == vietvm::frontend::TokenKind::Identifier;
    } catch (...) {
        return false;
    }
}

static std::vector<vietvm::frontend::SourceSpan> lspRenameSpans(
    const vietvm::compiler::CompilationArtifacts &artifacts,
    const vietvm::compiler::SemanticSymbol &symbol) {
    std::vector<vietvm::frontend::SourceSpan> spans;
    std::set<std::pair<std::size_t, std::size_t>> seen;
    auto addSpan = [&](const vietvm::frontend::SourceSpan &span) {
        const auto key = std::make_pair(span.begin.offset, span.end.offset);
        if (seen.insert(key).second) spans.push_back(span);
    };
    addSpan(lspDeclarationSpan(artifacts, symbol));
    for (const auto &expression : artifacts.ast.expressions) {
        const auto *binding = artifacts.semantic.bindingForExpression(expression.id);
        if (binding != nullptr && binding->kind == vietvm::compiler::BindingKind::Symbol &&
            binding->symbol == symbol.id) {
            addSpan(expression.span);
        }
    }
    for (const auto &reference : artifacts.semantic.references) {
        if (reference.resolvedSymbolId == static_cast<int>(symbol.id)) addSpan(reference.span);
    }
    std::sort(spans.begin(), spans.end(), [](const auto &left, const auto &right) {
        return left.begin.offset < right.begin.offset;
    });
    return spans;
}

// Ghi LSP thông báo; hàm tuần tự hóa hoặc chuyển dữ liệu đầu vào sang đích ghi tương ứng.
static void writeLspMessage(const std::string &payload) {
    std::cout << "Content-Length: " << payload.size() << "\r\n\r\n" << payload << std::flush;
}

static fs::path findLspLibraryCatalog(const fs::path &workspaceRoot) {
    if (!workspaceRoot.empty()) {
        const fs::path candidate = workspaceRoot / "vpp-libraries.json";
        if (fs::is_regular_file(candidate)) return candidate;
    }
    if (const char *home = std::getenv(vietvm::core::kEnvVppHome)) {
        const fs::path candidate = fs::u8path(home) / "vpp-libraries.json";
        if (fs::is_regular_file(candidate)) return candidate;
    }
    for (fs::path dir = workspaceRoot.empty() ? fs::current_path() : workspaceRoot;
         ; dir = dir.parent_path()) {
        const fs::path candidate = dir / "vpp-libraries.json";
        if (fs::is_regular_file(candidate)) return candidate;
        if (dir == dir.parent_path()) break;
    }
    return {};
}

static std::string lspLibraryRootsResult(const fs::path &workspaceRoot) {
    const fs::path catalogPath = findLspLibraryCatalog(workspaceRoot);
    if (catalogPath.empty()) {
        return "{\"revision\":\"\",\"catalogUri\":null,\"libraries\":[],"
               "\"diagnostics\":[{\"severity\":\"error\",\"message\":"
               "\"Không tìm thấy vpp-libraries.json từ VPP_HOME hoặc thư mục dự án.\"}]}";
    }

    std::string catalog;
    try {
        catalog = readFile(catalogPath.u8string());
    } catch (const std::exception &error) {
        return "{\"revision\":\"\",\"catalogUri\":\"" +
               jsonEscape(fileUriFromPath(catalogPath)) +
               "\",\"libraries\":[],\"diagnostics\":[{\"severity\":\"error\","
               "\"message\":\"" + jsonEscape(error.what()) + "\"}]}";
    }

    const std::string sdkVersion = extractJsonStringField(catalog, "sdkVersion");
    std::uint64_t revisionHash = 1469598103934665603ull;
    for (unsigned char byte : catalog) {
        revisionHash ^= static_cast<std::uint64_t>(byte);
        revisionHash *= 1099511628211ull;
    }
    std::ostringstream revision;
    revision << sdkVersion << ':' << std::hex << revisionHash;
    std::ostringstream result;
    result << "{\"revision\":\"" << jsonEscape(revision.str())
           << "\",\"catalogUri\":\"" << jsonEscape(fileUriFromPath(catalogPath))
           << "\",\"libraries\":[";

    // `moduleGroups` cũng có trường `libraries`; mảng thư viện cấp top-level
    // nằm sau cùng trong schema 1, nên lấy occurrence cuối để không nhầm với
    // danh sách id của từng nhóm.
    const std::size_t librariesKey = catalog.rfind("\"libraries\"");
    const std::size_t arrayBegin = librariesKey == std::string::npos
        ? std::string::npos
        : catalog.find('[', librariesKey);
    bool first = true;
    if (arrayBegin != std::string::npos) {
        std::size_t depth = 0;
        std::size_t objectBegin = std::string::npos;
        for (std::size_t index = arrayBegin + 1; index < catalog.size(); ++index) {
            const char ch = catalog[index];
            if (ch == '{') {
                if (depth == 0) objectBegin = index;
                ++depth;
                continue;
            }
            if (ch == '}') {
                if (depth == 0) continue;
                --depth;
                if (depth != 0 || objectBegin == std::string::npos) continue;
                const std::string object = catalog.substr(objectBegin, index - objectBegin + 1);
                const std::string id = extractJsonStringField(object, "id");
                const std::string sourceRoot = extractJsonStringField(object, "sourceRoot");
                const std::string entry = extractJsonStringField(object, "entry");
                if (id.empty() || sourceRoot.empty() || entry.empty()) continue;
                const fs::path root = catalogPath.parent_path() / fs::u8path(sourceRoot);
                const fs::path entryPath = root / fs::u8path(entry);
                if (!first) result << ',';
                first = false;
                result << "{\"id\":\"" << jsonEscape(id)
                       << "\",\"displayName\":\""
                       << jsonEscape(extractJsonStringField(object, "displayName"))
                       << "\",\"version\":\""
                       << jsonEscape(extractJsonStringField(object, "version"))
                       << "\",\"stability\":\""
                       << jsonEscape(extractJsonStringField(object, "stability"))
                       << "\",\"kind\":\""
                       << jsonEscape(extractJsonStringField(object, "kind"))
                       << "\",\"sourceRoot\":\"" << jsonEscape(sourceRoot)
                       << "\",\"entry\":\"" << jsonEscape(entry)
                       << "\",\"rootUri\":\"" << jsonEscape(fileUriFromPath(root))
                       << "\",\"entryUri\":\"" << jsonEscape(fileUriFromPath(entryPath))
                       << "\"}";
            }
            if (depth == 0 && ch == ']') break;
        }
    }
    result << "],\"diagnostics\":[]}";
    return result.str();
}

// Gửi `textDocument/publishDiagnostics` qua LSP; hàm chuyển diagnostic compiler thành JSON-RPC notification kèm range/message.
static void publishDiagnostics(const std::string &uri, const std::string &text) {
    const fs::path sourcePath = filePathFromUri(uri);
    const fs::path resolutionBase = sourcePath.empty() || sourcePath.parent_path().empty()
        ? fs::current_path()
        : sourcePath.parent_path();
    const auto diagnostics = vietvm::tooling::lintDiagnostics(text, resolutionBase);
    std::ostringstream diagnosticJson;
    diagnosticJson << '[';
    for (std::size_t index = 0; index < diagnostics.size(); ++index) {
        if (index != 0) diagnosticJson << ',';
        const auto &diagnostic = diagnostics[index];
        const auto &span = diagnostic.span;
        const int severity =
            diagnostic.severity == vietvm::tooling::DiagnosticSeverity::Error ? 1 : 2;
        diagnosticJson
            << "{\"range\":" << lspRangeJson(text, span)
            << ",\"severity\":" << severity
            << ",\"source\":\"vpp\",\"message\":\""
            << jsonEscape(diagnostic.message) << "\"}";
    }
    diagnosticJson << ']';

    std::ostringstream notif;
    notif << "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{"
          << "\"uri\":\"" << jsonEscape(uri) << "\","
          << "\"diagnostics\":" << diagnosticJson.str() << "}}";
    writeLspMessage(notif.str());
}

// Chạy language máy chủ; hàm điều phối toàn bộ luồng xử lý của tác vụ, gọi các bước con theo thứ tự và trả mã/kết quả cuối cùng.
static int runLanguageServer() {
    std::unordered_map<std::string, std::string> openDocuments;
    bool shutdownRequested = false;
    fs::path workspaceRoot;

    while (true) {
        auto payload = readLspMessage();
        if (!payload.has_value()) break;
        const std::string &body = payload.value();
        std::string method = extractJsonStringField(body, "method");
        std::string id = extractJsonRawField(body, "id");

        if (method == "initialize") {
            const std::string rootUri = extractJsonStringField(body, "rootUri");
            if (!rootUri.empty()) workspaceRoot = filePathFromUri(rootUri);
            std::ostringstream response;
            response << "{\"jsonrpc\":\"2.0\",\"id\":" << (id.empty() ? "null" : id)
                     << ",\"result\":{\"capabilities\":{"
                     << "\"textDocumentSync\":1,"
                     << "\"documentFormattingProvider\":true,"
                     << "\"completionProvider\":{\"triggerCharacters\":[\".\"]},"
                     << "\"definitionProvider\":true,"
                     << "\"hoverProvider\":true,"
                     << "\"renameProvider\":true"
                     << "}}}";
            writeLspMessage(response.str());
            continue;
        }

        if (method == "shutdown") {
            shutdownRequested = true;
            std::ostringstream response;
            response << "{\"jsonrpc\":\"2.0\",\"id\":" << (id.empty() ? "null" : id)
                     << ",\"result\":null}";
            writeLspMessage(response.str());
            continue;
        }

        if (method == "vpp/libraryRoots") {
            std::ostringstream response;
            response << "{\"jsonrpc\":\"2.0\",\"id\":" << (id.empty() ? "null" : id)
                     << ",\"result\":" << lspLibraryRootsResult(workspaceRoot) << '}';
            writeLspMessage(response.str());
            continue;
        }

        if (method == "workspace/didChangeWatchedFiles" &&
            body.find("vpp-libraries.json") != std::string::npos) {
            const std::string snapshot = lspLibraryRootsResult(workspaceRoot);
            writeLspMessage(
                "{\"jsonrpc\":\"2.0\",\"method\":\"vpp/libraryRootsChanged\","
                "\"params\":" + snapshot + "}");
            continue;
        }

        if (method == "exit") {
            break;
        }

        if (method == "textDocument/didOpen" || method == "textDocument/didChange") {
            std::string uri = extractJsonStringField(body, "uri");
            std::string text = extractJsonStringField(body, "text");
            if (!uri.empty()) {
                openDocuments[uri] = text;
                publishDiagnostics(uri, text);
            }
            continue;
        }

        if (method == "textDocument/didClose") {
            const std::string uri = extractJsonStringField(body, "uri");
            if (!uri.empty()) {
                openDocuments.erase(uri);
                std::ostringstream notif;
                notif << "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\","
                      << "\"params\":{\"uri\":\"" << jsonEscape(uri)
                      << "\",\"diagnostics\":[]}}";
                writeLspMessage(notif.str());
            }
            continue;
        }

        if (method == "textDocument/completion" ||
            method == "textDocument/definition" ||
            method == "textDocument/hover" ||
            method == "textDocument/rename" ||
            method == "textDocument/formatting") {
            const std::string uri = extractJsonStringField(body, "uri");
            const auto document = openDocuments.find(uri);
            if (document == openDocuments.end()) {
                std::ostringstream response;
                response << "{\"jsonrpc\":\"2.0\",\"id\":" << (id.empty() ? "null" : id)
                         << ",\"result\":"
                         << (method == "textDocument/completion" ? "{\"isIncomplete\":false,\"items\":[]}" : "null")
                         << '}';
                writeLspMessage(response.str());
                continue;
            }

            const std::string &text = document->second;
            if (method == "textDocument/formatting") {
                const std::string formatted = vietvm::tooling::formatSource(text);
                std::ostringstream response;
                response << "{\"jsonrpc\":\"2.0\",\"id\":" << (id.empty() ? "null" : id)
                         << ",\"result\":";
                if (formatted == text) {
                    response << "[]";
                } else {
                    const LspPosition end = lspPositionForOffset(text, text.size());
                    response << "[{\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":"
                             << lspPositionJson(end) << "},\"newText\":\""
                             << jsonEscape(formatted) << "\"}]";
                }
                response << '}';
                writeLspMessage(response.str());
                continue;
            }

            LspPosition position;
            if (!extractLspPosition(body, position)) {
                std::ostringstream response;
                response << "{\"jsonrpc\":\"2.0\",\"id\":" << (id.empty() ? "null" : id)
                         << ",\"result\":null}";
                writeLspMessage(response.str());
                continue;
            }

            vietvm::compiler::CompilationContext context;
            vietvm::compiler::CompilationArtifacts artifacts;
            if (!analyzeLspDocument(uri, text, context, artifacts)) {
                std::ostringstream response;
                response << "{\"jsonrpc\":\"2.0\",\"id\":" << (id.empty() ? "null" : id)
                         << ",\"result\":"
                         << (method == "textDocument/completion" ? "{\"isIncomplete\":false,\"items\":[]}" : "null")
                         << '}';
                writeLspMessage(response.str());
                continue;
            }

            const std::size_t offset = sourceOffsetForLspPosition(text, position);
            if (method == "textDocument/completion") {
                std::ostringstream response;
                response << "{\"jsonrpc\":\"2.0\",\"id\":" << (id.empty() ? "null" : id)
                         << ",\"result\":" << lspCompletionResult(artifacts, offset) << '}';
                writeLspMessage(response.str());
                continue;
            }

            const auto *symbol = symbolAtLspPosition(artifacts, offset);
            if (method == "textDocument/definition") {
                std::ostringstream response;
                response << "{\"jsonrpc\":\"2.0\",\"id\":" << (id.empty() ? "null" : id)
                         << ",\"result\":";
                if (symbol == nullptr) {
                    response << "null";
                } else if (symbol->origin == vietvm::compiler::SymbolOrigin::Imported &&
                           !symbol->sourceIdentity.empty()) {
                    try {
                        const fs::path definitionPath =
                            vietvm::core::utf8Path(symbol->sourceIdentity);
                        const std::string definitionText = readFile(symbol->sourceIdentity);
                        response << "{\"uri\":\""
                                 << jsonEscape(fileUriFromPath(definitionPath))
                                 << "\",\"range\":"
                                 << lspRangeJson(definitionText, symbol->declaration) << '}';
                    } catch (...) {
                        response << "null";
                    }
                } else {
                    const auto declaration = lspDeclarationSpan(artifacts, *symbol);
                    response << "{\"uri\":\"" << jsonEscape(uri) << "\",\"range\":"
                             << lspRangeJson(text, declaration) << '}';
                }
                response << '}';
                writeLspMessage(response.str());
                continue;
            }

            if (method == "textDocument/hover") {
                std::ostringstream response;
                response << "{\"jsonrpc\":\"2.0\",\"id\":" << (id.empty() ? "null" : id)
                         << ",\"result\":";
                if (symbol == nullptr) {
                    response << "null";
                } else {
                    const auto hoverSpan = lspSymbolSpanAtPosition(artifacts, *symbol, offset);
                    response << "{\"contents\":{\"kind\":\"plaintext\",\"value\":\""
                             << jsonEscape(lspHoverText(*symbol)) << "\"},\"range\":"
                             << lspRangeJson(text, hoverSpan) << '}';
                }
                response << '}';
                writeLspMessage(response.str());
                continue;
            }

            if (method == "textDocument/rename") {
                const std::string newName = extractJsonStringField(body, "newName");
                std::ostringstream response;
                response << "{\"jsonrpc\":\"2.0\",\"id\":" << (id.empty() ? "null" : id);
                if (symbol == nullptr || symbol->origin != vietvm::compiler::SymbolOrigin::Source ||
                    !validRenameIdentifier(newName)) {
                    response << ",\"result\":null}";
                } else {
                    const auto spans = lspRenameSpans(artifacts, *symbol);
                    response << ",\"result\":{\"changes\":{\"" << jsonEscape(uri) << "\":[";
                    for (std::size_t index = 0; index < spans.size(); ++index) {
                        if (index != 0) response << ',';
                        response << "{\"range\":" << lspRangeJson(text, spans[index])
                                 << ",\"newText\":\"" << jsonEscape(newName) << "\"}";
                    }
                    response << "]}}}";
                }
                writeLspMessage(response.str());
                continue;
            }
        }
    }

    return shutdownRequested ? EXIT_SUCCESS : EXIT_FAILURE;
}


// Chạy CLI với argv đã chuẩn hóa UTF-8. Trên Windows, entry point `wmain`
// chuyển command line UTF-16 sang UTF-8 trước khi đi vào parser này.
static int uninstallVpp(const std::string &argv0) {
#if defined(_WIN32) || defined(__APPLE__) || defined(__linux__)
    (void)argv0;
#endif
#if defined(_WIN32)
    std::vector<wchar_t> executable(32768);
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (length == 0 || length >= executable.size()) {
        throw std::runtime_error(std::string(messages::kCliWindowsInstallDirectoryUnknown));
    }
    const fs::path installDir = fs::path(executable.data()).parent_path();
    const fs::path script = installDir / vietvm::cli::kWindowsUninstallScript;
    if (!fs::exists(script)) {
        throw std::runtime_error(std::string(messages::kCliWindowsUninstallerMissing));
    }
    wchar_t tempDirectory[MAX_PATH + 1]{};
    wchar_t tempScript[MAX_PATH + 1]{};
    const DWORD tempLength = GetTempPathW(MAX_PATH + 1, tempDirectory);
    if (tempLength == 0 || tempLength > MAX_PATH ||
        GetTempFileNameW(tempDirectory, L"vpp", 0, tempScript) == 0) {
        throw std::runtime_error(std::string(messages::kCliTempUninstallerCreateFailed));
    }
    // The running executable is locked on Windows. A temporary child script
    // waits for this process to exit before removing the installed files.
    const fs::path temporaryScript = std::wstring(tempScript) + L".ps1";
    try {
        fs::rename(tempScript, temporaryScript);
        fs::copy_file(script, temporaryScript, fs::copy_options::overwrite_existing);
        wchar_t systemDirectory[MAX_PATH + 1]{};
        const UINT systemLength = GetSystemDirectoryW(systemDirectory, MAX_PATH + 1);
        if (systemLength == 0 || systemLength > MAX_PATH) {
            throw std::runtime_error(std::string(messages::kCliWindowsPowerShellMissing));
        }
        const fs::path powershell = fs::path(systemDirectory) / vietvm::cli::kPowerShellRelativePath;
        std::wstring command = L"\"" + powershell.wstring() +
            L"\" -NoProfile -ExecutionPolicy Bypass -File \"" + temporaryScript.wstring() +
            L"\" -InstallDir \"" + installDir.wstring() +
            L"\" -WaitForProcessId " + std::to_wstring(GetCurrentProcessId()) +
            L" -RemoveSelf";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(powershell.c_str(), command.data(), nullptr, nullptr,
                            FALSE, 0, nullptr, nullptr, &startup, &process)) {
            throw std::runtime_error(std::string(messages::kCliUninstallerStartFailed));
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    } catch (...) {
        std::error_code ignored;
        fs::remove(tempScript, ignored);
        fs::remove(temporaryScript, ignored);
        throw;
    }
    std::cout << messages::kCliUninstallerStarted;
    return EXIT_SUCCESS;
#else
    fs::path executable;
#if defined(__APPLE__)
    std::uint32_t size = 0;
    (void)_NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size + 1, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        throw std::runtime_error(std::string(messages::kCliExecutablePathUnknown));
    }
    executable = fs::weakly_canonical(fs::path(buffer.data()));
#elif defined(__linux__)
    std::vector<char> buffer(PATH_MAX + 1, '\0');
    const ssize_t length = readlink("/proc/self/exe", buffer.data(), PATH_MAX);
    if (length <= 0) {
        throw std::runtime_error(std::string(messages::kCliExecutablePathUnknown));
    }
    buffer[static_cast<std::size_t>(length)] = '\0';
    executable = fs::path(buffer.data());
#else
    executable = fs::weakly_canonical(fs::absolute(vietvm::core::utf8Path(argv0)));
#endif
    const fs::path installDir = executable.parent_path();
    const fs::path script = installDir / vietvm::cli::kUnixUninstallScript;
    if (!fs::exists(script)) {
        throw std::runtime_error(std::string(messages::kCliUninstallerMissing));
    }

    const pid_t child = fork();
    if (child < 0) throw std::runtime_error(std::string(messages::kCliUninstallerStartFailed));
    if (child == 0) {
        execl("/bin/sh", "sh", script.c_str(), installDir.c_str(), static_cast<char *>(nullptr));
        _exit(127);
    }
    int status = 0;
    if (waitpid(child, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        throw std::runtime_error(std::string(messages::kCliUninstallerFailed));
    }
    return EXIT_SUCCESS;
#endif
}

static int runCli(int argc, char* argv[]) {
    try {
        if ((argc == 4 && std::string(argv[1]) == vietvm::cli::kUninstallWord1 &&
             std::string(argv[2]) == vietvm::cli::kUninstallWord2 && std::string(argv[3]) == vietvm::cli::kUninstallWord3) ||
            (argc == 2 && std::string(argv[1]) == vietvm::cli::kUninstallCommandVi)) {
            return uninstallVpp(argv[0]);
        }
        if (argc >= 2) {
            std::string command = argv[1];
            if (command == "--bootstrap-native" || command == "--emit-native") {
                if (argc != 4) throw std::runtime_error("Cú pháp: vpp --bootstrap-native|--emit-native <source.vi> <output>");
                return buildFullNative(argv[2], argv[3], command == "--bootstrap-native");
            }
            if (command == "--batch-run-tests") {
                if (argc < 4) return EXIT_FAILURE;
                std::vector<fs::path> testFiles;
                testFiles.reserve(static_cast<std::size_t>(argc - 3));
                for (int index = 3; index < argc; ++index) {
                    testFiles.push_back(vietvm::core::utf8Path(argv[index]));
                }
                return runBatchTestFiles(vietvm::core::utf8Path(argv[2]), testFiles);
            }
            int commandWordOffset = 0;
            if (argc >= 3) {
                std::string twoWordCommand = command + " " + std::string(argv[2]);
                if (twoWordCommand == vietvm::cli::kCmdHelpVi || twoWordCommand == vietvm::cli::kCmdVersionVi ||
                    twoWordCommand == vietvm::cli::kCmdDoctorVi || twoWordCommand == vietvm::cli::kCmdDiagnoseVi ||
                    twoWordCommand == vietvm::cli::kPkgListVi ||
                    twoWordCommand == vietvm::cli::kCmdInitVi || twoWordCommand == vietvm::cli::kPkgInfoVi ||
                    twoWordCommand == vietvm::cli::kPkgHasVi || twoWordCommand == vietvm::cli::kPkgStatsVi ||
                    twoWordCommand == vietvm::cli::kPkgInstallVi || twoWordCommand == vietvm::cli::kCmdTestVi ||
                    twoWordCommand == vietvm::cli::kPkgPublishVi || twoWordCommand == vietvm::cli::kPkgSyncVi ||
                    twoWordCommand == vietvm::cli::kPkgUpdateVi || twoWordCommand == vietvm::cli::kPkgRestoreVi ||
                    twoWordCommand == vietvm::cli::kCmdDisassembleVi ||
                    twoWordCommand == vietvm::cli::kCmdFormatVi ||
                    twoWordCommand == vietvm::cli::kCmdLintVi) {
                    command = twoWordCommand;
                    commandWordOffset = 1;
                }
            }
            if (command == vietvm::cli::kCmdHelpLong || command == vietvm::cli::kCmdHelpShort || command == vietvm::cli::kCmdHelp || command == vietvm::cli::kCmdCommands || command == vietvm::cli::kCmdHelpVi) {
                printUsage();
                return EXIT_SUCCESS;
            }
            if (command == vietvm::cli::kCmdVersionLong || command == vietvm::cli::kCmdVersionShort || command == vietvm::cli::kCmdVersion || command == vietvm::cli::kCmdVersionVi) {
                printVersion();
                return EXIT_SUCCESS;
            }
            if (command == "--list-targets" || command == "targets") {
                if (argc != 2) {
                    std::cerr << "Cú pháp: vpp --list-targets\n";
                    return EXIT_FAILURE;
                }
                printTargets();
                return EXIT_SUCCESS;
            }
            if (command == vietvm::cli::kCmdDoctor || command == vietvm::cli::kCmdDoctorVi || command == vietvm::cli::kCmdDiagnoseVi) {
                return runDoctor(argv[0]);
            }
            if (command == vietvm::cli::kCmdWhere || command == vietvm::cli::kCmdWhereVi) {
                std::cout << fs::current_path().u8string() << std::endl;
                return EXIT_SUCCESS;
            }
            if (command == vietvm::cli::kCmdLsp) {
                return runLanguageServer();
            }
            if (command == vietvm::cli::kCmdRepl) {
                return runRepl();
            }
            if (command == vietvm::cli::kCmdRun || command == vietvm::cli::kCmdRunVi) {
                const int firstArgument = 2 + commandWordOffset;
                if (argc > firstArgument && std::string(argv[firstArgument]) == "--artifact") {
                    if (argc != firstArgument + 2) {
                        std::cerr << "Cú pháp: vpp run --artifact <output.vbc>\n";
                        return EXIT_FAILURE;
                    }
                    return runVppCompiler(argv[firstArgument + 1], "artifact");
                }
                if (argc > firstArgument &&
                    std::string(argv[firstArgument]).rfind("--compiler=", 0) == 0) {
                    const std::string choice = argv[firstArgument];
                    if (argc != firstArgument + 2 ||
                        (choice != "--compiler=vpp" && choice != "--compiler=cpp")) {
                        std::cerr << "Cú pháp: vpp run --compiler=vpp|cpp <source.vi>\n";
                        return EXIT_FAILURE;
                    }
                    if (choice == "--compiler=vpp")
                        return runVppCompiler(argv[firstArgument + 1], "run");
                    return runFile(argv[firstArgument + 1], SnippetMode::Execute);
                }
                if (argc < (3 + commandWordOffset)) {
                    std::cerr << messages::formatMessage(messages::kCliRunMissingFile) << '\n';
                    return EXIT_FAILURE;
                }
                return runFile(argv[2 + commandWordOffset], SnippetMode::Execute);
            }
            if (command == vietvm::cli::kCmdTest || command == vietvm::cli::kCmdTestVi || command == vietvm::cli::kCmdTestViHyphen) {
                const fs::path path =
                    argc >= (3 + commandWordOffset)
                        ? vietvm::core::utf8Path(argv[2 + commandWordOffset])
                        : vietvm::core::utf8Path("tests");
                return runTests(path);
            }
            if (command == vietvm::cli::kCmdBuild || command == vietvm::cli::kCmdBuildVi) {
                const int firstArgument = 2 + commandWordOffset;
                if (argc > firstArgument &&
                    std::string(argv[firstArgument]).rfind("--compiler=", 0) == 0) {
                    const std::string choice = argv[firstArgument];
                    const bool withOutput = choice == "--compiler=vpp" &&
                        argc == firstArgument + 4 &&
                        std::string(argv[firstArgument + 2]) == "-o";
                    if ((!withOutput && argc != firstArgument + 2) ||
                        (choice != "--compiler=vpp" && choice != "--compiler=cpp")) {
                        std::cerr << "Cú pháp: vpp build --compiler=vpp <source.vi> [-o output.vbc]\n"
                                  << "   hoặc: vpp build --compiler=cpp <source.vi>\n";
                        return EXIT_FAILURE;
                    }
                    if (choice == "--compiler=vpp")
                        return runVppCompiler(argv[firstArgument + 1], "build",
                            withOutput ? argv[firstArgument + 3] : "");
                    return buildFile(argv[firstArgument + 1]);
                }
                if (argc > firstArgument && std::string(argv[firstArgument]) == "--target") {
                    const bool nativeExecutable = argc >= firstArgument + 3 &&
                        std::string(argv[firstArgument + 2]) == "--emit-exe-selfhost";
                    const bool rawLeaf = argc >= firstArgument + 3 &&
                        (std::string(argv[firstArgument + 2]) == "--emit-raw" ||
                         std::string(argv[firstArgument + 2]) == "--emit-asm" ||
                         std::string(argv[firstArgument + 2]) == "--emit-obj" ||
                         std::string(argv[firstArgument + 2]) == "--emit-obj-selfhost");
                    if (argc != firstArgument + (rawLeaf ? 6 : nativeExecutable ? 5 : 3)) {
                        std::cerr << "Cú pháp: vpp build --target <triple> <source.vi>\n"
                                  << "   hoặc: vpp build --target <triple> --emit-raw "
                                     "<function> <source.vi> <output.bin>\n"
                                  << "   hoặc: vpp build --target <triple> --emit-asm "
                                     "<function> <source.vi> <output.s>\n"
                                  << "   hoặc: vpp build --target <triple> --emit-obj "
                                     "<function> <source.vi> <output.o>\n"
                                  << "   hoặc: vpp build --target <triple> --emit-obj-selfhost "
                                     "<function> <source.vi> <output.o>\n"
                                  << "   hoặc: vpp build --target <triple> --emit-exe-selfhost "
                                     "<source.vi> <output>\n";
                        return EXIT_FAILURE;
                    }
                    const auto parsed = vietvm::native::parseTargetVpp(
                        argv[firstArgument + 1], fs::current_path());
                    if (!parsed.target) {
                        std::cerr << "Target không hợp lệ: " << parsed.error << '\n';
                        return EXIT_FAILURE;
                    }
                    if (nativeExecutable) {
                        return buildSelfHostedNativeExecutable(*parsed.target,
                            argv[firstArgument + 3], argv[firstArgument + 4]);
                    }
                    if (rawLeaf) {
                        if (std::string(argv[firstArgument + 2]) == "--emit-obj-selfhost") {
                            return buildSelfHostedNativeObject(*parsed.target,
                                argv[firstArgument + 3], argv[firstArgument + 4],
                                argv[firstArgument + 5]);
                        }
                        return buildRawLeafFile(*parsed.target,
                            argv[firstArgument + 3], argv[firstArgument + 4],
                            argv[firstArgument + 5], argv[firstArgument + 2]);
                    }
                    std::cerr << "Native AOT chưa được triển khai cho "
                              << vietvm::native::canonicalTriple(*parsed.target)
                              << " (" << vietvm::native::objectFormatName(parsed.target->objectFormat)
                              << "). 'vpp build <source.vi>' hiện chỉ xác thực biên dịch bytecode; "
                                 "JIT mã máy thử nghiệm chỉ chạy trên host tương ứng.\n";
                    return EXIT_FAILURE;
                }
                if (argc < (3 + commandWordOffset)) {
                    std::cerr << messages::formatMessage(messages::kCliBuildMissingFile) << '\n';
                    return EXIT_FAILURE;
                }
                return buildFile(argv[2 + commandWordOffset]);
            }
            if (command == vietvm::cli::kCmdDisassemble || command == vietvm::cli::kCmdDisassembleVi || command == vietvm::cli::kCmdDisassembleViHyphen) {
                if (argc < (3 + commandWordOffset)) {
                    std::cerr << messages::formatMessage(messages::kCliDisassembleMissingFile) << '\n';
                    return EXIT_FAILURE;
                }
                return runFile(argv[2 + commandWordOffset], SnippetMode::Disassemble);
            }
            if (command == vietvm::cli::kCmdDumpAst) {
                if (argc < (3 + commandWordOffset)) {
                    std::cerr << messages::formatMessage(messages::kCliDumpAstMissingFile) << '\n';
                    return EXIT_FAILURE;
                }
                return runFile(argv[2 + commandWordOffset], SnippetMode::DumpAst);
            }
            if (command == vietvm::cli::kCmdDumpIr) {
                if (argc < (3 + commandWordOffset)) {
                    std::cerr << messages::formatMessage(messages::kCliDumpIrMissingFile) << '\n';
                    return EXIT_FAILURE;
                }
                return runFile(argv[2 + commandWordOffset], SnippetMode::DumpIr);
            }
            if (command == vietvm::cli::kCmdLint || command == vietvm::cli::kCmdLintViHyphen || command == vietvm::cli::kCmdLintVi) {
                if (argc < (3 + commandWordOffset)) {
                    std::cerr << messages::formatMessage(messages::kCliLintMissingFile) << '\n';
                    return EXIT_FAILURE;
                }
                return runLintPath(vietvm::core::utf8Path(argv[2 + commandWordOffset]));
            }
            if (command == vietvm::cli::kCmdFormat || command == vietvm::cli::kCmdFormatVi || command == vietvm::cli::kCmdFormatViHyphen) {
                if (argc < (3 + commandWordOffset)) {
                    std::cerr << messages::formatMessage(messages::kCliFormatMissingFile) << '\n';
                    return EXIT_FAILURE;
                }
                const fs::path path = vietvm::core::utf8Path(argv[2 + commandWordOffset]);
                bool writeFiles = false;
                bool checkOnly = false;
                for (int i = 3 + commandWordOffset; i < argc; ++i) {
                    const std::string option = argv[i];
                    if (option == vietvm::cli::kCmdFormatInPlace || option == vietvm::cli::kCmdFormatInPlaceVi) writeFiles = true;
                    if (option == vietvm::cli::kCmdFormatCheck || option == vietvm::cli::kCmdFormatCheckVi) checkOnly = true;
                }
                return runFormatPath(path, writeFiles, checkOnly);
            }
            if (command == vietvm::cli::kCmdPackage || command == vietvm::cli::kCmdPackageVi) {
                return runPackageCommand(argc, argv);
            }
            if (command == vietvm::cli::kCmdInit || command == vietvm::cli::kCmdNew || command == vietvm::cli::kCmdInitVi) {
                std::string name = (argc >= (3 + commandWordOffset)) ? argv[2 + commandWordOffset] : "";
                int initSubWordOffset = 0;
                if (name == "ứng" && argc >= (4 + commandWordOffset) &&
                    std::string(argv[3 + commandWordOffset]) == "dụng") {
                    name = "ứng dụng";
                    initSubWordOffset = 1;
                }
                if (name == vietvm::cli::kApplicationVi || name == vietvm::cli::kApplicationViHyphen ||
                    name == vietvm::cli::kApplicationShort || name == vietvm::cli::kApplication) {
                    std::string applicationName =
                        (argc >= (4 + commandWordOffset + initSubWordOffset))
                            ? argv[3 + commandWordOffset + initSubWordOffset]
                            : "";
                    return applicationInit(applicationName);
                }
                if (name == vietvm::cli::kBackend) {
                    std::string backendName = (argc >= (4 + commandWordOffset)) ? argv[3 + commandWordOffset] : "";
                    return backendInit(backendName);
                }
                return pkgInit(name);
            }
            if (command == vietvm::cli::kPkgInstall || command == vietvm::cli::kPkgInstallAscii ||
                command == vietvm::cli::kPkgInstallAsciiCompact || command == vietvm::cli::kPkgInstallVi) {
                if (argc < (3 + commandWordOffset)) return pkgRestore(false);
                std::string sourceArg = argv[2 + commandWordOffset];
                if (isOfflineFlag(sourceArg)) return pkgRestore(true);
                std::string name = (argc >= (4 + commandWordOffset)) ? argv[3 + commandWordOffset] : "";
                return pkgAdd(sourceArg, name);
            }
            if (command == vietvm::cli::kPkgPublish || command == vietvm::cli::kPkgPublishViHyphen ||
                command == vietvm::cli::kPkgPublishVi) {
                if (argc < (3 + commandWordOffset)) {
                    std::cerr << messages::formatMessage(messages::kPkgPublishRegistryMissing) << '\n';
                    return EXIT_FAILURE;
                }
                return pkgPublish(argv[2 + commandWordOffset]);
            }
            if (command == vietvm::cli::kPkgList || command == vietvm::cli::kPkgListVi) {
                return pkgList();
            }
            if (command == vietvm::cli::kPkgSync || command == vietvm::cli::kPkgSyncVi) {
                return pkgSync();
            }
            if (command == vietvm::cli::kPkgUpdate || command == vietvm::cli::kPkgUpdateVi) {
                return pkgSync();
            }
            if (command == vietvm::cli::kPkgLock || command == vietvm::cli::kPkgLockVi) {
                return pkgLock();
            }
            if (command == vietvm::cli::kPkgRestore || command == vietvm::cli::kPkgRestoreVi) {
                const bool offlineOnly =
                    argc >= (3 + commandWordOffset) &&
                    isOfflineFlag(argv[2 + commandWordOffset]);
                return pkgRestore(offlineOnly);
            }
            if (command == vietvm::cli::kPkgRemove || command == vietvm::cli::kPkgRemoveAscii ||
                command == vietvm::cli::kPkgRemoveVi) {
                if (argc < (3 + commandWordOffset)) {
                    std::cerr << messages::formatMessage(messages::kPkgTopLevelRemoveNameMissing) << '\n';
                    return EXIT_FAILURE;
                }
                return pkgRemove(argv[2 + commandWordOffset]);
            }
            if (command == vietvm::cli::kPkgInfo || command == vietvm::cli::kPkgInfoVi) {
                if (argc < (3 + commandWordOffset)) {
                    std::cerr << messages::formatMessage(messages::kPkgTopLevelInfoNameMissing) << '\n';
                    return EXIT_FAILURE;
                }
                return pkgInfo(argv[2 + commandWordOffset]);
            }
            if (command == vietvm::cli::kPkgHas || command == vietvm::cli::kPkgHasVi) {
                if (argc < (3 + commandWordOffset)) {
                    std::cerr << messages::formatMessage(messages::kPkgTopLevelHasNameMissing) << '\n';
                    return EXIT_FAILURE;
                }
                return pkgHas(argv[2 + commandWordOffset]);
            }
            if (command == vietvm::cli::kPkgStats || command == vietvm::cli::kPkgStatsVi) {
                return pkgStats();
            }
        }

        // -----------------------------
        // Trường hợp có đối số (chạy file được chỉ định)
        // -----------------------------
        if (argc == 2) {
            return runFile(argv[1], SnippetMode::Execute);
        }

        // Regression legacy chỉ được phép chạy khi CLI thực sự không nhận đối
        // số. Một command có nhiều đối số nhưng không khớp parser không được
        // phép vô tình kích hoạt toàn bộ src/tests.
        if (argc > 1) {
            printUsage();
            return EXIT_FAILURE;
        }

        // -----------------------------
        // Nếu không có đối số → chạy file mặc định
        // -----------------------------
        const std::string defaultFile = "../../src/tests/kiem_tra_stdlib_tinh_toan.vi";
        if (argc == 1 && fs::exists(defaultFile)) {
            std::string source = readFile(defaultFile);
            const fs::path defaultPath = vietvm::core::utf8Path(defaultFile);
            return runSnippet(
                source,
                defaultPath.parent_path().empty()
                    ? fs::current_path()
                    : defaultPath.parent_path(),
                SnippetMode::Execute,
                defaultPath);
        }

        std::string testDir = "../../src/tests";
        if (fs::exists(testDir)) {
            for (const auto& entry : fs::directory_iterator(testDir)) {
                if (entry.path().extension() == ".vi") {
                    const std::string filename = entry.path().u8string();
                    std::cout << messages::messageText(messages::kCliTestRunning, {filename})
                              << std::endl;

                    std::string source = readFile(filename);
                    const fs::path testPath = vietvm::core::utf8Path(filename);
                    (void)runSnippet(
                        source,
                        testPath.parent_path().empty()
                            ? fs::current_path()
                            : testPath.parent_path(),
                        SnippetMode::Execute,
                        testPath);
                }
            }
        } else {
            printUsage();
        }

    } catch (const vietvm::runtime::RuntimeError &error) {
        printErrorMessage(messages::kCliUnhandledException,
                          vietvm::runtime::formatRuntimeError(error));
        return EXIT_FAILURE;
    } catch (const std::exception &ex) {
        printErrorMessage(messages::kCliUnhandledException, ex.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

#if defined(_WIN32)
// Console code pages belong to the shared console; restore them on exit.
// Redirected output remains UTF-8 bytes, including the LSP transport.
class WindowsUtf8ConsoleGuard {
public:
    WindowsUtf8ConsoleGuard()
        : input_(GetConsoleCP()), output_(GetConsoleOutputCP()) {
        if (input_ != 0) SetConsoleCP(CP_UTF8);
        if (output_ != 0) SetConsoleOutputCP(CP_UTF8);
    }
    ~WindowsUtf8ConsoleGuard() {
        std::cout.flush();
        std::cerr.flush();
        if (input_ != 0) SetConsoleCP(input_);
        if (output_ != 0) SetConsoleOutputCP(output_);
    }
private:
    UINT input_;
    UINT output_;
};

// Chuyển một đối số UTF-16 do Windows CRT cung cấp thành UTF-8 để toàn bộ
// parser CLI dùng cùng encoding với source code và thông báo tiếng Việt.
static std::string wideArgumentToUtf8(const wchar_t *argument) {
    if (argument == nullptr) return {};
    const int required = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, argument, -1, nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        throw std::runtime_error(std::string(messages::kCliWindowsArgumentUtf8Failed));
    }

    std::string utf8(static_cast<std::size_t>(required), '\0');
    const int written = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, argument, -1,
        utf8.data(), required, nullptr, nullptr);
    if (written != required) {
        throw std::runtime_error(std::string(messages::kCliWindowsArgumentUtf8Failed));
    }
    utf8.pop_back();
    return utf8;
}

// Windows truyền command line Unicode qua `wmain`; chuẩn hóa một lần tại biên
// hệ điều hành rồi giữ parser/command contract nội bộ hoàn toàn bằng UTF-8.
int wmain(int argc, wchar_t *argv[]) {
    WindowsUtf8ConsoleGuard consoleEncoding;
    try {
        std::vector<std::string> utf8Arguments;
        utf8Arguments.reserve(static_cast<std::size_t>(argc));
        for (int index = 0; index < argc; ++index) {
            utf8Arguments.push_back(wideArgumentToUtf8(argv[index]));
        }

        std::vector<char *> utf8Argv;
        utf8Argv.reserve(utf8Arguments.size());
        for (std::string &argument : utf8Arguments) {
            utf8Argv.push_back(argument.data());
        }
        return runCli(static_cast<int>(utf8Argv.size()), utf8Argv.data());
    } catch (const std::exception &error) {
        printErrorMessage(messages::kCliUnhandledException, error.what());
        return EXIT_FAILURE;
    }
}
#else
// POSIX argv đã được môi trường CI/shell cung cấp dưới dạng UTF-8.
int main(int argc, char* argv[]) {
    return runCli(argc, argv);
}
#endif

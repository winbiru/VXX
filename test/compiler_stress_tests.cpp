#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

#include "frontend/keywords.h"
#include "vpp/compiler/pipeline.h"

namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

int failures = 0;

void expect(bool condition, const std::string &message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::uint64_t peakResidentBytes() noexcept {
#if defined(__unix__) || defined(__APPLE__)
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
#if defined(__APPLE__)
    return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
    return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024ULL;
#endif
#else
    return 0;
#endif
}

std::string makeLargeSource(std::size_t functionCount) {
    std::ostringstream source;
    for (std::size_t index = 0; index < functionCount; ++index) {
        source << "hàm stress_" << index
               << "(giá_trị) { trả về giá_trị + " << index << "; }\n";
    }
    source << "hàm main() {\n"
           << "  kết_quả = 0;\n";
    for (std::size_t index = 0; index < functionCount; index += 8) {
        source << "  kết_quả = kết_quả + stress_" << index << "(" << index << ");\n";
    }
    source << "}\n";
    return source.str();
}

void writeFile(const fs::path &path, const std::string &contents) {
    std::ofstream output(path, std::ios::binary);
    if (!output.is_open()) {
        throw std::runtime_error("không thể tạo stress fixture: " + path.u8string());
    }
    output << contents;
}

struct TemporaryProject {
    fs::path root;

    explicit TemporaryProject(fs::path path) : root(std::move(path)) {
        fs::create_directories(root);
    }

    ~TemporaryProject() {
        std::error_code error;
        fs::remove_all(root, error);
    }
};

std::string makeStressProject(const fs::path &root,
                              std::size_t moduleCount,
                              std::size_t functionsPerModule) {
    std::ostringstream mainSource;
    for (std::size_t module = 0; module < moduleCount; ++module) {
        const std::string moduleName = "module_" + std::to_string(module);
        std::ostringstream moduleSource;

        if (module > 0) {
            moduleSource << "nhập module_" << (module - 1) << ".vi;\n";
        }
        for (std::size_t function = 0; function < functionsPerModule; ++function) {
            moduleSource << "hàm " << moduleName << "_f" << function
                         << "(x) { trả về x + " << (module + function) << "; }\n";
        }
        writeFile(root / (moduleName + ".vi"), moduleSource.str());

        mainSource << "nhập " << moduleName << ".vi;\n";
    }

    mainSource << "hàm main() {\n"
               << "  tổng = 0;\n";
    for (std::size_t module = 0; module < moduleCount; ++module) {
        mainSource << "  tổng = tổng + module_" << module << "_f0(" << module << ");\n";
    }
    mainSource << "}\n";
    return mainSource.str();
}

struct StressMetrics {
    double milliseconds = 0.0;
    std::size_t bytecodeInstructions = 0;
    std::size_t functionCount = 0;
    std::size_t stringPoolEntries = 0;
    std::uint64_t peakBytes = 0;
};

StressMetrics compileMeasured(const std::string &source,
                              const fs::path &importRoot = {}) {
    vietvm::compiler::CompilationContext context;
    if (!importRoot.empty()) context.importResolutionBase = importRoot;

    const auto started = Clock::now();
    const auto artifacts = vietvm::compiler::compilePipeline(
        context, source, keywordMap, true);
    const auto elapsed = Clock::now() - started;

    StressMetrics metrics;
    metrics.milliseconds =
        std::chrono::duration<double, std::milli>(elapsed).count();
    metrics.bytecodeInstructions = artifacts.bytecode.size();
    metrics.functionCount = context.functionBytecode.size();
    metrics.stringPoolEntries = context.stringPool.size();
    metrics.peakBytes = peakResidentBytes();
    return metrics;
}

void printMetrics(const char *scenario, const StressMetrics &metrics) {
    std::cout << "compiler_stress=" << scenario
              << " compile_ms=" << metrics.milliseconds
              << " peak_rss_bytes=" << metrics.peakBytes
              << " bytecode=" << metrics.bytecodeInstructions
              << " functions=" << metrics.functionCount
              << " string_pool=" << metrics.stringPoolEntries
              << '\n';
}

void testLargeSingleSource() {
    constexpr std::size_t kFunctionCount = 800;
    const StressMetrics metrics = compileMeasured(makeLargeSource(kFunctionCount));
    printMetrics("large_source", metrics);

    expect(metrics.functionCount >= kFunctionCount,
           "large source giữ toàn bộ function metadata");
    expect(metrics.bytecodeInstructions > 0,
           "large source sinh bytecode");
    expect(metrics.milliseconds < 30000.0,
           "large source compile hoàn tất trong stress budget 30 giây");
}

void testLargeImportProject() {
    constexpr std::size_t kModuleCount = 24;
    constexpr std::size_t kFunctionsPerModule = 24;
    constexpr std::uint64_t kPeakMemoryBudget = 1024ULL * 1024ULL * 1024ULL;

    const auto nonce = Clock::now().time_since_epoch().count();
    TemporaryProject project(
        fs::temp_directory_path() /
        ("vpp-compiler-stress-" + std::to_string(nonce)));
    const std::string source = makeStressProject(
        project.root, kModuleCount, kFunctionsPerModule);

    const StressMetrics metrics = compileMeasured(source, project.root);
    printMetrics("large_project", metrics);

    expect(metrics.functionCount >= kModuleCount * kFunctionsPerModule,
           "large project compile toàn bộ imported function metadata");
    expect(metrics.stringPoolEntries >= kModuleCount,
           "large project giữ metadata/StringPool của import graph");
    expect(metrics.milliseconds < 30000.0,
           "large project compile hoàn tất trong stress budget 30 giây");
    if (metrics.peakBytes != 0) {
        expect(metrics.peakBytes < kPeakMemoryBudget,
               "compiler stress peak RSS dưới 1 GiB");
    }
}

void testRepeatedCompilationDoesNotAccumulateContextState() {
    const std::string source = makeLargeSource(300);
    std::size_t expectedFunctions = 0;
    std::size_t expectedPoolEntries = 0;

    vietvm::compiler::CompilationContext context;
    for (int iteration = 0; iteration < 5; ++iteration) {
        const auto artifacts = vietvm::compiler::compilePipeline(
            context, source, keywordMap, true);
        expect(!artifacts.bytecode.empty(),
               "repeated stress compile vẫn sinh bytecode");

        if (iteration == 0) {
            expectedFunctions = context.functionBytecode.size();
            expectedPoolEntries = context.stringPool.size();
        } else {
            expect(context.functionBytecode.size() == expectedFunctions,
                   "repeated compile không tích lũy function metadata");
            expect(context.stringPool.size() == expectedPoolEntries,
                   "repeated compile không tích lũy StringPool");
        }
    }
}

} // namespace

int main() {
    testLargeSingleSource();
    testLargeImportProject();
    testRepeatedCompilationDoesNotAccumulateContextState();

    if (failures != 0) {
        std::cerr << failures << " kiểm tra compiler stress thất bại\n";
        return 1;
    }
    std::cout << "compiler stress: đạt\n";
    return 0;
}

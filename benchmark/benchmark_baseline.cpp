#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "common/vm_native_http_helpers.h"
#include "frontend/keywords.h"
#include "frontend/lexer.h"
#include "vpp/compiler/pipeline.h"
#include "vpp/runtime/vm.h"

namespace {

using Clock = std::chrono::steady_clock;

volatile std::size_t benchmarkSink = 0;

template <typename Fn>
void runBenchmark(const char *name, int iterations, Fn &&fn) {
    const auto started = Clock::now();
    for (int i = 0; i < iterations; ++i) {
        benchmarkSink += static_cast<std::size_t>(fn());
    }
    const auto elapsed = Clock::now() - started;
    const double totalMs = std::chrono::duration<double, std::milli>(elapsed).count();
    const double nsPerIteration =
        std::chrono::duration<double, std::nano>(elapsed).count() / iterations;

    std::cout << "benchmark=" << name
              << " iterations=" << iterations
              << " total_ms=" << std::fixed << std::setprecision(3) << totalMs
              << " ns_per_iteration=" << std::setprecision(1) << nsPerIteration
              << '\n';
}

std::vector<Instruction> makeVmDispatchProgram() {
    std::vector<Instruction> code;
    code.reserve(2002);
    for (int i = 0; i < 500; ++i) {
        code.push_back({OP_BIEN_SO, i, 0, 0});
        code.push_back({OP_BIEN_SO, i + 1, 0, 0});
        code.push_back({OP_CONG, 0, 0, 0});
        code.push_back({OP_PHU_DINH, 0, 0, 0});
    }
    code.push_back({OP_DUNG_CHUONG_TRINH, 0, 0, 0});
    return code;
}

} // namespace

int main() {
    const std::string source =
        "hàm tổng(a, b) { trả về a + b; }\n"
        "hàm main() { x = tổng(20, 22); nếu (x == 42) { in x; } }\n";
    const auto vmProgram = makeVmDispatchProgram();

    runBenchmark("vm_dispatch", 250, [&]() {
        VM vm(vmProgram, {});
        vm.run();
        return vmProgram.size();
    });

    runBenchmark("lexer", 5000, [&]() {
        return vietvm::compiler::tokenizeWithSpans(source).size();
    });

    runBenchmark("compiler_pipeline", 250, [&]() {
        vietvm::compiler::CompilationContext context;
        const auto artifacts = vietvm::compiler::compilePipeline(
            context, source, keywordMap, true);
        return artifacts.bytecode.size();
    });

    runBenchmark("native_http_helpers", 100000, [&]() {
        std::string path;
        std::string query;
        vietvm::helpers::splitPathAndQuery(
            "/api/items?id=42&name=vpp", path, query);
        const std::string id = vietvm::helpers::queryParam(query, "id");
        const std::string name = vietvm::helpers::extractSimpleJsonStringField(
            "{\"name\":\"vpp\",\"status\":\"ok\"}", "status");
        return path.size() + query.size() + id.size() + name.size();
    });

    if (benchmarkSink == 0) {
        std::cerr << "benchmark sink unexpectedly remained zero\n";
        return 1;
    }
    return 0;
}

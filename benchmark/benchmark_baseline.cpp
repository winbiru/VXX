#include <chrono>
#include <cstddef>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "common/vm_native_http_helpers.h"
#include "frontend/keywords.h"
#include "frontend/lexer.h"
#include "vpp/bytecode/verifier.h"
#include "vpp/compiler/codegen.h"
#include "vpp/compiler/ir.h"
#include "vpp/compiler/module_graph.h"
#include "vpp/compiler/optimizer.h"
#include "vpp/compiler/package_resolver.h"
#include "vpp/compiler/pipeline.h"
#include "vpp/compiler/semantic.h"
#include "vpp/frontend/parser.h"
#include "vpp/runtime/value.h"
#include "vpp/runtime/vm.h"
#include "vpp/runtime/vm_fixture.h"

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

template <typename Setup, typename Fn>
void runStageBenchmark(const char *name, int iterations, Setup &&setup, Fn &&fn) {
    std::chrono::nanoseconds total{0};
    for (int i = 0; i < iterations; ++i) {
        setup();
        const auto started = Clock::now();
        benchmarkSink += static_cast<std::size_t>(fn());
        total += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started);
    }
    const double totalMs = std::chrono::duration<double, std::milli>(total).count();
    const double nsPerIteration = static_cast<double>(total.count()) / iterations;
    std::cout << "benchmark=" << name
              << " iterations=" << iterations
              << " total_ms=" << std::fixed << std::setprecision(3) << totalMs
              << " ns_per_iteration=" << std::setprecision(1) << nsPerIteration
              << '\n';
}

std::vector<Instruction> makeVmDispatchProgram() {
    std::vector<Instruction> code;
    code.reserve(1601);
    for (int i = 0; i < 400; ++i) {
        code.push_back({OP_BIEN_SO, i, 0, 0});
        code.push_back({OP_BIEN_SO, i + 1, 0, 0});
        code.push_back({OP_CONG, 0, 0, 0});
        code.push_back({OP_PHU_DINH, 0, 0, 0});
    }
    code.push_back({OP_DUNG_CHUONG_TRINH, 0, 0, 0});
    return code;
}

std::string makeCompilerSource() {
    std::string source;
    for (int i = 0; i < 24; ++i) {
        source += "hàm f" + std::to_string(i) + "(a, b) { trả về a + b + " +
                  std::to_string(i) + "; }\n";
    }
    source += "hàm main() { x = f23(19, 0); nếu (x == 42) { in x; } }\n";
    return source;
}

std::string makeLargeJsonBody() {
    std::string body = "{";
    for (int i = 0; i < 128; ++i) {
        if (i != 0) body += ',';
        body += "\"field" + std::to_string(i) + "\":\"giá trị-" +
                std::to_string(i) + "\"";
    }
    body += ",\"target\":\"V++ 🚀\"}";
    return body;
}

} // namespace

int main() {
    std::cout << "benchmark_meta=harness-v2 gc_policy=runtime-default build=Release-recommended\n";

    const auto vmProgram = makeVmDispatchProgram();
    vietvm::bytecode::BytecodeVerificationContext verifyContext;

    runBenchmark("vm_verify", 1500, [&]() {
        const auto issue = vietvm::bytecode::verifyBytecode(vmProgram, verifyContext);
        return issue.has_value() ? 0 : vmProgram.size();
    });

    VM dispatchVm(vmProgram, {});
    VMRuntimeFixture dispatchFixture(dispatchVm);
    if (vietvm::bytecode::verifyBytecode(vmProgram, verifyContext).has_value()) {
        std::cerr << "benchmark VM program failed verifier\n";
        return 1;
    }
    runStageBenchmark(
        "vm_dispatch_preverified", 300,
        [&]() { dispatchFixture.resetExecutionForBenchmark(); },
        [&]() {
            dispatchFixture.runInterpreterPreverifiedForBenchmark();
            return vmProgram.size();
        });

    runBenchmark("vm_end_to_end", 250, [&]() {
        VM vm(vmProgram, {});
        vm.run();
        return vmProgram.size();
    });

    const std::string source = makeCompilerSource();
    const auto rawTokens = vietvm::compiler::tokenizeWithSpans(source);
    const auto normalizedTokens = vietvm::compiler::postProcessTokensWithSpans(rawTokens);
    const auto ast = vietvm::frontend::parseTokens(normalizedTokens);
    const auto semantic = vietvm::compiler::analyzeSemantics(ast);
    const auto baseIr = vietvm::compiler::lowerToIr(ast, semantic);

    runBenchmark("compiler_lexer", 2500, [&]() {
        return vietvm::compiler::tokenizeWithSpans(source).size();
    });
    runBenchmark("compiler_token_normalize", 2500, [&]() {
        return vietvm::compiler::postProcessTokensWithSpans(rawTokens).size();
    });
    runBenchmark("compiler_parser", 1000, [&]() {
        return vietvm::frontend::parseTokens(normalizedTokens).statements.size();
    });
    runBenchmark("compiler_semantic", 500, [&]() {
        return vietvm::compiler::analyzeSemantics(ast).symbols.size();
    });
    runBenchmark("compiler_ir_lower", 500, [&]() {
        return vietvm::compiler::lowerToIr(ast, semantic).instructions.size();
    });

    vietvm::compiler::IrProgram optimizerFixture;
    runStageBenchmark(
        "compiler_optimizer", 1000,
        [&]() { optimizerFixture = baseIr; },
        [&]() {
            const auto report = vietvm::compiler::optimizeIr(optimizerFixture);
            return optimizerFixture.instructions.size() + report.removedNoOps;
        });

    vietvm::compiler::IrProgram optimizedIr = baseIr;
    (void)vietvm::compiler::optimizeIr(optimizedIr);
    vietvm::compiler::CompilationRegistryState codegenState;
    runStageBenchmark(
        "compiler_codegen", 500,
        [&]() { codegenState.clear(); },
        [&]() {
            return vietvm::compiler::emitDirectBytecode(
                       codegenState, optimizedIr, keywordMap, true)
                .size();
        });

    runBenchmark("compiler_pipeline", 250, [&]() {
        vietvm::compiler::CompilationContext context;
        return vietvm::compiler::compilePipeline(context, source, keywordMap, true)
            .bytecode.size();
    });

    const vietvm::compiler::PackageResolver resolver(std::filesystem::current_path());
    runBenchmark("compiler_package_resolve", 5000, [&]() {
        const auto resolved = resolver.resolve("dữ liệu");
        return resolved.path.native().size() + (resolved.exists ? 1u : 0u);
    });

    vietvm::frontend::AstImportSpec moduleImport;
    moduleImport.target = "dữ liệu";
    moduleImport.hasSemicolon = true;
    runBenchmark("compiler_module_graph", 200, [&]() {
        const auto index = vietvm::compiler::buildLocalModuleSemanticIndex(
            vietvm::compiler::LocalModuleResolver(std::filesystem::current_path()),
            "entry://benchmark",
            {moduleImport},
            vietvm::compiler::ModuleIndexMode::DirectOnly);
        return index.graph.modules.size() + index.graph.edges.size() + 1;
    });

    const std::string smallJson =
        R"({"name":"vpp","status":"ổn","escaped":"a\"b"})";
    const std::string largeJson = makeLargeJsonBody();
    runBenchmark("http_json_extract_small", 30000, [&]() {
        return vietvm::helpers::extractSimpleJsonStringField(smallJson, "status").size();
    });
    runBenchmark("http_json_extract_large", 3000, [&]() {
        return vietvm::helpers::extractSimpleJsonStringField(largeJson, "target").size();
    });
    runBenchmark("native_http_helpers", 30000, [&]() {
        std::string path;
        std::string query;
        vietvm::helpers::splitPathAndQuery(
            "/api/items?id=42&name=vpp", path, query);
        const std::string id = vietvm::helpers::queryParam(query, "id");
        const std::string status =
            vietvm::helpers::extractSimpleJsonStringField(smallJson, "status");
        return path.size() + query.size() + id.size() + status.size();
    });

    VM gcVm({{OP_DUNG_CHUONG_TRINH, 0, 0, 0}}, {});
    VMRuntimeFixture gcFixture(gcVm);
    runStageBenchmark(
        "gc_collect_cycles", 300,
        [&]() {
            for (int i = 0; i < 128; ++i) {
                auto list = std::make_shared<vietvm::runtime::ListValue>();
                list->elements.push_back(StackValue(list));
                gcFixture.trackHeapValue(StackValue(list));
            }
        },
        [&]() {
            gcFixture.collectGarbage();
            return gcFixture.gcStats().swept + 1;
        });

    if (benchmarkSink == 0) {
        std::cerr << "benchmark sink unexpectedly remained zero\n";
        return 1;
    }
    return 0;
}

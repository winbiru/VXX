#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <cstddef>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "benchmark_config.h"
#include "telemetry.h"
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

const std::vector<std::string> cases = {
    "vm_verify", "vm_dispatch_preverified", "vm_end_to_end",
    "vm_direct_function_calls_10k", "vm_direct_function_calls_consumed_10k",
    "vm_module_init_32x622_functions", "compiler_lexer", "compiler_token_normalize",
    "compiler_parser", "compiler_semantic", "compiler_ir_lower", "compiler_optimizer",
    "compiler_codegen", "compiler_pipeline", "compiler_package_resolve",
    "compiler_module_graph", "gc_collect_cycles", "gc_burst_collect_reuse",
    "gc_burst_collect_trim_reuse"};
std::string selectedCase;
int requestedIterations = 0;

void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

void emit(const char *name, int iterations, double nanoseconds, std::size_t checksum,
          const std::map<std::string, double> &extra = {},
          const std::vector<double> &pauses = {}) {
    require(nanoseconds > 0 && checksum != 0, "empty benchmark measurement");
    benchmarkSink += checksum;
    std::cout << std::setprecision(12)
              << "{\"case\":" << std::quoted(name)
              << ",\"iterations\":" << iterations
              << ",\"ns_per_iteration\":" << nanoseconds / iterations
              << ",\"checksum\":" << checksum
              << ",\"peak_rss_bytes\":" << benchmark::peakRssBytes();
    for (const auto &entry : extra) {
        std::cout << ',' << std::quoted(entry.first) << ':' << entry.second;
    }
    if (!pauses.empty()) {
        std::cout << ",\"gc_pauses_ns\":[";
        for (std::size_t i = 0; i < pauses.size(); ++i) {
            if (i) std::cout << ',';
            std::cout << pauses[i];
        }
        std::cout << ']';
    }
    std::cout << "}\n";
}

template <typename Setup, typename Fn>
void runStageBenchmark(const char *name, int iterations, Setup &&setup, Fn &&fn) {
    if (!selectedCase.empty() && selectedCase != name) return;
    if (requestedIterations) iterations = requestedIterations;
    std::chrono::nanoseconds total{0};
    std::size_t checksum = 0;
    for (int i = 0; i < iterations; ++i) {
        setup();
        const auto started = Clock::now();
        const auto result = static_cast<std::size_t>(fn());
        total += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started);
        if (i == 0) checksum = result;
        require(result == checksum && result != 0, "non-deterministic/empty benchmark output");
    }
    emit(name, iterations, static_cast<double>(total.count()), checksum);
}

template <typename Fn>
void runBenchmark(const char *name, int iterations, Fn &&fn) {
    runStageBenchmark(name, iterations, [] {}, std::forward<Fn>(fn));
}

// Real VM mark/sweep on live roots and unreachable self-cycles. Each iteration
// bursts, collects, and refills the SAME stack, so trimming/reallocation is visible.
void runGcBenchmark(const char *name, bool trim) {
    if (!selectedCase.empty() && selectedCase != name) return;
    const int iterations = requestedIterations ? requestedIterations : 80;
    constexpr int burst = 1024;
    constexpr int live = 32;
    VM vm({{OP_DUNG_CHUONG_TRINH, 0, 0, 0}}, {});
    VMRuntimeFixture fixture(vm);
    std::vector<double> pauses;
    pauses.reserve(iterations);
    std::uint64_t allocations = 0, bytes = 0, gcAllocations = 0, gcBytes = 0;
    std::size_t resizeCount = 0, trimCount = 0;
    double total = 0;
    // Reserve stack outside measurement, equally for both policies. Registry
    // allocation on the first burst is included in each process's observation.
    fixture.reserveStack(burst);
    auto root = std::make_shared<vietvm::runtime::ListValue>();
    root->elements.emplace_back(42);
    fixture.setVariable(0, StackValue(root));
    fixture.trackHeapValue(StackValue(root));
    for (int i = 0; i < iterations; ++i) {
        benchmark::startAllocations();
        const auto started = Clock::now();
        auto capacity = fixture.stackCapacity();
        for (int j = 0; j < burst; ++j) {
            auto list = std::make_shared<vietvm::runtime::ListValue>();
            list->elements.emplace_back(list);
            fixture.trackHeapValue(StackValue(list));
            fixture.push(StackValue(list));
            if (fixture.stackCapacity() != capacity) {
                ++resizeCount;
                capacity = fixture.stackCapacity();
            }
        }
        fixture.clearStack();
        // Keep a subset rooted through collection to check mark AND sweep.
        for (int j = 0; j < live; ++j) fixture.push(StackValue(root));
        const auto beforeGc = benchmark::stopAllocations();
        benchmark::startAllocations();
        const auto gcStarted = Clock::now();
        if (trim) fixture.collectGarbageAndTrim(); else fixture.collectGarbage();
        const auto gcElapsed = Clock::now() - gcStarted;
        const auto duringGc = benchmark::stopAllocations();
        if (fixture.stackCapacity() != capacity) ++trimCount;
        capacity = fixture.stackCapacity();
        benchmark::startAllocations();
        // Reuse after collection is measured as well as the burst itself.
        fixture.clearStack();
        for (int j = 0; j < burst; ++j) {
            fixture.push(j);
            if (fixture.stackCapacity() != capacity) {
                ++resizeCount;
                capacity = fixture.stackCapacity();
            }
        }
        fixture.clearStack();
        const auto afterGc = benchmark::stopAllocations();
        total += std::chrono::duration<double, std::nano>(Clock::now() - started).count();
        pauses.push_back(std::chrono::duration<double, std::nano>(gcElapsed).count());
        allocations += beforeGc.count + duringGc.count + afterGc.count;
        bytes += beforeGc.bytes + duringGc.bytes + afterGc.bytes;
        gcAllocations += duringGc.count;
        gcBytes += duringGc.bytes;
        const auto &stats = fixture.gcStats();
        require(stats.swept == burst && stats.marked == 1 && stats.trackedAfter == 1,
                "GC root/cycle invariant failed");
        require(std::get<int>(root->elements.at(0)) == 42, "GC corrupted live root");
        require(trim || fixture.stackCapacity() >= static_cast<std::size_t>(burst),
                "periodic GC discarded reusable capacity");
    }
    require(trim || (resizeCount == 0 && trimCount == 0), "periodic GC capacity thrashing");
    emit(name, iterations, total, burst + 42,
         {{"allocation_count", static_cast<double>(allocations) / iterations},
          {"allocation_bytes", static_cast<double>(bytes) / iterations},
          {"gc_allocation_count", static_cast<double>(gcAllocations) / iterations},
          {"gc_allocation_bytes", static_cast<double>(gcBytes) / iterations},
          {"managed_allocations", burst},
          {"stack_resize_count", static_cast<double>(resizeCount) / iterations},
          {"stack_trim_count", static_cast<double>(trimCount) / iterations},
          {"tracked_after", 1}, {"swept", burst}}, pauses);
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

std::vector<Instruction> makeVmFunctionCallProgram(int calls, int functionId) {
    std::vector<Instruction> code;
    code.reserve(static_cast<std::size_t>(calls) + 1);
    for (int i = 0; i < calls; ++i) {
        code.push_back({OP_GOI, 0, functionId, 0});
    }
    code.push_back({OP_DUNG_CHUONG_TRINH, 0, 0, 0});
    return code;
}

std::vector<Instruction> makeVmConsumedFunctionCallProgram(int calls, int functionId) {
    std::vector<Instruction> code;
    code.reserve(static_cast<std::size_t>(calls) * 3 + 1);
    for (int i = 0; i < calls; ++i) {
        code.push_back({OP_GOI, 0, functionId, 0});
        code.push_back({OP_TEN_BIEN_ID, 0, 0, 0});
        code.push_back({OP_GAN, 0, 0, 0});
    }
    code.push_back({OP_DUNG_CHUONG_TRINH, 0, 0, 0});
    return code;
}

std::vector<Instruction> makeTinyVmFunction() {
    return {
        {OP_BIEN_SO, 1, 0, 0},
        {OP_TRA_VE, 0, 0, 0},
    };
}

std::unordered_map<int, std::vector<Instruction>> makeVmFunctionTable(int count) {
    std::unordered_map<int, std::vector<Instruction>> functions;
    functions.reserve(static_cast<std::size_t>(count));
    for (int id = 1; id <= count; ++id) {
        functions.emplace(id, makeTinyVmFunction());
    }
    return functions;
}

std::vector<std::string> makeVmFunctionNamePool(int count) {
    std::vector<std::string> pool;
    pool.reserve(static_cast<std::size_t>(count));
    for (int id = 1; id <= count; ++id) {
        pool.push_back("bench_fn_" + std::to_string(id));
    }
    return pool;
}

std::unordered_map<int, int> makeVmFunctionNameTable(int count) {
    std::unordered_map<int, int> names;
    names.reserve(static_cast<std::size_t>(count));
    for (int id = 1; id <= count; ++id) {
        names.emplace(id - 1, id);
    }
    return names;
}

std::string makeCompilerSource() {
    std::string source;
    for (int i = 0; i < 24; ++i) {
        source += "hàm f" + std::to_string(i) + "(a, b) { trả về a + b + " +
                  std::to_string(i) + "; }\n";
    }
    source += "hàm chính() { x = f23(19, 0); nếu (x == 42) { in x; } }\n";
    return source;
}

} // namespace

int main(int argc, char **argv) {
    try {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--list") {
            for (const auto &name : cases) std::cout << name << '\n';
            return 0;
        }
        if ((arg == "--case" || arg == "--iterations") && i + 1 < argc) {
            const std::string value = argv[++i];
            if (arg == "--case") selectedCase = value;
            else {
                require(!value.empty() && value.find_first_not_of("0123456789") == std::string::npos,
                        "iterations must be a positive integer");
                requestedIterations = std::stoi(value);
                require(requestedIterations > 0 && requestedIterations <= 1000000,
                        "iterations must be in 1..1000000");
            }
        } else throw std::runtime_error("usage: vpp-benchmark-baseline [--list | --case NAME --iterations N]");
    }
    require(selectedCase.empty() || std::find(cases.begin(), cases.end(), selectedCase) != cases.end(),
            "unknown benchmark case");
    std::cout << "{\"meta\":\"harness-v3\",\"compiler\":" << std::quoted(VPP_BENCHMARK_COMPILER)
              << ",\"flags\":" << std::quoted(VPP_BENCHMARK_FLAGS)
              << ",\"coverage\":" << std::quoted(VPP_BENCHMARK_COVERAGE)
#ifdef NDEBUG
              << ",\"build\":\"Release\""
#else
              << ",\"build\":\"Debug\""
#endif
              << ",\"gc_policy\":\"runtime-default;gc-cases-explicit\"}\n";

    const auto vmProgram = makeVmDispatchProgram();
    vietvm::bytecode::BytecodeVerificationContext verifyContext;

    runBenchmark("vm_verify", 1500, [&]() {
        vietvm::bytecode::BytecodeVerificationContext context;
        const auto issue = vietvm::bytecode::verifyBytecode(vmProgram, context);
        require(!issue.has_value(), "VM verifier rejected valid corpus");
        return vmProgram.size();
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
            require(dispatchFixture.pc() + 1 == vmProgram.size() && dispatchFixture.stack().size() == 400
                    && std::get<int>(dispatchFixture.top()) == 0, "dispatch produced incorrect result");
            return vmProgram.size();
        });

    runBenchmark("vm_end_to_end", 250, [&]() {
        VM vm(vmProgram, {});
        vm.run();
        return vmProgram.size();
    });

    constexpr int kVmCallsPerIteration = 10000;
    constexpr int kVmCallFunctionId = 1;
    const auto vmCallProgram =
        makeVmFunctionCallProgram(kVmCallsPerIteration, kVmCallFunctionId);
    VM callVm(vmCallProgram, {});
    callVm.setFunctions({{kVmCallFunctionId, makeTinyVmFunction()}}, {});
    callVm.ensureBytecodeVerified();
    VMRuntimeFixture callFixture(callVm);
    runStageBenchmark(
        "vm_direct_function_calls_10k", 40,
        [&]() { callFixture.resetExecutionForBenchmark(); },
        [&]() {
            callFixture.runInterpreterPreverifiedForBenchmark();
            require(callFixture.pc() + 1 == vmCallProgram.size()
                    && callFixture.stack().size() == kVmCallsPerIteration
                    && std::get<int>(callFixture.top()) == 1, "function-call result mismatch");
            return kVmCallsPerIteration;
        });

    const auto vmConsumedCallProgram =
        makeVmConsumedFunctionCallProgram(kVmCallsPerIteration, kVmCallFunctionId);
    VM consumedCallVm(vmConsumedCallProgram, {});
    consumedCallVm.setFunctions({{kVmCallFunctionId, makeTinyVmFunction()}}, {});
    consumedCallVm.ensureBytecodeVerified();
    VMRuntimeFixture consumedCallFixture(consumedCallVm);
    runStageBenchmark(
        "vm_direct_function_calls_consumed_10k", 40,
        [&]() { consumedCallFixture.resetExecutionForBenchmark(); },
        [&]() {
            consumedCallFixture.runInterpreterPreverifiedForBenchmark();
            require(consumedCallFixture.pc() + 1 == vmConsumedCallProgram.size()
                    && consumedCallFixture.stack().empty()
                    && std::get<int>(consumedCallFixture.variable(0)) == 1, "consumed-call result mismatch");
            return kVmCallsPerIteration;
        });

    constexpr int kStdlibFunctionCount = 622;
    constexpr int kModuleInitializerCount = 32;
    const auto moduleFunctions = makeVmFunctionTable(kStdlibFunctionCount);
    const auto moduleStringPool = makeVmFunctionNamePool(kStdlibFunctionCount);
    const auto moduleFunctionNames = makeVmFunctionNameTable(kStdlibFunctionCount);
    std::unique_ptr<VM> moduleVm;
    runStageBenchmark(
        "vm_module_init_32x622_functions", 5,
        [&]() {
            moduleVm = std::make_unique<VM>(
                std::vector<Instruction>{{OP_DUNG_CHUONG_TRINH, 0, 0, 0}},
                moduleStringPool);
            moduleVm->setFunctions(moduleFunctions, moduleFunctionNames);
            for (int index = 0; index < kModuleInitializerCount; ++index) {
                moduleVm->addModuleInitializer(
                    "bench://module/" + std::to_string(index),
                    {{OP_DUNG_CHUONG_TRINH, 0, 0, 0}});
            }
        },
        [&]() {
            moduleVm->run();
            return kModuleInitializerCount * kStdlibFunctionCount;
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
        require(resolved.exists, "package benchmark target missing");
        return 1u;
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

    runGcBenchmark("gc_burst_collect_reuse", false);
    runGcBenchmark("gc_burst_collect_trim_reuse", true);

    if (benchmarkSink == 0) {
        std::cerr << "benchmark sink unexpectedly remained zero\n";
        return 1;
    }
    return 0;
    } catch (const std::exception &error) {
        benchmark::stopAllocations();
        std::cerr << "benchmark: " << error.what() << '\n';
        return 1;
    }
}

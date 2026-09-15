#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "compiler/compileRegistry.h"
#include "compiler/compiler.h"
#include "common/storeString.h"
#include "common/symbolTable.h"
#include "common/utility.h"
#include "frontend/keywords.h"
#include "vpp/compiler/pipeline.h"
#include "vpp/core/project_layout.h"
#include "vpp/core/text.h"

namespace {

namespace fs = std::filesystem;

int failures = 0;

void expect(bool condition, const std::string &message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool sameInstruction(const Instruction &left, const Instruction &right) {
    return left.op == right.op &&
           left.operand == right.operand &&
           left.operandIndex == right.operandIndex &&
           left.operandValue == right.operandValue;
}

bool sameBytecode(const std::vector<Instruction> &left,
                  const std::vector<Instruction> &right) {
    return left.size() == right.size() &&
           std::equal(left.begin(), left.end(), right.begin(), sameInstruction);
}

bool sameDebugLocation(const vietvm::runtime::RuntimeSourceLocation &left,
                       const vietvm::runtime::RuntimeSourceLocation &right) {
    return left.sourceFile == right.sourceFile &&
           left.moduleIdentity == right.moduleIdentity &&
           left.functionName == right.functionName &&
           left.line == right.line &&
           left.column == right.column;
}

bool sameDebugInfo(
    const std::vector<vietvm::runtime::RuntimeSourceLocation> &left,
    const std::vector<vietvm::runtime::RuntimeSourceLocation> &right) {
    return left.size() == right.size() &&
           std::equal(left.begin(), left.end(), right.begin(), sameDebugLocation);
}

bool sameFunctionBytecode(
    const std::unordered_map<int, std::vector<Instruction>> &left,
    const std::unordered_map<int, std::vector<Instruction>> &right) {
    if (left.size() != right.size()) return false;
    for (const auto &entry : left) {
        const auto found = right.find(entry.first);
        if (found == right.end() || !sameBytecode(entry.second, found->second)) {
            return false;
        }
    }
    return true;
}

bool sameFunctionDebugInfo(
    const std::unordered_map<int, std::vector<vietvm::runtime::RuntimeSourceLocation>> &left,
    const std::unordered_map<int, std::vector<vietvm::runtime::RuntimeSourceLocation>> &right) {
    if (left.size() != right.size()) return false;
    for (const auto &entry : left) {
        const auto found = right.find(entry.first);
        if (found == right.end() || !sameDebugInfo(entry.second, found->second)) {
            return false;
        }
    }
    return true;
}

bool sameModuleInitializers(
    const std::vector<vietvm::compiler::CompiledModuleInitializer> &left,
    const std::vector<vietvm::compiler::CompiledModuleInitializer> &right) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (left[index].identity != right[index].identity ||
            !sameBytecode(left[index].bytecode, right[index].bytecode) ||
            !sameDebugInfo(left[index].debugInfo, right[index].debugInfo)) {
            return false;
        }
    }
    return true;
}

bool sameModuleExports(
    const std::unordered_map<std::string,
                             std::vector<vietvm::compiler::CompiledModuleFunctionExport>> &left,
    const std::unordered_map<std::string,
                             std::vector<vietvm::compiler::CompiledModuleFunctionExport>> &right) {
    if (left.size() != right.size()) return false;
    for (const auto &entry : left) {
        const auto found = right.find(entry.first);
        if (found == right.end() || entry.second.size() != found->second.size()) {
            return false;
        }
        for (std::size_t index = 0; index < entry.second.size(); ++index) {
            if (entry.second[index].name != found->second[index].name ||
                entry.second[index].functionId != found->second[index].functionId) {
                return false;
            }
        }
    }
    return true;
}

void testStringPool() {
    using vietvm::compiler::StringPool;

    StringPool::clear();
    expect(StringPool::size() == 0, "StringPool starts empty after clear");

    const int alpha = StringPool::storeString("alpha");
    const int alphaAgain = StringPool::storeString("alpha");
    const int beta = StringPool::storeString("beta");

    expect(alpha == 0, "first StringPool entry has index zero");
    expect(alphaAgain == alpha, "StringPool deduplicates equal strings");
    expect(beta == 1, "StringPool allocates the next index for a new string");
    expect(StringPool::size() == 2, "StringPool size counts unique strings");
    expect(StringPool::findString("alpha") == alpha, "StringPool finds stored strings");
    expect(StringPool::findString("missing") == -1, "StringPool does not create missing strings");
    expect(StringPool::getString(beta) == "beta", "StringPool returns a stored string");

    bool rejectedBadIndex = false;
    try {
        (void)StringPool::getString(-1);
    } catch (const std::out_of_range &) {
        rejectedBadIndex = true;
    }
    expect(rejectedBadIndex, "StringPool rejects an invalid index");

    StringPool::clear();
    expect(StringPool::size() == 0, "StringPool clear removes entries");
    expect(StringPool::findString("alpha") == -1, "StringPool clear removes lookup entries");
}

void testHamMapAllocator() {
    using vietvm::compiler::hamMap;

    hamMap::bytecodeMap().clear();
    hamMap::clearHamNameIndexMap();
    hamMap::resetHamIdCounter();

    expect(hamMap::allocHamId() == 0, "hamMap allocator starts at zero after reset");
    expect(hamMap::allocHamId() == 1, "hamMap allocator increments IDs");

    hamMap::setHamNameIndex(1, 42);
    const auto name = hamMap::nameIndexMap().find(1);
    expect(name != hamMap::nameIndexMap().end() && name->second == 42,
           "hamMap stores a function name index");

    hamMap::clearHamNameIndexMap();
    expect(hamMap::nameIndexMap().empty(), "hamMap clears function name indices");
    hamMap::resetHamIdCounter();
    expect(hamMap::allocHamId() == 0, "hamMap reset makes IDs reproducible");
}

void testCompilationStateReset() {
    using vietvm::compiler::StringPool;
    using vietvm::compiler::hamMap;

    vietvm::compiler::resetCompilationState();

    StringPool::storeString("state-that-must-be-cleared");
    hamMap::bytecodeMap().emplace(7, std::vector<Instruction>{{OP_DUNG_CHUONG_TRINH, 0, 0, 0}});
    hamMap::setHamNameIndex(7, 3);
    (void)hamMap::allocHamId();
    vietvm::compiler::importedFileSet().insert("/tmp/imported-once.vi");
    vietvm::compiler::registerClassMethodVisibility("NoiBo.chiNoiBo", "NoiBo", "riêng tư");
    vietvm::compiler::pushClassContext("DangXuLy");

    expect(vietvm::compiler::currentClassContext() == "DangXuLy",
           "class context is populated before compilation-state reset");

    vietvm::compiler::resetCompilationState();

    expect(StringPool::size() == 0, "compilation-state reset clears StringPool");
    expect(hamMap::bytecodeMap().empty(), "compilation-state reset clears function bytecode");
    expect(hamMap::nameIndexMap().empty(), "compilation-state reset clears function name indices");
    expect(vietvm::compiler::importedFileSet().empty(), "compilation-state reset clears imported files");
    expect(vietvm::compiler::currentClassContext().empty(),
           "compilation-state reset clears class context");
    expect(hamMap::allocHamId() == 0,
           "compilation-state reset makes function IDs reproducible");

    vietvm::compiler::pushClassContext("BenNgoai");
    bool stalePrivateMethodWasRejected = false;
    try {
        vietvm::compiler::validateCallableAccess("NoiBo.chiNoiBo");
    } catch (const std::runtime_error &) {
        stalePrivateMethodWasRejected = true;
    }
    vietvm::compiler::popClassContext();
    expect(!stalePrivateMethodWasRejected,
           "compilation-state reset clears class access metadata");

    vietvm::compiler::resetCompilationState();
}

void testRepeatedTopLevelCompilationLifecycle() {
    using vietvm::compiler::StringPool;
    using vietvm::compiler::hamMap;

    const std::string firstSource = "hàm main() { in \"alpha-lifecycle\"; }";
    const std::string secondSource = "hàm main() { in \"beta-lifecycle\"; }";

    vietvm::compiler::resetCompilationState();
    const auto first = vietvm::compiler::compilePipeline(firstSource, keywordMap, true);
    const std::vector<std::string> firstPool = StringPool::getPool();
    const auto firstNames = hamMap::nameIndexMap();
    const std::size_t firstFunctionCount = hamMap::bytecodeMap().size();

    expect(StringPool::findString("alpha-lifecycle") >= 0,
           "first top-level compilation records its own string literal");

    vietvm::compiler::resetCompilationState();
    (void)vietvm::compiler::compilePipeline(secondSource, keywordMap, true);
    expect(StringPool::findString("alpha-lifecycle") == -1,
           "second top-level compilation does not retain the first StringPool");
    expect(StringPool::findString("beta-lifecycle") >= 0,
           "second top-level compilation records its own string literal");

    vietvm::compiler::resetCompilationState();
    const auto firstAgain = vietvm::compiler::compilePipeline(firstSource, keywordMap, true);
    expect(StringPool::getPool() == firstPool,
           "recompiling the same source after reset reproduces StringPool state");
    expect(hamMap::nameIndexMap() == firstNames,
           "recompiling the same source after reset reproduces function-name IDs");
    expect(hamMap::bytecodeMap().size() == firstFunctionCount,
           "recompiling the same source after reset reproduces function count");
    expect(firstAgain.bytecode.size() == first.bytecode.size(),
           "recompiling the same source after reset preserves root bytecode shape");

    vietvm::compiler::resetCompilationState();
}

void testCompilationContextLifecycle() {
    using vietvm::compiler::StringPool;
    using vietvm::compiler::hamMap;

    const std::string firstSource = "hàm main() { in \"alpha-context\"; }";
    const std::string secondSource = "hàm main() { in \"beta-context\"; }";

    vietvm::compiler::CompilationContext firstContext;
    const auto first = vietvm::compiler::compilePipeline(
        firstContext, firstSource, keywordMap, true);

    expect(std::find(firstContext.stringPool.begin(), firstContext.stringPool.end(),
                     "alpha-context") != firstContext.stringPool.end(),
           "CompilationContext snapshots the first compilation StringPool");
    expect(StringPool::size() == 0 && hamMap::bytecodeMap().empty() &&
               hamMap::nameIndexMap().empty(),
           "context-driven compilation clears legacy registries after success");

    vietvm::compiler::CompilationContext secondContext;
    (void)vietvm::compiler::compilePipeline(
        secondContext, secondSource, keywordMap, true);

    expect(std::find(secondContext.stringPool.begin(), secondContext.stringPool.end(),
                     "alpha-context") == secondContext.stringPool.end(),
           "independent CompilationContext does not retain previous strings");
    expect(std::find(secondContext.stringPool.begin(), secondContext.stringPool.end(),
                     "beta-context") != secondContext.stringPool.end(),
           "independent CompilationContext snapshots its own strings");

    vietvm::compiler::CompilationContext firstAgainContext;
    const auto firstAgain = vietvm::compiler::compilePipeline(
        firstAgainContext, firstSource, keywordMap, true);
    expect(firstAgainContext.stringPool == firstContext.stringPool,
           "repeated context-driven compilation reproduces StringPool state");
    expect(firstAgainContext.functionNameIndices == firstContext.functionNameIndices,
           "repeated context-driven compilation reproduces function-name IDs");
    expect(firstAgainContext.functionBytecode.size() == firstContext.functionBytecode.size(),
           "repeated context-driven compilation reproduces function count");
    expect(firstAgain.bytecode.size() == first.bytecode.size(),
           "repeated context-driven compilation preserves root bytecode shape");
}

void testDeterministicCompilationArtifacts() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path tempRoot = fs::temp_directory_path() /
                              ("vpp-compiler-reproducible-" + std::to_string(nonce));
    fs::create_directories(tempRoot);

    auto writeFile = [](const fs::path &path, const std::string &contents) {
        std::ofstream output(path);
        if (!output.is_open()) {
            throw std::runtime_error("cannot create reproducibility fixture: " + path.u8string());
        }
        output << contents;
    };

    writeFile(tempRoot / "dependency.vi",
              "hàm giá_trị_gốc() { trả về 7; };\n");
    writeFile(tempRoot / "module.vi",
              "nhập dependency.vi;\n"
              "hàm tính_toán(x) { trả về x + giá_trị_gốc(); };\n");

    const std::string source =
        "nhập module.vi;\n"
        "hàm main() { in tính_toán(5); };\n";

    vietvm::compiler::CompilationContext firstContext;
    firstContext.importResolutionBase = tempRoot;
    const auto first = vietvm::compiler::compilePipeline(
        firstContext, source, keywordMap, true);

    vietvm::compiler::CompilationContext secondContext;
    secondContext.importResolutionBase = tempRoot;
    const auto second = vietvm::compiler::compilePipeline(
        secondContext, source, keywordMap, true);

    expect(sameBytecode(first.bytecode, second.bytecode),
           "repeated compilation reproduces root bytecode exactly");
    expect(firstContext.stringPool == secondContext.stringPool,
           "repeated compilation reproduces StringPool ordering exactly");
    expect(sameFunctionBytecode(firstContext.functionBytecode,
                                secondContext.functionBytecode),
           "repeated compilation reproduces function bytecode exactly");
    expect(firstContext.functionNameIndices == secondContext.functionNameIndices,
           "repeated compilation reproduces function IDs and name indices exactly");
    expect(sameDebugInfo(firstContext.rootBytecodeDebugInfo,
                         secondContext.rootBytecodeDebugInfo) &&
               sameFunctionDebugInfo(firstContext.functionDebugInfo,
                                     secondContext.functionDebugInfo),
           "repeated compilation reproduces bytecode debug metadata exactly");
    expect(firstContext.importedFiles == secondContext.importedFiles &&
               sameModuleInitializers(firstContext.moduleInitializers,
                                      secondContext.moduleInitializers) &&
               sameModuleExports(firstContext.moduleFunctionExports,
                                 secondContext.moduleFunctionExports),
           "repeated compilation reproduces dependency and module metadata exactly");
    expect(firstContext.nextFunctionId == secondContext.nextFunctionId,
           "repeated compilation ends with the same function allocator state");

    std::error_code ignored;
    fs::remove_all(tempRoot, ignored);
}

void testContextCompilationIgnoresLegacyActiveRegistry() {
    vietvm::compiler::CompilationRegistryState legacyState;
    const int legacyName = legacyState.storeString("legacy-registry-poison");
    legacyState.functionBytecode.emplace(
        41, std::vector<Instruction>{{OP_DUNG_CHUONG_TRINH, 0, 0, 0}});
    legacyState.setFunctionNameIndex(41, legacyName);
    legacyState.nextFunctionId = 42;

    vietvm::compiler::CompilationContext context;
    auto *previous = vietvm::compiler::setActiveCompilationRegistryState(&legacyState);
    std::exception_ptr compileError;
    try {
        (void)vietvm::compiler::compilePipeline(
            context,
            "hàm main() { in \"context-registry-only\"; }",
            keywordMap,
            true);
    } catch (...) {
        compileError = std::current_exception();
    }
    vietvm::compiler::setActiveCompilationRegistryState(previous);

    expect(compileError == nullptr,
           "context-driven production pipeline compiles while a different legacy registry is active");
    expect(context.findString("context-registry-only") >= 0 &&
               context.findString("legacy-registry-poison") == -1,
           "context-driven production pipeline writes only to its explicit CompilationContext");
    expect(legacyState.findString("context-registry-only") == -1 &&
               legacyState.findString("legacy-registry-poison") == legacyName &&
               legacyState.functionBytecode.count(41) == 1 &&
               legacyState.nextFunctionId == 42,
           "context-driven production pipeline leaves the legacy active registry untouched");
}

void testConcurrentCompilationContexts() {
    const std::string firstSource = "hàm main() { in \"alpha-concurrent\"; }";
    const std::string secondSource = "hàm main() { in \"beta-concurrent\"; }";

    vietvm::compiler::CompilationContext firstContext;
    vietvm::compiler::CompilationContext secondContext;
    std::exception_ptr firstError;
    std::exception_ptr secondError;

    std::thread firstThread([&] {
        try {
            (void)vietvm::compiler::compilePipeline(
                firstContext, firstSource, keywordMap, true);
        } catch (...) {
            firstError = std::current_exception();
        }
    });
    std::thread secondThread([&] {
        try {
            (void)vietvm::compiler::compilePipeline(
                secondContext, secondSource, keywordMap, true);
        } catch (...) {
            secondError = std::current_exception();
        }
    });

    firstThread.join();
    secondThread.join();

    expect(firstError == nullptr && secondError == nullptr,
           "independent CompilationContext values can compile concurrently");
    expect(std::find(firstContext.stringPool.begin(), firstContext.stringPool.end(),
                     "alpha-concurrent") != firstContext.stringPool.end(),
           "first concurrent context keeps its own StringPool snapshot");
    expect(std::find(firstContext.stringPool.begin(), firstContext.stringPool.end(),
                     "beta-concurrent") == firstContext.stringPool.end(),
           "first concurrent context does not observe the second StringPool");
    expect(std::find(secondContext.stringPool.begin(), secondContext.stringPool.end(),
                     "beta-concurrent") != secondContext.stringPool.end(),
           "second concurrent context keeps its own StringPool snapshot");
    expect(std::find(secondContext.stringPool.begin(), secondContext.stringPool.end(),
                     "alpha-concurrent") == secondContext.stringPool.end(),
           "second concurrent context does not observe the first StringPool");
}

void testConcurrentCompilationContextsWithIndependentImportRoots() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path tempRoot = fs::temp_directory_path() /
                              ("vpp-compiler-import-context-" + std::to_string(nonce));
    const fs::path firstRoot = tempRoot / "first";
    const fs::path secondRoot = tempRoot / "second";
    fs::create_directories(firstRoot);
    fs::create_directories(secondRoot);

    auto writeFile = [](const fs::path &path, const std::string &contents) {
        std::ofstream output(path);
        if (!output.is_open()) {
            throw std::runtime_error("cannot create import fixture: " + path.u8string());
        }
        output << contents;
    };

    writeFile(firstRoot / "module.vi",
              "hàm marker() { in \"alpha-import-root\"; };\n");
    writeFile(secondRoot / "module.vi",
              "hàm marker() { in \"beta-import-root\"; };\n");

    const std::string source =
        "nhập module.vi;\n"
        "hàm main() { marker(); in \"root-module\"; };\n";

    vietvm::compiler::CompilationContext firstContext;
    firstContext.importResolutionBase = firstRoot;
    vietvm::compiler::CompilationContext secondContext;
    secondContext.importResolutionBase = secondRoot;
    std::exception_ptr firstError;
    std::exception_ptr secondError;

    std::thread firstThread([&] {
        try {
            (void)vietvm::compiler::compilePipeline(
                firstContext, source, keywordMap, true);
        } catch (...) {
            firstError = std::current_exception();
        }
    });
    std::thread secondThread([&] {
        try {
            (void)vietvm::compiler::compilePipeline(
                secondContext, source, keywordMap, true);
        } catch (...) {
            secondError = std::current_exception();
        }
    });

    firstThread.join();
    secondThread.join();

    auto containsString = [](const vietvm::compiler::CompilationContext &context,
                             const std::string &value) {
        return std::find(context.stringPool.begin(), context.stringPool.end(), value) !=
               context.stringPool.end();
    };

    expect(firstError == nullptr && secondError == nullptr,
           "concurrent import compilations use independent resolution roots");
    expect(containsString(firstContext, "alpha-import-root") &&
               !containsString(firstContext, "beta-import-root"),
           "first import context resolves module.vi only from its own root");
    expect(containsString(secondContext, "beta-import-root") &&
               !containsString(secondContext, "alpha-import-root"),
           "second import context resolves module.vi only from its own root");

    writeFile(firstRoot / "dependency.vi",
              "hàm dependency_marker() { in \"dependency-init-order\"; };\n");
    writeFile(firstRoot / "module.vi",
              "nhập dependency.vi;\n"
              "hàm marker() { in \"alpha-import-root\"; };\n");

    vietvm::compiler::CompilationContext semanticContext;
    semanticContext.importResolutionBase = firstRoot;
    const auto artifacts = vietvm::compiler::compilePipeline(
        semanticContext, source, keywordMap, true);
    bool resolvedImportedCall = false;
    for (const auto &binding : artifacts.semantic.callBindings) {
        if (binding.runtimeName == "marker" &&
            binding.kind == vietvm::compiler::CallTargetKind::ImportedFunction) {
            resolvedImportedCall = true;
        }
    }
    const bool dependencyFirst = semanticContext.moduleInitializers.size() == 2 &&
        fs::path(semanticContext.moduleInitializers[0].identity).filename() == "dependency.vi" &&
        fs::path(semanticContext.moduleInitializers[1].identity).filename() == "module.vi";
    expect(artifacts.moduleIndex.has_value() && resolvedImportedCall &&
               artifacts.unsupportedDirectIrRegions == 0 &&
               dependencyFirst,
           "top-level pipeline indexes local exports and keeps imported calls on direct IR");
    expect(dependencyFirst,
           "recursive imports retain dependency-before-importer module initializer order");

    // A recursively compiled module must receive the semantic exports of its own
    // direct imports. This is required when implementations and interfaces live
    // in separate source files.
    writeFile(firstRoot / "contract.vi",
              "giao diện WorkerContract { hàm run(); };\n");
    writeFile(firstRoot / "implementation.vi",
              "nhập contract.vi;\n"
              "lớp Worker triển khai WorkerContract {\n"
              "    hàm run() { trả về 1; };\n"
              "};\n");
    const std::string recursiveInterfaceSource =
        "nhập implementation.vi;\n"
        "hàm main() { in 1; };\n";
    vietvm::compiler::CompilationContext recursiveInterfaceContext;
    recursiveInterfaceContext.importResolutionBase = firstRoot;
    bool recursiveInterfaceCompiled = true;
    try {
        (void)vietvm::compiler::compilePipeline(
            recursiveInterfaceContext, recursiveInterfaceSource, keywordMap, true);
    } catch (...) {
        recursiveInterfaceCompiled = false;
    }
    expect(recursiveInterfaceCompiled,
           "recursive module compilation resolves interfaces from direct imports");

    std::error_code ignored;
    fs::remove_all(tempRoot, ignored);
}

void testSymbolTableIds() {
    std::unordered_map<std::string, int> symbols;
    int nextId = 10;

    const int alpha = vietvm::compiler::symbolTable::getOrCreate(symbols, "alpha", nextId);
    const int alphaAgain = vietvm::compiler::symbolTable::getOrCreate(symbols, "alpha", nextId);
    const int beta = vietvm::compiler::symbolTable::getOrCreate(symbols, "beta", nextId);

    expect(alpha == 10, "symbolTable uses the supplied next ID for a new symbol");
    expect(alphaAgain == alpha, "symbolTable preserves an existing symbol ID");
    expect(beta == 11, "symbolTable allocates sequential IDs for new symbols");
    expect(nextId == 12, "symbolTable advances next ID only for new symbols");

    std::unordered_map<std::string, int> independentSymbols;
    int independentNextId = 0;
    expect(vietvm::compiler::symbolTable::getOrCreate(independentSymbols, "alpha", independentNextId) == 0,
           "symbolTable keeps caller-provided symbol maps independent");
}

void testSharedTokenHelpers() {
    const std::vector<std::string> nameTokens = {"xin", "chào", "bạn"};
    expect(vietvm::compiler::joinNameTokens(nameTokens, 0, nameTokens.size()) == "xin chào bạn",
           "shared token join preserves multi-word names");
    expect(vietvm::compiler::isCallableNamePiece("xin chào"),
           "shared callable-name helper accepts multi-word identifiers");
    expect(!vietvm::compiler::isCallableNamePiece("42"),
           "shared callable-name helper rejects numeric tokens");
    expect(vietvm::compiler::isIdentifierLikeToken("tênBiến"),
           "shared identifier helper accepts identifiers");
    expect(!vietvm::compiler::isIdentifierLikeToken("đúng"),
           "shared identifier helper excludes boolean literals");

    expect(vietvm::compiler::splitTopLevelArguments("a, nested(b, c), d") ==
               std::vector<std::string>({"a", "nested(b, c)", "d"}),
           "shared argument splitter ignores commas inside nested parentheses");
    expect(vietvm::compiler::splitTopLevelArguments("  a  ,  b  ") ==
               std::vector<std::string>({"a", "b"}),
           "shared argument splitter trims top-level arguments");
    expect(vietvm::compiler::splitTopLevelArguments("\"x,y\", z") ==
               std::vector<std::string>({"\"x,y\"", "z"}),
           "shared argument splitter ignores commas inside quoted strings");
    expect(vietvm::compiler::splitTopLevelArguments("\"x(,)\", 'a,b', z") ==
               std::vector<std::string>({"\"x(,)\"", "'a,b'", "z"}),
           "shared argument splitter ignores punctuation inside both quote styles");
    expect(vietvm::compiler::splitTopLevelArguments("\"a\\\"(,b\", z") ==
               std::vector<std::string>({"\"a\\\"(,b\"", "z"}),
           "shared argument splitter keeps escaped quotes inside a string literal");
    expect(vietvm::compiler::splitTopLevelFields(
               "i = \"x;\"; i != \"\"; i = \"(\"", ';') ==
               std::vector<std::string>({"i = \"x;\"", "i != \"\"", "i = \"(\""}),
           "shared top-level splitter ignores loop delimiters inside strings");
}

void testSharedFunctionResolution() {
    using vietvm::compiler::StringPool;
    using vietvm::compiler::hamMap;

    vietvm::compiler::resetCompilationState();
    const int importedName = StringPool::storeString("hàm đã nhập");
    hamMap::bytecodeMap().emplace(5, std::vector<Instruction>{{OP_DUNG_CHUONG_TRINH, 0, 0, 0}});
    hamMap::setHamNameIndex(5, importedName);

    std::unordered_map<std::string, int> symbols;
    expect(vietvm::compiler::resolveFunctionIdByName("hàm đã nhập", symbols) == 5,
           "shared function resolver finds an imported/global function when enabled");
    expect(vietvm::compiler::resolveFunctionIdByName("hàm đã nhập", symbols, false) == -1,
           "shared function resolver preserves the explicit no-global-fallback mode");

    symbols.emplace("hàm đã nhập", 5);
    expect(vietvm::compiler::resolveFunctionIdByName("hàm đã nhập", symbols, false) == 5,
           "shared function resolver accepts a matching local symbol-table function");

    vietvm::compiler::resetCompilationState();
}

void testSharedCoreHelpersAndLayout() {
    expect(vietvm::core::trim(" \t xin chào \r\n") == "xin chào",
           "shared core trim removes leading and trailing whitespace");
    expect(vietvm::core::toLowerAscii("VPP HTTP") == "vpp http",
           "shared ASCII lowercase helper normalizes ASCII bytes");
    expect(vietvm::core::toLowerAscii("mạng") == "mạng",
           "shared ASCII lowercase helper preserves UTF-8 bytes");

    expect(vietvm::core::utf8Path(vietvm::core::kPrimaryPackageDirectory).u8string() == "gói",
           "shared package root keeps its UTF-8 spelling");
    expect(vietvm::core::isPackageDirectoryName("gói") &&
               vietvm::core::isPackageDirectoryName("goi") &&
               vietvm::core::isPackageDirectoryName("packages") &&
               !vietvm::core::isPackageDirectoryName("modules"),
           "shared package directory constants preserve lookup compatibility");
    expect(vietvm::core::packageEntryPath(vietvm::core::utf8Path("gói/demo")).filename().string() == "main.vi",
           "shared package entry helper uses the canonical entry filename");
}

} // namespace

int main() {
    testStringPool();
    testHamMapAllocator();
    testCompilationStateReset();
    testRepeatedTopLevelCompilationLifecycle();
    testCompilationContextLifecycle();
    testDeterministicCompilationArtifacts();
    testContextCompilationIgnoresLegacyActiveRegistry();
    testConcurrentCompilationContexts();
    testConcurrentCompilationContextsWithIndependentImportRoots();
    testSymbolTableIds();
    testSharedTokenHelpers();
    testSharedFunctionResolution();
    testSharedCoreHelpersAndLayout();

    if (failures != 0) {
        std::cerr << failures << " compiler support unit test(s) failed\n";
        return 1;
    }

    std::cout << "compiler support unit tests passed\n";
    return 0;
}

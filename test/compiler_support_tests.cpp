#include <algorithm>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
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

int failures = 0;

void expect(bool condition, const std::string &message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
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

    hamMap::hamBytecodeMap.clear();
    hamMap::clearHamNameIndexMap();
    hamMap::resetHamIdCounter();

    expect(hamMap::allocHamId() == 0, "hamMap allocator starts at zero after reset");
    expect(hamMap::allocHamId() == 1, "hamMap allocator increments IDs");

    hamMap::setHamNameIndex(1, 42);
    const auto name = hamMap::hamNameIndexMap.find(1);
    expect(name != hamMap::hamNameIndexMap.end() && name->second == 42,
           "hamMap stores a function name index");

    hamMap::clearHamNameIndexMap();
    expect(hamMap::hamNameIndexMap.empty(), "hamMap clears function name indices");
    hamMap::resetHamIdCounter();
    expect(hamMap::allocHamId() == 0, "hamMap reset makes IDs reproducible");
}

void testCompilationStateReset() {
    using vietvm::compiler::StringPool;
    using vietvm::compiler::hamMap;

    vietvm::compiler::resetCompilationState();

    StringPool::storeString("state-that-must-be-cleared");
    hamMap::hamBytecodeMap.emplace(7, std::vector<Instruction>{{OP_DUNG_CHUONG_TRINH, 0, 0, 0}});
    hamMap::setHamNameIndex(7, 3);
    (void)hamMap::allocHamId();
    vietvm::compiler::importedFiles.insert("/tmp/imported-once.vi");
    vietvm::compiler::registerClassMethodVisibility("NoiBo.chiNoiBo", "NoiBo", "riêng tư");
    vietvm::compiler::pushClassContext("DangXuLy");

    expect(vietvm::compiler::currentClassContext() == "DangXuLy",
           "class context is populated before compilation-state reset");

    vietvm::compiler::resetCompilationState();

    expect(StringPool::size() == 0, "compilation-state reset clears StringPool");
    expect(hamMap::hamBytecodeMap.empty(), "compilation-state reset clears function bytecode");
    expect(hamMap::hamNameIndexMap.empty(), "compilation-state reset clears function name indices");
    expect(vietvm::compiler::importedFiles.empty(), "compilation-state reset clears imported files");
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
    const auto firstNames = hamMap::hamNameIndexMap;
    const std::size_t firstFunctionCount = hamMap::hamBytecodeMap.size();

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
    expect(hamMap::hamNameIndexMap == firstNames,
           "recompiling the same source after reset reproduces function-name IDs");
    expect(hamMap::hamBytecodeMap.size() == firstFunctionCount,
           "recompiling the same source after reset reproduces function count");
    expect(firstAgain.backend == first.backend &&
               firstAgain.bytecode.size() == first.bytecode.size(),
           "recompiling the same source after reset preserves backend and root bytecode shape");

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
    expect(StringPool::size() == 0 && hamMap::hamBytecodeMap.empty() &&
               hamMap::hamNameIndexMap.empty(),
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
    expect(firstAgain.backend == first.backend &&
               firstAgain.bytecode.size() == first.bytecode.size(),
           "repeated context-driven compilation preserves backend and root bytecode shape");
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
    hamMap::hamBytecodeMap.emplace(5, std::vector<Instruction>{{OP_DUNG_CHUONG_TRINH, 0, 0, 0}});
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

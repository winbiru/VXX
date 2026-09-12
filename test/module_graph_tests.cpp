#include "vpp/compiler/module_graph.h"
#include "vpp/core/message_constants.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using vietvm::compiler::LocalModuleEdgeAction;
using vietvm::compiler::LocalModuleGraph;
using vietvm::compiler::LocalModuleGraphBuilder;
using vietvm::compiler::LocalModuleResolver;
using vietvm::frontend::AstImportSpec;

int failures = 0;

void expect(bool condition, const std::string &message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

class TemporaryTree {
public:
    TemporaryTree() {
        const auto stamp = std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count();
        root_ = fs::temp_directory_path() /
                ("vpp-module-graph-" + std::to_string(stamp));
        fs::create_directories(root_);
    }

    ~TemporaryTree() {
        std::error_code ignored;
        fs::remove_all(root_, ignored);
    }

    const fs::path &root() const noexcept { return root_; }

private:
    fs::path root_;
};

class ScopedCurrentPath {
public:
    explicit ScopedCurrentPath(const fs::path &path)
        : previous_(fs::current_path()) {
        fs::current_path(path);
    }

    ~ScopedCurrentPath() {
        std::error_code ignored;
        fs::current_path(previous_, ignored);
    }

private:
    fs::path previous_;
};

void writeFile(const fs::path &path, const std::string &contents) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path);
    if (!output.is_open()) {
        throw std::runtime_error("cannot create module-graph test file");
    }
    output << contents;
}

AstImportSpec importSpec(std::string target,
                         std::string alias = {},
                         bool quoted = false) {
    AstImportSpec spec;
    spec.target = std::move(target);
    spec.targetSpan.begin.offset = 11;
    spec.targetSpan.end.offset = 22;
    spec.quoted = quoted;
    spec.alias = std::move(alias);
    spec.aliasSpan.begin.offset = 25;
    spec.aliasSpan.end.offset = 29;
    spec.hasSemicolon = true;
    return spec;
}

void testResolveUpwardAndReadUtf8Source() {
    TemporaryTree tree;
    const fs::path searchRoot = tree.root() / "search-root";
    const fs::path resolutionBase = searchRoot / "out" / "debug" / "bin";
    const fs::path modulePath =
        searchRoot / "project" / "modules" / fs::u8path(u8"tính toán.vi");
    const fs::path unrelatedCwd = tree.root() / "unrelated-cwd";
    const fs::path decoyPath =
        unrelatedCwd / "project" / "modules" / fs::u8path(u8"tính toán.vi");
    fs::create_directories(resolutionBase);
    writeFile(modulePath, u8"hàm cộng(a, b) { trả về a + b; }\n");
    writeFile(decoyPath, "wrong module from process cwd");
    const ScopedCurrentPath currentPath(unrelatedCwd);

    const LocalModuleResolver resolver(resolutionBase);
    const AstImportSpec spec = importSpec(u8"project/modules/tính toán.vi", "toan", true);
    const auto location = resolver.resolve(spec);
    const fs::path expectedPath = fs::absolute(modulePath).lexically_normal();

    expect(resolver.resolutionBase() == fs::absolute(resolutionBase).lexically_normal(),
           "resolver stores an absolute lexical search base");
    expect(location.path == expectedPath,
           "resolver ignores the process cwd and walks upward from its explicit base");
    expect(location.identity == expectedPath.u8string(),
           "module identity is absolute.lexically_normal().u8string()");

    const auto loaded = resolver.read(location);
    expect(loaded.path == expectedPath && loaded.identity == location.identity,
           "source record retains the resolved path and lexical identity");
    expect(loaded.source == u8"hàm cộng(a, b) { trả về a + b; }\n",
           "resolver reads UTF-8 module source without rewriting it");
}

void testOrderedGraphActionsAndMetadata() {
    TemporaryTree tree;
    const fs::path resolutionBase = tree.root() / "build" / "test";
    fs::create_directories(resolutionBase);
    writeFile(tree.root() / "graph" / "a.vi", "module-a");
    writeFile(tree.root() / "graph" / "b.vi", "module-b");
    writeFile(tree.root() / "graph" / "c.vi", "module-c");

    std::unordered_map<std::string, int> scanCounts;
    LocalModuleGraphBuilder builder(
        LocalModuleResolver(resolutionBase),
        [&](const std::string &source, const fs::path &sourcePath) {
            ++scanCounts[sourcePath.filename().u8string()];
            if (source == "module-a") {
                return std::vector<AstImportSpec>{
                    importSpec("graph/b.vi", "bee", true),
                    importSpec("graph/c.vi"),
                    importSpec("graph/../graph/b.vi")};
            }
            if (source == "module-b") {
                return std::vector<AstImportSpec>{importSpec("graph/a.vi")};
            }
            return std::vector<AstImportSpec>{};
        });

    const AstImportSpec rootA = importSpec("graph/a.vi", "alpha", true);
    const LocalModuleGraph graph = builder.build(
        "entry://main.vi", {rootA, importSpec("graph/./a.vi")});

    expect(graph.entryIdentity == "entry://main.vi",
           "graph preserves the caller's entry identity");
    expect(graph.modules.size() == 3,
           "each imported module appears once despite a cycle and duplicates");
    if (graph.modules.size() == 3) {
        expect(graph.modules[0].path.filename() == "a.vi" &&
                   graph.modules[1].path.filename() == "b.vi" &&
                   graph.modules[2].path.filename() == "c.vi",
               "module records use first-load DFS preorder");
    }

    const std::vector<LocalModuleEdgeAction> expectedActions = {
        LocalModuleEdgeAction::Load,
        LocalModuleEdgeAction::Load,
        LocalModuleEdgeAction::CycleNoOp,
        LocalModuleEdgeAction::Load,
        LocalModuleEdgeAction::DuplicateNoOp,
        LocalModuleEdgeAction::DuplicateNoOp,
    };
    expect(graph.edges.size() == expectedActions.size(),
           "graph records loads, cycle no-ops, and duplicate no-ops as ordered edges");
    if (graph.edges.size() == expectedActions.size()) {
        for (std::size_t index = 0; index < expectedActions.size(); ++index) {
            expect(graph.edges[index].action == expectedActions[index],
                   "edge action follows source-order DFS semantics at index " +
                       std::to_string(index));
        }
        expect(graph.edges[0].importerIdentity == "entry://main.vi",
               "root edge is attributed to the entry module");
        expect(graph.edges[2].importedIdentity == graph.edges[0].importedIdentity,
               "an import of an Active identity is classified as a cycle");
        expect(graph.edges[4].importedIdentity == graph.edges[1].importedIdentity,
               "lexically equivalent paths share one module identity");
        expect(graph.edges[0].importSpec.alias == "alpha" &&
                   graph.edges[0].importSpec.quoted &&
                   graph.edges[0].importSpec.targetSpan.begin.offset == 11 &&
                   graph.edges[1].importSpec.alias == "bee",
               "ordered edges preserve structured import and alias metadata");
    }

    expect(scanCounts["a.vi"] == 1 && scanCounts["b.vi"] == 1 &&
               scanCounts["c.vi"] == 1,
           "duplicates and cycles do not re-read or re-scan modules");
    expect(std::string(vietvm::compiler::localModuleEdgeActionName(
               LocalModuleEdgeAction::DuplicateNoOp)) == "duplicate_no_op",
           "edge action has stable tooling text");
}

void testReadAndScanFailureCanRetry() {
    TemporaryTree tree;
    const fs::path resolutionBase = tree.root() / "base";
    const fs::path delayedModule = tree.root() / "delayed.vi";
    fs::create_directories(resolutionBase);

    int scanAttempts = 0;
    LocalModuleGraphBuilder builder(
        LocalModuleResolver(resolutionBase),
        [&](const std::string &, const fs::path &) {
            ++scanAttempts;
            if (scanAttempts == 1) {
                throw std::runtime_error("synthetic parse failure");
            }
            return std::vector<AstImportSpec>{};
        });
    const AstImportSpec absoluteImport = importSpec(delayedModule.u8string());

    bool readFailed = false;
    try {
        (void)builder.build("entry", {absoluteImport});
    } catch (const std::runtime_error &error) {
        const std::string_view messageTemplate = vietvm::messages::kImportCannotOpenFile;
        const std::string prefix(messageTemplate.substr(0, messageTemplate.find("{0}")));
        readFailed = std::string(error.what()).find(prefix) != std::string::npos;
    }
    expect(readFailed, "a missing resolved file reports the public import diagnostic");

    writeFile(delayedModule, "ready");
    bool parseFailed = false;
    try {
        (void)builder.build("entry", {absoluteImport});
    } catch (const std::runtime_error &error) {
        parseFailed = std::string(error.what()) == "synthetic parse failure";
    }
    expect(parseFailed, "scanner failures propagate after the module is marked Active");

    const LocalModuleGraph retried = builder.build("entry", {absoluteImport});
    expect(retried.modules.size() == 1 && retried.edges.size() == 1 &&
               retried.edges[0].action == LocalModuleEdgeAction::Load,
           "failed read/parse attempts do not poison a later graph build");
}

void testScannerIsRequired() {
    bool rejected = false;
    try {
        LocalModuleGraphBuilder builder(LocalModuleResolver{}, {});
        (void)builder;
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    expect(rejected, "graph construction rejects an empty import scanner");
}

} // namespace

int main() {
    testResolveUpwardAndReadUtf8Source();
    testOrderedGraphActionsAndMetadata();
    testReadAndScanFailureCanRetry();
    testScannerIsRequired();

    if (failures != 0) {
        std::cerr << failures << " module graph test(s) failed\n";
        return 1;
    }
    std::cout << "module graph tests passed\n";
    return 0;
}

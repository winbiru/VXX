#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "vpp/frontend/ast.h"

namespace vietvm::compiler {

// A resolved local import has a lexical identity on purpose.  Symlink-aware
// canonicalization would change the identity historically used by the import
// compiler and can also fail for a missing target before a useful diagnostic
// is produced.
struct LocalModuleLocation {
    std::filesystem::path path;
    std::string identity;
};

struct LocalModuleSource {
    std::filesystem::path path;
    std::string identity;
    std::string source;
};

// Resolves only the first structured import cohort: explicit local .vi files.
// Callers obtain AstImportSpec from an AstImportForm::LocalSourceFile
// statement; package and bare-module lookup remain outside this API for now.
class LocalModuleResolver {
public:
    explicit LocalModuleResolver(
        std::filesystem::path resolutionBase = std::filesystem::current_path());

    const std::filesystem::path &resolutionBase() const noexcept;
    LocalModuleLocation resolve(
        const vietvm::frontend::AstImportSpec &importSpec) const;
    LocalModuleSource read(const LocalModuleLocation &location) const;

private:
    std::filesystem::path resolutionBase_;
};

enum class LocalModuleEdgeAction {
    Load,
    DuplicateNoOp,
    CycleNoOp,
};

const char *localModuleEdgeActionName(LocalModuleEdgeAction action) noexcept;

// Edges include no-op encounters as well as first loads.  This makes source
// order, duplicate suppression, cycles, and alias metadata independently
// observable without consulting compiler-global state.
struct LocalModuleEdge {
    std::string importerIdentity;
    std::string importedIdentity;
    vietvm::frontend::AstImportSpec importSpec;
    LocalModuleEdgeAction action = LocalModuleEdgeAction::Load;
};

struct LocalModuleGraph {
    std::string entryIdentity;
    // Unique first loads in depth-first preorder.
    std::vector<LocalModuleSource> modules;
    // Every import encounter in the same traversal order, including no-ops.
    std::vector<LocalModuleEdge> edges;
};

using LocalModuleImportScanner = std::function<
    std::vector<vietvm::frontend::AstImportSpec>(
        const std::string &source,
        const std::filesystem::path &sourcePath)>;

// Builds an ordered graph from already-structured import metadata.  Parsing
// is injected so this foundation stays independent of the compiler pipeline;
// a later integration can scan each loaded module's AstProgram here.
class LocalModuleGraphBuilder {
public:
    LocalModuleGraphBuilder(LocalModuleResolver resolver,
                            LocalModuleImportScanner importScanner);

    LocalModuleGraph build(
        std::string entryIdentity,
        const std::vector<vietvm::frontend::AstImportSpec> &rootImports) const;

private:
    LocalModuleResolver resolver_;
    LocalModuleImportScanner importScanner_;
};

} // namespace vietvm::compiler

#include "vpp/compiler/module_graph.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "vpp/core/message_constants.h"
#include "vpp/core/project_layout.h"

namespace vietvm::compiler {
namespace {

namespace fs = std::filesystem;

fs::path absoluteLexical(const fs::path &path) {
    try {
        return fs::absolute(path).lexically_normal();
    } catch (...) {
        return path.lexically_normal();
    }
}

enum class VisitState {
    Active,
    Loaded,
};

struct BuildContext {
    const LocalModuleResolver &resolver;
    const LocalModuleImportScanner &scanner;
    LocalModuleGraph graph;
    std::unordered_map<std::string, VisitState> states;

    void visit(const std::string &importerIdentity,
               const vietvm::frontend::AstImportSpec &importSpec) {
        const LocalModuleLocation location = resolver.resolve(importSpec);

        LocalModuleEdge edge;
        edge.importerIdentity = importerIdentity;
        edge.importedIdentity = location.identity;
        edge.importSpec = importSpec;

        const auto existing = states.find(location.identity);
        if (existing != states.end()) {
            edge.action = existing->second == VisitState::Active
                              ? LocalModuleEdgeAction::CycleNoOp
                              : LocalModuleEdgeAction::DuplicateNoOp;
            graph.edges.push_back(std::move(edge));
            return;
        }

        // Mark before reading or parsing.  A recursive import can now observe
        // this identity as Active, while any failure below rolls the mark back.
        states.emplace(location.identity, VisitState::Active);
        edge.action = LocalModuleEdgeAction::Load;
        graph.edges.push_back(std::move(edge));

        try {
            LocalModuleSource module = resolver.read(location);
            const std::vector<vietvm::frontend::AstImportSpec> imports =
                scanner(module.source, module.path);
            graph.modules.push_back(std::move(module));

            for (const vietvm::frontend::AstImportSpec &nestedImport : imports) {
                visit(location.identity, nestedImport);
            }
            states[location.identity] = VisitState::Loaded;
        } catch (...) {
            states.erase(location.identity);
            throw;
        }
    }
};

} // namespace

LocalModuleResolver::LocalModuleResolver(fs::path resolutionBase)
    : resolutionBase_(absoluteLexical(std::move(resolutionBase))) {}

const fs::path &LocalModuleResolver::resolutionBase() const noexcept {
    return resolutionBase_;
}

LocalModuleLocation LocalModuleResolver::resolve(
    const vietvm::frontend::AstImportSpec &importSpec) const {
    const fs::path requested = vietvm::core::utf8Path(importSpec.target);
    // `resolutionBase_` defaults to the process cwd for legacy compatibility,
    // but an explicit base is authoritative (and makes graph construction
    // deterministic for embedders and tests whose process cwd is elsewhere).
    fs::path resolved = absoluteLexical(resolutionBase_ / requested);

    if (!fs::exists(resolved)) {
        for (fs::path directory = resolutionBase_;;
             directory = directory.parent_path()) {
            const fs::path candidate = directory / requested;
            if (fs::exists(candidate)) {
                resolved = absoluteLexical(candidate);
                break;
            }
            if (directory == directory.parent_path()) break;
        }
    }

    resolved = resolved.lexically_normal();
    return {resolved, resolved.u8string()};
}

LocalModuleSource LocalModuleResolver::read(
    const LocalModuleLocation &location) const {
    std::ifstream input(location.path);
    if (!input.is_open()) {
        throw std::runtime_error(vietvm::messages::formatMessage(
            vietvm::messages::kImportCannotOpenFile, {location.identity}));
    }

    std::ostringstream source;
    source << input.rdbuf();
    if (input.bad()) {
        throw std::runtime_error(vietvm::messages::formatMessage(
            vietvm::messages::kImportCannotOpenFile, {location.identity}));
    }
    return {location.path, location.identity, source.str()};
}

const char *localModuleEdgeActionName(LocalModuleEdgeAction action) noexcept {
    switch (action) {
        case LocalModuleEdgeAction::Load: return "load";
        case LocalModuleEdgeAction::DuplicateNoOp: return "duplicate_no_op";
        case LocalModuleEdgeAction::CycleNoOp: return "cycle_no_op";
    }
    return "unknown";
}

LocalModuleGraphBuilder::LocalModuleGraphBuilder(
    LocalModuleResolver resolver,
    LocalModuleImportScanner importScanner)
    : resolver_(std::move(resolver)),
      importScanner_(std::move(importScanner)) {
    if (!importScanner_) {
        throw std::invalid_argument(
            "LocalModuleGraphBuilder requires an import scanner");
    }
}

LocalModuleGraph LocalModuleGraphBuilder::build(
    std::string entryIdentity,
    const std::vector<vietvm::frontend::AstImportSpec> &rootImports) const {
    BuildContext context{resolver_, importScanner_, {}, {}};
    context.graph.entryIdentity = std::move(entryIdentity);
    for (const vietvm::frontend::AstImportSpec &rootImport : rootImports) {
        context.visit(context.graph.entryIdentity, rootImport);
    }
    return std::move(context.graph);
}

} // namespace vietvm::compiler

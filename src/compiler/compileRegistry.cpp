#include "compiler/compileRegistry.h"

#include <cstdlib>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common/storeString.h"
#include "vpp/compiler/package_resolver.h"
#include "vpp/compiler/pipeline.h"
#include "vpp/core/message_constants.h"
#include "vpp/core/project_layout.h"

namespace vietvm { namespace compiler {
    bool hasSelectiveImports(const CompilationRegistryState &state) {
        if (state.activeModuleSemanticIndex == nullptr) return false;
        const auto &edges = state.activeModuleSemanticIndex->graph.edges;
        return std::any_of(edges.begin(), edges.end(), [](const auto &edge) {
            return !edge.importSpec.selectedName.empty() && edge.importSpec.selectedName != "*";
        });
    }
    // Trả tập import của registry cụ thể để production path không phụ thuộc state thread-local.
    std::unordered_set<std::string> &importedFileSet(CompilationRegistryState &state) {
        return state.importedFiles;
    }

    // Trả tập đường dẫn file đã import trong compilation registry; compiler dùng tập này để ngăn import cùng source lặp lại.
    std::unordered_set<std::string> &importedFileSet() {
        return importedFileSet(activeCompilationRegistryState());
    }

    // Xóa import state của registry cụ thể.
    void clearImportedFiles(CompilationRegistryState &state) {
        state.importedFiles.clear();
        state.importedFunctionAliases.clear();
    }

    // Xóa imported files; hàm đưa cấu trúc trạng thái về rỗng để lần sử dụng tiếp theo không mang dữ liệu cũ.
    void clearImportedFiles() { clearImportedFiles(activeCompilationRegistryState()); }

    // Xóa access metadata/class-context của registry cụ thể.
    void clearClassAccessState(CompilationRegistryState &state) {
        state.methodAccess.clear();
        state.classContextStack.clear();
    }

    // Xóa lớp access trạng thái; hàm đưa cấu trúc trạng thái về rỗng để lần sử dụng tiếp theo không mang dữ liệu cũ.
    void clearClassAccessState() {
        clearClassAccessState(activeCompilationRegistryState());
    }

    // Đưa class context vào registry cụ thể.
    void pushClassContext(CompilationRegistryState &state, const std::string &className) {
        state.classContextStack.push_back(className);
    }

    // Đưa vào lớp ngữ cảnh; hàm thêm phần tử vào ngăn xếp hoặc ngữ cảnh hiện tại để được dùng trước khi rời phạm vi.
    void pushClassContext(const std::string &className) {
        pushClassContext(activeCompilationRegistryState(), className);
    }

    // Pop class context khỏi registry cụ thể.
    void popClassContext(CompilationRegistryState &state) {
        auto &stack = state.classContextStack;
        if (!stack.empty()) stack.pop_back();
    }

    // Lấy ra khỏi lớp ngữ cảnh; hàm loại bỏ phần tử/ngữ cảnh trên cùng và khôi phục trạng thái trước đó.
    void popClassContext() {
        popClassContext(activeCompilationRegistryState());
    }

    // Trả class context hiện tại từ registry cụ thể.
    std::string currentClassContext(const CompilationRegistryState &state) {
        if (state.classContextStack.empty()) return "";
        return state.classContextStack.back();
    }

    // Trả tên lớp đang ở đỉnh class-context stack; lookup method/visibility dùng giá trị này khi phân giải lời gọi không ghi rõ lớp.
    std::string currentClassContext() {
        return currentClassContext(activeCompilationRegistryState());
    }

    // Đăng ký visibility phương thức trực tiếp vào registry cụ thể.
    void registerClassMethodVisibility(CompilationRegistryState &state,
                                       const std::string &fullMethodName,
                                       const std::string &ownerClass,
                                       const std::string &visibility) {
        state.methodAccess[fullMethodName] = MethodAccessInfo{ownerClass, visibility};
    }

    // Đăng ký lớp phương thức phạm vi truy cập; hàm thêm metadata vào bảng đăng ký để các bước phân giải/thực thi có thể tra cứu về sau.
    void registerClassMethodVisibility(const std::string &fullMethodName,
                                       const std::string &ownerClass,
                                       const std::string &visibility) {
        registerClassMethodVisibility(
            activeCompilationRegistryState(), fullMethodName, ownerClass, visibility);
    }

    // Phân giải callable theo class context của registry cụ thể.
    std::string resolveCallableNameInContext(CompilationRegistryState &state,
                                             const std::string &name,
                                             const std::unordered_map<std::string,int> &symTab) {
        if (name.find('.') != std::string::npos) return name;

        std::string cls = currentClassContext(state);
        if (cls.empty()) return name;

        std::string scopedName = cls + "." + name;
        if (symTab.find(scopedName) != symTab.end()) return scopedName;

        if (state.findString(scopedName) >= 0) return scopedName;
        return name;
    }

    // Phân giải callable tên in ngữ cảnh; hàm lần theo metadata/phạm vi liên quan để biến tham chiếu đầu vào thành đích cụ thể.
    std::string resolveCallableNameInContext(const std::string &name,
                                             const std::unordered_map<std::string,int> &symTab) {
        return resolveCallableNameInContext(activeCompilationRegistryState(), name, symTab);
    }

    // Kiểm tra visibility dựa trên registry cụ thể.
    void validateCallableAccess(const CompilationRegistryState &state,
                                const std::string &resolvedName) {
        const auto it = state.methodAccess.find(resolvedName);
        if (it == state.methodAccess.end()) return;

        const std::string &owner = it->second.ownerClass;
        const std::string &visibility = it->second.visibility;
        const std::string currentClass = currentClassContext(state);

        if (visibility == "công khai") return;
        if (visibility == "riêng tư") {
            if (currentClass != owner) {
                throw std::runtime_error(vietvm::messages::formatMessage(
                    vietvm::messages::kSemanticPrivateMethodAccess, {resolvedName}));
            }
            return;
        }
        if (visibility == "bảo vệ") {
            if (currentClass != owner) {
                throw std::runtime_error(vietvm::messages::formatMessage(
                    vietvm::messages::kSemanticProtectedMethodAccess, {resolvedName}));
            }
            return;
        }
    }

    // Kiểm tra điều kiện của `validateCallableAccess`.
    void validateCallableAccess(const std::string &resolvedName) {
        validateCallableAccess(activeCompilationRegistryState(), resolvedName);
    }

    // Phân giải function id trực tiếp trong registry cụ thể.
    int resolveFunctionIdByName(CompilationRegistryState &state,
                                const std::string &name,
                                const std::unordered_map<std::string,int> &symTab,
                                bool includeGlobalFallback) {
        std::string resolvedName = resolveCallableNameInContext(state, name, symTab);
        validateCallableAccess(state, resolvedName);

        auto idMatchesResolvedName = [&](int candidateId) {
            const int resolvedNameIndex = state.findString(resolvedName);
            if (resolvedNameIndex < 0) return false;
            const auto itName = state.functionNameIndices.find(candidateId);
            if (itName == state.functionNameIndices.end()) return false;
            return itName->second == resolvedNameIndex;
        };

        const auto itSym = symTab.find(resolvedName);
        if (itSym != symTab.end()) {
            const int maybeId = itSym->second;
            const auto itCode = state.functionBytecode.find(maybeId);
            if (itCode != state.functionBytecode.end() &&
                !itCode->second.empty() && idMatchesResolvedName(maybeId)) {
                return maybeId;
            }
        }

        if (!includeGlobalFallback) return -1;

        // Import trùng chỉ được khởi tạo một lần nhưng mỗi bí danh vẫn phải
        // giải được về cùng function ID. functionNameIndices là 1:1 (id -> tên
        // runtime) nên không thể dùng nó để biểu diễn nhiều alias của một hàm.
        const auto importedAlias = state.importedFunctionAliases.find(resolvedName);
        if (importedAlias != state.importedFunctionAliases.end()) {
            return importedAlias->second;
        }

        const int nameIndex = state.findString(resolvedName);
        if (nameIndex >= 0) {
            for (const auto &entry : state.functionNameIndices) {
                const auto owner = state.functionModuleIdentities.find(entry.first);
                if (entry.second == nameIndex &&
                    (!hasSelectiveImports(state) || owner == state.functionModuleIdentities.end() ||
                     owner->second == state.currentSemanticModuleIdentity)) return entry.first;
            }
        }
        return -1;
    }

    // Phân giải hàm mã định danh by tên; hàm lần theo metadata/phạm vi liên quan để biến tham chiếu đầu vào thành đích cụ thể.
    int resolveFunctionIdByName(const std::string &name,
                                const std::unordered_map<std::string,int> &symTab,
                                bool includeGlobalFallback) {
        return resolveFunctionIdByName(
            activeCompilationRegistryState(), name, symTab, includeGlobalFallback);
    }

    // Biên dịch import vào registry cụ thể và giữ recursive imports trong cùng compilation session.
    void compileImportSpec(
        CompilationRegistryState &state,
        const vietvm::frontend::AstImportSpec &spec,
        int &nextId,
        const std::unordered_map<std::string,Opcode> &keywordMap) {
        if (spec.target.empty()) {
            throw std::runtime_error(vietvm::messages::formatMessage(
                vietvm::messages::kImportMissingTarget));
        }
        namespace fs = std::filesystem;
        const bool selectiveGraph = hasSelectiveImports(state) ||
            (!spec.selectedName.empty() && spec.selectedName != "*");

        // Tạo tên nguồn dễ đọc cho debug metadata. Module vẫn dùng đường dẫn
        // tuyệt đối làm khóa runtime, còn stack trace ưu tiên đường dẫn tương đối
        // khi file nằm dưới thư mục làm việc hiện tại để kết quả ổn định giữa máy.
        auto debugIdentityForPath = [](const fs::path &sourcePath) {
            try {
                const fs::path relative = sourcePath.lexically_relative(
                    fs::current_path().lexically_normal());
                const std::string display = relative.generic_u8string();
                if (!display.empty() && display != ".." &&
                    display.rfind("../", 0) != 0) {
                    return display;
                }
            } catch (...) {
            }
            return sourcePath.lexically_normal().generic_u8string();
        };

        std::optional<fs::path> installationHome;
        if (const char *vppHome = std::getenv(vietvm::core::kEnvVppHome)) {
            installationHome = vietvm::core::utf8Path(vppHome);
        }
        const PackageResolver resolver(state.importResolutionBase, installationHome);
        const PackageResolution resolved = resolver.resolve(spec.target);
        const fs::path abs = resolved.path;
        const std::string canonical = abs.u8string();

        auto registerImportedAliases = [&]() {
            const auto surface = state.moduleFunctionExports.find(canonical);
            if (surface == state.moduleFunctionExports.end()) return;
            for (const auto &exported : surface->second) {
                if (!matchesImportSelection(spec, exported.name)) continue;
                const std::string visibleName = importedSymbolName(spec, exported.name);
                const auto [existing, inserted] =
                    state.importedFunctionAliases.emplace(visibleName, exported.functionId);
                if (!inserted && existing->second != exported.functionId) {
                    throw std::runtime_error(
                        "import: xung đột bí danh hàm '" + visibleName + "'");
                }
            }
        };

        auto &importedFiles = state.importedFiles;
        if (importedFiles.find(canonical) != importedFiles.end()) {
            registerImportedAliases();
            return;
        }

        importedFiles.insert(canonical);

        try {
            const vietvm::frontend::AstProgram *cachedProgram =
                state.activeModuleSemanticIndex == nullptr
                    ? nullptr
                    : state.activeModuleSemanticIndex->parsedProgram(canonical);
            std::string src;
            if (cachedProgram == nullptr) {
                std::ifstream ifs(abs);
                if (!ifs.is_open()) {
                    throw std::runtime_error(vietvm::messages::formatMessage(
                        vietvm::messages::kImportCannotOpenFile, {canonical}));
                }
                std::stringstream ss;
                ss << ifs.rdbuf();
                src = ss.str();
            }

            // Import tương đối bên trong module phải resolve từ thư mục chứa chính
            // module đó. Khôi phục base của importer ngay sau recursive compile để
            // sibling import ở scope ngoài không bị đổi nghĩa.
            const fs::path previousResolutionBase = state.importResolutionBase;
            const std::string previousSourceIdentity = state.currentSourceIdentity;
            const std::string previousSemanticModuleIdentity =
                state.currentSemanticModuleIdentity;
            std::unordered_map<std::string, int> previousAliases;
            if (selectiveGraph) {
                previousAliases = std::move(state.importedFunctionAliases);
                state.importedFunctionAliases.clear();
            }
            state.importResolutionBase = abs.parent_path();
            state.currentSourceIdentity = debugIdentityForPath(abs);
            state.currentSemanticModuleIdentity = canonical;
            CompilationArtifacts moduleArtifacts;
            try {
                if (cachedProgram != nullptr) {
                    moduleArtifacts = compileParsedPipelineInRegistry(
                        state, *cachedProgram, keywordMap, false, false);
                } else {
                    moduleArtifacts = compilePipelineInRegistry(
                        state, src, keywordMap, false, false);
                }
            } catch (...) {
                state.importResolutionBase = previousResolutionBase;
                state.currentSourceIdentity = previousSourceIdentity;
                state.currentSemanticModuleIdentity = previousSemanticModuleIdentity;
                if (selectiveGraph) state.importedFunctionAliases = std::move(previousAliases);
                throw;
            }
            state.importResolutionBase = previousResolutionBase;
            state.currentSourceIdentity = previousSourceIdentity;
            state.currentSemanticModuleIdentity = previousSemanticModuleIdentity;
            if (selectiveGraph) state.importedFunctionAliases = std::move(previousAliases);
            auto moduleBytecode = moduleArtifacts.bytecode;
            state.moduleInitializers.push_back(
                CompiledModuleInitializer{canonical, moduleBytecode,
                                          moduleArtifacts.bytecodeDebugInfo});

            std::vector<CompiledModuleFunctionExport> moduleExports;
            std::unordered_set<std::string> exportedDirectFunctions;
            for (const auto &statement : moduleArtifacts.ast.statements) {
                if (statement.kind != vietvm::frontend::AstStatementKind::Function ||
                    statement.declarationName.empty()) {
                    continue;
                }
                if (statement.visibility == vietvm::frontend::AstVisibility::Unspecified ||
                    statement.visibility == vietvm::frontend::AstVisibility::Public) {
                    exportedDirectFunctions.insert(statement.declarationName);
                }
            }
            for (const auto &ins : moduleBytecode) {
                if (ins.op != OP_HAM || ins.operand < 0 ||
                    ins.operand >= static_cast<int>(state.stringCount())) {
                    continue;
                }
                const std::string &functionName = state.getString(ins.operand);
                if (exportedDirectFunctions.count(functionName) != 0) {
                    moduleExports.push_back({functionName, ins.operandIndex});
                }
            }

            const LocalModuleSemanticIndex *semanticIndex =
                moduleArtifacts.moduleIndex.has_value()
                    ? &*moduleArtifacts.moduleIndex
                    : state.activeModuleSemanticIndex;
            const std::string_view semanticImporterIdentity =
                moduleArtifacts.moduleIndex.has_value()
                    ? kCurrentCompilationModuleIdentity
                    : std::string_view(canonical);
            if (semanticIndex != nullptr) {
                for (const LocalModuleEdge &edge : semanticIndex->graph.edges) {
                    if (edge.importerIdentity != semanticImporterIdentity ||
                        !edge.importSpec.reExport) {
                        continue;
                    }
                    if (selectiveGraph) {
                        const auto dependency = state.moduleFunctionExports.find(edge.importedIdentity);
                        if (dependency == state.moduleFunctionExports.end()) continue;
                        for (const auto &exported : dependency->second) {
                            if (!matchesImportSelection(edge.importSpec, exported.name)) continue;
                            moduleExports.push_back({importedSymbolName(edge.importSpec, exported.name),
                                                     exported.functionId});
                        }
                    } else {
                        const auto *dependency = semanticIndex->module(edge.importedIdentity);
                        if (dependency == nullptr) continue;
                        for (const auto &exported : dependency->exports) {
                            if (exported.kind != SemanticSymbolKind::Function) continue;
                            const std::string visibleName = importedSymbolName(edge.importSpec, exported.name);
                            const int nameIndex = state.findString(visibleName);
                            if (nameIndex < 0) continue;
                            for (const auto &entry : state.functionNameIndices) {
                                if (entry.second == nameIndex) {
                                    moduleExports.push_back({visibleName, entry.first});
                                    break;
                                }
                            }
                        }
                    }
                }
            }

            std::unordered_set<std::string> seenModuleExports;
            std::vector<CompiledModuleFunctionExport> uniqueModuleExports;
            uniqueModuleExports.reserve(moduleExports.size());
            for (auto &exported : moduleExports) {
                if (exported.functionId >= 0 &&
                    seenModuleExports.insert(exported.name).second) {
                    uniqueModuleExports.push_back(std::move(exported));
                }
            }
            state.moduleFunctionExports[canonical] = std::move(uniqueModuleExports);

            if (selectiveGraph || !spec.alias.empty()) {
                // Runtime names are module-private, independent of the first
                // selection/alias used to load this module. Public bindings live
                // in importedFunctionAliases and can share the same function ID.
                for (const auto &ins : moduleBytecode) {
                    if (ins.op != OP_HAM) continue;
                    const int oldNameIndex = ins.operand;
                    const int hamId = ins.operandIndex;
                    if (oldNameIndex < 0 ||
                        oldNameIndex >= static_cast<int>(state.stringCount())) {
                        continue;
                    }
                    const std::string &funcName = state.getString(oldNameIndex);
                    const std::string namespaced = selectiveGraph
                        ? "@module:" + canonical + ":" + funcName : spec.alias + "." + funcName;
                    const int newNameIndex = state.storeString(namespaced);
                    state.setFunctionNameIndex(hamId, newNameIndex);
                }
                if (!selectiveGraph) {
                    for (const auto &exported : state.moduleFunctionExports.at(canonical)) {
                        const int nameIndex = state.storeString(spec.alias + "." + exported.name);
                        state.setFunctionNameIndex(exported.functionId, nameIndex);
                    }
                }
            }

            registerImportedAliases();

            int maxHamId = -1;
            for (const auto &entry : state.functionBytecode) {
                if (entry.first > maxHamId) maxHamId = entry.first;
            }
            if (nextId <= maxHamId) nextId = maxHamId + 1;
        } catch (...) {
            importedFiles.erase(canonical);
            throw;
        }
    }

    // Biên dịch nhập spec; hàm đưa dữ liệu qua các pha compiler cần thiết và tạo artifact thực thi cho bước sau.
    void compileImportSpec(
        const vietvm::frontend::AstImportSpec &spec,
        int &nextId,
        const std::unordered_map<std::string,Opcode> &keywordMap) {
        compileImportSpec(activeCompilationRegistryState(), spec, nextId, keywordMap);
    }
} }

#include "compiler/compileRegistry.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common/storeString.h"
#include "vpp/compiler/pipeline.h"
#include "vpp/core/message_constants.h"
#include "vpp/core/project_layout.h"

namespace vietvm { namespace compiler {
    // Trả tập import của registry cụ thể để production path không phụ thuộc state thread-local.
    std::unordered_set<std::string> &importedFileSet(CompilationRegistryState &state) {
        return state.importedFiles;
    }

    // Trả tập đường dẫn file đã import trong compilation registry; compiler dùng tập này để ngăn import cùng source lặp lại.
    std::unordered_set<std::string> &importedFileSet() {
        return importedFileSet(activeCompilationRegistryState());
    }

    // Xóa import state của registry cụ thể.
    void clearImportedFiles(CompilationRegistryState &state) { state.importedFiles.clear(); }

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

        const int nameIndex = state.findString(resolvedName);
        if (nameIndex >= 0) {
            for (const auto &entry : state.functionNameIndices) {
                if (entry.second == nameIndex) return entry.first;
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
        const std::string &moduleAlias = spec.alias;
        namespace fs = std::filesystem;

        fs::path resolutionBase = state.importResolutionBase;
        if (resolutionBase.empty()) {
            resolutionBase = fs::current_path();
        }
        try {
            resolutionBase = fs::absolute(resolutionBase).lexically_normal();
        } catch (...) {
            resolutionBase = resolutionBase.lexically_normal();
        }

        auto absoluteLexical = [&](const fs::path &candidate) {
            fs::path resolved = candidate.is_absolute()
                                    ? candidate
                                    : resolutionBase / candidate;
            try {
                return fs::absolute(resolved).lexically_normal();
            } catch (...) {
                return resolved.lexically_normal();
            }
        };

        // Determine path
        std::string path = spec.target;

        // Package shortcuts for the bundled standard packages.
        if (path == "stdlib" || path == "chuẩn") {
            path = vietvm::core::kStandardPackageMainFile;
        }

        // A package name may be quoted when it contains spaces, for example:
        //
        //     nhập "lõi";
        //
        // Quoting must not turn that into a file-only import.  Treat a target
        // without a path component or extension as a package candidate whether
        // it was quoted or not, while continuing to resolve an actual file
        // before trying package directories.
        // `std::filesystem::path(const char*)` uses the active Windows code
        // page on MSVC. Import targets and bundled directory names are UTF-8,
        // so construct every such path explicitly as UTF-8. Otherwise imports
        // such as `nhập mạng;` fall back to `<project>/mạng.vi` on Windows.
        const fs::path requestedPath = vietvm::core::utf8Path(path);
        const bool bareModuleName = requestedPath.parent_path().empty() &&
                                    requestedPath.extension().empty();
        const std::string bareModule = path;

        // Keep the previous bare `vpp_*` imports working. Standard packages
        // live directly under `gói/<tên>`; `gói/chuẩn` is only the aggregate
        // entrypoint. Local project packages with the same name still take
        // precedence during lookup below.
        static const std::unordered_map<std::string, std::string> packageAliases = {
            {"vpp_core", "lõi"},
            {"cốt lõi", "lõi"},
            {"vpp_io", "nhập xuất"},
            {"vào ra", "nhập xuất"},
            {"vpp_http", "mạng"},
            {"vpp_web", "mạng"},
            {"mạng web", "mạng"},
            {"vpp_system", "hệ thống"},
            {"vpp_data", "dữ liệu"},
            {"vpp_app", "ứng dụng"},
            {"vpp_starters", "dựng"},
            {"khởi động", "dựng"},
        };
        static const std::unordered_set<std::string> bundledPackageNames = {
            "lõi",
            "nhập xuất",
            "mạng",
            "hệ thống",
            "dữ liệu",
            "ứng dụng",
            "dựng",
            "kiểm thử",
        };

        std::vector<std::string> packageCandidates;
        if (bareModuleName) {
            packageCandidates.push_back(bareModule);
            auto alias = packageAliases.find(bareModule);
            if (alias != packageAliases.end()) {
                packageCandidates.push_back(alias->second);
            }
        }

        // For unquoted targets, append .vi only when there is no extension.
        if (!spec.quoted) {
            fs::path rawPath = vietvm::core::utf8Path(path);
            if (rawPath.extension().empty()) {
                path += ".vi";
            }
        }

        fs::path p = vietvm::core::utf8Path(path);

        // Compatibility fallbacks for the former flat package layout and the
        // retired leaf shims. They are intentionally fallbacks so a project
        // that owns a real file at an old path keeps working unchanged.
        fs::path compatibilityPackageRedirect;
        {
            fs::path normalized = p.lexically_normal();
            auto root = normalized.begin();
            const std::string rootName = (root != normalized.end()) ? root->u8string() : "";
            if (root != normalized.end() &&
                vietvm::core::isPackageDirectoryName(rootName)) {
                fs::path relativePath;
                for (auto item = std::next(root); item != normalized.end(); ++item) {
                    relativePath /= *item;
                }

                static const std::unordered_map<std::string, std::string> compatibilityModuleRedirects = {
                    {"chuẩn/hỗ trợ/nhật ký.vi", "nhập xuất/nhật ký.vi"},
                    {"chuẩn/hỗ trợ/xác thực.vi", "lõi/xác thực.vi"},
                    {"ứng dụng/tương thích/api.vi", "ứng dụng/cầu nối/api.vi"},
                    {"chuẩn/ứng dụng/tương thích/api.vi", "ứng dụng/cầu nối/api.vi"},
                    {"khởi động/khởi động web.vi", "dựng/web.vi"},
                    {"khởi động/khởi động dữ liệu.vi", "dựng/dữ liệu.vi"},
                    {"khởi động/khởi động ứng dụng.vi", "dựng/ứng dụng.vi"},
                    {"dựng/khởi động web.vi", "dựng/web.vi"},
                    {"dựng/khởi động dữ liệu.vi", "dựng/dữ liệu.vi"},
                    {"dựng/khởi động ứng dụng.vi", "dựng/ứng dụng.vi"},
                    {"chuẩn/khởi động/khởi động web.vi", "dựng/web.vi"},
                    {"chuẩn/khởi động/khởi động dữ liệu.vi", "dựng/dữ liệu.vi"},
                    {"chuẩn/khởi động/khởi động ứng dụng.vi", "dựng/ứng dụng.vi"},
                };

                auto leafRedirect = compatibilityModuleRedirects.find(relativePath.generic_u8string());
                if (leafRedirect != compatibilityModuleRedirects.end()) {
                    compatibilityPackageRedirect = vietvm::core::utf8Path(vietvm::core::kPrimaryPackageDirectory) /
                                            vietvm::core::utf8Path(leafRedirect->second);
                } else {
                    auto package = relativePath.begin();
                    bool groupedStandardPackages = false;
                    if (package != relativePath.end()) {
                        const std::string groupName = package->u8string();
                        if (groupName == vietvm::core::kStandardPackageDirectory) {
                            groupedStandardPackages = true;
                            ++package;
                        }
                    }

                    if (groupedStandardPackages && package != relativePath.end() &&
                        package->u8string() == vietvm::core::kPackageEntryFile) {
                        compatibilityPackageRedirect =
                            vietvm::core::utf8Path(vietvm::core::kPrimaryPackageDirectory) /
                            vietvm::core::utf8Path(vietvm::core::kStandardPackageDirectory) /
                            vietvm::core::utf8Path(vietvm::core::kPackageEntryFile);
                        package = relativePath.end();
                    }

                    if (package != relativePath.end()) {
                        std::string canonicalPackage;
                        const std::string packageName = package->u8string();
                        auto alias = packageAliases.find(packageName);
                        if (alias != packageAliases.end()) {
                            canonicalPackage = alias->second;
                        } else if (bundledPackageNames.find(packageName) != bundledPackageNames.end()) {
                            canonicalPackage = packageName;
                        }

                        if (!canonicalPackage.empty()) {
                            compatibilityPackageRedirect = vietvm::core::utf8Path(vietvm::core::kPrimaryPackageDirectory) /
                                                    vietvm::core::utf8Path(canonicalPackage);
                            for (auto rest = std::next(package); rest != relativePath.end(); ++rest) {
                                compatibilityPackageRedirect /= *rest;
                            }
                        }
                    }
                }
            }
        }
        // Resolve relative imports against the compilation context rather than
        // the process working directory.
        fs::path abs = absoluteLexical(p);

        auto resolvePackageAtBase = [&](const fs::path &base, const std::string &packageName) {
            const fs::path packagePath = vietvm::core::utf8Path(packageName);
            fs::path packageMain = vietvm::core::packageEntryPath(base / packagePath);
            fs::path packageRoot = base / packagePath;
            fs::path packageSource = base / vietvm::core::utf8Path(packageName + ".vi");
            if (fs::exists(packageMain)) {
                abs = absoluteLexical(packageMain);
                return true;
            }
            if (fs::exists(packageRoot) && fs::is_regular_file(packageRoot)) {
                abs = absoluteLexical(packageRoot);
                return true;
            }
            if (fs::exists(packageSource)) {
                abs = absoluteLexical(packageSource);
                return true;
            }
            return false;
        };

        // If resolved path does not exist, attempt to locate the file by searching
        // upward from the compilation base and appending the requested path.
        // This keeps imports like "src/tests/..." working when the entry file lives
        // below the repository root without mutating or consulting process cwd.
        if (!fs::exists(abs)) {
            for (fs::path dir = resolutionBase; ; dir = dir.parent_path()) {
                fs::path cand = dir / p;
                if (fs::exists(cand)) {
                    abs = absoluteLexical(cand);
                    break;
                }
                if (!compatibilityPackageRedirect.empty()) {
                    fs::path redirected = dir / compatibilityPackageRedirect;
                    if (fs::exists(redirected)) {
                        abs = absoluteLexical(redirected);
                        break;
                    }
                }
                if (bareModuleName) {
                    for (const char *packageDirectory : vietvm::core::kPackageDirectoryNames) {
                        const fs::path base = dir / vietvm::core::utf8Path(packageDirectory);
                        for (const auto &packageName : packageCandidates) {
                            if (resolvePackageAtBase(base, packageName)) {
                                break;
                            }
                        }
                        if (fs::exists(abs)) {
                            break;
                        }
                    }
                    if (fs::exists(abs)) {
                        break;
                    }
                }
                if (dir == dir.parent_path()) break; // reached filesystem root
            }
        }

        // Installed releases keep the standard library beside the executable.
        // The installer exposes that location through VPP_HOME, so a project
        // outside the repository can import gói/chuẩn/... and bare bundled
        // module names.
        if (!fs::exists(abs)) {
            if (const char *vppHome = std::getenv(vietvm::core::kEnvVppHome)) {
                const fs::path vppHomePath =
                    absoluteLexical(vietvm::core::utf8Path(vppHome));
                fs::path bundled = vppHomePath / p;
                if (fs::exists(bundled)) {
                    abs = absoluteLexical(bundled);
                }
                if (!fs::exists(abs) && !compatibilityPackageRedirect.empty()) {
                    fs::path redirected = vppHomePath / compatibilityPackageRedirect;
                    if (fs::exists(redirected)) {
                        abs = absoluteLexical(redirected);
                    }
                }
                if (!fs::exists(abs) && bareModuleName) {
                    for (const char *packageDir : vietvm::core::kPackageDirectoryNames) {
                        for (const auto &packageName : packageCandidates) {
                            if (resolvePackageAtBase(vppHomePath / vietvm::core::utf8Path(packageDir), packageName)) {
                                break;
                            }
                        }
                        if (fs::exists(abs)) {
                            break;
                        }
                    }
                }
            }
        }

        std::string canonical = abs.u8string();

        auto &importedFiles = state.importedFiles;
        if (importedFiles.find(canonical) != importedFiles.end()) {
            return;
        }

        importedFiles.insert(canonical);

        try {
            std::ifstream ifs(abs);
            if (!ifs.is_open()) {
                throw std::runtime_error(vietvm::messages::formatMessage(
                    vietvm::messages::kImportCannotOpenFile, {canonical}));
            }
            std::stringstream ss;
            ss << ifs.rdbuf();
            std::string src = ss.str();

            // Import tương đối bên trong module phải resolve từ thư mục chứa chính
            // module đó. Khôi phục base của importer ngay sau recursive compile để
            // sibling import ở scope ngoài không bị đổi nghĩa.
            const fs::path previousResolutionBase = state.importResolutionBase;
            state.importResolutionBase = abs.parent_path();
            CompilationArtifacts moduleArtifacts;
            try {
                moduleArtifacts = compilePipelineInRegistry(
                    state, src, keywordMap, false, false);
            } catch (...) {
                state.importResolutionBase = previousResolutionBase;
                throw;
            }
            state.importResolutionBase = previousResolutionBase;
            auto moduleBytecode = moduleArtifacts.bytecode;
            state.moduleInitializers.push_back(
                CompiledModuleInitializer{canonical, moduleBytecode});

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

            if (moduleArtifacts.moduleIndex.has_value()) {
                for (const LocalModuleEdge &edge : moduleArtifacts.moduleIndex->graph.edges) {
                    if (edge.importerIdentity != kCurrentCompilationModuleIdentity ||
                        !edge.importSpec.reExport) {
                        continue;
                    }
                    const LocalModuleSemanticRecord *dependency =
                        moduleArtifacts.moduleIndex->module(edge.importedIdentity);
                    if (dependency == nullptr) continue;
                    for (const ModuleExportSymbol &exported : dependency->exports) {
                        if (exported.kind != SemanticSymbolKind::Function) continue;
                        const std::string visibleName = edge.importSpec.alias.empty()
                            ? exported.name
                            : edge.importSpec.alias + "." + exported.name;
                        const int visibleNameIndex = state.findString(visibleName);
                        if (visibleNameIndex < 0) continue;
                        for (const auto &entry : state.functionNameIndices) {
                            if (entry.second == visibleNameIndex) {
                                moduleExports.push_back({visibleName, entry.first});
                                break;
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

            if (!moduleAlias.empty()) {
                // Namespace mọi function khai báo trực tiếp, kể cả private, để chúng
                // không rò thành callable không-qualified ở module nhập.
                for (const auto &ins : moduleBytecode) {
                    if (ins.op != OP_HAM) continue;
                    const int oldNameIndex = ins.operand;
                    const int hamId = ins.operandIndex;
                    if (oldNameIndex < 0 ||
                        oldNameIndex >= static_cast<int>(state.stringCount())) {
                        continue;
                    }
                    const std::string &funcName = state.getString(oldNameIndex);
                    const std::string namespaced = moduleAlias + "." + funcName;
                    const int newNameIndex = state.storeString(namespaced);
                    state.setFunctionNameIndex(hamId, newNameIndex);
                }

                // Re-export không có OP_HAM trong bytecode của module trung gian. Dùng
                // export metadata để prefix chúng qua alias ngoài cùng, ví dụ
                // `api.toán.nhân` từ `api` -> `công khai nhập math như toán`.
                const auto exportedSurface = state.moduleFunctionExports.find(canonical);
                if (exportedSurface != state.moduleFunctionExports.end()) {
                    for (const auto &exported : exportedSurface->second) {
                        const int newNameIndex = state.storeString(
                            moduleAlias + "." + exported.name);
                        state.setFunctionNameIndex(exported.functionId, newNameIndex);
                    }
                }
            }

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

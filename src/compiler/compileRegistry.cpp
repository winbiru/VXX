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
    std::unordered_set<std::string> importedFiles;
    void clearImportedFiles() { importedFiles.clear(); }

    struct MethodAccessInfo {
        std::string ownerClass;
        std::string visibility;
    };

    static std::unordered_map<std::string, MethodAccessInfo> g_methodAccess;
    static std::vector<std::string> g_classContextStack;

    void clearClassAccessState() {
        g_methodAccess.clear();
        g_classContextStack.clear();
    }

    void pushClassContext(const std::string &className) {
        g_classContextStack.push_back(className);
    }

    void popClassContext() {
        if (!g_classContextStack.empty()) g_classContextStack.pop_back();
    }

    std::string currentClassContext() {
        if (g_classContextStack.empty()) return "";
        return g_classContextStack.back();
    }

    void registerClassMethodVisibility(const std::string &fullMethodName,
                                       const std::string &ownerClass,
                                       const std::string &visibility) {
        g_methodAccess[fullMethodName] = MethodAccessInfo{ownerClass, visibility};
    }

    std::string resolveCallableNameInContext(const std::string &name,
                                             const std::unordered_map<std::string,int> &symTab) {
        if (name.find('.') != std::string::npos) return name;

        std::string cls = currentClassContext();
        if (cls.empty()) return name;

        std::string scopedName = cls + "." + name;
        if (symTab.find(scopedName) != symTab.end()) return scopedName;

        int scopedNameIndex = vietvm::compiler::StringPool::findString(scopedName);
        if (scopedNameIndex >= 0) return scopedName;

        return name;
    }

    void validateCallableAccess(const std::string &resolvedName) {
        auto it = g_methodAccess.find(resolvedName);
        if (it == g_methodAccess.end()) return;

        const std::string &owner = it->second.ownerClass;
        const std::string &visibility = it->second.visibility;
        const std::string currentClass = currentClassContext();

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

    int resolveFunctionIdByName(const std::string &name,
                                const std::unordered_map<std::string,int> &symTab,
                                bool includeGlobalFallback) {
        std::string resolvedName = resolveCallableNameInContext(name, symTab);
        validateCallableAccess(resolvedName);

        auto idMatchesResolvedName = [&](int candidateId) {
            int resolvedNameIndex = StringPool::findString(resolvedName);
            if (resolvedNameIndex < 0) return false;
            auto itName = hamMap::hamNameIndexMap.find(candidateId);
            if (itName == hamMap::hamNameIndexMap.end()) return false;
            return itName->second == resolvedNameIndex;
        };

        auto itSym = symTab.find(resolvedName);
        if (itSym != symTab.end()) {
            int maybeId = itSym->second;
            auto itCode = hamMap::hamBytecodeMap.find(maybeId);
            if (itCode != hamMap::hamBytecodeMap.end() &&
                !itCode->second.empty() &&
                idMatchesResolvedName(maybeId)) {
                return maybeId;
            }
        }

        if (!includeGlobalFallback) return -1;

        int nameIndex = StringPool::findString(resolvedName);
        if (nameIndex >= 0) {
            for (const auto &kv : hamMap::hamNameIndexMap) {
                if (kv.second == nameIndex) return kv.first;
            }
        }
        return -1;
    }

    void compileImportSpec(
        const vietvm::frontend::AstImportSpec &spec,
        int &nextId,
        const std::unordered_map<std::string,Opcode> &keywordMap) {
        if (spec.target.empty()) {
            throw std::runtime_error(vietvm::messages::formatMessage(
                vietvm::messages::kImportMissingTarget));
        }
        const std::string &moduleAlias = spec.alias;
        namespace fs = std::filesystem;

        // Determine path
        std::string path = spec.target;

        // Package shortcuts for the bundled standard library.
        if (path == "thư viện chuẩn" || path == "thu_vien_chuan" || path == "stdlib") {
            path = vietvm::core::kBundledLibraryMainFile;
        }
        if (path == vietvm::core::kBundledLibraryDirectory || path == "thu_vien") {
            path = vietvm::core::kBundledLibraryMainFile;
        }

        // A package name may be quoted when it contains spaces, for example:
        //
        //     nhập "cốt lõi";
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

        // Keep the previous bare `vpp_*` imports working. Bundled modules now
        // live under the single `gói/thư viện` package; local project packages
        // with the same name still take precedence during lookup below.
        static const std::unordered_map<std::string, std::string> packageAliases = {
            {"vpp_core", "cốt lõi"},
            {"vpp_io", "vào ra"},
            {"vpp_http", "mạng"},
            {"vpp_web", "mạng web"},
            {"vpp_data", "dữ liệu"},
            {"vpp_app", "ứng dụng"},
            {"vpp_starters", "khởi động"},
        };
        static const std::unordered_set<std::string> bundledPackageNames = {
            "cốt lõi",
            "vào ra",
            "mạng",
            "mạng web",
            "dữ liệu",
            "ứng dụng",
            "khởi động",
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
                    {"thư viện/cấu hình/cấu hình.vi", "thư viện/vào ra/cấu hình.vi"},
                    {"thư viện/hỗ trợ/nhật ký.vi", "thư viện/vào ra/nhật ký.vi"},
                    {"thư viện/hỗ trợ/xác thực.vi", "thư viện/cốt lõi/xác thực.vi"},
                    {"thư viện/thời gian/đồng hồ.vi", "thư viện/vào ra/đồng hồ.vi"},
                    {"thư viện/mạng/rest.vi", "thư viện/mạng web/rest.vi"},
                    {"thư viện/mạng/api.vi", "thư viện/mạng web/kiểm thử/api.vi"},
                    {"thư viện/ứng dụng/ứng dụng máy chủ.vi", "thư viện/ứng dụng/tương thích/api project.vi"},
                };

                auto leafRedirect = compatibilityModuleRedirects.find(relativePath.generic_u8string());
                if (leafRedirect != compatibilityModuleRedirects.end()) {
                    compatibilityPackageRedirect = vietvm::core::utf8Path(vietvm::core::kPrimaryPackageDirectory) /
                                            vietvm::core::utf8Path(leafRedirect->second);
                } else {
                    auto package = relativePath.begin();
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
                                                    vietvm::core::utf8Path(vietvm::core::kBundledLibraryDirectory) /
                                                    vietvm::core::utf8Path(canonicalPackage);
                            for (auto rest = std::next(package); rest != relativePath.end(); ++rest) {
                                compatibilityPackageRedirect /= *rest;
                            }
                        }
                    }
                }
            }
        }
        // Make absolute and normalized path (if possible)
        fs::path abs;
        try {
            abs = fs::absolute(p).lexically_normal();
        } catch (...) {
            abs = p;
        }

        auto resolvePackageAtBase = [&](const fs::path &base, const std::string &packageName) {
            const fs::path packagePath = vietvm::core::utf8Path(packageName);
            fs::path packageMain = vietvm::core::packageEntryPath(base / packagePath);
            fs::path packageRoot = base / packagePath;
            fs::path packageSource = base / vietvm::core::utf8Path(packageName + ".vi");
            if (fs::exists(packageMain)) {
                abs = fs::absolute(packageMain).lexically_normal();
                return true;
            }
            if (fs::exists(packageRoot) && fs::is_regular_file(packageRoot)) {
                abs = fs::absolute(packageRoot).lexically_normal();
                return true;
            }
            if (fs::exists(packageSource)) {
                abs = fs::absolute(packageSource).lexically_normal();
                return true;
            }
            if (bundledPackageNames.find(packageName) != bundledPackageNames.end()) {
                fs::path bundledMain = vietvm::core::packageEntryPath(
                    base / vietvm::core::utf8Path(vietvm::core::kBundledLibraryDirectory) / packagePath);
                if (fs::exists(bundledMain)) {
                    abs = fs::absolute(bundledMain).lexically_normal();
                    return true;
                }
            }
            return false;
        };

        // If resolved path does not exist, attempt to locate the file by searching
        // upward from the current working directory and appending the requested path.
        // This helps with imports like "src/tests/..." when the process cwd is build/bin.
        if (!fs::exists(abs)) {
            for (fs::path dir = fs::current_path(); ; dir = dir.parent_path()) {
                fs::path cand = dir / p;
                if (fs::exists(cand)) {
                    abs = fs::absolute(cand).lexically_normal();
                    break;
                }
                if (!compatibilityPackageRedirect.empty()) {
                    fs::path redirected = dir / compatibilityPackageRedirect;
                    if (fs::exists(redirected)) {
                        abs = fs::absolute(redirected).lexically_normal();
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
        // outside the repository can import gói/thư viện/... and bare bundled
        // module names.
        if (!fs::exists(abs)) {
            if (const char *vppHome = std::getenv(vietvm::core::kEnvVppHome)) {
                const fs::path vppHomePath = vietvm::core::utf8Path(vppHome);
                fs::path bundled = vppHomePath / p;
                if (fs::exists(bundled)) {
                    abs = fs::absolute(bundled).lexically_normal();
                }
                if (!fs::exists(abs) && !compatibilityPackageRedirect.empty()) {
                    fs::path redirected = vppHomePath / compatibilityPackageRedirect;
                    if (fs::exists(redirected)) {
                        abs = fs::absolute(redirected).lexically_normal();
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

        if (vietvm::compiler::importedFiles.find(canonical) != vietvm::compiler::importedFiles.end()) {
            // already imported in this compile session — no-op
            return;
        }

        // Mark as in-progress before reading/compiling to prevent circular imports
        vietvm::compiler::importedFiles.insert(canonical);

        try {
            // read file
            std::ifstream ifs(abs);
            if (!ifs.is_open()) {
                throw std::runtime_error(vietvm::messages::formatMessage(
                    vietvm::messages::kImportCannotOpenFile, {canonical}));
            }
            std::stringstream ss;
            ss << ifs.rdbuf();
            std::string src = ss.str();

            // Compile the module for its registration side effects only.
            // Imported functions/strings are recorded in the global registries;
            // the returned module bytecode is not merged or executed here.
            auto moduleBytecode = compilePipeline(src, keywordMap, false).bytecode;

            // Namespace alias: register ns.funcName -> same function id
            if (!moduleAlias.empty()) {
                for (const auto &ins : moduleBytecode) {
                    if (ins.op != OP_HAM) continue;
                    int oldNameIndex = ins.operand;
                    int hamId = ins.operandIndex;
                    if (oldNameIndex < 0 || oldNameIndex >= (int)vietvm::compiler::StringPool::size()) continue;
                    const std::string &funcName = vietvm::compiler::StringPool::getString(oldNameIndex);
                    std::string namespaced = moduleAlias + "." + funcName;
                    int newNameIndex = vietvm::compiler::StringPool::storeString(namespaced);
                    vietvm::compiler::hamMap::setHamNameIndex(hamId, newNameIndex);
                }
            }

            // Keep variable IDs in caller module disjoint from imported function IDs.
            int maxHamId = -1;
            for (const auto &kv : vietvm::compiler::hamMap::hamBytecodeMap) {
                if (kv.first > maxHamId) maxHamId = kv.first;
            }
            if (nextId <= maxHamId) nextId = maxHamId + 1;
        } catch (...) {
            // Rollback on failure
            vietvm::compiler::importedFiles.erase(canonical);
            throw;
        }
    }
} }

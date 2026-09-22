#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "vpp/compiler/module_graph.h"
#include "vpp/compiler/package_resolver.h"
#include "vpp/core/project_layout.h"

namespace {

namespace fs = std::filesystem;
int failures = 0;

void expect(bool condition, const std::string &message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void writeFile(const fs::path &path, const std::string &text) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << text;
    if (!output) throw std::runtime_error("không thể tạo fixture: " + path.string());
}

vietvm::frontend::AstImportSpec importSpec(std::string target) {
    vietvm::frontend::AstImportSpec spec;
    spec.target = std::move(target);
    spec.hasSemicolon = true;
    return spec;
}

fs::path absoluteNormalized(const fs::path &path) {
    return fs::absolute(path).lexically_normal();
}

fs::path utf8(const char *value) {
    return vietvm::core::utf8Path(value);
}

struct TempTree {
    fs::path root;
    fs::path oldCwd;

    TempTree() : oldCwd(fs::current_path()) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root = fs::temp_directory_path() /
               ("vpp-import-precedence-" + std::to_string(stamp));
        fs::create_directories(root);
    }

    ~TempTree() {
        std::error_code ignored;
        fs::current_path(oldCwd, ignored);
        fs::remove_all(root, ignored);
    }
};

void testResolverPrecedence() {
    TempTree tree;
    const fs::path project = tree.root / utf8(u8"dự án");
    const fs::path base = project / "src" / utf8(u8"sâu");
    fs::create_directories(base);

    const fs::path ancestorLocal = project / "collision.vi";
    const fs::path nearPackage =
        base / utf8(vietvm::core::kPrimaryPackageDirectory) / "collision" / "main.vi";
    writeFile(ancestorLocal, "hàm local() { trả về 1; }\n");
    writeFile(nearPackage, "hàm package() { trả về 2; }\n");

    vietvm::compiler::PackageResolver packageResolver(base);
    vietvm::compiler::LocalModuleResolver localResolver(base);
    const auto packageCollision = packageResolver.resolve("collision");
    const auto localCollision = localResolver.resolve(importSpec("collision"));
    expect(packageCollision.path == absoluteNormalized(ancestorLocal),
           "local file ở ancestor xa thắng package gần");
    expect(localCollision.path == packageCollision.path,
           "semantic resolver và package/codegen resolver chọn cùng collision target");

    const fs::path nearestPackage =
        base / utf8(vietvm::core::kPrimaryPackageDirectory) / "nearest" / "main.vi";
    const fs::path fartherPackage =
        project / utf8(vietvm::core::kPrimaryPackageDirectory) / "nearest" / "main.vi";
    writeFile(nearestPackage, "hàm gần() { trả về 1; }\n");
    writeFile(fartherPackage, "hàm xa() { trả về 2; }\n");
    expect(packageResolver.resolve("nearest").path == absoluteNormalized(nearestPackage),
           "package gần nhất thắng package cùng tên ở ancestor xa");

    const fs::path aliasTarget =
        base / utf8(vietvm::core::kPrimaryPackageDirectory) / utf8(u8"lõi") / "main.vi";
    writeFile(aliasTarget, "hàm lõi() { trả về 1; }\n");
    expect(packageResolver.resolve("vpp_core").path == absoluteNormalized(aliasTarget),
           "alias vpp_core resolve sang package lõi");

    const fs::path compatibilityTarget =
        project / utf8(vietvm::core::kPrimaryPackageDirectory) /
        utf8(u8"ứng dụng") / utf8(u8"cầu nối") / "api.vi";
    writeFile(compatibilityTarget, "hàm api() { trả về 1; }\n");
    expect(packageResolver.resolve("gói/ứng dụng/tương thích/api").path ==
               absoluteNormalized(compatibilityTarget),
           "compatibility redirect chạy sau local lookup");

    const fs::path installHome = tree.root / "vpp-home";
    const fs::path installedPackage =
        installHome / utf8(vietvm::core::kPrimaryPackageDirectory) /
        utf8(u8"hệ thống") / "main.vi";
    writeFile(installedPackage, "hàm hệ_thống() { trả về 1; }\n");
    vietvm::compiler::PackageResolver installedResolver(base, installHome);
    expect(installedResolver.resolve("vpp_system").path == absoluteNormalized(installedPackage),
           "VPP_HOME chỉ được dùng sau project lookup/alias");

    const fs::path unrelated = tree.root / utf8(u8"cwd-khác");
    fs::create_directories(unrelated);
    fs::current_path(unrelated);
    vietvm::compiler::PackageResolver cwdIndependent(base);
    expect(cwdIndependent.resolve("collision").path == absoluteNormalized(ancestorLocal),
           "explicit resolution base không phụ thuộc process cwd");
}

void testNestedImportUsesImporterDirectory() {
    TempTree tree;
    const fs::path project = tree.root / "project";
    const fs::path base = project / "src";
    const fs::path packageMain =
        project / utf8(vietvm::core::kPrimaryPackageDirectory) / "demo" / "main.vi";
    const fs::path nested =
        project / utf8(vietvm::core::kPrimaryPackageDirectory) / "demo" /
        utf8(u8"phụ.vi");
    fs::create_directories(base);
    writeFile(packageMain,
              "nhập phụ;\n"
              "hàm từ_demo() { trả về từ_phụ(); }\n");
    writeFile(nested, "hàm từ_phụ() { trả về 7; }\n");

    const auto index = vietvm::compiler::buildLocalModuleSemanticIndex(
        vietvm::compiler::LocalModuleResolver(base),
        "entry://test",
        {importSpec("demo")},
        vietvm::compiler::ModuleIndexMode::Recursive);

    bool sawMain = false;
    bool sawNested = false;
    for (const auto &module : index.graph.modules) {
        sawMain = sawMain || module.path == absoluteNormalized(packageMain);
        sawNested = sawNested || module.path == absoluteNormalized(nested);
    }
    expect(sawMain, "semantic graph nạp package main theo PackageResolver");
    expect(sawNested, "nested import resolve từ thư mục importer/package");
    expect(index.graph.modules.size() == 2,
           "nested graph không nạp module ngoài expected collision matrix");
}

} // namespace

int main() {
    try {
        testResolverPrecedence();
        testNestedImportUsesImporterDirectory();
    } catch (const std::exception &error) {
        std::cerr << "FAIL: exception: " << error.what() << '\n';
        ++failures;
    }

    if (failures != 0) {
        std::cerr << failures << " import precedence hardening check(s) failed\n";
        return 1;
    }
    std::cout << "Import precedence hardening: PASS\n";
    return 0;
}

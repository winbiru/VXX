#include <chrono>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "vpp/compiler/module_graph.h"
#include "compiler/compileRegistry.h"
#include "frontend/keywords.h"
#include "frontend/lexer.h"
#include "vpp/compiler/pipeline.h"
#ifdef VPP_IMPORT_RUNTIME_CHECKS
#include "vpp/runtime/vm.h"
#endif
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
        base / utf8(vietvm::core::kPrimaryPackageDirectory) / "collision" / utf8(u8"chính.vi");
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
        base / utf8(vietvm::core::kPrimaryPackageDirectory) / "nearest" / utf8(u8"chính.vi");
    const fs::path fartherPackage =
        project / utf8(vietvm::core::kPrimaryPackageDirectory) / "nearest" / utf8(u8"chính.vi");
    writeFile(nearestPackage, "hàm gần() { trả về 1; }\n");
    writeFile(fartherPackage, "hàm xa() { trả về 2; }\n");
    expect(packageResolver.resolve("nearest").path == absoluteNormalized(nearestPackage),
           "package gần nhất thắng package cùng tên ở ancestor xa");

    const fs::path aliasTarget =
        base / utf8(vietvm::core::kPrimaryPackageDirectory) / utf8(u8"lõi") / utf8(u8"chính.vi");
    writeFile(aliasTarget, "hàm lõi() { trả về 1; }\n");
    expect(packageResolver.resolve("vpp_core").path == absoluteNormalized(aliasTarget),
           "alias vpp_core resolve sang package lõi");

    const fs::path compatibilityTarget =
        project / utf8(vietvm::core::kPrimaryPackageDirectory) /
        utf8(u8"ứng dụng") / utf8(u8"cầu nối") /
        utf8(u8"giao diện lập trình.vi");
    writeFile(compatibilityTarget, "hàm api() { trả về 1; }\n");
    expect(packageResolver.resolve("gói/ứng dụng/tương thích/api").path ==
               absoluteNormalized(compatibilityTarget),
           "compatibility redirect chạy sau local lookup");

    const fs::path installHome = tree.root / "vpp-home";
    const fs::path installedPackage =
        installHome / utf8(vietvm::core::kPrimaryPackageDirectory) /
        utf8(u8"hệ thống") / utf8(u8"chính.vi");
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
        project / utf8(vietvm::core::kPrimaryPackageDirectory) / "demo" / utf8(u8"chính.vi");
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

vietvm::frontend::AstProgram parse(const std::string &source) {
    return vietvm::frontend::parseTokens(
        vietvm::compiler::postProcessTokensWithSpans(vietvm::compiler::tokenizeWithSpans(source)));
}

void testImportSelectionSyntax() {
    using namespace vietvm::frontend;
    const auto program = parse(
        "nhập path.to.module.căn bậc hai như gốc vuông;\n"
        "nhập path.to.module.* như toán;\n"
        "nhập ..path.to.module.*;\n"
        "công khai nhập path.to.module.Hộp như Thùng;\n"
        "nhập module.name.*;\n"
        "nhập .folder.level.module.dùng;\n"
        "nhập ..repository.module.*;\n"
        "nhập ...folder.module.*;\n"
        "nhập path/to/module.*;\n"); // Keep one explicit legacy-compatibility regression.
    expect(program.statements.size() == 9, "parse đủ chín dạng import");
    const std::vector<std::string> selections = {"căn bậc hai", "*", "*", "Hộp", "*", "dùng", "*", "*", "*"};
    const std::vector<std::string> paths = {"path/to/module", "path/to/module", "../path/to/module", "path/to/module",
                                            "module/name", "./folder/level/module", "../repository/module", "../../folder/module",
                                            "path/to/module"};
    for (std::size_t i = 0; i < program.statements.size(); ++i) {
        const auto &statement = program.statements[i];
        expect(statement.importForm == AstImportForm::LocalSourceFile &&
                   statement.importSpec.selectedName == selections[i],
               "AST giữ selectedName và import có cấu trúc");
        expect(statement.importSpec.target == paths[i],
               "target chỉ chứa đường dẫn mô-đun");
    }
    expect(program.statements[0].importSpec.alias == "gốc vuông" &&
               program.statements[3].importSpec.reExport,
           "giữ bí danh nhiều từ và cờ tái xuất");
    for (const std::string target : {
             "module.vi", "module.vi.*", "module.vi.name", "module.",
             ".name", "module*", "module*.*", "module.*name", "module.name*",
             "module.* name", "module.*.name", "*/module",
             "module.* /other"}) {
        bool rejected = false;
        try { (void)parse("nhập " + target + ";"); }
        catch (const ParseError &) { rejected = true; }
        expect(rejected, "từ chối import sai: " + target);
    }
}

void writeSelectionModule(const fs::path &root) {
    writeFile(root / "math.vi",
        "hàm công khai căn bậc hai(x) { trả về trợ giúp(x); };\n"
        "hàm riêng tư trợ giúp(x) { y = x + 1; trả về y; };\n"
        "hàm công khai khác() { trả về 9; };\n"
        "giao diện công khai Có Giá Trị { hàm đọc(); };\n"
        "lớp công khai Hộp triển khai Có Giá Trị { hàm đọc() { trả về 4; }; };\n"
        "lớp riêng tư Bí Mật { };\n");
}

bool hasExternal(const vietvm::compiler::SemanticEnvironment &environment,
                 const std::string &name) {
    return std::any_of(environment.importedSymbols.begin(), environment.importedSymbols.end(),
        [&](const auto &symbol) { return symbol.name == name; });
}

bool hasCall(const std::vector<Instruction> &code, int functionId) {
    return std::any_of(code.begin(), code.end(), [&](const auto &instruction) {
        return instruction.op == OP_GOI && instruction.operandIndex == functionId;
    });
}

// Preserve transient aliases so these checks can inspect the binding table too.
vietvm::compiler::CompilationArtifacts compileWithBindings(
    vietvm::compiler::CompilationContext &context, const std::string &source,
    const std::unordered_map<std::string, Opcode> &keywords, bool emitEntryPoint) {
    context.clear();
    return vietvm::compiler::compilePipelineInRegistry(
        context, source, keywords, emitEntryPoint, true);
}

void checkRuntime(vietvm::compiler::CompilationContext &context,
                  const vietvm::compiler::CompilationArtifacts &artifacts,
                  const std::string &expected) {
#ifdef VPP_IMPORT_RUNTIME_CHECKS
    VM vm(artifacts.bytecode, context.stringPool);
    std::string output;
    vm.setOutputSink([&](const std::string &text) { output += text; });
    std::unordered_map<int, int> names;
    for (const auto &entry : context.functionNameIndices) names[entry.second] = entry.first;
    vm.setFunctions(context.functionBytecode, std::move(names));
    for (const auto &module : context.moduleInitializers)
        (void)vm.addModuleInitializer(module.identity, module.bytecode, module.debugInfo);
    vm.run();
    expect(output == expected, "runtime output: " + output + " expected: " + expected);
#else
    (void)context;
    (void)artifacts;
    (void)expected;
#endif
}

void testSelectedImportPipeline() {
    using namespace vietvm::compiler;
    TempTree tree;
    writeSelectionModule(tree.root);
    CompilationContext context;
    context.importResolutionBase = tree.root;
    const auto artifacts = compileWithBindings(context,
        "nhập math.căn bậc hai như tăng;\n"
        "nhập math.khác như thứ hai;\n"
        "nhập math.căn bậc hai như bản sao;\n"
        "nhập math.Hộp như Thùng;\n"
        "nhập math.Có Giá Trị như Hợp Đồng;\n"
        "in tăng(2); gọi thứ hai(); gọi bản sao(3);\n"
        "f = tăng; in f(4); vật = Thùng(); in vật.đọc();\n",
        keywordMap, false);
    expect(artifacts.moduleIndex.has_value(), "pipeline giữ module index");
    const auto &index = *artifacts.moduleIndex;
    const auto environment = index.semanticEnvironmentFor(kCurrentCompilationModuleIdentity);
    expect(index.graph.modules.size() == 1 && index.graph.edges.size() == 5 &&
               context.moduleInitializers.size() == 1,
           "năm selections giữ năm cạnh nhưng chỉ nạp/khởi tạo một lần");
    expect(index.graph.edges[1].action == LocalModuleEdgeAction::DuplicateNoOp,
           "cạnh trùng vẫn mang selection để bind tiếp");
    expect(hasExternal(environment, "tăng") && hasExternal(environment, "thứ hai") &&
               hasExternal(environment, "Thùng") && hasExternal(environment, "Hợp Đồng") &&
               !hasExternal(environment, "căn bậc hai") && !hasExternal(environment, "khác") &&
               !hasExternal(environment, "Hộp") && !hasExternal(environment, "Có Giá Trị"),
           "chỉ tên được chọn/đổi mới có visibility trong semantic");
    expect(context.importedFunctionAliases.size() == 3 &&
               context.importedFunctionAliases.at("tăng") ==
                   context.importedFunctionAliases.at("bản sao"),
           "nhiều bí danh chia sẻ function ID, type không thành function alias");
    expect(hasCall(artifacts.bytecode, context.importedFunctionAliases.at("tăng")) &&
               hasCall(artifacts.bytecode, context.importedFunctionAliases.at("thứ hai")),
           "ordinary và gọi tường minh phát function ID đã bind");
    for (const auto &instruction : artifacts.bytecode) {
        if (instruction.op == OP_TAO_DOI_TUONG) {
            expect(context.getString(instruction.operandIndex) == "Hộp",
                   "type alias tạo instance của runtime class gốc");
        }
    }
    expect(artifacts.ir.instructions.front().importSpec.selectedName == "căn bậc hai",
           "lowering IR giữ nguyên selectedName");
    expect(resolveFunctionIdByName(context, "khác", {}) == -1 &&
               resolveFunctionIdByName(context, "trợ giúp", {}) == -1,
           "global registry fallback không lộ public chưa chọn hoặc private");
    const int selectedId = context.importedFunctionAliases.at("tăng");
    bool helperLinked = false;
    for (const auto &instruction : context.functionBytecode.at(selectedId)) {
        helperLinked = helperLinked || (instruction.op == OP_GOI && instruction.operandIndex >= 0);
    }
    expect(helperLinked && context.functionBytecode.size() == 4,
           "dependency private và các hàm được chọn vẫn được link");
    checkRuntime(context, artifacts, "3\n5\n4\n");

    const auto mixed = compileWithBindings(context,
        "nhập math.căn bậc hai như picked; nhập math.* như tất cả; nhập math;\n"
        "in picked(1); gọi tất cả.khác(); in khác();\n", keywordMap, false);
    expect(context.moduleInitializers.size() == 1 &&
               context.importedFunctionAliases.at("picked") ==
                   context.importedFunctionAliases.at("tất cả.căn bậc hai") &&
               context.importedFunctionAliases.at("picked") ==
                   context.importedFunctionAliases.at("căn bậc hai") &&
               hasCall(mixed.bytecode, context.importedFunctionAliases.at("tất cả.khác")),
           "selection rồi wildcard rồi bare mở rộng binding mà không load lại");
    checkRuntime(context, mixed, "2\n9\n");
}

void testSelectionDiagnosticsAndReExports() {
    using namespace vietvm::compiler;
    TempTree tree;
    writeSelectionModule(tree.root);
    writeFile(tree.root / "facade.vi",
        "công khai nhập math.căn bậc hai như tăng;\n"
        "nhập math.khác như nội bộ;\n"
        "công khai nhập math.Hộp như Thùng;\n");
    writeFile(tree.root / "outer.vi", "công khai nhập facade.* như api;\n");
    writeFile(tree.root / "direct.vi", "công khai nhập facade.tăng như forwarded;\n");
    CompilationContext context;
    context.importResolutionBase = tree.root;
    const auto artifacts = compileWithBindings(context,
        "nhập facade.tăng như đầu; nhập outer.* như ngoài; nhập direct.*;\n"
        "in đầu(1); gọi ngoài.api.tăng(2); in forwarded(3); vật = ngoài.api.Thùng();\n",
        keywordMap, false);
    expect(context.importedFunctionAliases.size() == 3 &&
               context.importedFunctionAliases.at("đầu") ==
                   context.importedFunctionAliases.at("ngoài.api.tăng") &&
               context.importedFunctionAliases.at("đầu") ==
                   context.importedFunctionAliases.at("forwarded") &&
               context.importedFunctionAliases.count("nội bộ") == 0 &&
               context.importedFunctionAliases.count("trợ giúp") == 0,
           "selected re-export và namespace lồng nhau link cùng ID, không rò import nội bộ");
    expect(hasCall(artifacts.bytecode, context.importedFunctionAliases.at("ngoài.api.tăng")),
           "gọi re-export qua wildcard alias được link ở codegen");
    checkRuntime(context, artifacts, "2\n4\n");

    for (const auto &sourceAndDiagnostic : std::vector<std::pair<std::string, std::string>>{
             {"nhập math.không tồn tại;", "không tìm thấy"},
             {"nhập math.trợ giúp;", "không được"},
             {"nhập math.Bí Mật;", "không được"},
             {"nhập math.căn bậc hai; in khác();", "không được"},
             {"nhập math.căn bậc hai như picked; in căn bậc hai(1);", "không được"},
             {"nhập math.Hộp; in khác();", "không được"},
             {"nhập math.căn bậc hai; vật = Hộp();", "không được"},
             {"nhập facade.nội bộ;", "không tìm thấy"}}) {
        bool rejected = false;
        try { (void)compileWithBindings(context, sourceAndDiagnostic.first, keywordMap, false); }
        catch (const std::exception &error) {
            rejected = std::string(error.what()).find(sourceAndDiagnostic.second) != std::string::npos;
        }
        expect(rejected, "diagnostic selection/visibility: " + sourceAndDiagnostic.first);
    }
    writeFile(tree.root / "bad.vi", "công khai nhập math.thiếu;\n");
    bool nestedRejected = false;
    try { (void)compileWithBindings(context, "nhập bad.*;", keywordMap, false); }
    catch (const std::exception &error) {
        nestedRejected = std::string(error.what()).find("thiếu") != std::string::npos;
    }
    expect(nestedRejected, "kiểm tra selection thiếu ở nested re-export dù không gọi");
}

void testSelectiveRetentionBeforeIr() {
    using namespace vietvm::compiler;
    TempTree tree;
    std::string source =
        "hàm công khai kept(x = 1) { f = hàm() { g = helper(); trả về g() + x; }; trả về f(); };\n"
        "hàm riêng tư helper() { trả về leaf; };\n"
        "hàm riêng tư leaf() { trả về 42; };\n"
        "hàm riêng tư init_helper() { trả về 2; }; in init_helper();\n"
        "hàm riêng tư method_helper() { trả về 3; };\n"
        "lớp công khai Box { hàm value() { trả về method_helper(); }; };\n";
    for (int i = 0; i < 60; ++i)
        source += "hàm công khai unused_" + std::to_string(i) + "() { trả về " +
                  std::to_string(i) + "; };\n";
    writeFile(tree.root / "retention.vi", source);
    CompilationContext context;
    context.importResolutionBase = tree.root;
    auto hasFunction = [&](const std::string &name) {
        return std::any_of(context.functionNameIndices.begin(), context.functionNameIndices.end(),
            [&](const auto &entry) {
                const auto &runtime = context.getString(entry.second);
                return runtime == name ||
                    (runtime.size() > name.size() &&
                     runtime.compare(runtime.size() - name.size() - 1, name.size() + 1,
                                     ":" + name) == 0);
            });
    };
    const auto selected = compileWithBindings(context, "nhập retention.kept; in kept();", keywordMap, false);
    for (const std::string name : {"kept", "helper", "leaf", "init_helper",
                                  "method_helper", "Box.value"})
        expect(hasFunction(name), "retention giữ root/dependency: " + name);
    expect(!hasFunction("unused_0") && !hasFunction("unused_59") &&
               context.functionBytecode.size() < 12,
           "60 hàm ngoài closure không được hạ IR hoặc phát bytecode");
    checkRuntime(context, selected, "2\n43\n");

    (void)compileWithBindings(context, "nhập retention.*;", keywordMap, false);
    expect(hasFunction("unused_0") && hasFunction("unused_59"),
           "wildcard giữ toàn bộ public function surface");
    (void)compileWithBindings(context, "nhập retention;", keywordMap, false);
    expect(hasFunction("unused_59"), "legacy graph bỏ qua retention để giữ parity");
    (void)compileWithBindings(context,
        "nhập retention.kept; nhập retention.unused_59 như extra;", keywordMap, false);
    expect(hasFunction("unused_59") && !hasFunction("unused_0") &&
               context.moduleInitializers.size() == 1,
           "retention lấy hợp của các cạnh selection trước lần load đầu");
    writeFile(tree.root / "invalid.vi",
        "hàm công khai kept() { trả về 42; };\n"
        "hàm công khai unused() { trái(1, 2); };\n"
        "hàm trái(x) { trả về x; };\n");
    bool rejected = false;
    try { (void)compileWithBindings(context, "nhập invalid.kept;", keywordMap, false); }
    catch (const std::exception &) { rejected = true; }
    expect(rejected, "unused function vẫn được full-source semantic diagnostics");
    writeFile(tree.root / "dynamic.vi",
        "hàm công khai kept() { trả về unresolved(); };\n"
        "hàm công khai unused_0() { trả về 0; };\n");
    (void)compileWithBindings(context, "nhập dynamic.kept;", keywordMap, false);
    expect(hasFunction("unused_0"), "dynamic call khiến retention giữ module bảo thủ");
}

} // namespace

int main() {
    try {
        testResolverPrecedence();
        testNestedImportUsesImporterDirectory();
        testImportSelectionSyntax();
        testSelectedImportPipeline();
        testSelectionDiagnosticsAndReExports();
        testSelectiveRetentionBeforeIr();
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

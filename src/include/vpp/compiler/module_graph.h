#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "vpp/compiler/semantic.h"
#include "vpp/frontend/ast.h"

namespace vietvm::compiler {

inline constexpr std::string_view kCurrentCompilationModuleIdentity =
    "entry://current-compilation";

// Mô tả vị trí một module local đã phân giải, gồm đường dẫn chuẩn và thông tin cần để resolver đọc source ổn định.
struct LocalModuleLocation {
    std::filesystem::path path;
    std::string identity;
};

// Gom vị trí module với nội dung source/AST đã đọc; module graph truyền record này giữa resolver, parser và semantic indexing.
struct LocalModuleSource {
    std::filesystem::path path;
    std::string identity;
    std::string source;
};

// Phân giải dữ liệu thông qua `LocalModuleResolver`.
class LocalModuleResolver {
public:
    // Khởi tạo `LocalModuleResolver` từ các tham số đầu vào; constructor lưu trạng thái ban đầu cần thiết để các phương thức của đối tượng hoạt động nhất quán.
    explicit LocalModuleResolver(
        std::filesystem::path resolutionBase = std::filesystem::current_path(),
        std::optional<std::filesystem::path> installationHome = std::nullopt);

    // Trả thư mục gốc dùng để phân giải import tương đối; resolver chuẩn hóa giá trị constructor thành path tuyệt đối/lexical ổn định.
    const std::filesystem::path &resolutionBase() const noexcept;
    // Phân giải phân giải; hàm lần theo metadata/phạm vi liên quan để biến tham chiếu đầu vào thành đích cụ thể.
    LocalModuleLocation resolve(
        const vietvm::frontend::AstImportSpec &importSpec) const;
    // Resolve cùng policy nhưng lấy base từ chính importer; dùng cho nested import
    // để semantic graph và codegen chọn cùng một source path.
    LocalModuleLocation resolveFrom(
        const std::filesystem::path &resolutionBase,
        const vietvm::frontend::AstImportSpec &importSpec) const;
    // Đọc read; hàm lấy nội dung từ nguồn tương ứng, kiểm tra lỗi cần thiết rồi trả dữ liệu đã đọc.
    LocalModuleSource read(const LocalModuleLocation &location) const;

private:
    std::filesystem::path resolutionBase_;
    std::optional<std::filesystem::path> installationHome_;
};

enum class LocalModuleEdgeAction {
    Load,
    DuplicateNoOp,
    CycleNoOp,
};

// Trả tên văn bản ổn định cho cục bộ mô-đun edge action; hàm ánh xạ enum/giá trị nội bộ sang chuỗi để tooling, log hoặc test có thể hiển thị nhất quán.
const char *localModuleEdgeActionName(LocalModuleEdgeAction action) noexcept;

// Biểu diễn một cạnh import giữa hai module local, kèm hành động/alias cần thiết để graph và diagnostic mô tả quan hệ phụ thuộc.
struct LocalModuleEdge {
    std::string importerIdentity;
    std::string importedIdentity;
    vietvm::frontend::AstImportSpec importSpec;
    LocalModuleEdgeAction action = LocalModuleEdgeAction::Load;
};

// Sở hữu tập module và cạnh import đã phân giải; graph là đầu vào cho thứ tự semantic/initialization và kiểm tra cycle.
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

// Dựng `LocalModuleGraph` từ entry module; lớp dùng resolver và DFS để đọc import đệ quy, deduplicate module và phát hiện chu trình.
class LocalModuleGraphBuilder {
public:
    // Khởi tạo `LocalModuleGraphBuilder` từ các tham số đầu vào; constructor lưu trạng thái ban đầu cần thiết để các phương thức của đối tượng hoạt động nhất quán.
    LocalModuleGraphBuilder(LocalModuleResolver resolver,
                            LocalModuleImportScanner importScanner);

    // Dựng build; hàm tổng hợp các phần tử đầu vào thành cấu trúc hoàn chỉnh, đồng thời thiết lập các quan hệ/chỉ mục cần thiết.
    LocalModuleGraph build(
        std::string entryIdentity,
        const std::vector<vietvm::frontend::AstImportSpec> &rootImports) const;

private:
    LocalModuleResolver resolver_;
    LocalModuleImportScanner importScanner_;
};

// Mô tả một symbol được module export, gồm tên/kind/visibility/span cần để module phụ thuộc tạo external semantic symbol.
struct ModuleExportSymbol {
    std::string name;
    SemanticSymbolKind kind = SemanticSymbolKind::Function;
    vietvm::frontend::SourceSpan declaration{};
};

// Gom semantic model và danh sách export của một module local; index dùng record này để xây môi trường cho module phụ thuộc.
struct LocalModuleSemanticRecord {
    std::filesystem::path path;
    std::string identity;
    std::vector<ModuleExportSymbol> exports;
    std::vector<ModuleExportSymbol> hiddenSymbols;
};

// Compile-time module index. The graph owns import identity/order while this
// layer adds the namespace surface consumed by semantic analysis.
// Khởi tạo `LocalModuleSemanticIndex` từ các tham số đầu vào; constructor lưu trạng thái ban đầu cần thiết để các phương thức của đối tượng hoạt động nhất quán.
struct LocalModuleSemanticIndex {
    std::string entryIdentity;
    LocalModuleGraph graph;
    std::vector<LocalModuleSemanticRecord> modules;

    // Tra `RuntimeModule` theo tên hoặc chỉ mục; hàm trả con trỏ/tham chiếu tới record đang được `ModuleTable` quản lý.
    const LocalModuleSemanticRecord *module(std::string_view identity) const noexcept;
    // Tra symbol được export từ một module semantic record; hàm tìm theo tên public và trả metadata cần cho module khác import.
    const ModuleExportSymbol *exportedSymbol(std::string_view moduleIdentity,
                                             std::string_view name) const noexcept;

    // Produces only direct imports of `importerIdentity`. An import alias
    // qualifies the exported name (`alias.symbol`); an unaliased import keeps
    // the legacy flat namespace behavior.
    // Tạo `SemanticEnvironment` cho một module; hàm gom các export từ dependency đã phân giải thành external symbol mà analyzer có thể nhìn thấy.
    SemanticEnvironment semanticEnvironmentFor(
        std::string_view importerIdentity) const;
};

enum class ModuleIndexMode {
    DirectOnly,
    Recursive,
};

// Dựng cục bộ mô-đun ngữ nghĩa chỉ số; hàm tổng hợp các phần tử đầu vào thành cấu trúc hoàn chỉnh, đồng thời thiết lập các quan hệ/chỉ mục cần thiết.
LocalModuleSemanticIndex buildLocalModuleSemanticIndex(
    LocalModuleResolver resolver,
    std::string entryIdentity,
    const std::vector<vietvm::frontend::AstImportSpec> &rootImports,
    ModuleIndexMode mode = ModuleIndexMode::Recursive);

enum class ModuleInitializationState {
    Uninitialized,
    Initializing,
    Initialized,
    Failed,
};

// Trả tên văn bản ổn định cho mô-đun khởi tạo trạng thái; hàm ánh xạ enum/giá trị nội bộ sang chuỗi để tooling, log hoặc test có thể hiển thị nhất quán.
const char *moduleInitializationStateName(
    ModuleInitializationState state) noexcept;

// Theo dõi trạng thái khởi tạo module và thứ tự hoàn tất; lớp ngăn initializer chạy lặp và phát hiện chu trình khởi tạo runtime.
class ModuleInitializationTracker {
public:
    // Khởi tạo `ModuleInitializationTracker` từ các tham số đầu vào; constructor lưu trạng thái ban đầu cần thiết để các phương thức của đối tượng hoạt động nhất quán.
    explicit ModuleInitializationTracker(const LocalModuleSemanticIndex &index);

    // Trả trạng thái hiện tại của đối tượng quản lý; hàm chỉ tra dữ liệu nội bộ tương ứng với khóa/module được yêu cầu.
    std::optional<ModuleInitializationState> state(
        std::string_view identity) const noexcept;
    // Chuyển thực thể sang trạng thái đang khởi tạo/đang xử lý; hàm kiểm tra trạng thái trước đó để ngăn bắt đầu lặp sai quy trình.
    bool begin(std::string_view identity);
    // Đánh dấu thao tác/module đã hoàn tất thành công; hàm cập nhật trạng thái và thứ tự hoàn tất dùng cho các lần tra cứu sau.
    bool complete(std::string_view identity);
    // Đánh dấu thao tác/module thất bại; hàm lưu trạng thái lỗi để caller không xem thực thể là đã khởi tạo thành công.
    bool fail(std::string_view identity);

private:
    std::unordered_map<std::string, ModuleInitializationState> states_;
};

} // namespace vietvm::compiler

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "vm/instruction.h"
#include "vpp/runtime/debug.h"

namespace vietvm::runtime {

enum class ModuleState {
    Uninitialized,
    Initializing,
    Initialized,
    Failed,
};

// Trả tên văn bản ổn định cho mô-đun trạng thái; hàm ánh xạ enum/giá trị nội bộ sang chuỗi để tooling, log hoặc test có thể hiển thị nhất quán.
const char *moduleStateName(ModuleState state) noexcept;

// Lưu trạng thái một module runtime gồm tên, initializer và trạng thái khởi tạo; `ModuleTable` quản lý các record này.
struct RuntimeModule {
    std::string identity;
    std::vector<Instruction> initializer;
    std::vector<RuntimeSourceLocation> debugInfo;
    ModuleState state = ModuleState::Uninitialized;
};

// Quản lý mô-đun bảng; lớp đóng gói bảng tra cứu và các thao tác thêm/đọc/xóa để duy trì trạng thái nhất quán.
class ModuleTable {
public:
    // Thêm add; hàm chèn dữ liệu mới vào cấu trúc trạng thái hiện tại và duy trì các chỉ mục liên quan.
    bool add(std::string identity,
             std::vector<Instruction> initializer,
             std::vector<RuntimeSourceLocation> debugInfo = {});
    // Trả trạng thái hiện tại của đối tượng quản lý; hàm chỉ tra dữ liệu nội bộ tương ứng với khóa/module được yêu cầu.
    std::optional<ModuleState> state(std::string_view identity) const noexcept;
    // Chuyển thực thể sang trạng thái đang khởi tạo/đang xử lý; hàm kiểm tra trạng thái trước đó để ngăn bắt đầu lặp sai quy trình.
    bool begin(std::string_view identity);
    // Đánh dấu thao tác/module đã hoàn tất thành công; hàm cập nhật trạng thái và thứ tự hoàn tất dùng cho các lần tra cứu sau.
    bool complete(std::string_view identity);
    // Đánh dấu thao tác/module thất bại; hàm lưu trạng thái lỗi để caller không xem thực thể là đã khởi tạo thành công.
    bool fail(std::string_view identity);

    // Trả thứ tự các module/thực thể đã hoàn tất; hàm đọc danh sách được tracker duy trì trong quá trình khởi tạo.
    const std::vector<std::string> &order() const noexcept { return order_; }
    // Tra `RuntimeModule` theo tên hoặc chỉ mục; hàm trả con trỏ/tham chiếu tới record đang được `ModuleTable` quản lý.
    const RuntimeModule *module(std::string_view identity) const noexcept;
    // Trả số phần tử hiện có trong cấu trúc quản lý; hàm chỉ đọc kích thước container nội bộ và không thay đổi trạng thái.
    std::size_t size() const noexcept { return modules_.size(); }

private:
    std::unordered_map<std::string, RuntimeModule> modules_;
    std::vector<std::string> order_;
};

} // namespace vietvm::runtime

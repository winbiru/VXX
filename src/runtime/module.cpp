#include "vpp/runtime/module.h"

#include <utility>

namespace vietvm::runtime {

// Trả tên văn bản ổn định cho mô-đun trạng thái; hàm ánh xạ enum/giá trị nội bộ sang chuỗi để tooling, log hoặc test có thể hiển thị nhất quán.
const char *moduleStateName(ModuleState state) noexcept {
    switch (state) {
        case ModuleState::Uninitialized: return "uninitialized";
        case ModuleState::Initializing: return "initializing";
        case ModuleState::Initialized: return "initialized";
        case ModuleState::Failed: return "failed";
    }
    return "unknown";
}

// Thêm add; hàm chèn dữ liệu mới vào cấu trúc trạng thái hiện tại và duy trì các chỉ mục liên quan.
bool ModuleTable::add(std::string identity,
                      std::vector<Instruction> initializer,
                      std::vector<RuntimeSourceLocation> debugInfo) {
    if (identity.empty() || modules_.find(identity) != modules_.end()) return false;
    RuntimeModule record{identity, std::move(initializer), std::move(debugInfo),
                         ModuleState::Uninitialized};
    order_.push_back(identity);
    modules_.emplace(identity, std::move(record));
    return true;
}

// Tra `RuntimeModule` theo tên hoặc chỉ mục; hàm trả con trỏ/tham chiếu tới record đang được `ModuleTable` quản lý.
const RuntimeModule *ModuleTable::module(std::string_view identity) const noexcept {
    const auto found = modules_.find(std::string(identity));
    return found == modules_.end() ? nullptr : &found->second;
}

// Trả trạng thái hiện tại của đối tượng quản lý; hàm chỉ tra dữ liệu nội bộ tương ứng với khóa/module được yêu cầu.
std::optional<ModuleState> ModuleTable::state(std::string_view identity) const noexcept {
    const RuntimeModule *record = module(identity);
    return record == nullptr ? std::nullopt
                             : std::optional<ModuleState>(record->state);
}

// Chuyển thực thể sang trạng thái đang khởi tạo/đang xử lý; hàm kiểm tra trạng thái trước đó để ngăn bắt đầu lặp sai quy trình.
bool ModuleTable::begin(std::string_view identity) {
    auto found = modules_.find(std::string(identity));
    if (found == modules_.end() || found->second.state != ModuleState::Uninitialized) {
        return false;
    }
    found->second.state = ModuleState::Initializing;
    return true;
}

// Đánh dấu thao tác/module đã hoàn tất thành công; hàm cập nhật trạng thái và thứ tự hoàn tất dùng cho các lần tra cứu sau.
bool ModuleTable::complete(std::string_view identity) {
    auto found = modules_.find(std::string(identity));
    if (found == modules_.end() || found->second.state != ModuleState::Initializing) {
        return false;
    }
    found->second.state = ModuleState::Initialized;
    return true;
}

// Đánh dấu thao tác/module thất bại; hàm lưu trạng thái lỗi để caller không xem thực thể là đã khởi tạo thành công.
bool ModuleTable::fail(std::string_view identity) {
    auto found = modules_.find(std::string(identity));
    if (found == modules_.end() || found->second.state != ModuleState::Initializing) {
        return false;
    }
    found->second.state = ModuleState::Failed;
    return true;
}

} // namespace vietvm::runtime

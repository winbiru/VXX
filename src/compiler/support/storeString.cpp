#include "common/storeString.h"
#include "vpp/core/message_constants.h"
#include <stdexcept>
#include <vector>
#include <iostream>

namespace vietvm::compiler {

    namespace {
        thread_local CompilationRegistryState defaultRegistryState;
        thread_local CompilationRegistryState *activeRegistryState = &defaultRegistryState;
    }

    // Trả con trỏ tới trạng thái registry đang được compiler sử dụng; các helper static dựa vào đây để thao tác đúng `CompilationContext` hiện hành.
    CompilationRegistryState &activeCompilationRegistryState() {
        return *activeRegistryState;
    }

    // Thiết lập active compilation bảng đăng ký trạng thái; hàm ghi giá trị đầu vào vào trạng thái đích và thay thế giá trị cũ nếu đã tồn tại.
    CompilationRegistryState *setActiveCompilationRegistryState(
        CompilationRegistryState *state) {
        CompilationRegistryState *previous = activeRegistryState;
        activeRegistryState = state == nullptr ? &defaultRegistryState : state;
        return previous;
    }

    // Trả bảng ánh xạ function id sang bytecode của registry đang hoạt động; caller sửa trực tiếp bảng dùng chung trong một lượt biên dịch.
    std::unordered_map<int, std::vector<Instruction>> &hamMap::bytecodeMap() {
        return activeCompilationRegistryState().functionBytecode;
    }

    // Trả tên văn bản ổn định cho tên chỉ số ánh xạ; hàm ánh xạ enum/giá trị nội bộ sang chuỗi để tooling, log hoặc test có thể hiển thị nhất quán.
    std::unordered_map<int, int> &hamMap::nameIndexMap() {
        return activeCompilationRegistryState().functionNameIndices;
    }

    // Cấp function id mới theo bộ đếm tăng dần của registry; hàm bảo đảm mỗi hàm trong lượt biên dịch có mã riêng.
    int hamMap::allocHamId() {
        return activeCompilationRegistryState().nextFunctionId++;
    }

    // Đặt lại ham mã định danh counter; hàm khôi phục trạng thái về giá trị ban đầu và xóa dữ liệu tạm của lần chạy trước.
    void hamMap::resetHamIdCounter() {
        activeCompilationRegistryState().nextFunctionId = 0;
    }

    // Tìm chuỗi; hàm tra cứu dữ liệu theo tiêu chí đầu vào và trả về vị trí hoặc phần tử phù hợp nếu có.
    int StringPool::findString(const std::string& s) {
        auto &state = activeCompilationRegistryState();
        auto it = state.stringPoolIndexMap.find(s);
        if (it != state.stringPoolIndexMap.end()) return it->second;
        return -1;
    }

    // Lưu chuỗi; hàm ghi dữ liệu đầu vào vào cấu trúc lưu trữ tương ứng để có thể truy xuất ở bước sau.
    int StringPool::storeString(const std::string& s) {
        auto &state = activeCompilationRegistryState();
        // fast path: return existing index if present
        auto it = state.stringPoolIndexMap.find(s);
        if (it != state.stringPoolIndexMap.end()) {
            return it->second;
        }
        // else add
        state.stringPool.push_back(s);
        int idx = static_cast<int>(state.stringPool.size() - 1);
        state.stringPoolIndexMap.emplace(s, idx);
        // debug log (temporary) to show additions
        // std::cerr << "DEBUG: StringPool added [" << idx << "] = \"" << s << "\"\n";
        return idx;
    }

    // Lấy chuỗi; hàm đọc dữ liệu từ trạng thái hiện tại và trả về cho caller mà không chủ động thay đổi dữ liệu.
    const std::string& StringPool::getString(int idx)  {
        auto &pool = activeCompilationRegistryState().stringPool;
        if (idx < 0 || static_cast<size_t>(idx) >= pool.size()) {
            throw std::out_of_range(vietvm::messages::formatMessage(
                vietvm::messages::kInternalStringPoolIndex));
        }
        return pool[idx];
    }
    // Lấy bể dữ liệu; hàm đọc dữ liệu từ trạng thái hiện tại và trả về cho caller mà không chủ động thay đổi dữ liệu.
    const std::vector<std::string>& StringPool::getPool() {
        return activeCompilationRegistryState().stringPool;
    }
    // Trả số phần tử hiện có trong cấu trúc quản lý; hàm chỉ đọc kích thước container nội bộ và không thay đổi trạng thái.
    size_t StringPool::size() noexcept {
        return activeCompilationRegistryState().stringPool.size();
    }
    // Xóa clear; hàm đưa cấu trúc trạng thái về rỗng để lần sử dụng tiếp theo không mang dữ liệu cũ.
    void StringPool::clear() {
        auto &state = activeCompilationRegistryState();
        state.stringPool.clear();
        state.stringPoolIndexMap.clear();
    }
} // namespace vietvm::compiler

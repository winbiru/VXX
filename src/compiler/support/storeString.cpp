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

    // Tìm chuỗi trong registry cụ thể; đây là primitive state-explicit dùng bởi production compiler.
    int CompilationRegistryState::findString(const std::string &value) const {
        const auto it = stringPoolIndexMap.find(value);
        return it == stringPoolIndexMap.end() ? -1 : it->second;
    }

    // Lưu chuỗi vào registry cụ thể và deduplicate theo nội dung để chỉ số bytecode ổn định.
    int CompilationRegistryState::storeString(const std::string &value) {
        const auto existing = stringPoolIndexMap.find(value);
        if (existing != stringPoolIndexMap.end()) return existing->second;
        stringPool.push_back(value);
        const int index = static_cast<int>(stringPool.size() - 1);
        stringPoolIndexMap.emplace(value, index);
        return index;
    }

    // Lấy chuỗi từ registry cụ thể và giữ nguyên contract lỗi chỉ số của StringPool cũ.
    const std::string &CompilationRegistryState::getString(int index) const {
        if (index < 0 || static_cast<size_t>(index) >= stringPool.size()) {
            throw std::out_of_range(vietvm::messages::formatMessage(
                vietvm::messages::kInternalStringPoolIndex));
        }
        return stringPool[static_cast<size_t>(index)];
    }

    // Trả số chuỗi đang được registry cụ thể sở hữu.
    size_t CompilationRegistryState::stringCount() const noexcept {
        return stringPool.size();
    }

    // Cấp function id trực tiếp trên registry cụ thể để nhiều compilation context không chia sẻ bộ đếm.
    int CompilationRegistryState::allocateFunctionId() noexcept {
        return nextFunctionId++;
    }

    // Ghi name-index của function trực tiếp vào registry cụ thể.
    void CompilationRegistryState::setFunctionNameIndex(int functionId, int nameIndex) {
        functionNameIndices[functionId] = nameIndex;
    }

    // Đặt lại bộ đếm function id của registry cụ thể.
    void CompilationRegistryState::resetFunctionIdCounter() noexcept {
        nextFunctionId = 0;
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
        return activeCompilationRegistryState().allocateFunctionId();
    }

    // Đặt lại ham mã định danh counter; hàm khôi phục trạng thái về giá trị ban đầu và xóa dữ liệu tạm của lần chạy trước.
    void hamMap::resetHamIdCounter() {
        activeCompilationRegistryState().resetFunctionIdCounter();
    }

    // Tìm chuỗi; hàm tra cứu dữ liệu theo tiêu chí đầu vào và trả về vị trí hoặc phần tử phù hợp nếu có.
    int StringPool::findString(const std::string& s) {
        return activeCompilationRegistryState().findString(s);
    }

    // Lưu chuỗi; hàm ghi dữ liệu đầu vào vào cấu trúc lưu trữ tương ứng để có thể truy xuất ở bước sau.
    int StringPool::storeString(const std::string& s) {
        return activeCompilationRegistryState().storeString(s);
    }

    // Lấy chuỗi; hàm đọc dữ liệu từ trạng thái hiện tại và trả về cho caller mà không chủ động thay đổi dữ liệu.
    const std::string& StringPool::getString(int idx)  {
        return activeCompilationRegistryState().getString(idx);
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

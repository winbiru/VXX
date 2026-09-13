#pragma once
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../vm/instruction.h"

namespace vietvm::compiler {
    // Lưu metadata truy cập của một phương thức đã biên dịch như lớp sở hữu và visibility; registry dùng nó để kiểm tra lời gọi hợp lệ.
    struct MethodAccessInfo {
        std::string ownerClass;
        std::string visibility;
    };

    // Lưu function id/metadata của initializer một module; runtime dùng record này để chạy khởi tạo đúng một lần theo dependency order.
    struct CompiledModuleInitializer {
        std::string identity;
        std::vector<Instruction> bytecode;
    };

    // Gom toàn bộ trạng thái mutable của một lượt biên dịch như bytecode hàm, StringPool, import và class context để các pipeline độc lập không dùng chung dữ liệu cũ.
    struct CompilationRegistryState {
        std::vector<std::string> stringPool;
        std::unordered_map<std::string, int> stringPoolIndexMap;
        std::unordered_map<int, std::vector<Instruction>> functionBytecode;
        std::unordered_map<int, int> functionNameIndices;
        std::unordered_set<std::string> importedFiles;
        std::vector<CompiledModuleInitializer> moduleInitializers;
        std::unordered_map<std::string, MethodAccessInfo> methodAccess;
        std::vector<std::string> classContextStack;
        // Base directory used to resolve relative imports for this compilation.
        // This is configuration, so clear() intentionally preserves it.
        std::filesystem::path importResolutionBase;
        int nextFunctionId = 0;

        // Xóa clear; hàm đưa cấu trúc trạng thái về rỗng để lần sử dụng tiếp theo không mang dữ liệu cũ.
        void clear() {
            stringPool.clear();
            stringPoolIndexMap.clear();
            functionBytecode.clear();
            functionNameIndices.clear();
            importedFiles.clear();
            moduleInitializers.clear();
            methodAccess.clear();
            classContextStack.clear();
            nextFunctionId = 0;
        }
    };

    // Trả con trỏ tới trạng thái registry đang được compiler sử dụng; các helper static dựa vào đây để thao tác đúng `CompilationContext` hiện hành.
    CompilationRegistryState &activeCompilationRegistryState();
    // Thiết lập active compilation bảng đăng ký trạng thái; hàm ghi giá trị đầu vào vào trạng thái đích và thay thế giá trị cũ nếu đã tồn tại.
    CompilationRegistryState *setActiveCompilationRegistryState(
        CompilationRegistryState *state);

    // Quản lý ánh xạ function id sang bytecode và name-index; các hàm static đọc/ghi vào `CompilationRegistryState` đang hoạt động.
    class hamMap {
    public:
        // Trả bảng ánh xạ function id sang bytecode của registry đang hoạt động; caller sửa trực tiếp bảng dùng chung trong một lượt biên dịch.
        static std::unordered_map<int, std::vector<Instruction>> &bytecodeMap();
        // Trả tên văn bản ổn định cho tên chỉ số ánh xạ; hàm ánh xạ enum/giá trị nội bộ sang chuỗi để tooling, log hoặc test có thể hiển thị nhất quán.
        static std::unordered_map<int, int> &nameIndexMap();
        // Thiết lập ham tên chỉ số; hàm ghi giá trị đầu vào vào trạng thái đích và thay thế giá trị cũ nếu đã tồn tại.
        static void setHamNameIndex(int hamId, int nameIndex) { nameIndexMap()[hamId] = nameIndex; }
        // Xóa ham tên chỉ số ánh xạ; hàm đưa cấu trúc trạng thái về rỗng để lần sử dụng tiếp theo không mang dữ liệu cũ.
        static void clearHamNameIndexMap() { nameIndexMap().clear(); }
        // Cấp function id mới theo bộ đếm tăng dần của registry; hàm bảo đảm mỗi hàm trong lượt biên dịch có mã riêng.
        static int allocHamId();
        // Đặt lại ham mã định danh counter; hàm khôi phục trạng thái về giá trị ban đầu và xóa dữ liệu tạm của lần chạy trước.
        static void resetHamIdCounter();
    };
    // Quản lý bể chuỗi dùng chung của bytecode; lớp deduplicate chuỗi, cấp chỉ số ổn định và cho compiler/runtime tra lại nội dung theo index.
    class StringPool {
    public:
        // Lưu chuỗi; hàm ghi dữ liệu đầu vào vào cấu trúc lưu trữ tương ứng để có thể truy xuất ở bước sau.
        static int storeString(const std::string& s);
        // Tìm chuỗi; hàm tra cứu dữ liệu theo tiêu chí đầu vào và trả về vị trí hoặc phần tử phù hợp nếu có.
        static int findString(const std::string& s);
        // Lấy chuỗi; hàm đọc dữ liệu từ trạng thái hiện tại và trả về cho caller mà không chủ động thay đổi dữ liệu.
        static const std::string& getString(int idx);
        // Trả số phần tử hiện có trong cấu trúc quản lý; hàm chỉ đọc kích thước container nội bộ và không thay đổi trạng thái.
        static size_t size() noexcept;
        // Lấy bể dữ liệu; hàm đọc dữ liệu từ trạng thái hiện tại và trả về cho caller mà không chủ động thay đổi dữ liệu.
        static const std::vector<std::string>& getPool();
        // Xóa clear; hàm đưa cấu trúc trạng thái về rỗng để lần sử dụng tiếp theo không mang dữ liệu cũ.
        static void clear();
    };

} // namespace vietvm::compiler

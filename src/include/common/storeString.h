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

        // Tìm chuỗi trong bể của chính compilation state này mà không phụ thuộc active context ẩn.
        int findString(const std::string &value) const;
        // Lưu chuỗi vào bể của chính compilation state này, tái sử dụng chỉ số nếu chuỗi đã tồn tại.
        int storeString(const std::string &value);
        // Lấy chuỗi theo chỉ số từ compilation state hiện tại và báo lỗi nếu chỉ số vượt biên.
        const std::string &getString(int index) const;
        // Trả số chuỗi hiện có trong compilation state mà không thay đổi dữ liệu.
        size_t stringCount() const noexcept;
        // Cấp function id mới trực tiếp từ compilation state để codegen không cần facade toàn cục.
        int allocateFunctionId() noexcept;
        // Ghi ánh xạ function id sang chỉ số tên trực tiếp vào compilation state.
        void setFunctionNameIndex(int functionId, int nameIndex);
        // Đặt lại bộ đếm function id của compilation state về đầu phiên biên dịch.
        void resetFunctionIdCounter() noexcept;

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

    // Trả registry compatibility của thread hiện tại; production compiler truyền `CompilationRegistryState` tường minh và không dùng API này.
    CompilationRegistryState &activeCompilationRegistryState();
    // Đổi registry compatibility của thread hiện tại cho test/caller cũ; production pipeline không bind context qua hàm này.
    CompilationRegistryState *setActiveCompilationRegistryState(
        CompilationRegistryState *state);

    // Facade tương thích cho test/caller cũ; production codegen ghi function metadata trực tiếp vào `CompilationRegistryState` được truyền vào.
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
    // Facade tương thích cho bể chuỗi cũ; production compiler dùng các phương thức state-explicit trên `CompilationRegistryState`.
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

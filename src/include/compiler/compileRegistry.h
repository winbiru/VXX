//
// Created by nx_thang on 10/21/2025.
//

// CompileRegistry.h
#pragma once
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>
#include "../vm/instruction.h"
#include "vpp/frontend/ast.h"
// Imported files tracking (shared for a single compilation session)
namespace vietvm { namespace compiler {
    // Trả tập đường dẫn file đã import trong compilation registry; compiler dùng tập này để ngăn import cùng source lặp lại.
    std::unordered_set<std::string> &importedFileSet();
    // Xóa imported files; hàm đưa cấu trúc trạng thái về rỗng để lần sử dụng tiếp theo không mang dữ liệu cũ.
    void clearImportedFiles();

    // Xóa lớp access trạng thái; hàm đưa cấu trúc trạng thái về rỗng để lần sử dụng tiếp theo không mang dữ liệu cũ.
    void clearClassAccessState();
    // Đưa vào lớp ngữ cảnh; hàm thêm phần tử vào ngăn xếp hoặc ngữ cảnh hiện tại để được dùng trước khi rời phạm vi.
    void pushClassContext(const std::string &className);
    // Lấy ra khỏi lớp ngữ cảnh; hàm loại bỏ phần tử/ngữ cảnh trên cùng và khôi phục trạng thái trước đó.
    void popClassContext();
    // Trả tên lớp đang ở đỉnh class-context stack; lookup method/visibility dùng giá trị này khi phân giải lời gọi không ghi rõ lớp.
    std::string currentClassContext();
    // Đăng ký lớp phương thức phạm vi truy cập; hàm thêm metadata vào bảng đăng ký để các bước phân giải/thực thi có thể tra cứu về sau.
    void registerClassMethodVisibility(const std::string &fullMethodName,
                                       const std::string &ownerClass,
                                       const std::string &visibility);
    // Phân giải callable tên in ngữ cảnh; hàm lần theo metadata/phạm vi liên quan để biến tham chiếu đầu vào thành đích cụ thể.
    std::string resolveCallableNameInContext(const std::string &name,
                                             const std::unordered_map<std::string,int> &symTab);
    // Kiểm tra điều kiện của `validateCallableAccess`.
    void validateCallableAccess(const std::string &resolvedName);
    // Resolve a callable through the local symbol table.  Imported/global
    // fallback is enabled for expression/statement calls and can be disabled
    // for `gọi`, which intentionally emits a name-based VM fallback instead.
    int resolveFunctionIdByName(const std::string &name,
                                const std::unordered_map<std::string,int> &symTab,
                                bool includeGlobalFallback = true);

    // Biên dịch nhập spec; hàm đưa dữ liệu qua các pha compiler cần thiết và tạo artifact thực thi cho bước sau.
    void compileImportSpec(
        const vietvm::frontend::AstImportSpec &spec,
        int &nextId,
        const std::unordered_map<std::string,Opcode> &keywordMap);
} }

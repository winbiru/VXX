#pragma once

#include <stdexcept>
#include <string>
#include <vector>

#include "vpp/frontend/ast.h"

namespace vietvm::frontend {

// Biểu diễn lỗi `ParseError`.
class ParseError : public std::runtime_error {
public:
    // Phân tích lỗi; hàm duyệt đầu vào theo ngữ pháp/định dạng quy định, tạo cấu trúc kết quả và báo lỗi khi dữ liệu không hợp lệ.
    ParseError(std::string message, SourceSpan span);

    const SourceSpan span;
};

// Điều phối quá trình phân tích của `Parser`.
class Parser {
public:
    // Phân tích bộ phân tích cú pháp; hàm duyệt đầu vào theo ngữ pháp/định dạng quy định, tạo cấu trúc kết quả và báo lỗi khi dữ liệu không hợp lệ.
    explicit Parser(std::vector<Token> tokens);

    // Phân tích program; hàm duyệt đầu vào theo ngữ pháp/định dạng quy định, tạo cấu trúc kết quả và báo lỗi khi dữ liệu không hợp lệ.
    AstProgram parseProgram();

private:
    // Phân tích câu lệnh; hàm duyệt đầu vào theo ngữ pháp/định dạng quy định, tạo cấu trúc kết quả và báo lỗi khi dữ liệu không hợp lệ.
    AstStatement parseStatement(bool insideBlock);
    // Phân tích khối; hàm duyệt đầu vào theo ngữ pháp/định dạng quy định, tạo cấu trúc kết quả và báo lỗi khi dữ liệu không hợp lệ.
    AstStatement parseBlock();
    // Thử nhận diện câu lệnh `chọn` có cấu trúc đầy đủ; hàm chỉ commit AST khi các nhánh ca/mặc định và delimiter hợp lệ, nếu không để đường fallback xử lý.
    bool tryParseStructuredSwitch(AstStatement &statement);
    // Phân loại classify; hàm đối chiếu dữ liệu với các quy tắc đã biết rồi trả enum/nhóm tương ứng.
    AstStatementKind classify(std::size_t begin) const noexcept;
    // Trả tên văn bản ổn định cho declaration; hàm ánh xạ enum/giá trị nội bộ sang chuỗi để tooling, log hoặc test có thể hiển thị nhất quán.
    std::string declarationName(std::size_t begin,
                                std::size_t end,
                                AstStatementKind kind) const;
    // Gắn metadata khai báo vào `AstStatement`; hàm đọc tên, modifier và tham số từ token để các pha semantic không phải phân tích lại chuỗi nguồn.
    void attachDeclarationPayload(AstStatement &statement);
    // Phân tích chi tiết câu lệnh nhập và gắn `AstImportSpec`; target import là đường dẫn/tên package không có dấu nháy.
    void attachImportForm(AstStatement &statement);
    // Tính `SourceSpan` của một dải token theo chỉ số đầu/cuối; hàm xử lý cả dải rỗng để diagnostic vẫn có vị trí hợp lệ.
    SourceSpan spanFor(std::size_t begin, std::size_t end) const noexcept;
    // Thử dựng expression root từ dải token của câu lệnh; khi parse thành công hàm lưu ExprId, còn lỗi dạng legacy được giữ cho đường tương thích.
    ExprId tryParseExpression(std::size_t begin, std::size_t end);
    // Thử phân tích thân lambda thành AST riêng; hàm tạo arena/lambda record và rollback nếu cú pháp không đủ điều kiện để tránh rò node bán phần.
    bool tryParseLambdaBody(std::size_t begin,
                            std::size_t end,
                            AstStatement &body);
    // Gắn các ExprId gốc vào câu lệnh dựa trên loại statement; nhờ đó semantic/lowering truy cập biểu thức trực tiếp thay vì quét lại token.
    void attachExpressionRoots(AstStatement &statement);
    // Nhận diện hình dạng khai báo lớp và gắn metadata lớp cha/phương thức; hàm chỉ đánh dấu structured khi token khớp đúng grammar lớp hiện hành.
    void attachClassForm(AstStatement &statement);
    // Nhận diện khai báo `giao diện`, danh sách giao diện cha và các chữ ký hàm không có thân để semantic kiểm tra hợp đồng triển khai.
    void attachInterfaceForm(AstStatement &statement);
    // Nhận diện cấu trúc `nếu`/`hoặc` và gắn dạng điều kiện vào AST; lowering dựa vào metadata này để phát control flow trực tiếp.
    void attachConditionalForm(AstStatement &statement);
    // Nhận diện header vòng `lặp` và gắn ba biểu thức init/condition/update cùng dạng vòng lặp vào AST.
    void attachLoopForm(AstStatement &statement);
    // Nhận diện cặp `thử`/`bắt lỗi`, lấy biến catch và gắn hai block con vào metadata AST để semantic tạo catch scope.
    void attachTryForm(AstStatement &statement);
    // Kiểm tra điều kiện của `isCompound`.
    bool isCompound(AstStatementKind kind) const noexcept;
    // Kiểm tra điều kiện của `isContinuation`.
    bool isContinuation(std::size_t tokenIndex) const noexcept;

    std::vector<Token> tokens_;
    std::vector<AstExpression> expressions_;
    std::vector<AstLambda> lambdas_;
    std::size_t pos_ = 0;
};

// Phân tích token; hàm duyệt đầu vào theo ngữ pháp/định dạng quy định, tạo cấu trúc kết quả và báo lỗi khi dữ liệu không hợp lệ.
AstProgram parseTokens(std::vector<Token> tokens);

} // namespace vietvm::frontend

#pragma once
// Các hàm trợ giúp cho lexer/parser của Compiler (nhẹ).
// Các hàm hiện được triển khai trong src/frontend/lexer.cpp
//
// Những helper này nằm trong namespace vietvm::compiler để phù hợp với mã hiện tại.
// Giữ header ở mức tối thiểu: chỉ khai báo các kiểu cần thiết trong chữ ký hàm.

#include <string>
#include <unordered_map>
#include <vector>

namespace vietvm::compiler {

    // Trả về true nếu chuỗi biểu diễn một số nguyên (có thể có dấu '-' ở đầu).
    bool isNumber(const std::string &s) noexcept;

    // True nếu tok là một toán tử đã biết (==, !=, +, -, ...).
    bool isOperator(const std::string &tok) noexcept;

    // True nếu token có dạng chuỗi được đóng bằng " hoặc '.
    bool isStringLiteral(const std::string &tk) noexcept;

    // Kiểm tra heuristic xem token có thể là identifier/biến hay không.
    // Lưu ý: implementation hiện chấp nhận tolerant UTF-8 ở mức thực dụng.
    bool isVariable(const std::string &tok) noexcept;

    // Chuyển các ký tự ASCII trong chuỗi về chữ thường; giữ nguyên các byte không-ASCII
    // (để không phá mã UTF-8). Dùng để chuẩn hoá khi so sánh từ khóa ASCII.
    std::string toLowerAscii(const std::string &s);

    // Bộ phân tích token cơ bản: chia source thành các token thô.
    // Hàm này hỗ trợ UTF-8 và xử lý comment/chuỗi/toán tử tương đối.
    std::vector<std::string> tokenize(const std::string &src);

    // Xử lý hậu token: gộp các từ khóa nhiều từ (ví dụ "mặc định", "nếu không"),
    // loại bỏ các dấu câu đuôi khi so sánh, v.v.
    std::vector<std::string> postProcessTokens(const std::vector<std::string>& tokens);

    // Chuẩn hóa token để so sánh (trim, bỏ dấu câu như ':'/','/';', và chuyển ký tự ASCII về chữ thường).
    std::string normalizeTokenForCompare(const std::string& s);

    // Loại bỏ dấu ngoặc của literal chuỗi và xử lý escape sequences
    // (ví dụ "\n", "\t", "\\", '\"').
    std::string stripQuotes(const std::string& input);

    // Truy cập chỉ đọc tới bảng ưu tiên toán tử dùng bởi parser/expr parser.
    const std::unordered_map<std::string,int>& operatorPrecedenceMap() noexcept;

    // Trợ giúp runtime/utility: lấy giá trị số (int) từ biến theo id.
    // Implementation có thể chuyển chuỗi sang số hoặc ném ngoại lệ nếu không hợp lệ.
    int getVarValueInt(int varId);
} // namespace vietvm::compiler
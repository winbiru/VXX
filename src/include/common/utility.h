#pragma once
#include <string>
#include <vector>
#include <utility>

namespace vietvm::compiler {

    // Loại bỏ khoảng trắng ở đầu và cuối chuỗi; hàm tìm biên trái/phải đầu tiên không phải whitespace rồi trả lát cắt tương ứng.
    std::string trim(const std::string &s);

    // Ghép tên token; hàm nối các phần tử theo thứ tự bằng dấu phân cách quy định để tạo kết quả duy nhất.
    std::string joinNameTokens(const std::vector<std::string>& tokens, size_t begin, size_t end);
    // Kiểm tra điều kiện của `isIdentifierLikeToken`.
    bool isIdentifierLikeToken(const std::string &token);
    // Kiểm tra điều kiện của `isCallableNamePiece`.
    bool isCallableNamePiece(const std::string &token);

    // Tách top level fields; hàm chia dữ liệu đầu vào theo quy tắc phân cách trong khi tôn trọng cấu trúc lồng nhau nếu có.
    std::vector<std::string> splitTopLevelFields(const std::string &text,
                                                 char delimiter);

    // Tách top level đối số; hàm chia dữ liệu đầu vào theo quy tắc phân cách trong khi tôn trọng cấu trúc lồng nhau nếu có.
    std::vector<std::string> splitTopLevelArguments(const std::string &text);

    // Trích xuất parens; hàm tìm phần dữ liệu cần thiết trong đầu vào và trả về lát cắt đã được chuẩn hóa.
    std::pair<std::string, size_t> extractParens(const std::vector<std::string>& tokens, size_t start);

    // Trích xuất khối; hàm tìm phần dữ liệu cần thiết trong đầu vào và trả về lát cắt đã được chuẩn hóa.
    std::pair<std::string, size_t> extractBlock(const std::vector<std::string>& tokens, size_t start);

    // Trích xuất biểu thức until semicolon; hàm tìm phần dữ liệu cần thiết trong đầu vào và trả về lát cắt đã được chuẩn hóa.
    std::pair<std::string, size_t> extractExpressionUntilSemicolon(const std::vector<std::string>& tokens, size_t start);

    // Trích xuất assigned var; hàm tìm phần dữ liệu cần thiết trong đầu vào và trả về lát cắt đã được chuẩn hóa.
    std::string extractAssignedVar(const std::string& expr);

} // namespace vietvm::Compiler

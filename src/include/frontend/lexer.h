#pragma once
// Các hàm trợ giúp cho lexer/parser của Compiler (nhẹ).
// Các hàm hiện được triển khai trong src/frontend/lexer.cpp
//
// Những helper này nằm trong namespace vietvm::compiler để phù hợp với mã hiện tại.
// Giữ header ở mức tối thiểu: chỉ khai báo các kiểu cần thiết trong chữ ký hàm.

#include <string>
#include <unordered_map>
#include <vector>
#include <stdexcept>
#include <sstream>

#include "vpp/core/message_constants.h"
#include "vpp/frontend/token.h"

namespace vietvm::compiler {

    // ==================== Exception Classes ====================

    // Lớp exception cơ bản cho lexer - cung cấp thông tin vị trí lỗi
    class LexerError : public std::runtime_error {
    public:
        size_t line;
        size_t column;
        std::string context;

        // Khởi tạo lỗi lexer với thông điệp và vị trí nguồn; object giữ dữ liệu diagnostic để CLI có thể hiển thị dòng/cột chính xác.
        explicit LexerError(const std::string& message, size_t line = 0, size_t column = 0, const std::string& context = "")
            : std::runtime_error(formatMessage(message, line, column, context)),
              line(line), column(column), context(context) {}

    private:
        // Định dạng thông báo; hàm chuyển dữ liệu đầu vào thành biểu diễn chuỗi ổn định để hiển thị hoặc ghi log.
        static std::string formatMessage(const std::string& msg, size_t line, size_t col, const std::string& ctx) {
            std::ostringstream oss;
            oss << "Lỗi lexer";
            if (line > 0) {
                oss << " [dòng " << line;
                if (col > 0) oss << ", cột " << col;
                oss << "]";
            }
            oss << ": " << msg;
            if (!ctx.empty()) {
                oss << "\n  --> " << ctx;
            }
            return oss.str();
        }
    };

    // Exception cho chuỗi không đóng (thiếu dấu nháy kết thúc)
    class UnclosedStringError : public LexerError {
    public:
        // Tạo lỗi cho chuỗi chưa đóng; constructor gắn vị trí bắt đầu literal và thông điệp chuyên biệt từ lexer.
        UnclosedStringError(size_t line, size_t column, const std::string& partialString)
            : LexerError(messages::formatMessage(messages::kLexerUnclosedString), line, column,
                         "\"" + (partialString.length() > 20 ? partialString.substr(0, 20) + "..." : partialString)) {}
    };

    // Exception cho comment block không đóng (thiếu */)
    class UnclosedCommentError : public LexerError {
    public:
        // Tạo lỗi cho comment khối chưa đóng; constructor giữ vị trí nguồn để diagnostic chỉ đúng nơi comment bắt đầu.
        UnclosedCommentError(size_t line, size_t column)
            : LexerError(messages::formatMessage(messages::kLexerUnclosedComment), line, column, "/*...") {}
    };

    // Exception cho ký tự không hợp lệ
    class InvalidCharacterError : public LexerError {
    public:
        // Tạo lỗi ký tự không hợp lệ; constructor ghi ký tự và vị trí gặp lỗi vào diagnostic lexer.
        InvalidCharacterError(char c, size_t line, size_t column)
            : LexerError(messages::formatMessage(messages::kLexerInvalidCharacter,
                                                  {std::string(1, c),
                                                   std::to_string(static_cast<int>(static_cast<unsigned char>(c)))}),
                         line, column, "") {}
    };

    // Exception cho escape sequence không hợp lệ (ví dụ: \x không được hỗ trợ)
    class InvalidEscapeSequenceError : public LexerError {
    public:
        // Tạo lỗi escape sequence không hợp lệ trong chuỗi; constructor giữ sequence và vị trí để thông báo rõ nguyên nhân.
        InvalidEscapeSequenceError(char escChar, size_t line, size_t column)
            : LexerError(messages::formatMessage(messages::kLexerInvalidEscapeSequence,
                                                  {std::string(1, escChar)}),
                         line, column, "") {}
    };

    // Exception cho token không mong đợi
    class UnexpectedTokenError : public LexerError {
    public:
        // Tạo lỗi khi lexer/parser gặp token ngoài ngữ cảnh cho phép; constructor đóng gói token thực tế cùng vị trí nguồn.
        UnexpectedTokenError(const std::string& token, const std::string& expected, size_t line, size_t column)
            : LexerError(expected.empty()
                             ? messages::formatMessage(messages::kLexerUnexpectedToken, {token})
                             : messages::formatMessage(messages::kLexerUnexpectedTokenWithExpectation,
                                                       {token, expected}),
                         line, column, "") {}
    };

    // Exception cho số không hợp lệ
    class InvalidNumberError : public LexerError {
    public:
        // Tạo lỗi literal số sai định dạng; constructor giữ lexeme số và vị trí để báo lỗi chính xác.
        InvalidNumberError(const std::string& token, size_t line, size_t column)
            : LexerError(messages::formatMessage(messages::kLexerInvalidNumber, {token}), line, column, "") {}
    };

    // Exception cho identifier không hợp lệ
    class InvalidIdentifierError : public LexerError {
    public:
        // Tạo lỗi identifier không hợp lệ; constructor lưu tên và vị trí vi phạm quy tắc đặt tên.
        InvalidIdentifierError(const std::string& token, size_t line, size_t column)
            : LexerError(messages::formatMessage(messages::kLexerInvalidIdentifier, {token}), line, column, "") {}
    };

    // ==================== Helper Functions ====================

    // Tính vị trí dòng và cột từ index trong source code
    std::pair<size_t, size_t> getLineAndColumn(const std::string& src, size_t index);

    // Trích xuất ngữ cảnh xung quanh vị trí lỗi (để hiển thị trong thông báo lỗi)
    std::string getErrorContext(const std::string& src, size_t index, size_t contextLen = 30);

    // ==================== Validation Functions ====================

    // Kiểm tra tính hợp lệ của một số (integer hoặc float)
    // Ném InvalidNumberError nếu không hợp lệ
    void validateNumber(const std::string& token, size_t line, size_t column);

    // Kiểm tra tính hợp lệ của identifier
    // Ném LexerError nếu không hợp lệ (bắt đầu bằng số, rỗng, v.v.)
    void validateIdentifier(const std::string& token, size_t line, size_t column);

    // Kiểm tra chuỗi có đúng định dạng không (có dấu nháy mở/đóng đúng)
    // Ném LexerError nếu không hợp lệ
    void validateStringLiteral(const std::string& token, size_t line, size_t column);

    // ==================== Core Lexer Functions ====================

    // Trả về true nếu chuỗi biểu diễn một số nguyên (có thể có dấu '-' ở đầu).
    bool isNumber(const std::string &s) noexcept;
    // Kiểm tra điều kiện của `isFloat`.
    bool isFloat(const std::string &s) noexcept;

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
    // Ném exception (UnclosedStringError, UnclosedCommentError, InvalidCharacterError)
    // khi gặp các trường hợp lỗi.
    std::vector<std::string> tokenize(const std::string &src);

    // Tách token cho with spans; hàm quét chuỗi nguồn từ trái sang phải và tạo dãy token, đồng thời bảo toàn thông tin vị trí khi cần.
    std::vector<vietvm::frontend::Token> tokenizeWithSpans(const std::string &src);

    // Xử lý hậu token: gộp các từ khóa nhiều từ (ví dụ "mặc định", "nếu không"),
    // loại bỏ các dấu câu đuôi khi so sánh, v.v.
    std::vector<std::string> postProcessTokens(const std::vector<std::string>& tokens);

    // Hậu xử lý token nhưng giữ `SourceSpan`; hàm thực hiện cùng quy tắc chuẩn hóa như bản chuỗi đồng thời hợp nhất vị trí nguồn của token được gộp.
    std::vector<vietvm::frontend::Token> postProcessTokensWithSpans(
        const std::vector<vietvm::frontend::Token> &tokens);

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

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace vietvm::core {

// Loại bỏ khoảng trắng ở đầu và cuối chuỗi; hàm tìm biên trái/phải đầu tiên không phải whitespace rồi trả lát cắt tương ứng.
std::string trim(const std::string &value);

// Chuyển lower ascii; hàm chuyển giá trị đầu vào sang kiểu/biểu diễn đích và trả kết quả đã chuẩn hóa.
std::string toLowerAscii(const std::string &value);

// Chuyển upper ascii; hàm chuyển giá trị đầu vào sang kiểu/biểu diễn đích và trả kết quả đã chuẩn hóa.
std::string toUpperAscii(const std::string &value);

// Chuyển hoa/thường UTF-8 cho ASCII và toàn bộ chữ cái tiếng Việt dựng sẵn
// trong Unicode. Code point ngoài phạm vi này và byte malformed được giữ nguyên.
std::string toLowerUtf8Vietnamese(const std::string &value);
std::string toUpperUtf8Vietnamese(const std::string &value);
// Chuẩn hóa NFC cho các tổ hợp chữ cái/dấu tiếng Việt mà V++ 1.0 cam kết hỗ
// trợ. Script ngoài tiếng Việt và byte malformed được giữ nguyên.
std::string normalizeUtf8VietnameseNfc(const std::string &value);
// Các phép substring thuộc contract tiếng Việt chuẩn hóa cả hai phía về NFC
// trước khi so khớp, nhờ đó chữ dựng sẵn và chữ tách dấu có cùng semantics.
std::size_t countVietnameseNormalizedSubstring(const std::string &value,
                                               const std::string &needle);
bool containsVietnameseNormalizedSubstring(const std::string &value,
                                           const std::string &needle);
std::string replaceVietnameseNormalizedAll(const std::string &value,
                                           const std::string &from,
                                           const std::string &to);

// Tách ascii words; hàm chia dữ liệu đầu vào theo quy tắc phân cách trong khi tôn trọng cấu trúc lồng nhau nếu có.
std::vector<std::string> splitAsciiWords(const std::string &value);
// Ghép with spaces; hàm nối các phần tử theo thứ tự bằng dấu phân cách quy định để tạo kết quả duy nhất.
std::string joinWithSpaces(const std::vector<std::string> &words);

// Đếm số code point UTF-8 trong chuỗi; hàm tiến qua dữ liệu theo độ dài từng sequence thay vì đếm trực tiếp số byte.
std::size_t utf8CodePointCount(const std::string &value);
// Kiểm tra toàn bộ chuỗi có phải UTF-8 hợp lệ hay không, bao gồm overlong,
// surrogate và code point vượt U+10FFFF.
bool isValidUtf8(const std::string &value) noexcept;
// Lấy code point UTF-8 theo chỉ số logic. Dữ liệu malformed giữ policy tương
// thích: byte lỗi được coi là một đơn vị thô thay vì bị bỏ hoặc thay thế.
std::optional<std::string> utf8CodePointAt(const std::string &value,
                                           std::size_t index);
// Cắt chuỗi theo chỉ số code point UTF-8. `start` và `count` đều tính theo
// code point; nếu `count` vượt phần còn lại thì kết quả được cắt tới cuối chuỗi.
std::string utf8CodePointSlice(const std::string &value,
                               std::size_t start,
                               std::size_t count);
// Đảo thứ tự code point UTF-8 mà không làm vỡ byte đa phần; hàm tách ranh giới từng code point rồi ghép chúng theo thứ tự ngược.
std::string reverseUtf8CodePoints(const std::string &value);

// Đếm số lần chuỗi con xuất hiện; hàm lặp `find` từ vị trí kế tiếp cho tới khi không còn kết quả.
std::size_t countSubstring(const std::string &value, const std::string &needle);
// Thay mọi lần xuất hiện của chuỗi nguồn bằng chuỗi đích; hàm tìm tuần tự và cập nhật vị trí sau mỗi lần thay để tránh lặp vô hạn.
std::string replaceAll(const std::string &value,
                       const std::string &from,
                       const std::string &to);
// Tìm từ ASCII dài nhất trong chuỗi; hàm tách theo ranh giới từ rồi giữ phần tử có độ dài lớn nhất.
std::string longestAsciiWord(const std::string &value);
// Viết hoa ký tự đầu từng từ theo bảng chữ cái tiếng Việt. Input được đưa về
// NFC tiếng Việt trước khi đổi case để chữ dựng sẵn và chữ tách dấu cho cùng kết quả.
std::string titleVietnameseWords(const std::string &value);
// Palindrome không phân biệt hoa/thường tiếng Việt. Contract cũ được giữ:
// không tự bỏ whitespace hay dấu câu.
bool isVietnameseCaseInsensitivePalindrome(const std::string &value);
// Kiểm tra anagram theo code point tiếng Việt sau NFC + lowercase; chỉ bỏ
// ASCII whitespace như contract stdlib hiện tại.
bool areVietnameseAnagrams(const std::string &left, const std::string &right);

// Dịch chữ cái ASCII theo khóa Caesar; hàm xoay ký tự trong miền A–Z/a–z và giữ nguyên ký tự ngoài bảng chữ cái.
std::string caesarAscii(const std::string &value, int shift);

} // namespace vietvm::core

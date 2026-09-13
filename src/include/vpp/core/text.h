#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace vietvm::core {

// Loại bỏ khoảng trắng ở đầu và cuối chuỗi; hàm tìm biên trái/phải đầu tiên không phải whitespace rồi trả lát cắt tương ứng.
std::string trim(const std::string &value);

// Chuyển lower ascii; hàm chuyển giá trị đầu vào sang kiểu/biểu diễn đích và trả kết quả đã chuẩn hóa.
std::string toLowerAscii(const std::string &value);

// Chuyển upper ascii; hàm chuyển giá trị đầu vào sang kiểu/biểu diễn đích và trả kết quả đã chuẩn hóa.
std::string toUpperAscii(const std::string &value);

// Tách ascii words; hàm chia dữ liệu đầu vào theo quy tắc phân cách trong khi tôn trọng cấu trúc lồng nhau nếu có.
std::vector<std::string> splitAsciiWords(const std::string &value);
// Ghép with spaces; hàm nối các phần tử theo thứ tự bằng dấu phân cách quy định để tạo kết quả duy nhất.
std::string joinWithSpaces(const std::vector<std::string> &words);

// Đếm số code point UTF-8 trong chuỗi; hàm tiến qua dữ liệu theo độ dài từng sequence thay vì đếm trực tiếp số byte.
std::size_t utf8CodePointCount(const std::string &value);
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
// Viết hoa ký tự đầu của từng từ ASCII; hàm theo dõi ranh giới từ và chỉ đổi chữ cái đầu tiên sau mỗi khoảng phân cách.
std::string titleAsciiWords(const std::string &value);
// Kiểm tra điều kiện của `isAsciiCaseInsensitivePalindrome`.
bool isAsciiCaseInsensitivePalindrome(const std::string &value);
// Kiểm tra hai chuỗi có phải hoán vị ký tự ASCII của nhau; hàm chuẩn hóa rồi so sánh tần suất ký tự của hai phía.
bool areAsciiAnagrams(const std::string &left, const std::string &right);

// Dịch chữ cái ASCII theo khóa Caesar; hàm xoay ký tự trong miền A–Z/a–z và giữ nguyên ký tự ngoài bảng chữ cái.
std::string caesarAscii(const std::string &value, int shift);

} // namespace vietvm::core

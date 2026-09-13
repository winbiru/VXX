#pragma once

#include <string>

namespace vietvm::helpers {

// Trích xuất đơn giản JSON chuỗi trường; hàm tìm phần dữ liệu cần thiết trong đầu vào và trả về lát cắt đã được chuẩn hóa.
std::string extractSimpleJsonStringField(const std::string &body, const std::string &key);
// Tách đường dẫn and query; hàm chia dữ liệu đầu vào theo quy tắc phân cách trong khi tôn trọng cấu trúc lồng nhau nếu có.
bool splitPathAndQuery(const std::string &target, std::string &path, std::string &query);
// Lấy giá trị một tham số query từ URL/path; hàm tách cặp khóa–giá trị và trả chuỗi rỗng khi khóa không tồn tại.
std::string queryParam(const std::string &query, const std::string &key);

} // namespace vietvm::helpers

#include "common/vm_native_http_helpers.h"

#include "common/vm_native_json_helpers.h"
#include "vpp/runtime/value.h"

namespace vietvm::helpers {

// Trích xuất đơn giản JSON chuỗi trường; hàm tìm phần dữ liệu cần thiết trong đầu vào và trả về lát cắt đã được chuẩn hóa.
std::string extractSimpleJsonStringField(const std::string &body, const std::string &key) {
    StackValue parsed;
    std::string error;
    if (!parseJson(body, parsed, error) || !std::holds_alternative<MapHandle>(parsed)) {
        return "";
    }
    const MapHandle &map = std::get<MapHandle>(parsed);
    if (map == nullptr) return "";
    const auto found = map->entries.find(key);
    if (found == map->entries.end() ||
        !std::holds_alternative<std::string>(found->second)) {
        return "";
    }
    return std::get<std::string>(found->second);
}

// Tách đường dẫn and query; hàm chia dữ liệu đầu vào theo quy tắc phân cách trong khi tôn trọng cấu trúc lồng nhau nếu có.
bool splitPathAndQuery(const std::string &target, std::string &path, std::string &query) {
    size_t q = target.find('?');
    if (q == std::string::npos) {
        path = target;
        query.clear();
        return true;
    }
    path = target.substr(0, q);
    query = target.substr(q + 1);
    return true;
}

// Lấy giá trị một tham số query từ URL/path; hàm tách cặp khóa–giá trị và trả chuỗi rỗng khi khóa không tồn tại.
std::string queryParam(const std::string &query, const std::string &key) {
    size_t start = 0;
    while (start <= query.size()) {
        size_t end = query.find('&', start);
        if (end == std::string::npos) end = query.size();
        std::string pair = query.substr(start, end - start);
        size_t eq = pair.find('=');
        if (eq != std::string::npos) {
            std::string k = pair.substr(0, eq);
            if (k == key) return pair.substr(eq + 1);
        } else if (pair == key) {
            return "";
        }
        if (end == query.size()) break;
        start = end + 1;
    }
    return "";
}

} // namespace vietvm::helpers

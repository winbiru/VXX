#include "common/vm_native_http_helpers.h"

#include <cctype>
#include <regex>

namespace vietvm::helpers {

std::string toLowerAscii(std::string s) {
    for (char &c : s) {
        unsigned char uc = static_cast<unsigned char>(c);
        c = static_cast<char>(std::tolower(uc));
    }
    return s;
}

std::string extractSimpleJsonStringField(const std::string &body, const std::string &key) {
    try {
        std::regex rgx("\\\"" + key + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
        std::smatch m;
        if (std::regex_search(body, m, rgx) && m.size() >= 2) {
            return m[1].str();
        }
    } catch (...) {
        return "";
    }
    return "";
}

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

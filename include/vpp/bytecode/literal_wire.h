#pragma once

#include <string>

namespace vietvm::bytecode {

// Compact StringPool representation for map/list literal fields.  Keeping the
// separators and escaping in the bytecode layer gives both compiler paths and
// the VM one wire-format contract.
inline constexpr char kLiteralRecordSeparator = '\x1e';
inline constexpr char kLiteralFieldSeparator = '\x1f';

inline std::string escapeLiteralWireField(const std::string &value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (unsigned char byte : value) {
        if (byte == '\\' || byte == '\n' || byte == '\r' || byte == '\t' ||
            byte == static_cast<unsigned char>(kLiteralRecordSeparator) ||
            byte == static_cast<unsigned char>(kLiteralFieldSeparator)) {
            escaped.push_back('\\');
            if (byte == '\n') escaped.push_back('n');
            else if (byte == '\r') escaped.push_back('r');
            else if (byte == '\t') escaped.push_back('t');
            else if (byte == static_cast<unsigned char>(kLiteralRecordSeparator)) {
                escaped.push_back('e');
            } else if (byte == static_cast<unsigned char>(kLiteralFieldSeparator)) {
                escaped.push_back('f');
            } else {
                escaped.push_back('\\');
            }
        } else {
            escaped.push_back(static_cast<char>(byte));
        }
    }
    return escaped;
}

inline std::string unescapeLiteralWireField(const std::string &value) {
    std::string decoded;
    decoded.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] == '\\' && index + 1 < value.size()) {
            const char next = value[index + 1];
            if (next == 'n') decoded.push_back('\n');
            else if (next == 'r') decoded.push_back('\r');
            else if (next == 't') decoded.push_back('\t');
            else if (next == 'e') decoded.push_back(kLiteralRecordSeparator);
            else if (next == 'f') decoded.push_back(kLiteralFieldSeparator);
            else decoded.push_back(next);
            ++index;
            continue;
        }
        decoded.push_back(value[index]);
    }
    return decoded;
}

} // namespace vietvm::bytecode

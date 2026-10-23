#pragma once

#include <string>
#include <string_view>

namespace vietvm::native {

// Keep simple ASCII names stable; encode UTF-8 or names with spaces into
// ASCII symbols. Reserve both mangling prefixes to prevent collisions.
// Malformed UTF-8 or control bytes yield an invalid empty symbol.
inline std::string nativeAbiSymbol(std::string_view name) {
    if (name.empty()) return {};
    bool ascii = true;
    bool simple = true;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const auto byte = static_cast<unsigned char>(name[i]);
        if (byte < 128) {
            if (byte < 32 || byte == 127) return {};
            const bool letter = (byte >= 'a' && byte <= 'z') ||
                                (byte >= 'A' && byte <= 'Z') || byte == '_';
            if (!letter && !(i > 0 && byte >= '0' && byte <= '9')) simple = false;
            continue;
        }
        ascii = false;
        // Decode only to validate UTF-8 scalar boundaries. Preserve the
        // original bytes in ABI symbols, independently of platform locale.
        unsigned trailing = 0;
        if (byte >= 0xc2 && byte <= 0xdf) trailing = 1;
        else if (byte >= 0xe0 && byte <= 0xef) trailing = 2;
        else if (byte >= 0xf0 && byte <= 0xf4) trailing = 3;
        else return {};
        if (i + trailing >= name.size()) return {};
        const auto second = static_cast<unsigned char>(name[i + 1]);
        if (second < 0x80 || second > 0xbf ||
            (byte == 0xe0 && second < 0xa0) ||
            (byte == 0xed && second >= 0xa0) ||
            (byte == 0xf0 && second < 0x90) ||
            (byte == 0xf4 && second > 0x8f)) return {};
        for (unsigned j = 2; j <= trailing; ++j) {
            const auto continuation = static_cast<unsigned char>(name[i + j]);
            if (continuation < 0x80 || continuation > 0xbf) return {};
        }
        i += trailing;
    }
    const bool reserved = name.compare(0, 6, "vpp_u_") == 0 ||
                          name.compare(0, 6, "vpp_a_") == 0;
    if (ascii && simple && !reserved) return std::string(name);

    const char *digits = "0123456789abcdef";
    std::string symbol = ascii && simple && reserved ? "vpp_a_" : "vpp_u_";
    symbol.reserve(symbol.size() + name.size() * 2);
    for (unsigned char byte : name) {
        symbol.push_back(digits[byte >> 4]);
        symbol.push_back(digits[byte & 15]);
    }
    return symbol;
}

} // namespace vietvm::native

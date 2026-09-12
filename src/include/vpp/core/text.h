#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace vietvm::core {

std::string trim(const std::string &value);

// Lowercase ASCII bytes while preserving UTF-8 bytes unchanged.
std::string toLowerAscii(const std::string &value);

// Uppercase ASCII bytes while preserving UTF-8 bytes unchanged.
std::string toUpperAscii(const std::string &value);

// Tokenize and join text using ASCII/locale-independent whitespace.  The
// functions intentionally preserve UTF-8 bytes and are shared by string and
// file native APIs.
std::vector<std::string> splitAsciiWords(const std::string &value);
std::string joinWithSpaces(const std::vector<std::string> &words);

// Shared byte-oriented string algorithms used by the native stdlib. These
// intentionally follow the existing ASCII/UTF-8-byte semantics until Unicode
// code-point handling is introduced as a separate contract.
std::size_t countSubstring(const std::string &value, const std::string &needle);
std::string replaceAll(const std::string &value,
                       const std::string &from,
                       const std::string &to);
std::string longestAsciiWord(const std::string &value);
std::string titleAsciiWords(const std::string &value);
bool isAsciiCaseInsensitivePalindrome(const std::string &value);
bool areAsciiAnagrams(const std::string &left, const std::string &right);

// Rotate ASCII Latin letters by `shift`; other bytes are preserved.
std::string caesarAscii(const std::string &value, int shift);

} // namespace vietvm::core

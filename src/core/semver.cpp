#include "vpp/core/semver.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>
#include <utility>

namespace vietvm::core {
namespace {

bool isAsciiDigit(char c) {
    return c >= '0' && c <= '9';
}

bool isIdentifierChar(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           isAsciiDigit(c) || c == '-';
}

bool allDigits(std::string_view value) {
    return !value.empty() &&
           std::all_of(value.begin(), value.end(), isAsciiDigit);
}

bool parseNumber(std::string_view text,
                 std::size_t &cursor,
                 int &value,
                 std::string *error) {
    const std::size_t begin = cursor;
    while (cursor < text.size() && isAsciiDigit(text[cursor])) ++cursor;
    if (begin == cursor) {
        if (error) *error = "thiếu thành phần số trong semantic version";
        return false;
    }
    if (cursor - begin > 1 && text[begin] == '0') {
        if (error) *error = "thành phần số semantic version không được có số 0 ở đầu";
        return false;
    }

    long long parsed = 0;
    for (std::size_t index = begin; index < cursor; ++index) {
        parsed = parsed * 10 + (text[index] - '0');
        if (parsed > std::numeric_limits<int>::max()) {
            if (error) *error = "thành phần semantic version vượt phạm vi số nguyên";
            return false;
        }
    }
    value = static_cast<int>(parsed);
    return true;
}

bool parseIdentifiers(std::string_view text,
                      std::size_t &cursor,
                      std::vector<std::string> &out,
                      bool rejectNumericLeadingZero,
                      std::string *error) {
    while (cursor < text.size()) {
        const std::size_t begin = cursor;
        while (cursor < text.size() && isIdentifierChar(text[cursor])) ++cursor;
        if (begin == cursor) {
            if (error) *error = "identifier semantic version rỗng hoặc chứa ký tự không hợp lệ";
            return false;
        }
        std::string identifier(text.substr(begin, cursor - begin));
        if (rejectNumericLeadingZero && allDigits(identifier) &&
            identifier.size() > 1 && identifier.front() == '0') {
            if (error) *error = "identifier prerelease dạng số không được có số 0 ở đầu";
            return false;
        }
        out.push_back(std::move(identifier));
        if (cursor == text.size() || text[cursor] == '+') return true;
        if (text[cursor] != '.') return true;
        ++cursor;
        if (cursor == text.size()) {
            if (error) *error = "identifier semantic version không được kết thúc bằng dấu chấm";
            return false;
        }
    }
    return !out.empty();
}

int compareIdentifier(const std::string &left, const std::string &right) noexcept {
    const bool leftNumeric = allDigits(left);
    const bool rightNumeric = allDigits(right);
    if (leftNumeric && rightNumeric) {
        if (left.size() != right.size()) return left.size() < right.size() ? -1 : 1;
        if (left == right) return 0;
        return left < right ? -1 : 1;
    }
    if (leftNumeric != rightNumeric) return leftNumeric ? -1 : 1;
    if (left == right) return 0;
    return left < right ? -1 : 1;
}

void setError(std::string *error, std::string message) {
    if (error) *error = std::move(message);
}

std::vector<std::string> splitWhitespace(std::string_view text) {
    std::vector<std::string> tokens;
    std::size_t cursor = 0;
    while (cursor < text.size()) {
        while (cursor < text.size() &&
               std::isspace(static_cast<unsigned char>(text[cursor])) != 0) {
            ++cursor;
        }
        if (cursor == text.size()) break;
        const std::size_t begin = cursor;
        while (cursor < text.size() &&
               std::isspace(static_cast<unsigned char>(text[cursor])) == 0) {
            ++cursor;
        }
        tokens.emplace_back(text.substr(begin, cursor - begin));
    }
    return tokens;
}

VersionComparator makeComparator(VersionComparatorOperator op,
                                 SemanticVersion version) {
    return VersionComparator{op, std::move(version)};
}

SemanticVersion caretUpperBound(const SemanticVersion &version) {
    SemanticVersion upper;
    if (version.major > 0) {
        upper.major = version.major + 1;
    } else if (version.minor > 0) {
        upper.minor = version.minor + 1;
    } else {
        upper.patch = version.patch + 1;
    }
    return upper;
}

SemanticVersion tildeUpperBound(const SemanticVersion &version) {
    SemanticVersion upper;
    upper.major = version.major;
    upper.minor = version.minor + 1;
    return upper;
}

bool compareMatches(const SemanticVersion &version,
                    const VersionComparator &comparator) noexcept {
    const int ordering = compareSemanticVersion(version, comparator.version);
    switch (comparator.op) {
        case VersionComparatorOperator::Equal: return ordering == 0;
        case VersionComparatorOperator::Greater: return ordering > 0;
        case VersionComparatorOperator::GreaterOrEqual: return ordering >= 0;
        case VersionComparatorOperator::Less: return ordering < 0;
        case VersionComparatorOperator::LessOrEqual: return ordering <= 0;
    }
    return false;
}

} // namespace

std::optional<SemanticVersion> SemanticVersion::parse(
    std::string_view text,
    std::string *error) {
    if (error) error->clear();
    if (text.empty()) {
        setError(error, "semantic version rỗng");
        return std::nullopt;
    }

    SemanticVersion version;
    std::size_t cursor = 0;
    if (!parseNumber(text, cursor, version.major, error)) return std::nullopt;
    if (cursor >= text.size() || text[cursor++] != '.') {
        setError(error, "semantic version phải có dạng major.minor.patch");
        return std::nullopt;
    }
    if (!parseNumber(text, cursor, version.minor, error)) return std::nullopt;
    if (cursor >= text.size() || text[cursor++] != '.') {
        setError(error, "semantic version phải có dạng major.minor.patch");
        return std::nullopt;
    }
    if (!parseNumber(text, cursor, version.patch, error)) return std::nullopt;

    if (cursor < text.size() && text[cursor] == '-') {
        ++cursor;
        if (!parseIdentifiers(text, cursor, version.prerelease, true, error)) {
            return std::nullopt;
        }
    }
    if (cursor < text.size() && text[cursor] == '+') {
        ++cursor;
        if (!parseIdentifiers(text, cursor, version.build, false, error)) {
            return std::nullopt;
        }
    }
    if (cursor != text.size()) {
        setError(error, "semantic version chứa phần dư không hợp lệ");
        return std::nullopt;
    }
    return version;
}

std::string SemanticVersion::toString() const {
    std::ostringstream out;
    out << major << '.' << minor << '.' << patch;
    if (!prerelease.empty()) {
        out << '-';
        for (std::size_t index = 0; index < prerelease.size(); ++index) {
            if (index != 0) out << '.';
            out << prerelease[index];
        }
    }
    if (!build.empty()) {
        out << '+';
        for (std::size_t index = 0; index < build.size(); ++index) {
            if (index != 0) out << '.';
            out << build[index];
        }
    }
    return out.str();
}

int compareSemanticVersion(const SemanticVersion &left,
                           const SemanticVersion &right) noexcept {
    if (left.major != right.major) return left.major < right.major ? -1 : 1;
    if (left.minor != right.minor) return left.minor < right.minor ? -1 : 1;
    if (left.patch != right.patch) return left.patch < right.patch ? -1 : 1;
    if (left.prerelease.empty() != right.prerelease.empty()) {
        return left.prerelease.empty() ? 1 : -1;
    }
    const std::size_t shared = std::min(left.prerelease.size(), right.prerelease.size());
    for (std::size_t index = 0; index < shared; ++index) {
        const int ordering = compareIdentifier(left.prerelease[index], right.prerelease[index]);
        if (ordering != 0) return ordering;
    }
    if (left.prerelease.size() == right.prerelease.size()) return 0;
    return left.prerelease.size() < right.prerelease.size() ? -1 : 1;
}

std::optional<VersionRange> VersionRange::parse(
    std::string_view text,
    std::string *error) {
    if (error) error->clear();
    VersionRange range;
    const std::vector<std::string> tokens = splitWhitespace(text);
    if (tokens.empty() || (tokens.size() == 1 && tokens.front() == "*")) {
        range.any_ = true;
        return range;
    }

    for (const std::string &token : tokens) {
        if (token == "*") {
            setError(error, "'*' chỉ hợp lệ khi đứng một mình trong version range");
            return std::nullopt;
        }
        if (token.find("||") != std::string::npos) {
            setError(error, "OR version range chưa được hỗ trợ trong Package 0.9");
            return std::nullopt;
        }

        if (token.front() == '^' || token.front() == '~') {
            const char prefix = token.front();
            const auto parsed = SemanticVersion::parse(
                std::string_view(token).substr(1), error);
            if (!parsed.has_value()) return std::nullopt;
            range.comparators_.push_back(makeComparator(
                VersionComparatorOperator::GreaterOrEqual, *parsed));
            range.comparators_.push_back(makeComparator(
                VersionComparatorOperator::Less,
                prefix == '^' ? caretUpperBound(*parsed) : tildeUpperBound(*parsed)));
            continue;
        }

        VersionComparatorOperator op = VersionComparatorOperator::Equal;
        std::size_t versionOffset = 0;
        if (token.rfind(">=", 0) == 0) {
            op = VersionComparatorOperator::GreaterOrEqual;
            versionOffset = 2;
        } else if (token.rfind("<=", 0) == 0) {
            op = VersionComparatorOperator::LessOrEqual;
            versionOffset = 2;
        } else if (token.front() == '>') {
            op = VersionComparatorOperator::Greater;
            versionOffset = 1;
        } else if (token.front() == '<') {
            op = VersionComparatorOperator::Less;
            versionOffset = 1;
        } else if (token.front() == '=') {
            versionOffset = 1;
        }

        if (versionOffset == token.size()) {
            setError(error, "version comparator thiếu phiên bản");
            return std::nullopt;
        }
        const auto parsed = SemanticVersion::parse(
            std::string_view(token).substr(versionOffset), error);
        if (!parsed.has_value()) return std::nullopt;
        range.comparators_.push_back(makeComparator(op, *parsed));
    }

    return range;
}

bool VersionRange::matches(const SemanticVersion &version) const noexcept {
    if (any_) return true;
    return std::all_of(
        comparators_.begin(), comparators_.end(),
        [&](const VersionComparator &comparator) {
            return compareMatches(version, comparator);
        });
}

} // namespace vietvm::core

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vietvm::core {

// Semantic Versioning 2.0 value used by the package layer. Build metadata does
// not participate in precedence, while prerelease identifiers follow SemVer.
struct SemanticVersion {
    int major = 0;
    int minor = 0;
    int patch = 0;
    std::vector<std::string> prerelease;
    std::vector<std::string> build;

    static std::optional<SemanticVersion> parse(
        std::string_view text,
        std::string *error = nullptr);

    std::string toString() const;
};

int compareSemanticVersion(const SemanticVersion &left,
                           const SemanticVersion &right) noexcept;

inline bool operator==(const SemanticVersion &left,
                       const SemanticVersion &right) noexcept {
    return compareSemanticVersion(left, right) == 0;
}

inline bool operator<(const SemanticVersion &left,
                      const SemanticVersion &right) noexcept {
    return compareSemanticVersion(left, right) < 0;
}

enum class VersionComparatorOperator {
    Equal,
    Greater,
    GreaterOrEqual,
    Less,
    LessOrEqual,
};

struct VersionComparator {
    VersionComparatorOperator op = VersionComparatorOperator::Equal;
    SemanticVersion version;
};

// Version range MVP for Package 0.9. Supported forms:
//   *, 1.2.3, ^1.2.3, ~1.2.3, >=1.2.0 <2.0.0
// Whitespace-separated comparators are ANDed. OR ranges are intentionally not
// accepted until dependency conflict policy is defined.
class VersionRange {
public:
    static std::optional<VersionRange> parse(
        std::string_view text,
        std::string *error = nullptr);

    bool matches(const SemanticVersion &version) const noexcept;
    bool isAny() const noexcept { return any_; }
    const std::vector<VersionComparator> &comparators() const noexcept {
        return comparators_;
    }

private:
    bool any_ = false;
    std::vector<VersionComparator> comparators_;
};

} // namespace vietvm::core

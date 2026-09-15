#include "vpp/core/semver.h"

#include <iostream>
#include <string>

namespace {

using vietvm::core::SemanticVersion;
using vietvm::core::VersionRange;

int failures = 0;

void expect(bool condition, const std::string &message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

SemanticVersion version(const std::string &text) {
    std::string error;
    const auto parsed = SemanticVersion::parse(text, &error);
    if (!parsed.has_value()) {
        std::cerr << "FAIL: cannot parse version " << text << ": " << error << '\n';
        ++failures;
        return {};
    }
    return *parsed;
}

VersionRange range(const std::string &text) {
    std::string error;
    const auto parsed = VersionRange::parse(text, &error);
    if (!parsed.has_value()) {
        std::cerr << "FAIL: cannot parse range " << text << ": " << error << '\n';
        ++failures;
        return {};
    }
    return *parsed;
}

void testSemverParsingAndFormatting() {
    std::string error;
    const auto parsed = SemanticVersion::parse("1.2.3-alpha.1+build.9", &error);
    expect(parsed.has_value(), "valid SemVer 2.0 value parses");
    if (parsed.has_value()) {
        expect(parsed->major == 1 && parsed->minor == 2 && parsed->patch == 3,
               "major/minor/patch are preserved");
        expect(parsed->prerelease.size() == 2 && parsed->build.size() == 2,
               "prerelease and build identifiers are preserved");
        expect(parsed->toString() == "1.2.3-alpha.1+build.9",
               "semantic version round-trips to canonical text");
    }

    expect(!SemanticVersion::parse("1.2", &error).has_value(),
           "missing patch component is rejected");
    expect(!SemanticVersion::parse("01.2.3", &error).has_value(),
           "leading zero in numeric core component is rejected");
    expect(!SemanticVersion::parse("1.2.3-alpha.01", &error).has_value(),
           "leading zero in numeric prerelease identifier is rejected");
}

void testSemverPrecedence() {
    expect(version("1.0.0-alpha") < version("1.0.0-alpha.1"),
           "shorter equal prerelease prefix has lower precedence");
    expect(version("1.0.0-alpha.1") < version("1.0.0-alpha.beta"),
           "numeric prerelease identifier has lower precedence than text");
    expect(version("1.0.0-beta.2") < version("1.0.0-beta.11"),
           "numeric prerelease identifiers compare numerically");
    expect(version("1.0.0-rc.1") < version("1.0.0"),
           "release has higher precedence than prerelease");
    expect(version("1.2.3+build.1") == version("1.2.3+build.99"),
           "build metadata does not affect precedence");
}

void testVersionRanges() {
    expect(range("*").matches(version("99.0.0")),
           "wildcard range matches any version");
    expect(range("1.2.3").matches(version("1.2.3")) &&
               !range("1.2.3").matches(version("1.2.4")),
           "exact range only matches the exact version");
    expect(range("^1.2.3").matches(version("1.9.9")) &&
               !range("^1.2.3").matches(version("2.0.0")),
           "caret range advances to next major for major > 0");
    expect(range("^0.2.3").matches(version("0.2.9")) &&
               !range("^0.2.3").matches(version("0.3.0")),
           "caret range advances to next minor for 0.x");
    expect(range("^0.0.3").matches(version("0.0.3")) &&
               !range("^0.0.3").matches(version("0.0.4")),
           "caret range advances to next patch for 0.0.x");
    expect(range("~1.2.3").matches(version("1.2.99")) &&
               !range("~1.2.3").matches(version("1.3.0")),
           "tilde range stays within the minor version");
    expect(range(">=1.2.0 <2.0.0").matches(version("1.8.4")) &&
               !range(">=1.2.0 <2.0.0").matches(version("2.0.0")),
           "comparator range ANDs all constraints");

    std::string error;
    expect(!VersionRange::parse("^", &error).has_value(),
           "range missing a version is rejected");
    expect(!VersionRange::parse(">=1.0.0 || <2.0.0", &error).has_value(),
           "unsupported OR range is rejected explicitly");
}

} // namespace

int main() {
    testSemverParsingAndFormatting();
    testSemverPrecedence();
    testVersionRanges();

    if (failures != 0) {
        std::cerr << failures << " semver test(s) failed\n";
        return 1;
    }
    std::cout << "semver tests passed\n";
    return 0;
}

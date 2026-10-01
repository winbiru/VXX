#include "vpp/core/package_manifest.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "vpp/core/project_layout.h"
#include "vpp/core/message_constants.h"
#include "vpp/core/semver.h"

namespace vietvm::core {
namespace {

struct JsonValue {
    enum class Kind { Null, Boolean, Number, String, Array, Object };

    Kind kind = Kind::Null;
    bool boolean = false;
    std::string text;
    std::vector<JsonValue> array;
    std::map<std::string, JsonValue> object;
};

class JsonParser {
public:
    JsonParser(std::string_view input, std::string_view documentName)
        : input_(input), documentName_(documentName) {}

    JsonValue parse() {
        skipWhitespace();
        JsonValue value = parseValue();
        skipWhitespace();
        if (cursor_ != input_.size()) fail(messages::messageText(messages::kPackageJsonTrailingData));
        return value;
    }

private:
    [[noreturn]] void fail(const std::string &message) const {
        const std::string offset = std::to_string(cursor_);
        throw std::runtime_error(messages::formatMessage(
            messages::kPackageJsonInvalidAtOffset,
            {documentName_, offset, message}));
    }

    void skipWhitespace() {
        while (cursor_ < input_.size() &&
               std::isspace(static_cast<unsigned char>(input_[cursor_])) != 0) {
            ++cursor_;
        }
    }

    bool consume(char expected) {
        skipWhitespace();
        if (cursor_ >= input_.size() || input_[cursor_] != expected) return false;
        ++cursor_;
        return true;
    }

    char take() {
        if (cursor_ >= input_.size()) fail(messages::messageText(messages::kPackageJsonUnexpectedEnd));
        return input_[cursor_++];
    }

    static int hexValue(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    unsigned parseHex4() {
        unsigned value = 0;
        for (int count = 0; count < 4; ++count) {
            const int digit = hexValue(take());
            if (digit < 0) fail(messages::messageText(messages::kPackageJsonUnicodeEscapeInvalid));
            value = value * 16u + static_cast<unsigned>(digit);
        }
        return value;
    }

    static void appendUtf8(std::string &output, unsigned codePoint) {
        if (codePoint <= 0x7f) {
            output.push_back(static_cast<char>(codePoint));
        } else if (codePoint <= 0x7ff) {
            output.push_back(static_cast<char>(0xc0 | (codePoint >> 6)));
            output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
        } else if (codePoint <= 0xffff) {
            output.push_back(static_cast<char>(0xe0 | (codePoint >> 12)));
            output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
        } else {
            output.push_back(static_cast<char>(0xf0 | (codePoint >> 18)));
            output.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
        }
    }

    std::string parseString() {
        skipWhitespace();
        if (take() != '"') fail(messages::messageText(messages::kPackageJsonStringMustStartWithQuote));
        std::string output;
        while (cursor_ < input_.size()) {
            const char c = take();
            if (c == '"') return output;
            if (static_cast<unsigned char>(c) < 0x20) {
                fail(messages::messageText(messages::kPackageJsonRawControlCharacter));
            }
            if (c != '\\') {
                output.push_back(c);
                continue;
            }
            const char escape = take();
            switch (escape) {
                case '"': output.push_back('"'); break;
                case '\\': output.push_back('\\'); break;
                case '/': output.push_back('/'); break;
                case 'b': output.push_back('\b'); break;
                case 'f': output.push_back('\f'); break;
                case 'n': output.push_back('\n'); break;
                case 'r': output.push_back('\r'); break;
                case 't': output.push_back('\t'); break;
                case 'u': {
                    unsigned codePoint = parseHex4();
                    if (codePoint >= 0xd800 && codePoint <= 0xdbff) {
                        if (take() != '\\' || take() != 'u') {
                            fail(messages::messageText(messages::kPackageJsonHighSurrogateMissingLow));
                        }
                        const unsigned low = parseHex4();
                        if (low < 0xdc00 || low > 0xdfff) {
                            fail(messages::messageText(messages::kPackageJsonLowSurrogateInvalid));
                        }
                        codePoint = 0x10000u +
                                    ((codePoint - 0xd800u) << 10u) +
                                    (low - 0xdc00u);
                    } else if (codePoint >= 0xdc00 && codePoint <= 0xdfff) {
                        fail(messages::messageText(messages::kPackageJsonLowSurrogateWithoutHigh));
                    }
                    appendUtf8(output, codePoint);
                    break;
                }
                default: fail(messages::messageText(messages::kPackageJsonStringEscapeInvalid));
            }
        }
        fail(messages::messageText(messages::kPackageJsonStringUnterminated));
    }

    JsonValue parseNumber() {
        const std::size_t begin = cursor_;
        if (input_[cursor_] == '-') ++cursor_;
        if (cursor_ >= input_.size() ||
            !std::isdigit(static_cast<unsigned char>(input_[cursor_]))) {
            fail(messages::messageText(messages::kPackageJsonNumberInvalid));
        }
        if (input_[cursor_] == '0') {
            ++cursor_;
        } else {
            while (cursor_ < input_.size() &&
                   std::isdigit(static_cast<unsigned char>(input_[cursor_]))) {
                ++cursor_;
            }
        }
        if (cursor_ < input_.size() && input_[cursor_] == '.') {
            ++cursor_;
            if (cursor_ >= input_.size() ||
                !std::isdigit(static_cast<unsigned char>(input_[cursor_]))) {
                fail(messages::messageText(messages::kPackageJsonFractionInvalid));
            }
            while (cursor_ < input_.size() &&
                   std::isdigit(static_cast<unsigned char>(input_[cursor_]))) {
                ++cursor_;
            }
        }
        if (cursor_ < input_.size() &&
            (input_[cursor_] == 'e' || input_[cursor_] == 'E')) {
            ++cursor_;
            if (cursor_ < input_.size() &&
                (input_[cursor_] == '+' || input_[cursor_] == '-')) {
                ++cursor_;
            }
            if (cursor_ >= input_.size() ||
                !std::isdigit(static_cast<unsigned char>(input_[cursor_]))) {
                fail(messages::messageText(messages::kPackageJsonExponentInvalid));
            }
            while (cursor_ < input_.size() &&
                   std::isdigit(static_cast<unsigned char>(input_[cursor_]))) {
                ++cursor_;
            }
        }
        JsonValue value;
        value.kind = JsonValue::Kind::Number;
        value.text = std::string(input_.substr(begin, cursor_ - begin));
        return value;
    }

    JsonValue parseArray() {
        JsonValue value;
        value.kind = JsonValue::Kind::Array;
        if (!consume('[')) fail(messages::messageText(messages::kPackageJsonArrayMustStart));
        if (consume(']')) return value;
        while (true) {
            value.array.push_back(parseValue());
            if (consume(']')) return value;
            if (!consume(',')) fail(messages::messageText(messages::kPackageJsonArrayCommaMissing));
        }
    }

    JsonValue parseObject() {
        JsonValue value;
        value.kind = JsonValue::Kind::Object;
        if (!consume('{')) fail(messages::messageText(messages::kPackageJsonObjectMustStart));
        if (consume('}')) return value;
        while (true) {
            skipWhitespace();
            if (cursor_ >= input_.size() || input_[cursor_] != '"') {
                fail(messages::messageText(messages::kPackageJsonObjectKeyMustBeString));
            }
            std::string key = parseString();
            if (!consume(':')) fail(messages::messageText(messages::kPackageJsonObjectColonMissing));
            auto inserted = value.object.emplace(std::move(key), parseValue());
            if (!inserted.second) fail(messages::messageText(messages::kPackageJsonObjectDuplicateKey));
            if (consume('}')) return value;
            if (!consume(',')) fail(messages::messageText(messages::kPackageJsonObjectCommaMissing));
        }
    }

    JsonValue parseLiteral(std::string_view literal,
                           JsonValue::Kind kind,
                           bool boolean = false) {
        if (input_.substr(cursor_, literal.size()) != literal) {
            fail(messages::messageText(messages::kPackageJsonLiteralInvalid));
        }
        cursor_ += literal.size();
        JsonValue value;
        value.kind = kind;
        value.boolean = boolean;
        return value;
    }

    JsonValue parseValue() {
        skipWhitespace();
        if (cursor_ >= input_.size()) fail(messages::messageText(messages::kPackageJsonValueMissing));
        switch (input_[cursor_]) {
            case '{': return parseObject();
            case '[': return parseArray();
            case '"': {
                JsonValue value;
                value.kind = JsonValue::Kind::String;
                value.text = parseString();
                return value;
            }
            case 't': return parseLiteral("true", JsonValue::Kind::Boolean, true);
            case 'f': return parseLiteral("false", JsonValue::Kind::Boolean, false);
            case 'n': return parseLiteral("null", JsonValue::Kind::Null);
            default:
                if (input_[cursor_] == '-' ||
                    std::isdigit(static_cast<unsigned char>(input_[cursor_]))) {
                    return parseNumber();
                }
                fail(messages::messageText(messages::kPackageJsonValueInvalid));
        }
    }

    std::string_view input_;
    std::string_view documentName_;
    std::size_t cursor_ = 0;
};

const JsonValue *member(const JsonValue &object, const std::string &name) {
    if (object.kind != JsonValue::Kind::Object) return nullptr;
    const auto found = object.object.find(name);
    return found == object.object.end() ? nullptr : &found->second;
}

std::string requireString(const JsonValue &object, const std::string &name) {
    const JsonValue *value = member(object, name);
    if (value == nullptr || value->kind != JsonValue::Kind::String) {
        throw std::runtime_error(messages::formatMessage(
            messages::kPackageJsonStringFieldRequired, {name}));
    }
    return value->text;
}

std::string optionalString(const JsonValue &object,
                           const std::string &name,
                           std::string fallback) {
    const JsonValue *value = member(object, name);
    if (value == nullptr) return fallback;
    if (value->kind != JsonValue::Kind::String) {
        throw std::runtime_error(messages::formatMessage(
            messages::kPackageJsonStringFieldRequired, {name}));
    }
    return value->text;
}

std::string jsonEscape(const std::string &input) {
    std::string output;
    output.reserve(input.size() + 8);
    for (unsigned char c : input) {
        switch (c) {
            case '"': output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\b': output += "\\b"; break;
            case '\f': output += "\\f"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default:
                if (c < 0x20) {
                    static const char hex[] = "0123456789abcdef";
                    output += "\\u00";
                    output.push_back(hex[(c >> 4) & 0x0f]);
                    output.push_back(hex[c & 0x0f]);
                } else {
                    output.push_back(static_cast<char>(c));
                }
        }
    }
    return output;
}

PackageDependencySpec dependencyFromJson(const JsonValue &value) {
    if (value.kind != JsonValue::Kind::Object) {
        throw std::runtime_error(std::string(messages::kPackageJsonDependencyMustBeObject));
    }
    PackageDependencySpec dependency;
    dependency.name = requireString(value, "name");
    dependency.versionRange = optionalString(value, "version", "*");
    dependency.sourceKind = parsePackageSourceKind(
        optionalString(value, "source", "registry"));
    dependency.location = optionalString(value, "location", "");
    if (dependency.sourceKind == PackageSourceKind::Git) {
        dependency.reference = optionalString(value, "ref", "HEAD");
    }
    return dependency;
}

PackageLockEntry lockEntryFromJson(const JsonValue &value) {
    if (value.kind != JsonValue::Kind::Object) {
        throw std::runtime_error(std::string(messages::kPackageLockEntryMustBeObject));
    }
    PackageLockEntry entry;
    entry.name = requireString(value, "name");
    entry.version = requireString(value, "version");
    entry.sourceKind = parsePackageSourceKind(requireString(value, "source"));
    entry.location = optionalString(value, "location", "");
    entry.resolvedPath = requireString(value, "resolved");
    entry.fingerprint = requireString(value, "fingerprint");
    entry.revision = optionalString(value, "revision", "");
    if (!isValidPackageName(entry.name)) {
        throw std::runtime_error(messages::formatMessage(
            messages::kPackageLockNameInvalid, {entry.name}));
    }
    std::string versionError;
    if (!SemanticVersion::parse(entry.version, &versionError).has_value()) {
        throw std::runtime_error(messages::formatMessage(
            messages::kPackageLockVersionInvalid, {entry.name, versionError}));
    }
    if (entry.resolvedPath.empty() || entry.fingerprint.empty()) {
        throw std::runtime_error(messages::formatMessage(
            messages::kPackageLockResolvedFingerprintMissing, {entry.name}));
    }
    if (entry.sourceKind == PackageSourceKind::Git && entry.revision.empty()) {
        throw std::runtime_error(messages::formatMessage(
            messages::kPackageLockGitRevisionMissing, {entry.name}));
    }
    return entry;
}

void hashByte(std::uint64_t &hash, unsigned char byte) {
    constexpr std::uint64_t kFnvPrime = 1099511628211ull;
    hash ^= static_cast<std::uint64_t>(byte);
    hash *= kFnvPrime;
}

void hashText(std::uint64_t &hash, const std::string &text) {
    for (unsigned char byte : text) hashByte(hash, byte);
    hashByte(hash, 0);
}

bool normalizeLineEndingsForFingerprint(const std::filesystem::path &file) {
    std::string extension = file.extension().u8string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".vi" || extension == ".vvm" || extension == ".json" ||
           extension == ".md" || extension == ".txt" || extension == ".toml" ||
           extension == ".yaml" || extension == ".yml" || extension == ".xml" ||
           extension == ".csv" || extension == ".tsv";
}

} // namespace

std::string packageSourceKindName(PackageSourceKind kind) {
    switch (kind) {
        case PackageSourceKind::Path: return "path";
        case PackageSourceKind::Git: return "git";
        case PackageSourceKind::Registry: return "registry";
    }
    throw std::runtime_error(std::string(messages::kPackageSourceKindInvalid));
}

PackageSourceKind parsePackageSourceKind(const std::string &text) {
    if (text == "path") return PackageSourceKind::Path;
    if (text == "git") return PackageSourceKind::Git;
    if (text == "registry") return PackageSourceKind::Registry;
    throw std::runtime_error(messages::formatMessage(
        messages::kPackageJsonSourceInvalid, {text}));
}

bool isValidPackageName(std::string_view name) noexcept {
    if (name.empty() || name == "." || name == "..") return false;
    for (unsigned char c : name) {
        if (c < 0x20 || c == 0x7f) return false;
        switch (c) {
            case '/':
            case '\\':
            case ':':
            case '*':
            case '?':
            case '"':
            case '<':
            case '>':
            case '|':
                return false;
            default:
                break;
        }
    }
    return true;
}

void validateProjectManifest(const ProjectManifest &manifest) {
    if (manifest.schema != kProjectManifestSchemaVersion) {
        const std::string schema = std::to_string(manifest.schema);
        throw std::runtime_error(messages::formatMessage(
            messages::kPackageJsonSchemaUnsupported, {schema}));
    }
    if (!isValidPackageName(manifest.name)) {
        throw std::runtime_error(messages::formatMessage(
            messages::kPackageJsonNameInvalid, {manifest.name}));
    }
    std::string versionError;
    if (!SemanticVersion::parse(manifest.version, &versionError).has_value()) {
        throw std::runtime_error(messages::formatMessage(
            messages::kPackageJsonVersionInvalid, {versionError}));
    }

    std::vector<std::string> names;
    names.reserve(manifest.dependencies.size());
    for (const PackageDependencySpec &dependency : manifest.dependencies) {
        if (!isValidPackageName(dependency.name)) {
            throw std::runtime_error(messages::formatMessage(
                messages::kPackageJsonDependencyNameInvalid, {dependency.name}));
        }
        std::string rangeError;
        if (!VersionRange::parse(dependency.versionRange, &rangeError).has_value()) {
            throw std::runtime_error(messages::formatMessage(
                messages::kPackageJsonDependencyRangeInvalid,
                {dependency.name, rangeError}));
        }
        if ((dependency.sourceKind == PackageSourceKind::Path ||
             dependency.sourceKind == PackageSourceKind::Git) &&
            dependency.location.empty()) {
            const std::string sourceKind = packageSourceKindName(dependency.sourceKind);
            throw std::runtime_error(messages::formatMessage(
                messages::kPackageJsonDependencyLocationMissing,
                {dependency.name, sourceKind}));
        }
        if (dependency.sourceKind == PackageSourceKind::Git &&
            dependency.reference.empty()) {
            throw std::runtime_error(messages::formatMessage(
                messages::kPackageJsonGitRefMissing, {dependency.name}));
        }
        names.push_back(dependency.name);
    }
    std::sort(names.begin(), names.end());
    const auto duplicate = std::adjacent_find(names.begin(), names.end());
    if (duplicate != names.end()) {
        throw std::runtime_error(messages::formatMessage(
            messages::kPackageJsonDependencyDuplicate, {*duplicate}));
    }
}

ProjectManifest readProjectManifest(const std::filesystem::path &path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        throw std::runtime_error(messages::formatMessage(
            messages::kPackageJsonOpenFailed, {path.u8string()}));
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    const std::string json = buffer.str();
    const JsonValue root = JsonParser(json, "vpp.json").parse();
    if (root.kind != JsonValue::Kind::Object) {
        throw std::runtime_error(std::string(messages::kPackageJsonRootMustBeObject));
    }

    ProjectManifest manifest;
    if (const JsonValue *schema = member(root, "schema")) {
        if (schema->kind != JsonValue::Kind::Number) {
            throw std::runtime_error(std::string(messages::kPackageJsonSchemaMustBeInteger));
        }
        try {
            std::size_t consumed = 0;
            manifest.schema = std::stoi(schema->text, &consumed);
            if (consumed != schema->text.size()) {
                throw std::invalid_argument(std::string(messages::kPackageSchemaParseSentinel));
            }
        } catch (...) {
            throw std::runtime_error(std::string(messages::kPackageJsonSchemaMustBeInteger));
        }
    }
    manifest.name = requireString(root, "name");
    manifest.version = optionalString(root, "version", "0.1.0");

    if (const JsonValue *dependencies = member(root, "dependencies")) {
        if (dependencies->kind != JsonValue::Kind::Array) {
            throw std::runtime_error(std::string(messages::kPackageJsonDependenciesMustBeArray));
        }
        for (const JsonValue &dependency : dependencies->array) {
            manifest.dependencies.push_back(dependencyFromJson(dependency));
        }
    } else if (const JsonValue *legacy = member(root, u8"gói")) {
        if (legacy->kind != JsonValue::Kind::Array) {
            throw std::runtime_error(std::string(messages::kPackageJsonLegacyPackagesMustBeArray));
        }
        for (const JsonValue &package : legacy->array) {
            if (package.kind != JsonValue::Kind::String) {
                throw std::runtime_error(std::string(messages::kPackageJsonLegacyPackageMustBeString));
            }
            PackageDependencySpec dependency;
            dependency.name = package.text;
            dependency.versionRange = "*";
            dependency.sourceKind = PackageSourceKind::Path;
            dependency.location =
                (utf8Path(kPrimaryPackageDirectory) / utf8Path(package.text))
                    .generic_u8string();
            manifest.dependencies.push_back(std::move(dependency));
        }
    }

    validateProjectManifest(manifest);
    return manifest;
}

void writeProjectManifest(const std::filesystem::path &path,
                          const ProjectManifest &manifest) {
    validateProjectManifest(manifest);
    std::vector<PackageDependencySpec> dependencies = manifest.dependencies;
    std::sort(dependencies.begin(), dependencies.end(),
              [](const PackageDependencySpec &left,
                 const PackageDependencySpec &right) {
                  return left.name < right.name;
              });

    std::ofstream output(path);
    if (!output.is_open()) {
        throw std::runtime_error(messages::formatMessage(
            messages::kPackageJsonWriteFailed, {path.u8string()}));
    }
    output << "{\n"
           << "  \"schema\": " << manifest.schema << ",\n"
           << "  \"name\": \"" << jsonEscape(manifest.name) << "\",\n"
           << "  \"version\": \"" << jsonEscape(manifest.version) << "\",\n"
           << "  \"dependencies\": [";
    if (!dependencies.empty()) output << '\n';
    for (std::size_t index = 0; index < dependencies.size(); ++index) {
        const PackageDependencySpec &dependency = dependencies[index];
        output << "    {\"name\": \"" << jsonEscape(dependency.name)
               << "\", \"version\": \"" << jsonEscape(dependency.versionRange)
               << "\", \"source\": \""
               << packageSourceKindName(dependency.sourceKind)
               << "\", \"location\": \"" << jsonEscape(dependency.location)
               << "\"";
        if (dependency.sourceKind == PackageSourceKind::Git) {
            output << ", \"ref\": \"" << jsonEscape(dependency.reference)
                   << "\"";
        }
        output << "}";
        if (index + 1 != dependencies.size()) output << ',';
        output << '\n';
    }
    if (dependencies.empty()) {
        output << "]\n}\n";
    } else {
        output << "  ]\n}\n";
    }
}

PackageLockfile readPackageLockfile(const std::filesystem::path &path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        throw std::runtime_error(messages::formatMessage(
            messages::kPackageLockOpenFailed, {path.u8string()}));
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    const std::string json = buffer.str();
    const JsonValue root = JsonParser(json, "vpp.lock").parse();
    if (root.kind != JsonValue::Kind::Object) {
        throw std::runtime_error(std::string(messages::kPackageLockRootMustBeObject));
    }

    PackageLockfile lockfile;
    const JsonValue *schema = member(root, "schema");
    if (schema == nullptr || schema->kind != JsonValue::Kind::Number) {
        throw std::runtime_error(std::string(messages::kPackageLockSchemaMustBeInteger));
    }
    try {
        std::size_t consumed = 0;
        lockfile.schema = std::stoi(schema->text, &consumed);
        if (consumed != schema->text.size()) {
            throw std::invalid_argument(std::string(messages::kPackageSchemaParseSentinel));
        }
    } catch (...) {
        throw std::runtime_error(std::string(messages::kPackageLockSchemaMustBeInteger));
    }
    if (lockfile.schema != kPackageLockSchemaVersion) {
        const std::string schema = std::to_string(lockfile.schema);
        throw std::runtime_error(messages::formatMessage(
            messages::kPackageLockSchemaUnsupported, {schema}));
    }

    const JsonValue *packages = member(root, "packages");
    if (packages == nullptr || packages->kind != JsonValue::Kind::Array) {
        throw std::runtime_error(std::string(messages::kPackageLockPackagesMustBeArray));
    }
    for (const JsonValue &value : packages->array) {
        lockfile.packages.push_back(lockEntryFromJson(value));
    }
    std::vector<std::string> names;
    for (const PackageLockEntry &entry : lockfile.packages) names.push_back(entry.name);
    std::sort(names.begin(), names.end());
    if (std::adjacent_find(names.begin(), names.end()) != names.end()) {
        throw std::runtime_error(std::string(messages::kPackageLockPackageDuplicate));
    }
    return lockfile;
}

void writePackageLockfile(const std::filesystem::path &path,
                          const PackageLockfile &lockfile) {
    if (lockfile.schema != kPackageLockSchemaVersion) {
        throw std::runtime_error(messages::formatMessage(
            messages::kPackageLockSchemaUnsupported,
            {std::to_string(lockfile.schema)}));
    }
    std::vector<PackageLockEntry> packages = lockfile.packages;
    std::sort(packages.begin(), packages.end(),
              [](const PackageLockEntry &left, const PackageLockEntry &right) {
                  return left.name < right.name;
              });
    for (std::size_t index = 0; index < packages.size(); ++index) {
        if (!isValidPackageName(packages[index].name)) {
            throw std::runtime_error(messages::formatMessage(
                messages::kPackageLockNameInvalid, {packages[index].name}));
        }
        if (index > 0 && packages[index - 1].name == packages[index].name) {
            throw std::runtime_error(messages::formatMessage(
                messages::kPackageLockPackageDuplicateNamed, {packages[index].name}));
        }
        std::string versionError;
        if (!SemanticVersion::parse(packages[index].version, &versionError).has_value()) {
            throw std::runtime_error(messages::formatMessage(
                messages::kPackageLockVersionInvalid,
                {packages[index].name, versionError}));
        }
        if (packages[index].resolvedPath.empty() || packages[index].fingerprint.empty()) {
            throw std::runtime_error(messages::formatMessage(
                messages::kPackageLockResolvedFingerprintMissing,
                {packages[index].name}));
        }
        if (packages[index].sourceKind == PackageSourceKind::Git &&
            packages[index].revision.empty()) {
            throw std::runtime_error(messages::formatMessage(
                messages::kPackageLockGitRevisionMissing, {packages[index].name}));
        }
    }

    std::ofstream output(path);
    if (!output.is_open()) {
        throw std::runtime_error(messages::formatMessage(
            messages::kPackageLockWriteFailed, {path.u8string()}));
    }
    output << "{\n  \"schema\": " << lockfile.schema << ",\n  \"packages\": [";
    if (!packages.empty()) output << '\n';
    for (std::size_t index = 0; index < packages.size(); ++index) {
        const PackageLockEntry &entry = packages[index];
        output << "    {\"name\": \"" << jsonEscape(entry.name)
               << "\", \"version\": \"" << jsonEscape(entry.version)
               << "\", \"source\": \"" << packageSourceKindName(entry.sourceKind)
               << "\", \"location\": \"" << jsonEscape(entry.location)
               << "\"";
        if (entry.sourceKind == PackageSourceKind::Git) {
            output << ", \"revision\": \"" << jsonEscape(entry.revision)
                   << "\"";
        }
        output << ", \"resolved\": \"" << jsonEscape(entry.resolvedPath)
               << "\", \"fingerprint\": \"" << jsonEscape(entry.fingerprint)
               << "\"}";
        if (index + 1 != packages.size()) output << ',';
        output << '\n';
    }
    if (packages.empty()) {
        output << "]\n}\n";
    } else {
        output << "  ]\n}\n";
    }
}

std::string fingerprintPackageTree(const std::filesystem::path &root) {
    if (!std::filesystem::exists(root) || !std::filesystem::is_directory(root)) {
        throw std::runtime_error(messages::formatMessage(
            messages::kPackageFingerprintDirectoryInvalid, {root.u8string()}));
    }
    std::vector<std::filesystem::path> files;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file()) files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end(), [&](const auto &left, const auto &right) {
        return std::filesystem::relative(left, root).generic_u8string() <
               std::filesystem::relative(right, root).generic_u8string();
    });

    std::uint64_t hash = 14695981039346656037ull;
    for (const auto &file : files) {
        hashText(hash, std::filesystem::relative(file, root).generic_u8string());
        std::ifstream input(file, std::ios::binary);
        if (!input.is_open()) {
            throw std::runtime_error(messages::formatMessage(
                messages::kPackageFingerprintFileReadFailed, {file.u8string()}));
        }
        const bool normalizeLineEndings = normalizeLineEndingsForFingerprint(file);
        bool pendingCarriageReturn = false;
        char byte = 0;
        while (input.get(byte)) {
            const unsigned char value = static_cast<unsigned char>(byte);
            if (!normalizeLineEndings) {
                hashByte(hash, value);
                continue;
            }
            if (pendingCarriageReturn) {
                if (value == static_cast<unsigned char>('\n')) {
                    hashByte(hash, static_cast<unsigned char>('\n'));
                    pendingCarriageReturn = false;
                    continue;
                }
                hashByte(hash, static_cast<unsigned char>('\r'));
                pendingCarriageReturn = false;
            }
            if (value == static_cast<unsigned char>('\r')) {
                pendingCarriageReturn = true;
            } else {
                hashByte(hash, value);
            }
        }
        if (pendingCarriageReturn) hashByte(hash, static_cast<unsigned char>('\r'));
        hashByte(hash, 0xff);
    }

    std::ostringstream out;
    out << "fnv1a64:" << std::hex << std::setfill('0') << std::setw(16) << hash;
    return out.str();
}

} // namespace vietvm::core

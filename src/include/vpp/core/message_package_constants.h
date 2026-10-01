#pragma once

#include <string_view>

namespace vietvm::messages {

// Package manifest / lockfile diagnostics.
inline constexpr std::string_view kPackageJsonInvalidAtOffset = "{0} không hợp lệ tại offset {1}: {2}";
inline constexpr std::string_view kPackageJsonTrailingData = "dữ liệu dư sau JSON";
inline constexpr std::string_view kPackageJsonUnexpectedEnd = "kết thúc JSON đột ngột";
inline constexpr std::string_view kPackageJsonUnicodeEscapeInvalid = "escape Unicode không hợp lệ";
inline constexpr std::string_view kPackageJsonStringMustStartWithQuote = "chuỗi JSON phải bắt đầu bằng dấu nháy kép";
inline constexpr std::string_view kPackageJsonRawControlCharacter = "chuỗi JSON chứa control character thô";
inline constexpr std::string_view kPackageJsonHighSurrogateMissingLow = "high surrogate thiếu low surrogate";
inline constexpr std::string_view kPackageJsonLowSurrogateInvalid = "low surrogate không hợp lệ";
inline constexpr std::string_view kPackageJsonLowSurrogateWithoutHigh = "low surrogate không có high surrogate";
inline constexpr std::string_view kPackageJsonStringEscapeInvalid = "escape chuỗi JSON không hợp lệ";
inline constexpr std::string_view kPackageJsonStringUnterminated = "chuỗi JSON chưa đóng";
inline constexpr std::string_view kPackageJsonNumberInvalid = "number JSON không hợp lệ";
inline constexpr std::string_view kPackageJsonFractionInvalid = "fraction JSON không hợp lệ";
inline constexpr std::string_view kPackageJsonExponentInvalid = "exponent JSON không hợp lệ";
inline constexpr std::string_view kPackageJsonArrayMustStart = "array JSON phải bắt đầu bằng '['";
inline constexpr std::string_view kPackageJsonArrayCommaMissing = "array JSON thiếu ','";
inline constexpr std::string_view kPackageJsonObjectMustStart = "object JSON phải bắt đầu bằng '{'";
inline constexpr std::string_view kPackageJsonObjectKeyMustBeString = "key object JSON phải là chuỗi";
inline constexpr std::string_view kPackageJsonObjectColonMissing = "object JSON thiếu ':' sau key";
inline constexpr std::string_view kPackageJsonObjectDuplicateKey = "object JSON có key trùng";
inline constexpr std::string_view kPackageJsonObjectCommaMissing = "object JSON thiếu ','";
inline constexpr std::string_view kPackageJsonLiteralInvalid = "literal JSON không hợp lệ";
inline constexpr std::string_view kPackageJsonValueMissing = "thiếu JSON value";
inline constexpr std::string_view kPackageJsonValueInvalid = "JSON value không hợp lệ";
inline constexpr std::string_view kPackageJsonStringFieldRequired = "vpp.json: trường '{0}' phải là chuỗi";
inline constexpr std::string_view kPackageJsonDependencyMustBeObject = "vpp.json: mỗi dependency phải là object";
inline constexpr std::string_view kPackageLockEntryMustBeObject = "vpp.lock: mỗi package phải là object";
inline constexpr std::string_view kPackageLockNameInvalid = "vpp.lock: package name không hợp lệ: {0}";
inline constexpr std::string_view kPackageLockVersionInvalid = "vpp.lock: version của package '{0}' không hợp lệ: {1}";
inline constexpr std::string_view kPackageLockResolvedFingerprintMissing = "vpp.lock: package '{0}' thiếu resolved/fingerprint";
inline constexpr std::string_view kPackageLockGitRevisionMissing = "vpp.lock: Git package '{0}' thiếu exact revision";
inline constexpr std::string_view kPackageSourceKindInvalid = "package source kind không hợp lệ";
inline constexpr std::string_view kPackageJsonSourceInvalid = "vpp.json: source dependency không hợp lệ: {0}";
inline constexpr std::string_view kPackageJsonSchemaUnsupported = "vpp.json: schema không được hỗ trợ: {0}";
inline constexpr std::string_view kPackageJsonNameInvalid = "vpp.json: name không hợp lệ: {0}";
inline constexpr std::string_view kPackageJsonVersionInvalid = "vpp.json: version không hợp lệ: {0}";
inline constexpr std::string_view kPackageJsonDependencyNameInvalid = "vpp.json: dependency name không hợp lệ: {0}";
inline constexpr std::string_view kPackageJsonDependencyRangeInvalid = "vpp.json: version range của dependency '{0}' không hợp lệ: {1}";
inline constexpr std::string_view kPackageJsonDependencyLocationMissing = "vpp.json: dependency '{0}' cần location cho source {1}";
inline constexpr std::string_view kPackageJsonGitRefMissing = "vpp.json: Git dependency '{0}' cần ref (dùng HEAD nếu muốn theo default branch)";
inline constexpr std::string_view kPackageJsonDependencyDuplicate = "vpp.json: dependency bị khai báo trùng: {0}";
inline constexpr std::string_view kPackageJsonOpenFailed = "không thể mở vpp.json: {0}";
inline constexpr std::string_view kPackageJsonRootMustBeObject = "vpp.json: root phải là object";
inline constexpr std::string_view kPackageJsonSchemaMustBeInteger = "vpp.json: schema phải là số nguyên";
inline constexpr std::string_view kPackageJsonDependenciesMustBeArray = "vpp.json: dependencies phải là array";
inline constexpr std::string_view kPackageJsonLegacyPackagesMustBeArray = "vpp.json: trường legacy 'gói' phải là array";
inline constexpr std::string_view kPackageJsonLegacyPackageMustBeString = "vpp.json: phần tử legacy 'gói' phải là chuỗi";
inline constexpr std::string_view kPackageJsonWriteFailed = "không thể ghi vpp.json: {0}";
inline constexpr std::string_view kPackageLockOpenFailed = "không thể mở vpp.lock: {0}";
inline constexpr std::string_view kPackageLockRootMustBeObject = "vpp.lock: root phải là object";
inline constexpr std::string_view kPackageLockSchemaMustBeInteger = "vpp.lock: schema phải là số nguyên";
inline constexpr std::string_view kPackageLockSchemaUnsupported = "vpp.lock: schema không được hỗ trợ: {0}";
inline constexpr std::string_view kPackageLockPackagesMustBeArray = "vpp.lock: packages phải là array";
inline constexpr std::string_view kPackageLockPackageDuplicate = "vpp.lock: package bị lặp";
inline constexpr std::string_view kPackageLockPackageDuplicateNamed = "vpp.lock: package bị lặp: {0}";
inline constexpr std::string_view kPackageLockWriteFailed = "không thể ghi vpp.lock: {0}";
inline constexpr std::string_view kPackageSchemaParseSentinel = "schema";
inline constexpr std::string_view kPackageFingerprintDirectoryInvalid = "không thể fingerprint package directory: {0}";
inline constexpr std::string_view kPackageFingerprintFileReadFailed = "không thể đọc package file để fingerprint: {0}";

// Package cache / installer diagnostics.
inline constexpr std::string_view kPackageCacheFingerprintInvalid = "fingerprint package không hợp lệ cho cache: {0}";
inline constexpr std::string_view kPackageCacheSymlinkUnsupported = "cache package không hỗ trợ symlink: {0}";
inline constexpr std::string_view kPackageCacheEntryUnsupported = "cache package gặp entry không được hỗ trợ: {0}";
inline constexpr std::string_view kPackageCacheChanged = "cache package bị thay đổi ngoài ý muốn: {0}";
inline constexpr std::string_view kPackageCacheFingerprintChanged = "không thể cache package: fingerprint thay đổi trong khi snapshot";
inline constexpr std::string_view kPackagePathSymlinkUnsupported = "package path không hỗ trợ symlink trong 0.9: {0}";
inline constexpr std::string_view kPackagePathEntryUnsupported = "package path chứa entry không được hỗ trợ: {0}";
inline constexpr std::string_view kPackageSourceNotFoundRuntime = "không tìm thấy package source: {0}";
inline constexpr std::string_view kPackageSourceTargetSameNotDirectory = "package source trùng target nhưng không phải directory";
inline constexpr std::string_view kPackageFingerprintMismatch = "fingerprint package không khớp: mong đợi {0}, thực tế {1}";
inline constexpr std::string_view kPackageInstallInsideSource = "không thể cài package vào bên trong chính source tree: {0}";
inline constexpr std::string_view kPackageSourceUnsupportedEntry = "package source không phải file hoặc directory: {0}";
inline constexpr std::string_view kPackageReplaceFailed = "không thể thay package cũ: {0} ({1})";

// Dependency solver / source transport diagnostics.
inline constexpr std::string_view kPackageDependencyRangeInvalid = "dependency '{0}' từ '{1}' có version range không hợp lệ '{2}': {3}";
inline constexpr std::string_view kPackageDependencySourceVersionInvalid = "dependency '{0}' có source version không hợp lệ '{1}': {2}";
inline constexpr std::string_view kPackageDependencyVersionConflict = "xung đột dependency '{0}': phiên bản {1} không thỏa range {2} do '{3}' yêu cầu";
inline constexpr std::string_view kPackageTransportUnsupported = "dependency '{0}' dùng source '{1}' nhưng transport này chưa được hỗ trợ trong Package 0.9";
inline constexpr std::string_view kPackageDependencySourceMissing = "không tìm thấy source path của dependency '{0}': {1}";
inline constexpr std::string_view kPackageDependencySourceInvalid = "source path của dependency '{0}' không phải file hoặc directory: {1}";
inline constexpr std::string_view kPackageDependencyResolvedConflict = "xung đột dependency '{0}': đã resolve {1} từ {2}, nhưng '{3}' yêu cầu source {4} phiên bản {5}";
inline constexpr std::string_view kPackageDependencyCycle = "phát hiện vòng lặp dependency: {0}";
inline constexpr std::string_view kPackageGitUtf8ToUtf16Failed = "không thể chuyển đối số Git UTF-8 sang UTF-16";
inline constexpr std::string_view kPackageGitExecutableMissing = "process Git thiếu executable";
inline constexpr std::string_view kPackageGitPipeFailed = "không thể tạo pipe cho Git process";
inline constexpr std::string_view kPackageGitNullInputFailed = "không thể mở NUL cho Git process";
inline constexpr std::string_view kPackageGitStartFailed = "không thể khởi động Git; hãy kiểm tra git có trong PATH";
inline constexpr std::string_view kPackageGitForkFailed = "không thể fork Git process";
inline constexpr std::string_view kPackageGitWaitFailed = "không thể chờ Git process";
inline constexpr std::string_view kPackageGitOperationFailed = "{0} thất bại (exit {1}){2}";
inline constexpr std::string_view kPackageGitRefInvalid = "Git ref của dependency '{0}' không hợp lệ: {1}";
inline constexpr std::string_view kPackageGitCheckoutCleanupFailed = "không thể dọn Git checkout cũ: {0} ({1})";
inline constexpr std::string_view kPackageGitExactCommitMissing = "Git không trả exact commit cho dependency '{0}'";
inline constexpr std::string_view kPackageRegistryLocationMissing = "registry dependency '{0}' thiếu location và biến VPP_REGISTRY chưa được cấu hình";
inline constexpr std::string_view kPackageRegistryRootMissing = "không tìm thấy registry root cho dependency '{0}': {1}";
inline constexpr std::string_view kPackageRegistryPackageMissing = "registry không có package '{0}' tại {1}";
inline constexpr std::string_view kPackageRegistryRangeInvalid = "registry dependency '{0}' có version range không hợp lệ '{1}': {2}";
inline constexpr std::string_view kPackageRegistryManifestMissing = "registry package '{0}@{1}' thiếu vpp.json";
inline constexpr std::string_view kPackageRegistryMetadataMismatch = "registry metadata không khớp layout cho package '{0}@{1}'";
inline constexpr std::string_view kPackageRegistryVersionMissing = "registry không có version của package '{0}' thỏa range {1}";
inline constexpr std::string_view kPackageRegistryMetadataCreateFailed = "không thể tạo registry metadata: {0}";
inline constexpr std::string_view kPackagePublishManifestMissing = "publish: không tìm thấy vpp.json tại {0}";
inline constexpr std::string_view kPackageRegistryVersionPathInvalid = "registry version path không phải directory: {0}";
inline constexpr std::string_view kPackageRegistryVersionImmutable = "registry đã có '{0}@{1}' với nội dung khác; version đã publish là bất biến";

// Semantic version diagnostics.
inline constexpr std::string_view kSemverNumberComponentMissing = "thiếu thành phần số trong semantic version";
inline constexpr std::string_view kSemverNumberLeadingZero = "thành phần số semantic version không được có số 0 ở đầu";
inline constexpr std::string_view kSemverNumberOutOfRange = "thành phần semantic version vượt phạm vi số nguyên";
inline constexpr std::string_view kSemverIdentifierInvalid = "identifier semantic version rỗng hoặc chứa ký tự không hợp lệ";
inline constexpr std::string_view kSemverPrereleaseLeadingZero = "identifier prerelease dạng số không được có số 0 ở đầu";
inline constexpr std::string_view kSemverIdentifierTrailingDot = "identifier semantic version không được kết thúc bằng dấu chấm";
inline constexpr std::string_view kSemverEmpty = "semantic version rỗng";
inline constexpr std::string_view kSemverExpectedCoreFormat = "semantic version phải có dạng major.minor.patch";
inline constexpr std::string_view kSemverTrailingDataInvalid = "semantic version chứa phần dư không hợp lệ";
inline constexpr std::string_view kSemverWildcardMustStandAlone = "'*' chỉ hợp lệ khi đứng một mình trong version range";
inline constexpr std::string_view kSemverOrRangeUnsupported = "OR version range chưa được hỗ trợ trong Package 0.9";
inline constexpr std::string_view kSemverComparatorMissingVersion = "version comparator thiếu phiên bản";

} // namespace vietvm::messages

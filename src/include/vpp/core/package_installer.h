#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace vietvm::core {

// Materialize một local path package bằng staging directory nằm cạnh target.
// Target cũ chỉ bị thay sau khi copy và fingerprint đã thành công. Nếu truyền
// expectedFingerprint, mismatch làm thao tác thất bại trước commit.
std::string materializePathPackage(
    const std::filesystem::path &source,
    const std::filesystem::path &targetDirectory,
    const std::optional<std::string> &expectedFingerprint = std::nullopt);

} // namespace vietvm::core

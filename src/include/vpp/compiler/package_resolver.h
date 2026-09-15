#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace vietvm::compiler {

// Kết quả phân giải một import trước khi compiler đọc source. `path` luôn được
// chuẩn hóa lexical tuyệt đối; `exists` cho biết resolver đã tìm thấy source thật.
struct PackageResolution {
    std::filesystem::path path;
    bool exists = false;
    bool barePackageCandidate = false;
};

// Sở hữu policy tìm file/module/package cho một compilation session. Resolver
// không đọc/biên dịch source và không chạm registry compiler; nhiệm vụ duy nhất
// là biến target import thành một đường dẫn canonical theo project layout.
class PackageResolver {
public:
    explicit PackageResolver(
        std::filesystem::path resolutionBase,
        std::optional<std::filesystem::path> installationHome = std::nullopt);

    const std::filesystem::path &resolutionBase() const noexcept {
        return resolutionBase_;
    }

    const std::optional<std::filesystem::path> &installationHome() const noexcept {
        return installationHome_;
    }

    PackageResolution resolve(const std::string &target, bool quoted) const;

private:
    std::filesystem::path absoluteLexical(
        const std::filesystem::path &candidate) const;

    std::filesystem::path resolutionBase_;
    std::optional<std::filesystem::path> installationHome_;
};

} // namespace vietvm::compiler

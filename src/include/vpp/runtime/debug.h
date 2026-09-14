#pragma once

#include <cstddef>
#include <string>

namespace vietvm::runtime {

// Metadata nguồn song song với bytecode. `line == 0` nghĩa là instruction đó
// chưa có vị trí nguồn đủ chính xác để hiển thị trong stack trace.
struct RuntimeSourceLocation {
    std::string sourceFile;
    std::string moduleIdentity;
    std::string functionName;
    std::size_t line = 0;
    std::size_t column = 0;

    // Cho biết metadata có đủ vị trí dòng để dùng làm một frame stack trace.
    bool valid() const noexcept { return line != 0; }
};

} // namespace vietvm::runtime

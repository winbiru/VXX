#include "common/storeString.h"
#include <stdexcept>
#include <vector>

namespace vietvm::compiler {
    std::vector<std::string> StringPool::pool_;

    int StringPool::storeString(const std::string& s) {
        pool_.push_back(s);
        return static_cast<int>(pool_.size() - 1);
    }

    const std::string& StringPool::getString(int idx)  {
        if (idx < 0 || static_cast<size_t>(idx) >= pool_.size()) throw std::out_of_range("StringPool: index");
        return pool_[idx];
    }

    size_t StringPool::size() noexcept { return pool_.size(); }

} // namespace vietvm::compiler
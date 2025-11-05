#include "common/storeString.h"
#include <stdexcept>
#include <vector>

namespace vietvm::compiler {

    std::vector<std::string> StringPool::pool_;
    std::unordered_map<int, std::vector<Instruction>> hamMap::hamBytecodeMap;

    int StringPool::storeString(const std::string& s) {
        pool_.push_back(s);
        return static_cast<int>(pool_.size() - 1);
    }

    const std::string& StringPool::getString(int idx)  {
        if (idx < 0 || static_cast<size_t>(idx) >= pool_.size()) throw std::out_of_range("StringPool: index");
        return pool_[idx];
    }
    const std::vector<std::string>& StringPool::getPool() {
        return pool_;
    }
    size_t StringPool::size() noexcept { return pool_.size(); }
    void StringPool::clear() {
        pool_.clear();
    }
} // namespace vietvm::compiler
#include "common/storeString.h"
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace vietvm::compiler {
    std::vector<std::string> StringPool::pool_;
    std::unordered_map<std::string,int> StringPool::indexMap_;
    int StringPool::storeString(const std::string& s) {
        pool_.push_back(s);
        return static_cast<int>(pool_.size() - 1);
    }
    int StringPool::getOrInsertString(const std::string& s) {
        // fast-path: check existing map
        auto it = indexMap_.find(s);
        if (it != indexMap_.end()) {
            return it->second;
        }

        // not found -> insert and record index
        pool_.push_back(s);
        int idx = static_cast<int>(pool_.size() - 1);
        indexMap_.emplace(pool_.back(), idx);
        return idx;
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
        indexMap_.clear();
    }
} // namespace vietvm::compiler
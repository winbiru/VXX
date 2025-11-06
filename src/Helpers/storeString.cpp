#include "common/storeString.h"
#include <stdexcept>
#include <vector>
#include <iostream>

namespace vietvm::compiler {

    std::vector<std::string> StringPool::pool_;
    std::unordered_map<std::string,int> StringPool::poolIndexMap_;

    std::unordered_map<int, std::vector<Instruction>> hamMap::hamBytecodeMap;
    std::unordered_map<int,int> hamMap::hamNameIndexMap;

    int hamMap::nextHamId_ = 0;

    int hamMap::allocHamId() {
        return nextHamId_++;
    }

    void hamMap::resetHamIdCounter() {
        nextHamId_ = 0;
    }

    int StringPool::findString(const std::string& s) {
        auto it = poolIndexMap_.find(s);
        if (it != poolIndexMap_.end()) return it->second;
        return -1;
    }

    int StringPool::storeString(const std::string& s) {
        // fast path: return existing index if present
        auto it = poolIndexMap_.find(s);
        if (it != poolIndexMap_.end()) {
            return it->second;
        }
        // else add
        pool_.push_back(s);
        int idx = static_cast<int>(pool_.size() - 1);
        poolIndexMap_.emplace(s, idx);
        // debug log (temporary) to show additions
        std::cerr << "DEBUG: StringPool added [" << idx << "] = \"" << s << "\"\n";
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
        poolIndexMap_.clear();
    }
} // namespace vietvm::compiler
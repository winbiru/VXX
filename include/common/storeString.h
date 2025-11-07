#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "../vm/instruction.h"

namespace vietvm::compiler {
    class hamMap {
    public:
        static std::unordered_map<int, std::vector<Instruction>> hamBytecodeMap;
        static std::unordered_map<int,int> hamNameIndexMap;
        static void setHamNameIndex(int hamId, int nameIndex) { hamNameIndexMap[hamId] = nameIndex; }
        static void clearHamNameIndexMap() { hamNameIndexMap.clear(); }
        static int allocHamId();
        static void resetHamIdCounter();
    private:
        static int nextHamId_;
    };
    class StringPool {
    public:
        // returns existing index if string exists (dedupe), otherwise pushes new string
        static int storeString(const std::string& s);
        // returns index if exists, -1 if not found (does not create)
        static int findString(const std::string& s);
        static const std::string& getString(int idx);
        static size_t size() noexcept;
        static const std::vector<std::string>& getPool();
        static void clear();
    private:
        static std::vector<std::string> pool_;
        static std::unordered_map<std::string,int> poolIndexMap_;
    };

} // namespace vietvm::compiler
#pragma once
#include <iosfwd>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "instruction.h"

namespace vietvm::compiler {

    class StringPool {
    public:
        static int storeString(const std::string& s);
        static int getOrInsertString(const std::string& s);
        static const std::string& getString(int idx);
        static size_t size() noexcept;
        static const std::vector<std::string>& getPool();
        static void clear();
    private:
        static std::vector<std::string> pool_;
        static std::unordered_map<std::string,int> indexMap_;
    };

    // Global map of function id -> function bytecode (defined in CompileRegistry.cpp).
    // Declare it extern here so other translation units (main.cpp, VM, ...) can use it.
    extern std::unordered_map<int, std::vector<Instruction>> hamBytecodeMap;

} // namespace vietvm::compiler
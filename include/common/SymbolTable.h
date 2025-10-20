//
// Created by nx_thang on 10/20/2025.
//

#pragma once
#include <string>
#include <unordered_map>

namespace vietvm::compiler {

    class SymbolTable {
    public:
        // static int getOrCreate(const std::string& name);
        bool contains(const std::string& name) const noexcept;
        static int getOrCreate(std::unordered_map<std::string,int>& symTab, const std::string& name, int& nextId);

    private:
        std::unordered_map<std::string,int> table_;
        int nextId_ = 0;
    };
} // namespace vietvm::compiler
//
// Created by nx_thang on 10/20/2025.
//

#include "common/symbolTable.h"

namespace vietvm::compiler {

    // Lấy or create; hàm đọc dữ liệu từ trạng thái hiện tại và trả về cho caller mà không chủ động thay đổi dữ liệu.
    int symbolTable::getOrCreate(std::unordered_map<std::string,int>& symTab,
                                               const std::string& name,
                                               int& nextId) {
        auto it = symTab.find(name);
        if (it == symTab.end()) {
            symTab[name] = nextId;
            return nextId++;
        }
        return it->second;
    }

    // Kiểm tra điều kiện của `contains`.
    bool symbolTable::contains(const std::string& name) const noexcept {
        return table_.find(name) != table_.end();
    }

} // namespace vietvm::Compiler
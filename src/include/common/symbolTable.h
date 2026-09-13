//
// Created by nx_thang on 10/20/2025.
//

#pragma once
#include <string>
#include <unordered_map>

namespace vietvm::compiler {

    // Quản lý ký hiệu bảng; lớp đóng gói bảng tra cứu và các thao tác thêm/đọc/xóa để duy trì trạng thái nhất quán.
    class symbolTable {
    public:
        // Kiểm tra điều kiện của `contains`.
        bool contains(const std::string& name) const noexcept;
        // Lấy or create; hàm đọc dữ liệu từ trạng thái hiện tại và trả về cho caller mà không chủ động thay đổi dữ liệu.
        static int getOrCreate(std::unordered_map<std::string,int>& symTab, const std::string& name, int& nextId);

    private:
        std::unordered_map<std::string,int> table_;
    };
} // namespace vietvm::Compiler
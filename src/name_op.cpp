//
// Created by Nguyễn Xuân Thắng on 19/6/25.
//

#include "name_op.h"

// Ánh xạ từ từ khóa (chuỗi) sang Opcode
const std::unordered_map<std::string, Opcode> keywordMap = {
    {"nếu", OP_NEU},
    {"hoặc", OP_HOAC},
    {"nếu không", OP_NEU_KHONG},
    {"lặp", OP_LAP},
    {"khởi tạo", OP_KHOI_TAO},
    {"điều kiện", OP_DIEU_KIEN},
    {"cập nhật", OP_CAP_NHAT},
    {"kiểm tra sau", OP_KIEM_TRA_SAU},
    {"chuyển", OP_CHUYEN},
    {"trường hợp", OP_TRUONG_HOP},
    {"mặc định", OP_MAC_DINH},
    {"hàm", OP_HAM},
    {"gọi", OP_GOI},
    {"trả về", OP_TRA_VE},
    {"biến", OP_BIEN_SO},
    {"in", OP_IN},
    {"dừng", OP_DUNG_CHUONG_TRINH},
    {"bỏ qua", OP_BO_QUA},
    {"thoát", OP_THOAT},
    {"chọn", OP_CHON},
    {"ca", OP_CA},
    {"==", OP_SO_SANH_BANG},
    {"!=", OP_KHAC_BANG},
    {"!", OP_PHU_DINH},
    {">", OP_LON_HON},
    {"<", OP_NHO_HON},
    {">=", OP_LON_HON_HOAC_BANG},
    {"<=", OP_NHO_HON_HOAC_BANG},
    {"=", OP_GAN},
    {"+", OP_CONG},
    {"-", OP_TRU},
    {"*", OP_NHAN},
    {"/", OP_CHIA},
    {"&&", OP_Logic_VA},
    {"||", OP_Logic_HOAC},
    {"{", OP_MO_KHOI},
    {"}", OP_DONG_KHOI},
    {"(", OP_MO_NGOAC},
    {")", OP_DONG_NGOAC},
    {"[", OP_MO_MANG},
    {"]", OP_DONG_MANG},
    {";", OP_DONG_LENH},
    {",", OP_PHAY},
    {"%", OP_MODULO}
};
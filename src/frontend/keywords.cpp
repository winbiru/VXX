//
// Created by Nguyễn Xuân Thắng on 19/6/25.
//

#include "../../include/frontend/keywords.h"

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
    {"%", OP_MODULO},
    {"++", OP_CONG_MOT},
    {"--", OP_TRU_MOT},
    {"+=", OP_CONG_GAN},
    {"-=", OP_TRU_GAN},
    {"*=", OP_NHAN_GAN},
    {"/=", OP_CHIA_GAN},
    {"%=", OP_MODULO_GAN},
    {"đúng", OP_DUNG_GIA_TRI},
    {"sai", OP_SAI_GIA_TRI},
    {"ném", OP_NEM},
    {"thử", OP_THU},
    {"bắt lỗi", OP_BAT_LOI}
};

std::string name_op(Opcode op) {
    switch (op) {
        case OP_CONG: return "OP_CONG";
        case OP_TRU: return "OP_TRU";
        case OP_NHAN: return "OP_NHAN";
        case OP_CHIA: return "OP_CHIA";
        case OP_MODULO: return "OP_MODULO";
        case OP_JUMP: return "OP_JUMP";
        case OP_JUMP_IF_FALSE: return "OP_JUMP_IF_FALSE";
        case OP_Logic_VA: return "OP_LOGIC_VA";
        case OP_Logic_HOAC: return "OP_Logic_HOAC";
        case OP_KHONG: return "OP_KHONG";
        case OP_SO_SANH_BANG: return "OP_SO_SANH_BANG";
        case OP_KHAC_BANG: return "OP_KHAC_BANG";
        case OP_LON_HON: return "OP_LON_HON";
        case OP_NHO_HON: return "OP_NHO_HON";
        case OP_LON_HON_HOAC_BANG: return "OP_LON_HON_HOAC_BANG";
        case OP_NHO_HON_HOAC_BANG: return "OP_NHO_HON_HOAC_BANG";
        case OP_GAN: return "OP_GAN";
        case OP_MO_KHOI: return "OP_MO_KHOI";
        case OP_DONG_KHOI: return "OP_DONG_KHOI";
        case OP_MO_NGOAC: return "OP_MO_NGOAC";
        case OP_DONG_NGOAC: return "OP_DONG_NGOAC";
        case OP_MO_MANG: return "OP_MO_MANG";
        case OP_DONG_MANG: return "OP_DONG_MANG";
        case OP_DONG_LENH: return "OP_DONG_LENH";
        case OP_PHAY: return "OP_PHAY";
        case OP_NEU: return "OP_NEU";
        case OP_HOAC: return "OP_HOAC";
        case OP_NEU_KHONG: return "OP_NEU_KHONG";
        case OP_KET_THUC_NEU: return "OP_KET_THUC_NEU";
        case OP_LAP: return "OP_LAP";
        case OP_KHOI_TAO: return "OP_KHOI_TAO";
        case OP_DIEU_KIEN: return "OP_DIEU_KIEN";
        case OP_CAP_NHAT: return "OP_CAP_NHAT";
        case OP_KIEM_TRA_SAU: return "OP_KIEM_TRA_SAU";
        case OP_KET_THUC_LAP: return "OP_KET_THUC_LAP";
        case OP_BO_QUA: return "OP_BO_QUA";
        case OP_THOAT: return "OP_THOAT";
        case OP_CHUYEN: return "OP_CHUYEN";
        case OP_TRUONG_HOP: return "OP_TRUONG_HOP";
        case OP_MAC_DINH: return "OP_MAC_DINH";
        case OP_KET_THUC_CHUYEN: return "OP_KET_THUC_CHUYEN";
        case OP_HAM: return "OP_HAM";
        case OP_GOI: return "OP_GOI";
        case OP_TRA_VE: return "OP_TRA_VE";
        case OP_BIEN_SO: return "OP_BIEN_SO";
        case OP_TEN_BIEN_ID: return "OP_TEN_BIEN_ID";
        case OP_TEN_BIEN_GIA_TRI: return "OP_TEN_BIEN_GIA_TRI";
        case OP_IN: return "OP_IN";
        case OP_CHUOI: return "OP_CHUOI";
        case OP_PHU_DINH: return "OP_PHU_DINH";
        case OP_CHON: return "OP_CHON";
        case OP_CA: return "OP_CA";
        case OP_PARAM: return "OP_PARAM";
        case OP_DUNG_CHUONG_TRINH: return "OP_DUNG_CHUONG_TRINH";
        case OP_CONG_MOT: return "OP_CONG_MOT";
        case OP_TRU_MOT: return "OP_TRU_MOT";
        case OP_CONG_GAN: return "OP_CONG_GAN";
        case OP_TRU_GAN: return "OP_TRU_GAN";
        case OP_NHAN_GAN: return "OP_NHAN_GAN";
        case OP_CHIA_GAN: return "OP_CHIA_GAN";
        case OP_MODULO_GAN: return "OP_MODULO_GAN";
        case OP_DUNG_GIA_TRI: return "OP_DUNG_GIA_TRI";
        case OP_SAI_GIA_TRI: return "OP_SAI_GIA_TRI";
        case OP_BIEN_SO_FLOAT: return "OP_BIEN_SO_FLOAT";
        case OP_NEM: return "OP_NEM";
        case OP_THU: return "OP_THU";
        case OP_THU_KET_THUC: return "OP_THU_KET_THUC";
        case OP_BAT_LOI: return "OP_BAT_LOI";
        default: return "UNKNOWN_OPCODE";
    }
}

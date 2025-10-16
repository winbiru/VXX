#ifndef TO_STRING_H
#define TO_STRING_H

#include <string>
#include <unordered_map>
#include "instruction.h" // để nhận diện enum Opcode

extern const std::unordered_map<std::string, Opcode> keywordMap;

std::string name_op(Opcode op);  // tên hàm ánh xạ ngược


// Đổi Opcode thành tên chuỗi để debug
inline std::string name_op(Opcode op) {
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
        case OP_DUNG_CHUONG_TRINH: return "OP_DUNG_CHUONG_TRINH";
        default: return "UNKNOWN_OPCODE";
    }
}


#endif // TO_STRING_H

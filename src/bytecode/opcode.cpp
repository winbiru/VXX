#include "vpp/bytecode/opcode.h"

namespace vietvm::bytecode {

bool isKnownOpcode(int rawOpcode) noexcept {
    switch (rawOpcode) {
        case OP_CONG:
        case OP_TRU:
        case OP_NHAN:
        case OP_CHIA:
        case OP_Logic_VA:
        case OP_Logic_HOAC:
        case OP_KHONG:
        case OP_SO_SANH_BANG:
        case OP_KHAC_BANG:
        case OP_LON_HON:
        case OP_NHO_HON:
        case OP_LON_HON_HOAC_BANG:
        case OP_NHO_HON_HOAC_BANG:
        case OP_GAN:
        case OP_MO_KHOI:
        case OP_DONG_KHOI:
        case OP_MO_NGOAC:
        case OP_DONG_NGOAC:
        case OP_MO_MANG:
        case OP_DONG_MANG:
        case OP_DONG_LENH:
        case OP_PHAY:
        case OP_NEU:
        case OP_HOAC:
        case OP_NEU_KHONG:
        case OP_KET_THUC_NEU:
        case OP_LAP:
        case OP_KHOI_TAO:
        case OP_DIEU_KIEN:
        case OP_CAP_NHAT:
        case OP_KIEM_TRA_SAU:
        case OP_KET_THUC_LAP:
        case OP_BO_QUA:
        case OP_THOAT:
        case OP_CHUYEN:
        case OP_TRUONG_HOP:
        case OP_MAC_DINH:
        case OP_KET_THUC_CHUYEN:
        case OP_HAM:
        case OP_GOI:
        case OP_TRA_VE:
        case OP_BIEN_SO:
        case OP_IN:
        case OP_DUNG_CHUONG_TRINH:
        case OP_TEN_BIEN_ID:
        case OP_TEN_BIEN_GIA_TRI:
        case OP_JUMP_IF_FALSE:
        case OP_JUMP:
        case OP_MODULO:
        case OP_CHUOI:
        case OP_PHU_DINH:
        case OP_CHON:
        case OP_CA:
        case OP_PARAM:
        case OP_CONG_MOT:
        case OP_TRU_MOT:
        case OP_CONG_GAN:
        case OP_TRU_GAN:
        case OP_NHAN_GAN:
        case OP_CHIA_GAN:
        case OP_MODULO_GAN:
        case OP_DUNG_GIA_TRI:
        case OP_SAI_GIA_TRI:
        case OP_BIEN_SO_FLOAT:
        case OP_NEM:
        case OP_THU:
        case OP_THU_KET_THUC:
        case OP_BAT_LOI:
        case OP_RONG_GIA_TRI:
        case OP_MAP_LITERAL:
        case OP_GOI_GIAN_TIEP:
        case OP_PARAM_MAC_DINH:
        case OP_LIST_LITERAL:
        case OP_DOC_CHI_SO:
        case OP_GAN_CHI_SO:
        case OP_TAO_LOP:
        case OP_THEM_PHUONG_THUC:
        case OP_TAO_DOI_TUONG:
        case OP_DOC_THUOC_TINH:
        case OP_GAN_THUOC_TINH:
        case OP_GOI_PHUONG_THUC:
        case OP_TAO_DONG_BAO:
        case OP_VM_BIEN_DICH_PHAN_TICH:
        case OP_VM_DNS_PHAN_GIAI:
        case OP_VM_DOC_BIEN_MOI_TRUONG:
        case OP_VM_DONG_HO_DIA_PHUONG:
        case OP_VM_DONG_HO_UTC:
        case OP_VM_DUONG_DAN_TON_TAI:
        case OP_VM_IO_DOC_BYTES:
        case OP_VM_IO_DOC_FILE:
        case OP_VM_IO_GHI_BYTES:
        case OP_VM_IO_GHI_FILE:
        case OP_VM_IO_GHI_TIEP_FILE:
        case OP_VM_KICH_BAN_CHAY:
        case OP_VM_LA_TEP:
        case OP_VM_LA_THU_MUC_KHONG_THEO_LIEN_KET:
        case OP_VM_LA_THU_MUC:
        case OP_VM_LIET_KE_THU_MUC:
        case OP_VM_NGAU_NHIEN_BAO_MAT_BYTES:
        case OP_VM_NGU_MILI_GIAY:
        case OP_VM_SO_THUC_BITS:
        case OP_VM_SOCKET_CHAP_NHAN:
        case OP_VM_SOCKET_DAT_TIMEOUT:
        case OP_VM_SOCKET_DONG:
        case OP_VM_SOCKET_GUI:
        case OP_VM_SOCKET_NHAN:
        case OP_VM_SOCKET_PHAN_GIAI:
        case OP_VM_SOCKET_TCP_LANG_NGHE:
        case OP_VM_SOCKET_TCP_MO:
        case OP_VM_SOCKET_TLS_NANG_CAP:
        case OP_VM_SOCKET_UDP_MO:
        case OP_VM_TAO_THU_MUC:
        case OP_VM_TEN_NEN_TANG:
        case OP_VM_THOI_GIAN_DON_DIEU_MS:
        case OP_VM_TIEN_TRINH_CHAY:
        case OP_VM_XOA_DUONG_DAN:
        case OP_VM_THREAD_SPAWN:
        case OP_VM_THREAD_WAIT:
        case OP_VM_THREAD_CANCEL:
        case OP_VM_THREAD_STATUS:
        case OP_VM_THREAD_PARK:
        case OP_VM_TUPLE_FROM_LIST:
        case OP_VM_TYPE_OF:
        case OP_VM_IDENTITY_HASH:
        case OP_VM_LENGTH:
        case OP_VM_LIST_APPEND:
        case OP_VM_LIST_REMOVE:
        case OP_VM_MAP_HAS:
        case OP_VM_MAP_REMOVE:
        case OP_VM_MAP_KEYS:
        case OP_VM_STRING_BYTES:
        case OP_VM_STRING_FROM_BYTES:
        case OP_VM_FLOAT_FROM_BITS:
        case OP_FFI_CALL:
            return true;
        default:
            return false;
    }
}

// Trả tên văn bản ổn định cho opcode; hàm ánh xạ enum/giá trị nội bộ sang chuỗi để tooling, log hoặc test có thể hiển thị nhất quán.
std::string opcodeName(Opcode op) {
    return opcodeName(static_cast<int>(op));
}

// Trả tên opcode từ mã số thô; overload này giữ đường diagnostic cho input chưa
// kiểm chứng mà không phải static_cast một số tùy ý thành enum `Opcode`.
std::string opcodeName(int rawOpcode) {
    switch (rawOpcode) {
        case OP_CONG: return "OP_CONG";
        case OP_TRU: return "OP_TRU";
        case OP_NHAN: return "OP_NHAN";
        case OP_CHIA: return "OP_CHIA";
        case OP_MODULO: return "OP_MODULO";
        case OP_JUMP: return "OP_JUMP";
        case OP_JUMP_IF_FALSE: return "OP_JUMP_IF_FALSE";
        case OP_Logic_VA: return "OP_LOGIC_VA";
        case OP_Logic_HOAC: return "OP_LOGIC_HOAC";
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
        case OP_RONG_GIA_TRI: return "OP_RONG_GIA_TRI";
        case OP_MAP_LITERAL: return "OP_MAP_LITERAL";
        case OP_GOI_GIAN_TIEP: return "OP_GOI_GIAN_TIEP";
        case OP_PARAM_MAC_DINH: return "OP_PARAM_MAC_DINH";
        case OP_LIST_LITERAL: return "OP_LIST_LITERAL";
        case OP_DOC_CHI_SO: return "OP_DOC_CHI_SO";
        case OP_GAN_CHI_SO: return "OP_GAN_CHI_SO";
        case OP_TAO_LOP: return "OP_TAO_LOP";
        case OP_THEM_PHUONG_THUC: return "OP_THEM_PHUONG_THUC";
        case OP_TAO_DOI_TUONG: return "OP_TAO_DOI_TUONG";
        case OP_DOC_THUOC_TINH: return "OP_DOC_THUOC_TINH";
        case OP_GAN_THUOC_TINH: return "OP_GAN_THUOC_TINH";
        case OP_GOI_PHUONG_THUC: return "OP_GOI_PHUONG_THUC";
        case OP_TAO_DONG_BAO: return "OP_TAO_DONG_BAO";
        case OP_VM_BIEN_DICH_PHAN_TICH: return "OP_VM_BIEN_DICH_PHAN_TICH";
        case OP_VM_DNS_PHAN_GIAI: return "OP_VM_DNS_PHAN_GIAI";
        case OP_VM_DOC_BIEN_MOI_TRUONG: return "OP_VM_DOC_BIEN_MOI_TRUONG";
        case OP_VM_DONG_HO_DIA_PHUONG: return "OP_VM_DONG_HO_DIA_PHUONG";
        case OP_VM_DONG_HO_UTC: return "OP_VM_DONG_HO_UTC";
        case OP_VM_DUONG_DAN_TON_TAI: return "OP_VM_DUONG_DAN_TON_TAI";
        case OP_VM_IO_DOC_BYTES: return "OP_VM_IO_DOC_BYTES";
        case OP_VM_IO_DOC_FILE: return "OP_VM_IO_DOC_FILE";
        case OP_VM_IO_GHI_BYTES: return "OP_VM_IO_GHI_BYTES";
        case OP_VM_IO_GHI_FILE: return "OP_VM_IO_GHI_FILE";
        case OP_VM_IO_GHI_TIEP_FILE: return "OP_VM_IO_GHI_TIEP_FILE";
        case OP_VM_KICH_BAN_CHAY: return "OP_VM_KICH_BAN_CHAY";
        case OP_VM_LA_TEP: return "OP_VM_LA_TEP";
        case OP_VM_LA_THU_MUC_KHONG_THEO_LIEN_KET: return "OP_VM_LA_THU_MUC_KHONG_THEO_LIEN_KET";
        case OP_VM_LA_THU_MUC: return "OP_VM_LA_THU_MUC";
        case OP_VM_LIET_KE_THU_MUC: return "OP_VM_LIET_KE_THU_MUC";
        case OP_VM_NGAU_NHIEN_BAO_MAT_BYTES: return "OP_VM_NGAU_NHIEN_BAO_MAT_BYTES";
        case OP_VM_NGU_MILI_GIAY: return "OP_VM_NGU_MILI_GIAY";
        case OP_VM_SO_THUC_BITS: return "OP_VM_SO_THUC_BITS";
        case OP_VM_SOCKET_CHAP_NHAN: return "OP_VM_SOCKET_CHAP_NHAN";
        case OP_VM_SOCKET_DAT_TIMEOUT: return "OP_VM_SOCKET_DAT_TIMEOUT";
        case OP_VM_SOCKET_DONG: return "OP_VM_SOCKET_DONG";
        case OP_VM_SOCKET_GUI: return "OP_VM_SOCKET_GUI";
        case OP_VM_SOCKET_NHAN: return "OP_VM_SOCKET_NHAN";
        case OP_VM_SOCKET_PHAN_GIAI: return "OP_VM_SOCKET_PHAN_GIAI";
        case OP_VM_SOCKET_TCP_LANG_NGHE: return "OP_VM_SOCKET_TCP_LANG_NGHE";
        case OP_VM_SOCKET_TCP_MO: return "OP_VM_SOCKET_TCP_MO";
        case OP_VM_SOCKET_TLS_NANG_CAP: return "OP_VM_SOCKET_TLS_NANG_CAP";
        case OP_VM_SOCKET_UDP_MO: return "OP_VM_SOCKET_UDP_MO";
        case OP_VM_TAO_THU_MUC: return "OP_VM_TAO_THU_MUC";
        case OP_VM_TEN_NEN_TANG: return "OP_VM_TEN_NEN_TANG";
        case OP_VM_THOI_GIAN_DON_DIEU_MS: return "OP_VM_THOI_GIAN_DON_DIEU_MS";
        case OP_VM_TIEN_TRINH_CHAY: return "OP_VM_TIEN_TRINH_CHAY";
        case OP_VM_XOA_DUONG_DAN: return "OP_VM_XOA_DUONG_DAN";
        case OP_VM_THREAD_SPAWN: return "OP_VM_THREAD_SPAWN";
        case OP_VM_THREAD_WAIT: return "OP_VM_THREAD_WAIT";
        case OP_VM_THREAD_CANCEL: return "OP_VM_THREAD_CANCEL";
        case OP_VM_THREAD_STATUS: return "OP_VM_THREAD_STATUS";
        case OP_VM_THREAD_PARK: return "OP_VM_THREAD_PARK";
        case OP_VM_TUPLE_FROM_LIST: return "OP_VM_TUPLE_FROM_LIST";
        case OP_VM_TYPE_OF: return "OP_VM_TYPE_OF";
        case OP_VM_IDENTITY_HASH: return "OP_VM_IDENTITY_HASH";
        case OP_VM_LENGTH: return "OP_VM_LENGTH";
        case OP_VM_LIST_APPEND: return "OP_VM_LIST_APPEND";
        case OP_VM_LIST_REMOVE: return "OP_VM_LIST_REMOVE";
        case OP_VM_MAP_HAS: return "OP_VM_MAP_HAS";
        case OP_VM_MAP_REMOVE: return "OP_VM_MAP_REMOVE";
        case OP_VM_MAP_KEYS: return "OP_VM_MAP_KEYS";
        case OP_VM_STRING_BYTES: return "OP_VM_STRING_BYTES";
        case OP_VM_STRING_FROM_BYTES: return "OP_VM_STRING_FROM_BYTES";
        case OP_VM_FLOAT_FROM_BITS: return "OP_VM_FLOAT_FROM_BITS";
        case OP_FFI_CALL: return "OP_FFI_CALL";
        default: return "UNKNOWN_OPCODE";
    }
}

} // namespace vietvm::bytecode

#pragma once

#include <array>
#include <cstddef>
#include <string_view>
#include <utility>

#include "vpp/bytecode/instruction.h"

namespace vietvm::bytecode {

struct IntrinsicDescriptor {
    std::string_view name;
    Opcode opcode;
    std::size_t arity;
    bool reserved = true;
};

inline constexpr std::array<IntrinsicDescriptor, 51> kVmIntrinsics{{
    {"bien_dich_phan_tich_vm", OP_VM_BIEN_DICH_PHAN_TICH, 2},
    {"dns_phan_giai_vm", OP_VM_DNS_PHAN_GIAI, 1},
    {"doc_bien_moi_truong_vm", OP_VM_DOC_BIEN_MOI_TRUONG, 1},
    {"dong_ho_dia_phuong_vm", OP_VM_DONG_HO_DIA_PHUONG, 0},
    {"dong_ho_utc_vm", OP_VM_DONG_HO_UTC, 0},
    {"duong_dan_ton_tai_vm", OP_VM_DUONG_DAN_TON_TAI, 1},
    {"io_doc_bytes_vm", OP_VM_IO_DOC_BYTES, 1},
    {"io_doc_file_vm", OP_VM_IO_DOC_FILE, 1},
    {"io_ghi_bytes_vm", OP_VM_IO_GHI_BYTES, 2},
    {"io_ghi_file_vm", OP_VM_IO_GHI_FILE, 2},
    {"io_ghi_tiep_file_vm", OP_VM_IO_GHI_TIEP_FILE, 2},
    {"kich_ban_chay_vm", OP_VM_KICH_BAN_CHAY, 2},
    {"la_tep_vm", OP_VM_LA_TEP, 1},
    {"la_thu_muc_khong_theo_lien_ket_vm", OP_VM_LA_THU_MUC_KHONG_THEO_LIEN_KET, 1},
    {"la_thu_muc_vm", OP_VM_LA_THU_MUC, 1},
    {"liet_ke_thu_muc_vm", OP_VM_LIET_KE_THU_MUC, 1},
    {"ngau_nhien_bao_mat_bytes_vm", OP_VM_NGAU_NHIEN_BAO_MAT_BYTES, 1},
    {"ngu_mili_giay_vm", OP_VM_NGU_MILI_GIAY, 1},
    {"so_thuc_bits_vm", OP_VM_SO_THUC_BITS, 1},
    {"socket_chap_nhan_vm", OP_VM_SOCKET_CHAP_NHAN, 1},
    {"socket_dat_timeout_vm", OP_VM_SOCKET_DAT_TIMEOUT, 2},
    {"socket_dong_vm", OP_VM_SOCKET_DONG, 1},
    {"socket_gui_vm", OP_VM_SOCKET_GUI, 3},
    {"socket_nhan_vm", OP_VM_SOCKET_NHAN, 2},
    {"socket_phan_giai_vm", OP_VM_SOCKET_PHAN_GIAI, 3},
    {"socket_tcp_lang_nghe_vm", OP_VM_SOCKET_TCP_LANG_NGHE, 2},
    {"socket_tcp_mo_vm", OP_VM_SOCKET_TCP_MO, 2},
    {"socket_tls_nang_cap_vm", OP_VM_SOCKET_TLS_NANG_CAP, 2},
    {"socket_udp_mo_vm", OP_VM_SOCKET_UDP_MO, 2},
    {"tao_thu_muc_vm", OP_VM_TAO_THU_MUC, 1},
    {"ten_nen_tang_vm", OP_VM_TEN_NEN_TANG, 0},
    {"thoi_gian_don_dieu_ms_vm", OP_VM_THOI_GIAN_DON_DIEU_MS, 0},
    {"tien_trinh_chay_vm", OP_VM_TIEN_TRINH_CHAY, 3},
    {"xoa_duong_dan_vm", OP_VM_XOA_DUONG_DAN, 1},
    {"thread_vm_spawn", OP_VM_THREAD_SPAWN, 2},
    {"thread_vm_wait", OP_VM_THREAD_WAIT, 2},
    {"thread_vm_cancel", OP_VM_THREAD_CANCEL, 1},
    {"thread_vm_status", OP_VM_THREAD_STATUS, 1},
    {"thread_vm_park", OP_VM_THREAD_PARK, 1},
    {"bộ", OP_VM_TUPLE_FROM_LIST, 1},
    {"loai_cua", OP_VM_TYPE_OF, 1, false},
    {"bam_dinh_danh", OP_VM_IDENTITY_HASH, 1, false},
    {"do_dai", OP_VM_LENGTH, 1, false},
    {"them", OP_VM_LIST_APPEND, 2, false},
    {"xoa_tai", OP_VM_LIST_REMOVE, 2, false},
    {"co_khoa", OP_VM_MAP_HAS, 2, false},
    {"xoa_khoa", OP_VM_MAP_REMOVE, 2, false},
    {"khoa_map", OP_VM_MAP_KEYS, 1, false},
    {"chuoi_bytes_vm", OP_VM_STRING_BYTES, 1, true},
    {"chuoi_tu_bytes_vm", OP_VM_STRING_FROM_BYTES, 1, true},
    {"so_thuc_tu_bits_vm", OP_VM_FLOAT_FROM_BITS, 1, true},
}};

inline constexpr const IntrinsicDescriptor *intrinsicByName(std::string_view name) noexcept {
    for (const auto &intrinsic : kVmIntrinsics) {
        if (intrinsic.name == name) return &intrinsic;
    }
    constexpr std::pair<std::string_view, Opcode> aliases[] = {
        {"loại của", OP_VM_TYPE_OF},
        {"độ dài", OP_VM_LENGTH},
        {"thêm", OP_VM_LIST_APPEND},
        {"xóa tại", OP_VM_LIST_REMOVE},
        {"có khóa", OP_VM_MAP_HAS},
        {"xóa khóa", OP_VM_MAP_REMOVE},
        {"khóa map", OP_VM_MAP_KEYS},
    };
    for (const auto &alias : aliases) {
        if (alias.first != name) continue;
        for (const auto &intrinsic : kVmIntrinsics) {
            if (intrinsic.opcode == alias.second) return &intrinsic;
        }
    }
    return nullptr;
}

inline constexpr const IntrinsicDescriptor *intrinsicByOpcode(int opcode) noexcept {
    for (const auto &intrinsic : kVmIntrinsics) {
        if (static_cast<int>(intrinsic.opcode) == opcode) return &intrinsic;
    }
    return nullptr;
}

} // namespace vietvm::bytecode

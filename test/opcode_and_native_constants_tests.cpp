#include <iostream>
#include <string>

#include "common/vm_native_constants.h"
#include "frontend/keywords.h"
#include "vpp/bytecode/opcode.h"

namespace {

bool expect(bool condition, const std::string &message) {
    if (condition) return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}

} // namespace

int main() {
    static_assert(vietvm::constants::kHttpStatusOk == 200,
                  "The default HTTP status must remain OK.");

    const Opcode knownOpcodes[] = {
        OP_CONG, OP_TRU, OP_NHAN, OP_CHIA,
        OP_Logic_VA, OP_Logic_HOAC, OP_KHONG, OP_SO_SANH_BANG,
        OP_KHAC_BANG, OP_LON_HON, OP_NHO_HON, OP_LON_HON_HOAC_BANG,
        OP_NHO_HON_HOAC_BANG, OP_GAN,
        OP_MO_KHOI, OP_DONG_KHOI, OP_MO_NGOAC, OP_DONG_NGOAC,
        OP_MO_MANG, OP_DONG_MANG, OP_DONG_LENH, OP_PHAY,
        OP_NEU, OP_HOAC, OP_NEU_KHONG, OP_KET_THUC_NEU,
        OP_LAP, OP_KHOI_TAO, OP_DIEU_KIEN, OP_CAP_NHAT,
        OP_KIEM_TRA_SAU, OP_KET_THUC_LAP, OP_BO_QUA, OP_THOAT,
        OP_CHUYEN, OP_TRUONG_HOP, OP_MAC_DINH, OP_KET_THUC_CHUYEN,
        OP_HAM, OP_GOI, OP_TRA_VE, OP_BIEN_SO, OP_IN,
        OP_DUNG_CHUONG_TRINH, OP_TEN_BIEN_ID, OP_TEN_BIEN_GIA_TRI,
        OP_JUMP_IF_FALSE, OP_JUMP, OP_MODULO, OP_CHUOI, OP_PHU_DINH,
        OP_CHON, OP_CA, OP_PARAM, OP_CONG_MOT, OP_TRU_MOT,
        OP_CONG_GAN, OP_TRU_GAN, OP_NHAN_GAN, OP_CHIA_GAN,
        OP_MODULO_GAN, OP_DUNG_GIA_TRI, OP_SAI_GIA_TRI,
        OP_BIEN_SO_FLOAT, OP_NEM, OP_THU, OP_THU_KET_THUC,
        OP_BAT_LOI, OP_RONG_GIA_TRI, OP_MAP_LITERAL, OP_GOI_GIAN_TIEP,
        OP_PARAM_MAC_DINH, OP_LIST_LITERAL, OP_DOC_CHI_SO, OP_GAN_CHI_SO,
    };

    bool ok = true;
    for (Opcode opcode : knownOpcodes) {
        const std::string canonical = vietvm::bytecode::opcodeName(opcode);
        ok &= expect(canonical != "UNKNOWN_OPCODE", "canonical opcode name is missing");
        ok &= expect(name_op(opcode) == canonical,
                     "legacy name_op must delegate to the canonical opcode name");
    }

    ok &= expect(vietvm::bytecode::opcodeName(OP_DONG_LENH) == "OP_DONG_LENH",
                 "OP_DONG_LENH must have a canonical name");
    ok &= expect(vietvm::bytecode::opcodeName(static_cast<Opcode>(-1)) == "UNKNOWN_OPCODE",
                 "unknown opcodes must preserve their fallback name");

    ok &= expect(std::string(vietvm::constants::kHttpMethodDelete) == "DELETE",
                 "DELETE method constant must retain its wire value");
    ok &= expect(vietvm::constants::matchesAnyName("mang_http_delete",
                                                   vietvm::constants::kFnHttpDelete),
                 "DELETE native function aliases must remain recognized");
    ok &= expect(std::string(vietvm::constants::kReqFieldMethod) == "method" &&
                     std::string(vietvm::constants::kReqFieldPath) == "path" &&
                     std::string(vietvm::constants::kReqFieldQuery) == "query" &&
                     std::string(vietvm::constants::kReqFieldBody) == "body" &&
                     std::string(vietvm::constants::kReqFieldHeader) == "header" &&
                     std::string(vietvm::constants::kReqFieldQueryParam) == "query_param" &&
                     std::string(vietvm::constants::kReqFieldJsonField) == "json_field" &&
                     std::string(vietvm::constants::kReqFieldPathSuffix) == "path_suffix",
                 "HTTP request-field constants must retain their protocol values");

    if (!ok) return 1;
    std::cout << "opcode and native constants tests passed\n";
    return 0;
}

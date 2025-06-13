#ifndef INSTRUCTION_H
#define INSTRUCTION_H

// Định nghĩa các opcode của bytecode
enum Opcode {
    // ====== 1. Toán học - Arithmetic ======
    OP_CONG,                // 0  +
    OP_TRU,                 // 1  -
    OP_NHAN,                // 2  *
    OP_CHIA,                // 3  /

    // ====== 2. Logic & So sánh - Logic and Comparisons ======
    OP_VA,                  // 4  &&
    OP_HOAC,                // 5  ||
    OP_KHONG,               // 6  !
    OP_SO_SANH_BANG,        // 7  ==
    OP_KHAC_BANG,           // 8  !=
    OP_LON_HON,             // 9  >
    OP_NHO_HON,             // 10 <
    OP_LON_HON_HOAC_BANG,   // 11 >=
    OP_NHO_HON_HOAC_BANG,   // 12 <=
    OP_GAN,                 // 13 =

    // ====== 3. Dấu câu & ký hiệu cú pháp - Syntax Tokens ======
    OP_MO_KHOI,             // 14 {
    OP_DONG_KHOI,           // 15 }
    OP_MO_NGOAC,            // 16 (
    OP_DONG_NGOAC,          // 17 )
    OP_MO_MANG,             // 18 [
    OP_DONG_MANG,           // 19 ]
    OP_DONG_LENH,           // 20 ;
    OP_PHAY,                // 21 ,

    // ====== 4. Kiểm soát luồng - Control Flow ======
    OP_NEU,                 // 22 NẾU
    OP_KHAC,                // 23 KHÁC
    OP_NEU_KHONG,           // 24 NẾU_KHÔNG
    OP_KET_THUC_NEU,        // 25 Kết thúc khối if (tuỳ bạn xử lý VM)

    OP_LAP,                 // 26 LẶP
    OP_KHOI_TAO,            // 27 KHỞI_TẠO
    OP_DIEU_KIEN,           // 28 ĐIỀU_KIỆN
    OP_CAP_NHAT,            // 29 CẬP_NHẬT
    OP_KIEM_TRA_SAU,        // 30 KIỂM_TRA_SAU
    OP_KET_THUC_LAP,        // 31 Kết thúc vòng lặp

    OP_BO_QUA,              // 32 BỎ_QUA (continue)
    OP_THOAT,               // 33 THOÁT (break)

    OP_CHUYEN,              // 34 CHUYỂN (switch)
    OP_TRUONG_HOP,          // 35 TRƯỜNG_HỢP (case)
    OP_MAC_DINH,            // 36 MẶC_ĐỊNH (default)
    OP_KET_THUC_CHUYEN,     // 37 Kết thúc switch

    // ====== 5. Hàm - Functions ======
    OP_HAM,                 // 38 HÀM: định nghĩa
    OP_GOI,                 // 39 GỌI_HÀM: gọi hàm
    OP_TRA_VE,              // 40 TRẢ_VỀ

    // ====== 6. Khác - Miscellaneous ======
    OP_BIEN_SO,             // 41 BIẾN: đẩy giá trị biến hoặc hằng số
    OP_TEN_BIEN,            // 42 TÊN BIẾN

    OP_IN,                  // 43 IN: in giá trị
    OP_DUNG_CHUONG_TRINH    // 44 DỪNG: kết thúc chương trình
};

enum CompareOp {
    CMP_LE = 0, // <=
    CMP_LT = 1, // <
    CMP_EQ = 2, // ==
    CMP_NE = 3, // !=
    CMP_GE = 4, // >=
    CMP_GT = 5, // >
};

// Cấu trúc của một câu lệnh bytecode
struct Instruction {
    Opcode op;
    int operand; // chỉ dùng cho OP_BIEN_SO (đẩy giá trị biến hoặc hằng số)
    int operandIndex; // Thêm dòng này để xác định chỉ số biến (ví dụ: i trong for)
};

#endif // INSTRUCTION_H

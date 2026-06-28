#ifndef INSTRUCTION_H
#define INSTRUCTION_H

// Định nghĩa các opcode của bytecode
enum Opcode {
    // ====== 1. Toán học - Arithmetic ======
    OP_CONG = 0,                // 0  +
    OP_TRU = 1,                 // 1  -
    OP_NHAN = 2,                // 2  *
    OP_CHIA = 3,                // 3  /

    // ====== 2. Logic & So sánh - Logic and Comparisons ======
    OP_Logic_VA = 4,            // 4  &&
    OP_Logic_HOAC = 5,          // 5  ||
    OP_KHONG = 6,               // 6  !
    OP_SO_SANH_BANG = 7,        // 7  ==
    OP_KHAC_BANG = 8,           // 8  !=
    OP_LON_HON = 9,             // 9  >
    OP_NHO_HON = 10,            // 10 <
    OP_LON_HON_HOAC_BANG = 11,   // 11 >=
    OP_NHO_HON_HOAC_BANG = 12,   // 12 <=
    OP_GAN = 13,                 // 13 =

    // ====== 3. Dấu câu & ký hiệu cú pháp - Syntax Tokens ======
    OP_MO_KHOI = 14,             // 14 {
    OP_DONG_KHOI = 15,           // 15 }
    OP_MO_NGOAC = 16,            // 16 (
    OP_DONG_NGOAC = 17,          // 17 )
    OP_MO_MANG = 18,             // 18 [
    OP_DONG_MANG = 19,           // 19 ]
    OP_DONG_LENH = 20,           // 20 ;
    OP_PHAY = 21,                // 21 ,

    // ====== 4. Kiểm soát luồng - Control Flow ======
    OP_NEU = 22,                 // 22 NẾU
    OP_HOAC = 23,                // 23 HOẶC
    OP_NEU_KHONG = 24,           // 24 NẾU_KHÔNG
    OP_KET_THUC_NEU = 25,        // 25 Kết thúc khối if (tuỳ bạn xử lý VM)

    OP_LAP = 26,                 // 26 LẶP
    OP_KHOI_TAO = 27,            // 27 KHỞI_TẠO
    OP_DIEU_KIEN = 28,           // 28 ĐIỀU_KIỆN
    OP_CAP_NHAT = 29,            // 29 CẬP_NHẬT
    OP_KIEM_TRA_SAU = 30,        // 30 KIỂM_TRA_SAU
    OP_KET_THUC_LAP = 31,        // 31 Kết thúc vòng lặp

    OP_BO_QUA = 32,              // 32 BỎ_QUA (continue)
    OP_THOAT = 33,               // 33 THOÁT (break)

    OP_CHUYEN = 34,              // 34 CHUYỂN (switch)
    OP_TRUONG_HOP = 35,          // 35 TRƯỜNG_HỢP (case)
    OP_MAC_DINH = 36,            // 36 MẶC_ĐỊNH (default)
    OP_KET_THUC_CHUYEN = 37,     // 37 Kết thúc switch

    // ====== 5. Hàm - Functions ======
    OP_HAM = 38,                 // 38 HÀM: định nghĩa
    OP_GOI = 39,                 // 39 GỌI_HÀM: gọi hàm
    OP_TRA_VE = 40,              // 40 TRẢ_VỀ

    // ====== 6. Khác - Miscellaneous ======
    OP_BIEN_SO = 41,             // 41 BIẾN: đẩy giá trị biến hoặc hằng số
    // OP_TEN_BIEN = 42,         // 42 TÊN BIẾN

    OP_IN = 43,                  // 43 IN: in giá trị
    OP_DUNG_CHUONG_TRINH = 44,   // 44 DỪNG: kết thúc chương trình
    OP_TEN_BIEN_ID = 60,         // Đẩy ID của biến lên stack
    OP_TEN_BIEN_GIA_TRI = 61,    // Đẩy giá trị của biến lên stack
    OP_JUMP_IF_FALSE = 62,       // Nhảy nếu điều kiện sai (stack top == 0)
    OP_JUMP          = 63,       // Nhảy vô điều kiện
    OP_MODULO        = 64,       // Chia lấy dư
    OP_CHUOI         = 65,       // Chuỗi
    OP_PHU_DINH      = 66,        // Phủ định cái gì đó.
    OP_CHON           = 67,      // switch chọn
    OP_CA            = 68,       // case ca
    OP_PARAM = 69,               // tham số
    OP_CONG_MOT = 70,            // tương đương với i = i + 1
    OP_TRU_MOT = 71,             // tương đương với i = i - 1 (--)
    OP_CONG_GAN = 72,            // +=
    OP_TRU_GAN = 73,             // -=
    OP_NHAN_GAN = 74,            // *=
    OP_CHIA_GAN = 75,            // /=
    OP_MODULO_GAN = 76,          // %=
    OP_DUNG_GIA_TRI = 77,        // true (đúng) → push 1
    OP_SAI_GIA_TRI = 78,         // false (sai) → push 0
    OP_BIEN_SO_FLOAT = 79,       // push float literal (operandIndex = string pool index)
    OP_NEM = 80,                 // ném (throw)
    OP_THU = 81,                 // thử (try begin, operand = catch_addr)
    OP_THU_KET_THUC = 82,        // kết thúc try block, operand = past_catch_addr
    OP_BAT_LOI = 83,             // bắt lỗi (catch begin, operandIndex = err_var_id or -1)
    OP_RONG_GIA_TRI = 84,        // rỗng (null)
    OP_MAP_LITERAL = 85,         // map literal (operandIndex = encoded map in string pool)
    OP_GOI_GIAN_TIEP = 86,       // gọi hàm qua giá trị biến (function ref)
    OP_PARAM_MAC_DINH = 87       // bind tham số với giá trị mặc định

};

enum CompareOp {
    CMP_LE = 0, // <=
    CMP_LT = 1, // <
    CMP_EQ = 2, // ==
    CMP_NE = 3, // !=
    CMP_GE = 4, // >=
    CMP_GT = 5  // >
};

// Cấu trúc của một câu lệnh bytecode
struct Instruction {
    Opcode op;
    int operand; // chỉ dùng cho OP_BIEN_SO (đẩy giá trị biến hoặc hằng số)
    int operandIndex; // Thêm dòng này để xác định chỉ số biến (ví dụ: i trong for)
    int operandValue;
};

#endif // INSTRUCTION_H

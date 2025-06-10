// instruction.h
#ifndef INSTRUCTION_H
#define INSTRUCTION_H

// Định nghĩa các opcode của bytecode
enum Opcode {
    OP_BIEN_SO, // BIẾN Số: đẩy hằng số vào stack
    OP_NEU, //Hàm Nếu
    OP_CONG,   // CỘNG: Phép Cộng
    OP_TRU,   // CỘNG: Phép trừ
    OP_NHAN,   // CỘNG: Phép Nhân
    OP_CHIA,   // CỘNG: Phép Chia
    OP_IN,     // IN: in giá trị ra màn hình
    OP_DUNG_CHUONG_TRINH,    // DỪNG: dừng chương trình
    OP_NGUOC_LAI,
    OP_LAP,
    OP_KET_THUC_LAP,
    OP_HAM,
    OP_GOI_HAM,
    OP_TRA_VE,
    OP_BO_QUA,
    OP_THOAT,
    OP_VA,
    OP_HOAC,
    OP_KHONG,
};


// Cấu trúc của một câu lệnh bytecode
struct Instruction {
    Opcode op;
    int operand; // chỉ dùng cho OP_BIẾN (BIẾN)
};

#endif // INSTRUCTION_H

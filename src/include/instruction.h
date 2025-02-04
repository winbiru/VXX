// instruction.h
#ifndef INSTRUCTION_H
#define INSTRUCTION_H

// Định nghĩa các opcode của bytecode
enum Opcode {
    OP_BIEN, // BIẾN Số: đẩy hằng số vào stack
    OP_NEU, //Hàm Nếu
    OP_CONG,   // CỘNG: Phép Cộng
    OP_TRU,   // CỘNG: Phép trừ
    OP_NHAN,   // CỘNG: Phép Nhân
    OP_CHIA,   // CỘNG: Phép Chia
    OP_IN,     // IN: in giá trị ra màn hình
    OP_DUNG    // DỪNG: dừng chương trình
};


// Cấu trúc của một câu lệnh bytecode
struct Instruction {
    Opcode op;
    int operand; // chỉ dùng cho OP_BIẾN (BIẾN)
};

#endif // INSTRUCTION_H

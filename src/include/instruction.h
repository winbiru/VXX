// instruction.h
#ifndef INSTRUCTION_H
#define INSTRUCTION_H

// Định nghĩa các opcode của bytecode
enum Opcode {
    OP_TAI_SO, // TẢI_SỐ: đẩy hằng số vào stack
    OP_CONG,   // CỘNG: cộng 2 số
    OP_IN,     // IN: in giá trị ra màn hình
    OP_DUNG    // DỪNG: dừng chương trình
};


// Cấu trúc của một câu lệnh bytecode
struct Instruction {
    Opcode op;
    int operand; // chỉ dùng cho OP_TAI_SO (TẢI_SỐ)
};

#endif // INSTRUCTION_H

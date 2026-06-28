# Bytecode — V++ (nhánh `developer`)

Tài liệu này mô tả định dạng bytecode đề xuất cho V++, cách mã hóa instructions/operands, layout file/section, một số opcode chính lấy cảm hứng từ `include/keywords.h`, và ví dụ mẫu. Mục tiêu: có spec đủ rõ để hiện thực assembler/disassembler, loader và verifier trong `src/vm`/`src/compiler`.

> Ghi chú: đây là đề xuất dựa trên cấu trúc repo hiện tại. Sau khi thống nhất spec, cần đồng bộ `include/instruction.h` (enum Opcode) và lexer/parser/assembler.

---

## 1. Tổng quan
- Kiến trúc: stack-based VM (nghiên cứu các opcodes thấy thuận lợi cho stack machine).
- Bytecode là chuỗi bytes độc lập nền tảng, little-endian, với cấu trúc file có header + sections.
- Giá trị số nguyên được mã hóa bằng unsigned LEB128 (ULEB128) để tiết kiệm không gian cho hầu hết các constants/offset nhỏ.
- String, symbol, constant được tham chiếu qua Constant Pool (indices).

---

## 2. Bytecode file layout (đề xuất)
File .vbc (Viet ByteCode) cấu trúc như sau:

- Magic (4 bytes): 'V','M','B','C'  (0x56 0x4D 0x42 0x43)
- Version (1 byte): format version (ví dụ 0x01)
- Flags (1 byte): bitmask (endianness, reserved)
- Header padding/reserved (2 bytes) => tổng 8 bytes header

Sections (tuần tự):
1. Constant Pool Section
    - uleb128: count
    - repeat count lần:
        - u8: tag (0 = Integer, 1 = Float, 2 = String, 3 = FunctionRef, 4 = NativeRef, ...)
        - payload: tùy tag (number raw hoặc uleb128 length + bytes cho string)
2. Function Table Section
    - uleb128: function_count
    - cho mỗi function:
        - uleb128: name_const_index (index vào constant pool string)
        - uleb128: num_params
        - uleb128: num_locals
        - uleb128: code_size (số byte)
        - code bytes (sequence of instructions)
        - uleb128: debug_info_index (optional, 0 = none)
3. Data/Globals Section (optional)
    - uleb128: global_count
    - cho mỗi global: name_const_index, init_const_index (-1 or 0 for none), flags
4. Symbol/Table/Debug section (optional)

Sections có thể mở rộng; mỗi section bắt đầu với section id byte và length (ULEB128) nếu muốn hỗ trợ skipping.

---

## 3. Instruction encoding (đề xuất)
- Mỗi instruction bắt đầu bằng 1 byte opcode (0..255).
- Sau opcode là 0..n operand bytes tuỳ opcode. Số lượng và kiểu operand mô tả trong spec opcode.
- Số nguyên/offset: mã hóa bằng ULEB128.
- Constant reference: ULEB128 index vào constant pool.
- Jump offsets: relative signed LEB128 (SLEB128) hoặc ULEB128 chỉ byte-offset trong code.

Ví dụ encoding mẫu:
- PUSH_CONST <const_index> : [OP_PUSH_CONST][ULEB128(index)]
- LOAD_LOCAL <local_index>  : [OP_LOAD_LOCAL][ULEB128(index)]
- STORE_LOCAL <local_index> : [OP_STORE_LOCAL][ULEB128(index)]
- CALL <func_index> <arg_count> : [OP_CALL][ULEB128(func_index)][ULEB128(arg_count)]
- RET <ret_count_opt> : [OP_RET][ULEB128(ret_count)]  (hoặc OP_RET không operand)

---

## 4. Opcode đề xuất & ý nghĩa (mapping từ include/keywords.h)
Dưới đây là danh sách opcode tên gợi ý (cần đồng bộ enum trong `include/instruction.h`). Mỗi mục gồm tên, mô tả ngắn và operands nếu có.

- OP_NOP (0x00) — no-op
- OP_PUSH_CONST (0x01) — push constant pool[idx] onto stack. operand: uleb128 const_index
- OP_LOAD_LOCAL (0x02) — push local var value. operand: uleb128 local_index
- OP_STORE_LOCAL (0x03) — pop -> store into local. operand: uleb128 local_index
- OP_LOAD_GLOBAL (0x04) — load global var. operand: uleb128 global_index
- OP_STORE_GLOBAL (0x05) — store global. operand: uleb128 global_index

Aritmetic / numeric:
- OP_CONG (ADD) (0x10) — pop b,a -> push (a + b)
- OP_TRU (SUB) (0x11)
- OP_NHAN (MUL) (0x12)
- OP_CHIA (DIV) (0x13)
- OP_MODULO (MOD) (0x14)
- OP_PHU_DINH (UNARY_NEG) (0x15) — unary minus

Logic / Comparison:
- OP_KHONG (NOT) (0x20)
- OP_Logic_VA (AND) (0x21)
- OP_Logic_HOAC (OR) (0x22)
- OP_SO_SANH_BANG (EQ) (0x23)
- OP_KHAC_BANG (NEQ) (0x24)
- OP_LON_HON (GT) (0x25)
- OP_NHO_HON (LT) (0x26)
- OP_LON_HON_HOAC_BANG (GE) (0x27)
- OP_NHO_HON_HOAC_BANG (LE) (0x28)

Control flow:
- OP_JUMP (0x30) — unconditional jump. operand: sleb128 offset (relative from next byte)
- OP_JUMP_IF_FALSE (0x31) — pop condition; if false jump offset.
- OP_CALL (0x40) — operand: uleb128 func_index, uleb128 arg_count (or args are already on stack)
- OP_GOI (alias for OP_CALL) if you keep source name
- OP_TRA_VE (OP_RET) (0x41) — return (optionally with count)
- OP_MO_KHOI / OP_DONG_KHOI — for block delimiters in high-level not needed in bytecode

Structured / other:
- OP_MO_MANG / OP_DONG_MANG — array ops or handled by library
- OP_IN (0x50) — builtin print: operand: count of args or uses stack
- OP_KET_THUC_LAP / OP_BO_QUA / OP_THOAT — mapped to control flow (continue, break, exit) implemented by jumps

Notes:
- Tên opcode ở trên lấy cảm hứng từ file keywords.h; khi đồng bộ hãy sửa enum value và gắn số opcode cụ thể.
- Một số khái niệm (ví dụ OP_MO_KHOI) là cú pháp bậc cao; bytecode có thể không cần những opcode tương ứng nếu compiler chuyển chúng thành jump/call/stack ops.

---

## 5. Constant pool (chi tiết)
- Mục đích: giảm lặp chuỗi, giữ function names, literal strings, số, references.
- Tag ví dụ:
    - 0x00 = INT (signed 64-bit) → payload: SLEB128
    - 0x01 = DOUBLE (IEEE754 64-bit) → payload: 8 bytes little-endian
    - 0x02 = STRING → payload: uleb128 length + bytes (UTF-8)
    - 0x03 = SYMBOL/FUNC_NAME → string index (uleb128) or direct string
- Các chỉ số pool bắt đầu từ 0.

---

## 6. Function table / frames
- Mỗi function có:
    - number of params, number of locals, max stack size (optional), code_size, code bytes.
- Khi CALL: create new frame with locals array sized num_locals, push return address + previous frame pointer.
- Locals và params được truy cập bằng LOAD_LOCAL/STORE_LOCAL (index 0..n-1, quy ước param trước local hay ngược lại).

---

## 7. Assembler / Disassembler API
- Assembler: input là human-readable mnemonics (ví dụ PUSH_CONST 10; LOAD_LOCAL 0; OP_CONG), output là bytecode file.
- Disassembler: đọc bytes, map opcode -> mnemonic, resolve constant pool indices to readable forms.
- Đề xuất sử dụng JSON hoặc TOML cho intermediate representation (IR) giữa compiler và assembler.

---

## 8. Ví dụ bytecode (mẫu)
Giả sử constant pool:
0: INT 2
1: INT 3

Function main code (pseudo):
- PUSH_CONST 0
- PUSH_CONST 1
- OP_CONG
- OP_IN
- OP_TRA_VE

Giả sử opcode encoding:
OP_PUSH_CONST = 0x01
OP_CONG = 0x10
OP_IN = 0x50
OP_TRA_VE = 0x41

Mã byte (hex, ULEB128 for indices small => single byte):
56 4D 42 43 01 00 00 00   ; header
... pool ...
01 00   ; PUSH_CONST 0x00
01 01   ; PUSH_CONST 0x01
10      ; OP_CONG
50      ; OP_IN
41      ; OP_TRA_VE

(Đây là minh họa đơn giản — thực tế file có sections trước mã.)

---

## 9. Verifier & safety
- Trước khi chạy, VM có thể chạy verifier check:
    - function code_size khớp actual length
    - all constant indices in range
    - jumps target land on valid instruction boundary
    - stack depth analysis: kiểm tra không bị underflow/overflow (optional)
- Kiểm tra version compatibility.

---

## 10. Debugging & Profiling
- Gợi ý ghi debug info: mapping bytecode offset -> source file:line (store in debug section).
- Thêm optional instruction names and human-readable disassembly for logging.

---

## 11. Next steps thực thi (gợi ý tasks)
1. Đồng bộ enum Opcode trong `include/instruction.h` với danh sách opcode và gán mã byte (0x00..).
2. Viết assembler prototype `src/tools/assembler`:
    - parse mnemonics -> emit bytes, build constant pool.
3. Viết disassembler `src/tools/disasm` để debug.
4. Implement loader + verifier trong `src/vm/loader.cpp`.
5. Tạo tests: unit test round-trip assemble → disassemble, và small program run tests (sum, if/else, loop).
6. Document final format (patch `bytecode.md`) và add example .vbc files under `tests/fixtures`.

---

## 12. Lời kết
Tài liệu này là bản đề xuất chi tiết đủ để:
- implement assembler/disassembler,
- sửa `include/instruction.h`,
- xây loader/verifier trong VM.

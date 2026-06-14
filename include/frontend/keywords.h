#ifndef TO_STRING_H
#define TO_STRING_H

#include <string>
#include <unordered_map>
#include "../vm/instruction.h"

extern const std::unordered_map<std::string, Opcode> keywordMap;


// Đổi Opcode thành tên chuỗi để debug
std::string name_op(Opcode op);


#endif // TO_STRING_H

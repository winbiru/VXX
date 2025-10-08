// compiler.h
#ifndef COMPILER_H
#define COMPILER_H

#include <vector>
#include <string>
#include <unordered_map>
#include "instruction.h"
#include "../include/name_op.h"  // Đảm bảo đã có

// Hàm biên dịch: chuyển từ mã nguồn (string) sang vector<Instruction>
std::vector<Instruction> compileSource(const std::string &source,
                                       const std::unordered_map<std::string, Opcode> &keywordMap);

extern std::unordered_map<std::string, int> symbolTable;
extern int nextSymbolIndex;
extern std::unordered_map<std::string, int> variableTable;
extern int nextVariableID;

void compileToken(const std::string & tok, const std::vector<Instruction> & vector, const std::unordered_map<std::string, int> & pairs, int next_var_id, const std::unordered_map<std::string, Opcode> & keyword_map);
std::vector<std::string> convertToPostfix(const std::vector<std::string>& infix_tokens);

inline int getVariableID(const std::string& name) {
    if (variableTable.count(name)) {
        return variableTable[name];
    } else {
        int id = nextVariableID++;
        variableTable[name] = id;
        return id;
    }
}

#endif // COMPILER_H

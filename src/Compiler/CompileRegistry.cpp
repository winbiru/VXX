//
// Created by nx_thang on 10/21/2025.
//

#include "../../include/compiler/CompileRegistry.h"

#include "common/LoopUltil.h"
#include "common/Utility.h"
#include "compiler/compileCondition.h"
#include "compiler/compileFunction.h"
#include "compiler/compileLoop.h"
#include "compiler/compilerExpr.h"
#include "compiler/compileSwitch.h"

std::unordered_map<std::string, CompileFunc> compileMap;
std::unordered_map<int, std::vector<Instruction>> hamBytecodeMap;

void initCompileMap() {
    compileMap["in"]  = [](const std::vector<std::string>& tokens, size_t &pos,
                           std::vector<Instruction>& bytecode,
                           std::unordered_map<std::string,int>& symTab,
                           int& nextId,
                           const std::unordered_map<std::string,Opcode>& kwMap) {
                                ++pos; // bỏ qua "in"
                                auto pr = vietvm::compiler::extractExpressionUntilSemicolon(tokens, pos);
                                compileExpr(pr.first, bytecode, symTab, nextId, kwMap);
                                bytecode.push_back({OP_IN, 0, 0});
                                pos = pr.second;
    };

    compileMap["nếu"] = compileCondition;
    compileMap["lặp"] = [](const std::vector<std::string>& tokens, size_t &pos,
                       std::vector<Instruction>& bytecode,
                       std::unordered_map<std::string,int>& symTab,
                       int& nextId,
                       const std::unordered_map<std::string,Opcode>& keywordMap) {
                            if (tokens[pos] == "lặp") {
                                std::string loopHeader = vietvm::compiler::extractParens(tokens, pos + 1).first;
                                std::vector<std::string> parts = splitLoopParts(loopHeader);
                                std::string varName = vietvm::compiler::extractAssignedVar(parts[0]);
                                if (!varName.empty()) {
                                    if (symTab.find(varName) == symTab.end()) {
                                        symTab[varName] = nextId++;
                                    }
                                    int varId = symTab[varName];
                                    bytecode.push_back({OP_KHOI_TAO, varId, 0});
                                }
                                compileLoop(tokens, pos, bytecode, symTab, nextId, keywordMap);
                            }
                       };
    compileMap["chọn"] = compileSwitch;
    compileMap["hàm"] = [&](const std::vector<std::string>& tokens, size_t& pos,
                            std::vector<Instruction>& bytecode,
                            std::unordered_map<std::string, int>& symTab,
                            int& nextId,
                            const std::unordered_map<std::string, Opcode>& keywordMap) {
        compileFunction(tokens, pos, symTab, nextId, keywordMap, hamBytecodeMap);
    };
}
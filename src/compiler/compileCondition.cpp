//
// Created by nx_thang on 10/21/2025.
//

#include "../../include/compiler/compileCondition.h"

#include <string>
#include <unordered_map>
#include <vector>

#include "../../include/compiler/compileStatement.h"
#include "../../include/vm/instruction.h"
#include "common/utility.h"
#include "compiler/compileBlock.h"
#include "compiler/compilerExpr.h"

struct Instruction;

void compileCondition(const std::vector<std::string>& tokens, size_t &pos,
                                  std::vector<Instruction> &bytecode,
                                  std::unordered_map<std::string,int> &symTab,
                                  int &nextId,
                                  const std::unordered_map<std::string,Opcode> &keywordMap)
{
    // tokens[pos] is 'nếu' (OP_NEU)
    // parse parens
    auto parenPair = vietvm::compiler::extractParens(tokens, pos + 1); // expects '(' at pos+1
    std::string condExpr = parenPair.first;
    size_t afterParen = parenPair.second;

    // compile condition: push OP_NEU/OP_MO_NGOAC markers similar to earlier design
    bytecode.push_back({OP_NEU,0,0,0});
    bytecode.push_back({OP_MO_NGOAC,0,0,0});
    // compile condition expression
    compileExpr(condExpr, bytecode, symTab, nextId, keywordMap);
    // push jump if false placeholder
    bytecode.push_back({OP_JUMP_IF_FALSE, 0, 0,0});
    int jumpIndex = (int)bytecode.size() - 1;
    bytecode.push_back({OP_DONG_NGOAC,0,0,0});

    pos = afterParen; // pos after ')'

    // Now expect block
    if (pos < tokens.size() && tokens[pos] == "{") {
        // open block
        bytecode.push_back({OP_MO_KHOI,0,0,0});
        compileBlock(tokens, pos, bytecode, symTab, nextId, keywordMap);
        bytecode.push_back({OP_DONG_KHOI,0,0,0});
    } else {
        // single statement after if
        compileStatement(tokens, pos, bytecode, symTab, nextId, keywordMap);
    }

    // Optional else branch: "hoặc { ... }" or "nếu không { ... }"
    bool hasElse = false;
    if (pos < tokens.size()) {
        if (tokens[pos] == "hoặc" || tokens[pos] == "nếu không") {
            hasElse = true;
        }
    }

    if (!hasElse) {
        // no else: jump-if-false goes to after if-body
        int target = (int)bytecode.size();
        bytecode[jumpIndex].operand = target;
        return;
    }

    // with else:
    // - false jump goes to else-start
    // - end of if-body jumps over else-body
    bytecode.push_back({OP_JUMP, 0, 0, 0});
    int jumpOverElseIndex = (int)bytecode.size() - 1;

    // consume else token
    ++pos;

    int elseStart = (int)bytecode.size();
    bytecode[jumpIndex].operand = elseStart;

    if (pos < tokens.size() && tokens[pos] == "{") {
        bytecode.push_back({OP_MO_KHOI,0,0,0});
        compileBlock(tokens, pos, bytecode, symTab, nextId, keywordMap);
        bytecode.push_back({OP_DONG_KHOI,0,0,0});
    } else {
        compileStatement(tokens, pos, bytecode, symTab, nextId, keywordMap);
    }

    int endTarget = (int)bytecode.size();
    bytecode[jumpOverElseIndex].operand = endTarget;
}

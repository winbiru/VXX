// compiler.cpp
#include "../include/compiler.h"
#include "../include/common/Utility.h"
#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <cctype>
#include <iostream>
#include <stack>
#include <unordered_map>
#include "../include/instruction.h"
#include "common/LoopUltil.h" // nếu bạn đã có splitLoopParts(...) ở đây
#include <functional>
#include <string>
// Trim helper
static std::string trim(const std::string &s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static int getOrCreate(std::unordered_map<std::string,int>& symTab,
                       const std::string& name,
                       int& nextId) {
    auto it = symTab.find(name);
    if (it == symTab.end()) {
        symTab[name] = nextId;
        return nextId++;
    }
    return it->second;
}

// ---------- Tokenizer ----------
static std::vector<std::string> tokenize(const std::string &src) {
    std::vector<std::string> tokens;
    size_t i = 0;
    while (i < src.size()) {
        auto ch = static_cast<unsigned char>(src[i]);
        if (isspace(ch)) { ++i; continue; }
        // handle string literals
        if (ch == '"') {
            size_t j = i + 1;
            while (j < src.size() && src[j] != '"') ++j;

            if (j < src.size()) {
                ++j; // include closing quote
                tokens.push_back(src.substr(i, j - i));
                i = j;
            } else {
                // Không có dấu " đóng → chỉ lấy phần còn lại như một token lỗi
                tokens.push_back(src.substr(i));
                i = src.size();
            }
            continue;
        }
        // two-char tokens
        if (i + 1 < src.size()) {
            std::string two = src.substr(i, 2);
            if (two == "==" || two == "!=" || two == "<=" || two == ">=" ||
                two == "&&" || two == "||" || two == "++" || two == "--") {
                tokens.push_back(two);
                i += 2;
                continue;
            }
        }

        // single char tokens that we treat separately
        char c = src[i];
        if (c == '+' || c == '-' || c == '*' || c == '/' ||
            c == '(' || c == ')' || c == '{' || c == '}' ||
            c == '[' || c == ']' || c == ';' || c == ',' ||
            c == '<' || c == '>' || c == '=' || c == '!') {
            tokens.emplace_back(1, c);
            ++i;
             continue;
        }

        // word / number
        size_t j = i;
        while (j < src.size()) {
            auto cj = static_cast<unsigned char>(src[j]);
            if (isspace(cj)) break;
            // break on these punctuation chars
            if (cj == '"' || cj == '+' || cj == '-' || cj == '*' || cj == '/' ||
                cj == '(' || cj == ')' || cj == '{' || cj == '}' ||
                cj == '[' || cj == ']' || cj == ';' || cj == ',' ||
                cj == '<' || cj == '>' || cj == '=' || cj == '!') break;
            ++j;
        }
        tokens.push_back(src.substr(i, j - i));
        i = j;
    }
    return tokens;
}

// ---------- Helpers ----------
static bool isNumber(const std::string &s) {
    if (s.empty()) return false;
    size_t start = (s[0] == '-') ? 1 : 0;
    return std::all_of(s.begin()+start, s.end(), [](char c){ return std::isdigit(static_cast<unsigned char>(c)); });
}

static bool isOperator(const std::string &tok) {
    static const std::unordered_map<std::string,int> ops = {
        {"=",0},{"||",1},{"&&",2},{"==",3},{"!=",3},{"<",3},{">",3},{"<=",3},{">=",3},
        {"+",4},{"-",4},{"*",5},{"/",5},{"%",5}, {"!",6}
    };
    return ops.count(tok) > 0;
}

bool isStringLiteral(const std::string & tk);

static bool isVariable(const std::string &tok) {
    if (tok.empty()) return false;
    if (isStringLiteral(tok)) return false;
    if (isNumber(tok) || isOperator(tok)) return false;
    if (tok == "(" || tok == ")" || tok == "{" || tok == "}" || tok == ";" || tok == ",") return false;

    // Biến phải bắt đầu bằng chữ cái hoặc dấu gạch dưới
    if (!std::isalpha(tok[0]) && tok[0] != '_') return false;

    // Các ký tự còn lại phải là chữ cái, số hoặc dấu gạch dưới
    for (char c : tok) {
        if (!std::isalnum(c) && c != '_') return false;
    }

    return true;
}
bool isStringLiteral(const std::string& tk) {
    return tk.size() >= 2 && tk.front() == '"' && tk.back() == '"';
}
extern std::vector<std::string> stringPool;

int storeString(const std::string& s) {
    stringPool.push_back(s);
    return static_cast<int>(stringPool.size() - 1);
}
// parse from tokens[start] which must be '(' -> return content string and index after closing ')'
static std::pair<std::string, size_t> extractParens(const std::vector<std::string>& tokens, size_t start) {
    if (start >= tokens.size() || tokens[start] != "(")
        throw std::runtime_error("extractParens: expected '('");
    std::ostringstream oss;
    int depth = 1;
    size_t i = start + 1;
    while (i < tokens.size() && depth > 0) {
        if (tokens[i] == "(") { ++depth; }
        else if (tokens[i] == ")") { --depth; if (depth == 0) { ++i; break; } }
        if (depth > 0) {
            oss << tokens[i];
            // put space except when token is semicolon or closing brace (space is harmless)
            oss << " ";
        }
        ++i;
    }
    if (depth != 0) throw std::runtime_error("extractParens: unbalanced parentheses");
    return {trim(oss.str()), i};
}

// extract block content from tokens[start] where start points to '{', returns content and index after closing '}'
static std::pair<std::string, size_t> extractBlock(const std::vector<std::string>& tokens, size_t start) {
    if (start >= tokens.size() || tokens[start] != "{")
        throw std::runtime_error("extractBlock: expected '{'");
    std::ostringstream oss;
    int depth = 1;
    size_t i = start + 1;
    while (i < tokens.size() && depth > 0) {
        if (tokens[i] == "{") { ++depth; }
        else if (tokens[i] == "}") { --depth; if (depth == 0) { ++i; break; } }
        if (depth > 0) {
            oss << tokens[i] << " ";
        }
        ++i;
    }
    if (depth != 0) throw std::runtime_error("extractBlock: unbalanced braces");
    return {trim(oss.str()), i};
}

// Compose expression tokens until semicolon or end; returns expr string and index after semicolon
static std::pair<std::string, size_t> extractExpressionUntilSemicolon(const std::vector<std::string>& tokens, size_t start) {
    std::ostringstream oss;
    size_t i = start;
    while (i < tokens.size() && tokens[i] != ";") {
        oss << tokens[i] << " ";
        ++i;
    }
    if (i < tokens.size() && tokens[i] == ";") ++i; // skip semicolon
    return {trim(oss.str()), i};
}

// ---------- Shunting-yard to postfix ----------
static int precedence_op(const std::string& op) {
    if (op == "=") return 0;
    if (op == "||") return 1;
    if (op == "&&") return 2;
    if (op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=") return 3;
    if (op == "+" || op == "-") return 4;
    if (op == "*" || op == "/" || op == "%") return 5;
    if (op == "!") return 6; // Phủ định
    return -1;
}
static char associativity_op(const std::string &op) { return (op == "=") ? 'r' : 'l'; }

static std::vector<std::string> convertToPostfix(const std::vector<std::string>& infix_tokens) {
    std::vector<std::string> output;
    std::stack<std::string> ops;

    for (const auto &token : infix_tokens) {
        if (token.empty()) continue;

        // Xử lý toán hạng: số, chuỗi, biến
        if (isNumber(token) || isStringLiteral(token) || isVariable(token)) {
            output.push_back(token);
        }

        // Xử lý toán tử
        else if (isOperator(token)) {
            while (!ops.empty() && isOperator(ops.top())) {
                const std::string &top = ops.top();
                if ((precedence_op(top) > precedence_op(token)) ||
                    (precedence_op(top) == precedence_op(token) && associativity_op(token) == 'l')) {
                    output.push_back(top);
                    ops.pop();
                } else break;
            }
            ops.push(token);
        }

        // Dấu mở ngoặc
        else if (token == "(") {
            ops.push(token);
        }

        // Dấu đóng ngoặc
        else if (token == ")") {
            while (!ops.empty() && ops.top() != "(") {
                output.push_back(ops.top());
                ops.pop();
            }
            if (ops.empty()) throw std::runtime_error("convertToPostfix: mismatched parens");
            ops.pop(); // pop "("
        }

        // Token không hợp lệ
        else {
            throw std::runtime_error("convertToPostfix: unknown token '" + token + "'");
        }
    }

    // Đẩy nốt các toán tử còn lại
    while (!ops.empty()) {
        if (ops.top() == "(" || ops.top() == ")")
            throw std::runtime_error("convertToPostfix: mismatched parens");
        output.push_back(ops.top());
        ops.pop();
    }

    return output;
}


// ---------- compileExpr: produce bytecode for an expression or assignment ----------
static void compileExpr(const std::string &expr,
                        std::vector<Instruction> &bytecode,
                        std::unordered_map<std::string,int> &symTab,
                        int &nextId,
                        const std::unordered_map<std::string,Opcode> &keywordMap)
{
    if (trim(expr).empty()) return;

    // Tokenize the expr string
    auto toks = tokenize(expr);

    // Detect assignment: form: <var> = <rhs...>
    auto itEq = std::find(toks.begin(), toks.end(), "=");
    if (itEq != toks.end() && std::distance(toks.begin(), itEq) == 1) {
        // LHS is single var
        std::string varName = toks[0];
        int dstId = getOrCreate(symTab, varName, nextId);

        std::vector<std::string> rhsTokens(itEq + 1, toks.end());
        // convert to postfix
        auto postfix = convertToPostfix(rhsTokens);

        // Generate bytecode for RHS
        for (const auto &tk : postfix) {
            if (isNumber(tk)) {
                bytecode.push_back({OP_BIEN_SO, std::stoi(tk), 0});
            }else if (isStringLiteral(tk)) {
                int strIndex = storeString(tk.substr(1, tk.size() - 2)); // bỏ dấu ngoặc kép
                std::cerr << "[STORE] Chuỗi: " << tk << " → index = " << strIndex << "\n";
                bytecode.push_back({OP_CHUOI, 0, strIndex});
            } else if (isVariable(tk)) {
                int id = getOrCreate(symTab, tk, nextId);
                bytecode.push_back({OP_TEN_BIEN_GIA_TRI, 0, id});
            } else {
                if (tk == "+") bytecode.push_back({OP_CONG,0,0});
                else if (tk == "-") bytecode.push_back({OP_TRU,0,0});
                else if (tk == "*") bytecode.push_back({OP_NHAN,0,0});
                else if (tk == "/") bytecode.push_back({OP_CHIA,0,0});
                else if (tk == "%") bytecode.push_back({OP_MODULO,0,0});
                else if (tk == "==") bytecode.push_back({OP_SO_SANH_BANG,0,0});
                else if (tk == "!") bytecode.push_back({OP_PHU_DINH,0,0});
                else if (tk == "!=") bytecode.push_back({OP_KHAC_BANG,0,0});
                else if (tk == "<") bytecode.push_back({OP_NHO_HON,0,0});
                else if (tk == ">") bytecode.push_back({OP_LON_HON,0,0});
                else if (tk == "<=") bytecode.push_back({OP_NHO_HON_HOAC_BANG,0,0});
                else if (tk == ">=") bytecode.push_back({OP_LON_HON_HOAC_BANG,0,0});
                else if (tk == "&&") bytecode.push_back({OP_Logic_VA,0,0});
                else if (tk == "||") bytecode.push_back({OP_Logic_HOAC,0,0});
                else throw std::runtime_error("compileExpr: unsupported operator " + tk);
            }
        }

        // push destination id and assign
        bytecode.push_back({OP_TEN_BIEN_ID, dstId, 0});
        bytecode.push_back({OP_GAN,0,0});
        return;
    }

    // Otherwise, expression (produce value on stack)
    auto postfix = convertToPostfix(toks);
    for (const auto &tk : postfix) {
        if (isNumber(tk)) {
            bytecode.push_back({OP_BIEN_SO, std::stoi(tk), 0});
        }else if (isStringLiteral(tk)) {
            int strIndex = storeString(tk.substr(1, tk.size() - 2)); // bỏ dấu ngoặc kép
            // std::cerr << "[STORE] Chuỗi: " << tk << " → index = " << strIndex << "\n";
            bytecode.push_back({OP_CHUOI, 0, strIndex});
        } else if (isVariable(tk)) {
            int id = getOrCreate(symTab, tk, nextId);
            bytecode.push_back({OP_TEN_BIEN_GIA_TRI, 0, id});
        } else {
            if (tk == "+") bytecode.push_back({OP_CONG,0,0});
            else if (tk == "-") bytecode.push_back({OP_TRU,0,0});
            else if (tk == "*") bytecode.push_back({OP_NHAN,0,0});
            else if (tk == "/") bytecode.push_back({OP_CHIA,0,0});
            else if (tk == "%") bytecode.push_back({OP_MODULO,0,0});
            else if (tk == "!") bytecode.push_back({OP_PHU_DINH,0,0});
            else if (tk == "==") bytecode.push_back({OP_SO_SANH_BANG,0,0});
            else if (tk == "!=") bytecode.push_back({OP_KHAC_BANG,0,0});
            else if (tk == "<") bytecode.push_back({OP_NHO_HON,0,0});
            else if (tk == ">") bytecode.push_back({OP_LON_HON,0,0});
            else if (tk == "<=") bytecode.push_back({OP_NHO_HON_HOAC_BANG,0,0});
            else if (tk == ">=") bytecode.push_back({OP_LON_HON_HOAC_BANG,0,0});
            else if (tk == "&&") bytecode.push_back({OP_Logic_VA,0,0});
            else if (tk == "||") bytecode.push_back({OP_Logic_HOAC,0,0});
            else throw std::runtime_error("compileExpr: unsupported token " + tk);
        }
    }
}

// forward declarations
static void compileStatement(const std::vector<std::string>& tokens, size_t &pos,
                             std::vector<Instruction> &bytecode,
                             std::unordered_map<std::string,int> &symTab,
                             int &nextId,
                             const std::unordered_map<std::string,Opcode> &keywordMap);

static void compileBlock(const std::vector<std::string>& tokens, size_t &pos,
                         std::vector<Instruction> &bytecode,
                         std::unordered_map<std::string,int> &symTab,
                         int &nextId,
                         const std::unordered_map<std::string,Opcode> &keywordMap);

// ---------- compileConditionBlock (if) ----------
static void compileConditionBlock(const std::vector<std::string>& tokens, size_t &pos,
                                  std::vector<Instruction> &bytecode,
                                  std::unordered_map<std::string,int> &symTab,
                                  int &nextId,
                                  const std::unordered_map<std::string,Opcode> &keywordMap)
{
    // tokens[pos] is 'nếu' (OP_NEU)
    // parse parens
    auto parenPair = extractParens(tokens, pos + 1); // expects '(' at pos+1
    std::string condExpr = parenPair.first;
    size_t afterParen = parenPair.second;

    // compile condition: push OP_NEU/OP_MO_NGOAC markers similar to earlier design
    bytecode.push_back({OP_NEU,0,0});
    bytecode.push_back({OP_MO_NGOAC,0,0});
    // compile condition expression
    compileExpr(condExpr, bytecode, symTab, nextId, keywordMap);
    // push jump if false placeholder
    bytecode.push_back({OP_JUMP_IF_FALSE, 0, 0});
    int jumpIndex = (int)bytecode.size() - 1;
    bytecode.push_back({OP_DONG_NGOAC,0,0});

    pos = afterParen; // pos after ')'

    // Now expect block
    if (pos < tokens.size() && tokens[pos] == "{") {
        // open block
        bytecode.push_back({OP_MO_KHOI,0,0});
        compileBlock(tokens, pos, bytecode, symTab, nextId, keywordMap);
        bytecode.push_back({OP_DONG_KHOI,0,0});
    } else {
        // single statement after if
        compileStatement(tokens, pos, bytecode, symTab, nextId, keywordMap);
    }

    // backpatch jump target to after the if body
    int target = (int)bytecode.size();
    bytecode[jumpIndex].operand = target;
}

// ---------- compileLoop ----------
struct LoopIndices { int cond_index; int exit_jump_index; };

static LoopIndices compileLoop(const std::vector<std::string>& tokens, size_t &pos,
                               std::vector<Instruction> &bytecode,
                               std::unordered_map<std::string,int> &symTab,
                               int &nextId,
                               const std::unordered_map<std::string,Opcode> &keywordMap)
{
    // tokens[pos] is 'lặp' (OP_LAP)
    // next token should be '('
    auto parenPair = extractParens(tokens, pos + 1); // returns content and index after ')'
    std::string inside = parenPair.first;
    size_t afterParen = parenPair.second;

    // parse inside into 3 parts - use splitLoopParts if available (from LoopUltil.h)
    std::vector<std::string> parts;
    try {
        parts = splitLoopParts(inside); // expected to return vector<string> with 3 elements
    } catch (...) {
        // fallback: naive split by ';' into exactly 3 parts
        std::vector<std::string> tmp;
        std::istringstream iss(inside);
        std::string seg;
        while (std::getline(iss, seg, ';')) {
            tmp.push_back(trim(seg));
        }
        if (tmp.size() >= 3) {
            parts = {tmp[0], tmp[1], tmp[2]};
        } else {
            throw std::runtime_error("compileLoop: cannot parse loop parts");
        }
    }

    // 1) compile init
    bytecode.push_back({OP_KHOI_TAO,0,0});
    if (!parts[0].empty()) compileExpr(parts[0], bytecode, symTab, nextId, keywordMap);

    // 2) mark loop start and compile condition
    bytecode.push_back({OP_LAP,0,0});
    int cond_index = (int)bytecode.size();
    bytecode.push_back({OP_DIEU_KIEN,0,0});
    if (!parts[1].empty()) compileExpr(parts[1], bytecode, symTab, nextId, keywordMap);

    // 3) jump-if-false placeholder
    bytecode.push_back({OP_JUMP_IF_FALSE,0,0});
    int exit_jump_index = (int)bytecode.size() - 1;

    // 4) body
    // if following token is block, extract it and compile; else if the caller already has body tokens, caller will call compileBlock
    pos = afterParen;
    if (pos < tokens.size() && tokens[pos] == "{") {
        bytecode.push_back({OP_MO_KHOI,0,0});
        compileBlock(tokens, pos, bytecode, symTab, nextId, keywordMap);
        bytecode.push_back({OP_DONG_KHOI,0,0});
    } else {
        // no block, nothing to do (or caller will handle)
    }

    // 5) update
    bytecode.push_back({OP_CAP_NHAT,0,0});
    if (!parts[2].empty()) compileExpr(parts[2], bytecode, symTab, nextId, keywordMap);

    // 6) jump back to cond
    bytecode.push_back({OP_JUMP, cond_index, 0});

    // 7) backpatch exit jump
    int end_index = (int)bytecode.size();
    bytecode[exit_jump_index].operand = end_index;

    // 8) close
    bytecode.push_back({OP_DONG_NGOAC,0,0});
    bytecode.push_back({OP_DONG_LENH,0,0});

    // advance pos past block if there was one (extractParens already moved pos; we handled block by compileBlock which moves pos after '}' )
    return {cond_index, exit_jump_index};
}

using CompileFunc = std::function<void(
    const std::vector<std::string>& tokens,
    size_t &pos,
    std::vector<Instruction>& bytecode,
    std::unordered_map<std::string,int>& symTab,
    int& nextId,
    const std::unordered_map<std::string,Opcode>& keywordMap)>;

// Bản đồ ánh xạ keyword → compile function
static std::unordered_map<std::string, CompileFunc> compileMap;

// ---------- compileStatement ----------
static void compileStatement(const std::vector<std::string>& tokens, size_t &pos,
                             std::vector<Instruction> &bytecode,
                             std::unordered_map<std::string,int> &symTab,
                             int &nextId,
                             const std::unordered_map<std::string,Opcode> &keywordMap)
{
    if (pos >= tokens.size()) return;
    const std::string &tk = tokens[pos];

    // ---- Trường hợp Block ----
    if (tk == "{") {
        bytecode.push_back({OP_MO_KHOI,0,0});
        compileBlock(tokens, pos, bytecode, symTab, nextId, keywordMap);
        bytecode.push_back({OP_DONG_KHOI,0,0});
        return;
    }

    // ---- Trường hợp Keyword có CompileFunc riêng ----
    auto it = compileMap.find(tk);
    if (it != compileMap.end()) {
        it->second(tokens, pos, bytecode, symTab, nextId, keywordMap);
        return;
    }

    // ---- Trường hợp Keyword chưa có compileFunc (nhưng vẫn là opcode hợp lệ) ----
    if (keywordMap.count(tk)) {
        Opcode code = keywordMap.at(tk);
        bytecode.push_back({code,0,0});
        ++pos;
        return;
    }

    // ---- Còn lại là Biểu thức thông thường ----
    auto pr = extractExpressionUntilSemicolon(tokens, pos);
    compileExpr(pr.first, bytecode, symTab, nextId, keywordMap);
    bytecode.push_back({OP_DONG_LENH,0,0});
    pos = pr.second;
}

void initCompileMap() {
    compileMap["in"]  = [](const std::vector<std::string>& tokens, size_t &pos,
                           std::vector<Instruction>& bytecode,
                           std::unordered_map<std::string,int>& symTab,
                           int& nextId,
                           const std::unordered_map<std::string,Opcode>& kwMap) {
        ++pos; // bỏ qua "in"
        auto pr = extractExpressionUntilSemicolon(tokens, pos);
        compileExpr(pr.first, bytecode, symTab, nextId, kwMap);
        bytecode.push_back({OP_IN, 0, 0});
        pos = pr.second;
    };

    compileMap["nếu"] = compileConditionBlock;
    compileMap["lặp"] = [](const std::vector<std::string>& tokens, size_t &pos,
                           std::vector<Instruction>& bytecode,
                           std::unordered_map<std::string,int>& symTab,
                           int& nextId,
                           const std::unordered_map<std::string,Opcode>& kwMap) {
        compileLoop(tokens, pos, bytecode, symTab, nextId, kwMap);
    };
}

// ---------- compileBlock (parse and compile until matching '}' ) ----------
// tokens[pos] should be at '{', after this call pos will be index after the matching '}'
static void compileBlock(const std::vector<std::string>& tokens, size_t &pos,
                         std::vector<Instruction> &bytecode,
                         std::unordered_map<std::string,int> &symTab,
                         int &nextId,
                         const std::unordered_map<std::string,Opcode> &keywordMap)
{
    if (pos >= tokens.size() || tokens[pos] != "{") throw std::runtime_error("compileBlock: expected '{'");
    // move past '{'
    ++pos;

    while (pos < tokens.size() && tokens[pos] != "}") {
        compileStatement(tokens, pos, bytecode, symTab, nextId, keywordMap);
    }

    if (pos >= tokens.size() || tokens[pos] != "}") throw std::runtime_error("compileBlock: missing '}'");
    // move pos to token after '}'
    ++pos;
}

// ---------- compileSource: top-level ----------
// This replaces the old line-by-line code and uses token stream + recursive parsing.
// It returns vector<Instruction>.
std::vector<Instruction> compileSource(const std::string& source,
                                       const std::unordered_map<std::string,Opcode>& keywordMap)
{
    std::vector<Instruction> bytecode;
    std::unordered_map<std::string,int> symTab;
    int nextId = 0;

    // Tokenize entire source (supports multi-line)
    auto tokens = tokenize(source);

    size_t pos = 0;
    while (pos < tokens.size()) {
        // skip stray semicolons or closing braces at top-level
        if (tokens[pos] == ";") { ++pos; continue; }
        if (tokens[pos] == "}") { ++pos; continue; }

        compileStatement(tokens, pos, bytecode, symTab, nextId, keywordMap);
    }

    bytecode.push_back({OP_DUNG_CHUONG_TRINH,0,0});
    return bytecode;
}

//
// Created by nx_thang on 10/21/2025.
//

#include "../../include/compiler/compileRegistry.h"
#include "../../include/compiler/compiler.h"

#include <iostream>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <unordered_set>

#include "common/loopUltil.h"
#include "common/utility.h"
#include "compiler/compileCondition.h"
#include "compiler/compileLoop.h"
#include "compiler/compilerExpr.h"
#include "compiler/compileSwitch.h"
#include "common/storeString.h"
#include "compiler/compileBlock.h"

std::unordered_map<std::string, CompileFunc> compileMap;

namespace vietvm { namespace compiler {
    std::unordered_set<std::string> importedFiles;
    void clearImportedFiles() { importedFiles.clear(); }
} }

static std::vector<std::string> splitArgs(const std::string &s) {
    std::vector<std::string> res;
    std::string cur;
    int depth = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '(') { depth++; cur.push_back(c); }
        else if (c == ')') { depth--; cur.push_back(c); }
        else if (c == ',' && depth == 0) {
            res.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) res.push_back(cur);
    // trim spaces
    for (auto &t : res) {
        // simple trim
        size_t a = t.find_first_not_of(" \t\n\r");
        size_t b = t.find_last_not_of(" \t\n\r");
        if (a == std::string::npos) t = "";
        else t = t.substr(a, b - a + 1);
    }
    return res;
}

void initCompileMap() {
    compileMap["in"]  = [](const std::vector<std::string>& tokens, size_t &pos,
                           std::vector<Instruction>& bytecode,
                           std::unordered_map<std::string,int>& symTab,
                           int& nextId,
                           const std::unordered_map<std::string,Opcode>& keywordMap) {
                                ++pos; // bỏ qua "in"
                                auto pr = vietvm::compiler::extractExpressionUntilSemicolon(tokens, pos);
                                compileExpr(pr.first, bytecode, symTab, nextId, keywordMap);
                                bytecode.push_back({OP_IN, 0, 0,0});
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
                                    bytecode.push_back({OP_KHOI_TAO, varId, 0,0});
                                }
                                compileLoop(tokens, pos, bytecode, symTab, nextId, keywordMap);
                            }
                       };
    compileMap["chọn"] = compileSwitch;

    // Handler "hàm" (robust pos handling)
    compileMap["hàm"] = [](const std::vector<std::string>& tokens, size_t &pos,
                           std::vector<Instruction>& bytecode,
                           std::unordered_map<std::string,int>& symTab,
                           int& nextId,
                           const std::unordered_map<std::string,Opcode>& keywordMap) {
        ++pos; // skip 'hàm'
        if (pos >= tokens.size()) throw std::runtime_error("compile: thiếu tên hàm sau 'hàm'");
        std::string fname = tokens[pos++];
        int nameIndex = vietvm::compiler::StringPool::storeString(fname);

        // assign function id
        int hamId = vietvm::compiler::hamMap::allocHamId();
        symTab[fname] = hamId;

        // parse parameter list if present
        std::vector<std::string> params;
        if (pos < tokens.size() && tokens[pos] == "(") {
            auto pr = vietvm::compiler::extractParens(tokens, pos);
            // extractParens should return pair<inside, newPos>
            std::string inside = pr.first;
            pos = pr.second;
            // split args
            std::stringstream ss(inside);
            std::string item;
            while (std::getline(ss, item, ',')) {
                size_t a = item.find_first_not_of(" \t\n\r");
                size_t b = item.find_last_not_of(" \t\n\r");
                if (a != std::string::npos) params.push_back(item.substr(a, b - a + 1));
            }
        }

        // Now pos should be at the next token after params. Expect '{'
        if (pos >= tokens.size() || tokens[pos] != "{") {
            throw std::runtime_error(std::string("compileBlock: expected '{' at pos=") + std::to_string(pos)
                                     + ", found token='" + (pos < tokens.size() ? tokens[pos] : "EOF") + "'");
        }

        // compile function body into temporary vector
        std::vector<Instruction> funcCode;
        funcCode.push_back({OP_MO_KHOI, 0, 0, 0});

        // emit OP_PARAM prologue
        for (size_t i = 0; i < params.size(); ++i) {
            const std::string &pname = params[i];
            if (pname.empty()) continue;
            if (symTab.find(pname) == symTab.end()) symTab[pname] = nextId++;
            int varId = symTab[pname];
            funcCode.push_back({OP_KHOI_TAO, 0, varId, 0});
            funcCode.push_back({OP_PARAM, 0, varId, (int)i});
        }
        // Let compileBlock consume until matching '}' — compileBlock must update pos to point after '}'
        compileBlock(tokens, pos, funcCode, symTab, nextId, keywordMap);

        // ensure compileBlock left pos at token after '}', if not adjust as needed
        // function epilogue
        funcCode.push_back({OP_DONG_KHOI, 0, 0, 0});
        funcCode.push_back({OP_DONG_LENH, 0, 0, 0});

        // store function code
        vietvm::compiler::hamMap::hamBytecodeMap[hamId] = std::move(funcCode);
        vietvm::compiler::hamMap::setHamNameIndex(hamId, nameIndex);

        // Optionally emit an OP_HAM marker into outer bytecode for discovery
        bytecode.push_back({OP_HAM, nameIndex , hamId, 0});
    };

    // ---- Handler: gọi hàm bằng từ khóa 'gọi' ----
    // cú pháp: gọi <tên>(arg1, arg2, ...)
    // ---- Handler: gọi hàm bằng từ khóa 'gọi' ----
    compileMap["gọi"] = [](const std::vector<std::string>& tokens, size_t &pos,
                           std::vector<Instruction>& bytecode,
                           std::unordered_map<std::string,int>& symTab,
                           int& nextId,
                           const std::unordered_map<std::string,Opcode>& keywordMap) {
        ++pos;
        if (pos >= tokens.size()) throw std::runtime_error("gọi: thiếu tên hàm");
        std::string fname = tokens[pos++];

        // resolve function id from symTab (must have been set when compiling declaration)
        int hamId = -1;
        auto itSym = symTab.find(fname);
        if (itSym != symTab.end()) {
            hamId = itSym->second;
        } else {
            // fallback: store name in string pool and emit OP_GOI with nameIndex (VM fallback will try resolve)
            hamId = -1;
        }

        // parse args
        if (pos >= tokens.size() || tokens[pos] != "(") throw std::runtime_error("gọi: thiếu '(' sau tên hàm");
        auto pr = vietvm::compiler::extractParens(tokens, pos);
        std::string inside = pr.first;
        pos = pr.second;

        std::vector<std::string> args = splitArgs(inside);
        int compiledArgs = 0;
        for (const auto &aexpr : args) {
            if (aexpr.empty()) continue;
            compileExpr(aexpr, bytecode, symTab, nextId, keywordMap);
            ++compiledArgs;
        }

        if (hamId >= 0) {
            // emit with hamId in operand and argc in operandIndex
            bytecode.push_back({OP_GOI, compiledArgs, hamId, 0});
        } else {
            // fallback: emit with nameIndex so VM fallback can resolve (less ideal)
            int nameIndex = vietvm::compiler::StringPool::storeString(fname);
            bytecode.push_back({OP_GOI, compiledArgs, nameIndex, 0});
        }

        if (pos < tokens.size() && tokens[pos] == ";") ++pos;
    };

    // ---- Handler: nhập (import modules/files) ----
    // Syntax (MVP):
    //   nhập "path/to/module.vi";
    //   nhập moduleName;    // resolves to moduleName.vi in cwd
    compileMap["nhập"] = [](const std::vector<std::string>& tokens, size_t &pos,
                             std::vector<Instruction>& bytecode,
                             std::unordered_map<std::string,int>& symTab,
                             int& nextId,
                             const std::unordered_map<std::string,Opcode>& keywordMap) {
        ++pos; // skip 'nhập'
        if (pos >= tokens.size()) throw std::runtime_error("nhập: thiếu đường dẫn hoặc tên module");
        std::string target = tokens[pos++];

        // optional semicolon will be consumed later

        // Determine path
        std::string path;
        if (target.size() >= 2 && (target.front() == '"' || target.front() == '\'')) {
            // strip quotes (no escape processing here)
            path = target.substr(1, target.size() - 2);
        } else {
            // treat identifier as file name with .vi
            path = target + ".vi";
        }

        namespace fs = std::filesystem;
        fs::path p(path);
        // Make absolute and normalized path (if possible)
        fs::path abs;
        try {
            abs = fs::absolute(p).lexically_normal();
        } catch (...) {
            abs = p;
        }

        // If resolved path does not exist, attempt to locate the file by searching
        // upward from the current working directory and appending the requested path.
        // This helps with imports like "src/tests/..." when the process cwd is build/bin.
        if (!fs::exists(abs)) {
            for (fs::path dir = fs::current_path(); ; dir = dir.parent_path()) {
                fs::path cand = dir / p;
                if (fs::exists(cand)) {
                    abs = fs::absolute(cand).lexically_normal();
                    break;
                }
                if (dir == dir.parent_path()) break; // reached filesystem root
            }
        }

        std::string canonical = abs.string();

        if (vietvm::compiler::importedFiles.find(canonical) != vietvm::compiler::importedFiles.end()) {
            // already imported in this compile session — no-op
            if (pos < tokens.size() && tokens[pos] == ";") ++pos;
            return;
        }

        // read file
        std::ifstream ifs(canonical);
        if (!ifs.is_open()) {
            throw std::runtime_error(std::string("nhập: không thể mở file '") + canonical + "'");
        }
        std::stringstream ss;
        ss << ifs.rdbuf();
        std::string src = ss.str();

        // compile module without emitting main call
        auto moduleBC = compileSource(src, keywordMap, false);

        // functions and strings from module are already registered in global StringPool and hamMap
        vietvm::compiler::importedFiles.insert(canonical);

        if (pos < tokens.size() && tokens[pos] == ";") ++pos;
    };
}
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
#include "../../include/frontend/lexer.h"

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
                                    bytecode.push_back({OP_KHOI_TAO, 0, varId, 0});
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

        // assign/reuse function id (predeclared in compileSource when available)
        int hamId = -1;
        auto itFn = symTab.find(fname);
        if (itFn != symTab.end()) {
            hamId = itFn->second;
        } else {
            hamId = vietvm::compiler::hamMap::allocHamId();
            symTab[fname] = hamId;
        }
        // Ensure registration exists so recursive/self calls can resolve while compiling body.
        vietvm::compiler::hamMap::hamBytecodeMap[hamId] = {};
        vietvm::compiler::hamMap::setHamNameIndex(hamId, nameIndex);

        // parse parameter list if present
        struct ParamSpec {
            std::string name;
            bool hasDefault = false;
            std::string defaultEncoded; // i:10, d:3.14, s:text, n:
        };
        std::vector<ParamSpec> params;
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
                if (a == std::string::npos) continue;

                std::string token = item.substr(a, b - a + 1);
                ParamSpec p;
                size_t eq = token.find('=');
                if (eq == std::string::npos) {
                    p.name = vietvm::compiler::trim(token);
                } else {
                    p.name = vietvm::compiler::trim(token.substr(0, eq));
                    std::string dv = vietvm::compiler::trim(token.substr(eq + 1));
                    p.hasDefault = true;
                    if (vietvm::compiler::isNumber(dv)) p.defaultEncoded = "i:" + dv;
                    else if (vietvm::compiler::isFloat(dv)) p.defaultEncoded = "d:" + dv;
                    else if (vietvm::compiler::isStringLiteral(dv)) p.defaultEncoded = "s:" + vietvm::compiler::stripQuotes(dv);
                    else if (dv == "đúng") p.defaultEncoded = "i:1";
                    else if (dv == "sai") p.defaultEncoded = "i:0";
                    else if (dv == "rỗng") p.defaultEncoded = "n:";
                    else throw std::runtime_error("hàm: tham số mặc định chỉ hỗ trợ literal (int/float/string/đúng/sai/rỗng)");
                }
                if (!p.name.empty()) params.push_back(p);
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
            const std::string &pname = params[i].name;
            if (pname.empty()) continue;
            if (symTab.find(pname) == symTab.end()) symTab[pname] = nextId++;
            int varId = symTab[pname];
            funcCode.push_back({OP_KHOI_TAO, 0, varId, 0});
            if (params[i].hasDefault) {
                int dIdx = vietvm::compiler::StringPool::storeString(params[i].defaultEncoded);
                funcCode.push_back({OP_PARAM_MAC_DINH, dIdx, varId, (int)i});
            } else {
                funcCode.push_back({OP_PARAM, 0, varId, (int)i});
            }
        }
        // Let compileBlock consume until matching '}' — compileBlock must update pos to point after '}'
        compileBlock(tokens, pos, funcCode, symTab, nextId, keywordMap);

        // ensure compileBlock left pos at token after '}', if not adjust as needed
        // function epilogue
        funcCode.push_back({OP_DONG_KHOI, 0, 0, 0});
        funcCode.push_back({OP_DONG_LENH, 0, 0, 0});

        // store function code
        vietvm::compiler::hamMap::hamBytecodeMap[hamId] = std::move(funcCode);

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
            bytecode.push_back({OP_GOI, compiledArgs, -(nameIndex + 1), 0});
        }

        if (pos < tokens.size() && tokens[pos] == ";") ++pos;
    };

    // ---- Handler: trả về ----
    // Syntax:
    //   trả về <expr>;
    //   trả về;
    auto compileReturn = [](const std::vector<std::string>& tokens, size_t &pos,
                            std::vector<Instruction>& bytecode,
                            std::unordered_map<std::string,int>& symTab,
                            int& nextId,
                            const std::unordered_map<std::string,Opcode>& keywordMap) {
        ++pos; // skip 'trả về'

        bool hasExpr = (pos < tokens.size() && tokens[pos] != ";");
        if (hasExpr) {
            auto pr = vietvm::compiler::extractExpressionUntilSemicolon(tokens, pos);
            if (!pr.first.empty()) {
                compileExpr(pr.first, bytecode, symTab, nextId, keywordMap);
            } else {
                bytecode.push_back({OP_BIEN_SO, 0, 0, 0});
            }
            pos = pr.second;
        } else {
            if (pos < tokens.size() && tokens[pos] == ";") ++pos;
            bytecode.push_back({OP_BIEN_SO, 0, 0, 0});
        }

        bytecode.push_back({OP_TRA_VE, 0, 0, 0});
    };

    compileMap["trả về"] = compileReturn;

    // ---- Handler: bỏ qua (continue) ----
    compileMap["bỏ qua"] = [](const std::vector<std::string>& tokens, size_t &pos,
                               std::vector<Instruction>& bytecode,
                               std::unordered_map<std::string,int>&,
                               int&,
                               const std::unordered_map<std::string,Opcode>&) {
        ++pos;
        if (pos < tokens.size() && tokens[pos] == ";") ++pos;
        bytecode.push_back({OP_BO_QUA, 0, 0, 0});
    };

    // ---- Handler: ném (throw) ----
    compileMap["ném"] = [](const std::vector<std::string>& tokens, size_t &pos,
                            std::vector<Instruction>& bytecode,
                            std::unordered_map<std::string,int>& symTab,
                            int& nextId,
                            const std::unordered_map<std::string,Opcode>& keywordMap) {
        ++pos; // skip 'ném'
        auto pr = vietvm::compiler::extractExpressionUntilSemicolon(tokens, pos);
        if (!pr.first.empty()) {
            compileExpr(pr.first, bytecode, symTab, nextId, keywordMap);
        } else {
            // ném; without value → push empty string
            int strIdx = vietvm::compiler::StringPool::storeString("lỗi không xác định");
            bytecode.push_back({OP_CHUOI, 0, strIdx, 0});
        }
        bytecode.push_back({OP_NEM, 0, 0, 0});
        pos = pr.second;
    };

    // ---- Handler: thử ... bắt lỗi ... ----
    // Syntax: thử { ... } bắt lỗi { ... }
    //      OR: thử { ... } bắt lỗi (errVar) { ... }
    compileMap["thử"] = [](const std::vector<std::string>& tokens, size_t &pos,
                            std::vector<Instruction>& bytecode,
                            std::unordered_map<std::string,int>& symTab,
                            int& nextId,
                            const std::unordered_map<std::string,Opcode>& keywordMap) {
        ++pos; // skip 'thử'

        // Emit OP_THU with placeholder for catch address
        int thuIdx = (int)bytecode.size();
        bytecode.push_back({OP_THU, 0, -1, 0}); // operand = catch_addr (patched later)

        // Compile try body
        if (pos < tokens.size() && tokens[pos] == "{") {
            bytecode.push_back({OP_MO_KHOI, 0, 0, 0});
            compileBlock(tokens, pos, bytecode, symTab, nextId, keywordMap);
            bytecode.push_back({OP_DONG_KHOI, 0, 0, 0});
        }

        // Emit OP_THU_KET_THUC with placeholder for past-catch address
        int thuKetThucIdx = (int)bytecode.size();
        bytecode.push_back({OP_THU_KET_THUC, 0, 0, 0}); // operand = past_catch (patched later)

        // Patch OP_THU to point here (catch handler start)
        int catchAddr = (int)bytecode.size();
        bytecode[thuIdx].operand = catchAddr;

        // Expect 'bắt lỗi'
        if (pos < tokens.size() && tokens[pos] == "bắt lỗi") ++pos;

        // Optional: (errVar)
        int errVarId = -1;
        if (pos < tokens.size() && tokens[pos] == "(") {
            auto pr = vietvm::compiler::extractParens(tokens, pos);
            std::string varName = vietvm::compiler::trim(pr.first);
            if (!varName.empty()) {
                if (symTab.find(varName) == symTab.end()) symTab[varName] = nextId++;
                errVarId = symTab[varName];
            }
            pos = pr.second;
        }

        // Emit OP_BAT_LOI
        bytecode.push_back({OP_BAT_LOI, 0, errVarId, 0});

        // Compile catch body
        if (pos < tokens.size() && tokens[pos] == "{") {
            bytecode.push_back({OP_MO_KHOI, 0, 0, 0});
            compileBlock(tokens, pos, bytecode, symTab, nextId, keywordMap);
            bytecode.push_back({OP_DONG_KHOI, 0, 0, 0});
        }

        // Patch OP_THU_KET_THUC to jump here (past catch)
        int pastCatch = (int)bytecode.size();
        bytecode[thuKetThucIdx].operand = pastCatch;
    };

    // ---- Handler: nhập (import modules/files) ----
    // Syntax (MVP):
    //   nhập "path/to/module.vi";
    //   nhập moduleName;    // resolves to moduleName.vi in cwd
    //   nhập "path/to/module.vi" như ten_module;
    compileMap["nhập"] = [](const std::vector<std::string>& tokens, size_t &pos,
                             std::vector<Instruction>& bytecode,
                             std::unordered_map<std::string,int>& symTab,
                             int& nextId,
                             const std::unordered_map<std::string,Opcode>& keywordMap) {
        // Import mainly causes side effects (registering functions/strings).
        // `nextId` is updated below to avoid var-id collisions with imported function ids.
        (void)bytecode;
        (void)symTab;

        ++pos; // skip 'nhập'
        if (pos >= tokens.size()) throw std::runtime_error("nhập: thiếu đường dẫn hoặc tên module");
        std::string target = tokens[pos++];

        // Optional alias namespace: nhập "..." như ns;
        std::string moduleAlias;
        if (pos < tokens.size() && tokens[pos] == "như") {
            ++pos;
            if (pos >= tokens.size()) throw std::runtime_error("nhập: thiếu tên namespace sau 'như'");
            moduleAlias = tokens[pos++];
        }

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

        // stdlib shortcut
        if (path == "stdlib" || path == "thư viện chuẩn" || path == "thu_vien_chuan") {
            path = "lib/stdlib.vi";
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
                if (p.parent_path().empty() && p.extension().empty()) {
                    fs::path packageMain = dir / "packages" / p / "main.vi";
                    fs::path packageRoot = dir / "packages" / p;
                    fs::path packageSource = dir / "packages" / (p.string() + ".vi");
                    if (fs::exists(packageMain)) {
                        abs = fs::absolute(packageMain).lexically_normal();
                        break;
                    }
                    if (fs::exists(packageRoot) && fs::is_regular_file(packageRoot)) {
                        abs = fs::absolute(packageRoot).lexically_normal();
                        break;
                    }
                    if (fs::exists(packageSource)) {
                        abs = fs::absolute(packageSource).lexically_normal();
                        break;
                    }
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

        // Mark as in-progress before reading/compiling to prevent circular imports
        vietvm::compiler::importedFiles.insert(canonical);

        try {
            // read file
            std::ifstream ifs(canonical);
            if (!ifs.is_open()) {
                throw std::runtime_error(std::string("nhập: không thể mở file '") + canonical + "'");
            }
            std::stringstream ss;
            ss << ifs.rdbuf();
            std::string src = ss.str();

            // Compile the module for its registration side effects only.
            // Imported functions/strings are recorded in the global registries;
            // the returned module bytecode is not merged or executed here.
            auto moduleBytecode = compileSource(src, keywordMap, false);

            // Namespace alias: register ns.funcName -> same function id
            if (!moduleAlias.empty()) {
                for (const auto &ins : moduleBytecode) {
                    if (ins.op != OP_HAM) continue;
                    int oldNameIndex = ins.operand;
                    int hamId = ins.operandIndex;
                    if (oldNameIndex < 0 || oldNameIndex >= (int)vietvm::compiler::StringPool::size()) continue;
                    const std::string &funcName = vietvm::compiler::StringPool::getString(oldNameIndex);
                    std::string namespaced = moduleAlias + "." + funcName;
                    int newNameIndex = vietvm::compiler::StringPool::storeString(namespaced);
                    vietvm::compiler::hamMap::setHamNameIndex(hamId, newNameIndex);
                }
            }

            // Keep variable IDs in caller module disjoint from imported function IDs.
            int maxHamId = -1;
            for (const auto &kv : vietvm::compiler::hamMap::hamBytecodeMap) {
                if (kv.first > maxHamId) maxHamId = kv.first;
            }
            if (nextId <= maxHamId) nextId = maxHamId + 1;
        } catch (...) {
            // Rollback on failure
            vietvm::compiler::importedFiles.erase(canonical);
            throw;
        }

        if (pos < tokens.size() && tokens[pos] == ";") ++pos;
    };
}
#include <iostream>
#include <stdexcept>
#include <vector>
#include <stack>
#include <variant>
#include <string>
#include <fstream>
#include <sstream>
#include <ctime>
#include <cstdio>
#include <stdio.h>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <optional>
#include <unordered_map>
#include <regex>
#include "../../include/vm/vm.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <memory>
#include <atomic>
#include "../../include/common/vm_utils.h"
#include "common/vm_native_helpers.h"
#include "common/vm_native_constants.h"
#include "common/vm_native_http_helpers.h"
#include "common/vm_low_level_http_server.h"

#if defined(_WIN32) && defined(_MSC_VER)
#ifndef popen
#define popen _popen
#endif
#ifndef pclose
#define pclose _pclose
#endif
#endif

static bool handleNativeHttpClientFunction(const std::string &fn,
                                           const std::vector<StackValue> &args,
                                           StackValue &result,
                                           std::string &err) {
    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnHttpGet)) {
        if (args.size() != 1) { err = fn + " yêu cầu 1 tham số"; return true; }
        std::string url = vietvm::helpers::argToRawString(args[0]);
        return vietvm::helpers::runCurlHttpRequest(vietvm::constants::kHttpMethodGet, fn, url, std::nullopt, result, err);
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnHttpPost)) {
        if (args.size() != 2) { err = fn + " yêu cầu 2 tham số"; return true; }
        std::string url = vietvm::helpers::argToRawString(args[0]);
        std::string payload = vietvm::helpers::argToRawString(args[1]);
        return vietvm::helpers::runCurlHttpRequest(vietvm::constants::kHttpMethodPost, fn, url, payload, result, err);
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnHttpPut)) {
        if (args.size() != 2) { err = fn + " yêu cầu 2 tham số"; return true; }
        std::string url = vietvm::helpers::argToRawString(args[0]);
        std::string payload = vietvm::helpers::argToRawString(args[1]);
        return vietvm::helpers::runCurlHttpRequest(vietvm::constants::kHttpMethodPut, fn, url, payload, result, err);
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnHttpDelete)) {
        if (args.size() != 1) { err = fn + " yêu cầu 1 tham số"; return true; }
        std::string url = vietvm::helpers::argToRawString(args[0]);
        return vietvm::helpers::runCurlHttpRequest("DELETE", fn, url, std::nullopt, result, err);
    }

    return false;
}

static std::string escapeJsonString(const std::string &input) {
    static constexpr char hex[] = "0123456789abcdef";

    std::string output;
    output.reserve(input.size() + 16);
    for (unsigned char c : input) {
        switch (c) {
            case '"': output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\b': output += "\\b"; break;
            case '\f': output += "\\f"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default:
                if (c < 0x20) {
                    output += "\\u00";
                    output.push_back(hex[(c >> 4) & 0x0f]);
                    output.push_back(hex[c & 0x0f]);
                } else {
                    output.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    return output;
}

static bool handleNativeJsonFunction(const std::string &fn,
                                     const std::vector<StackValue> &args,
                                     StackValue &result,
                                     std::string &err) {
    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnJsonEscape)) {
        if (args.size() != 1) { err = fn + " yêu cầu 1 tham số"; return true; }
        result = make_string_value(escapeJsonString(vietvm::helpers::argToRawString(args[0])));
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnJsonString)) {
        if (args.size() != 1) { err = fn + " yêu cầu 1 tham số"; return true; }
        result = make_string_value(std::string("\"") +
                                   escapeJsonString(vietvm::helpers::argToRawString(args[0])) +
                                   "\"");
        return true;
    }

    return false;
}

static bool handleNativeLowLevelHttpFunction(const std::string &fn,
                                             const std::vector<StackValue> &args,
                                             StackValue &result,
                                             std::string &err) {
    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnHttpServerOpen)) {
        if (args.size() != 1) { err = fn + " yêu cầu 1 tham số"; return true; }
        int port = 0;
        if (!vietvm::helpers::parseIntArgFromStack(args[0], fn, vietvm::constants::kArgLabelPort, port, err)) return true;
        return vietvm::helpers::runLowLevelHttpServerOpen(port, result, err);
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnHttpServerNext)) {
        if (args.size() != 1) { err = fn + " yêu cầu 1 tham số"; return true; }
        int serverId = 0;
        if (!vietvm::helpers::parseIntArgFromStack(args[0], fn, vietvm::constants::kArgLabelServerId, serverId, err)) return true;
        return vietvm::helpers::runLowLevelHttpServerNext(serverId, result, err);
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnHttpReqMethod)) {
        if (args.size() != 1) { err = fn + " yêu cầu 1 tham số"; return true; }
        return vietvm::helpers::runLowLevelHttpReqField(vietvm::helpers::argToRawString(args[0]), vietvm::constants::kReqFieldMethod, std::nullopt, result, err);
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnHttpReqPath)) {
        if (args.size() != 1) { err = fn + " yêu cầu 1 tham số"; return true; }
        return vietvm::helpers::runLowLevelHttpReqField(vietvm::helpers::argToRawString(args[0]), vietvm::constants::kReqFieldPath, std::nullopt, result, err);
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnHttpReqQuery)) {
        if (args.size() != 1) { err = fn + " yêu cầu 1 tham số"; return true; }
        return vietvm::helpers::runLowLevelHttpReqField(vietvm::helpers::argToRawString(args[0]), vietvm::constants::kReqFieldQuery, std::nullopt, result, err);
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnHttpReqBody)) {
        if (args.size() != 1) { err = fn + " yêu cầu 1 tham số"; return true; }
        return vietvm::helpers::runLowLevelHttpReqField(vietvm::helpers::argToRawString(args[0]), vietvm::constants::kReqFieldBody, std::nullopt, result, err);
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnHttpReqHeader)) {
        if (args.size() != 2) { err = fn + " yêu cầu 2 tham số"; return true; }
        return vietvm::helpers::runLowLevelHttpReqField(vietvm::helpers::argToRawString(args[0]), vietvm::constants::kReqFieldHeader, vietvm::helpers::argToRawString(args[1]), result, err);
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnHttpReqQueryParam)) {
        if (args.size() != 2) { err = fn + " yêu cầu 2 tham số"; return true; }
        return vietvm::helpers::runLowLevelHttpReqField(vietvm::helpers::argToRawString(args[0]), vietvm::constants::kReqFieldQueryParam, vietvm::helpers::argToRawString(args[1]), result, err);
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnHttpReqJsonField)) {
        if (args.size() != 2) { err = fn + " yêu cầu 2 tham số"; return true; }
        return vietvm::helpers::runLowLevelHttpReqField(vietvm::helpers::argToRawString(args[0]), vietvm::constants::kReqFieldJsonField, vietvm::helpers::argToRawString(args[1]), result, err);
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnHttpReqPathSuffix)) {
        if (args.size() != 2) { err = fn + " yêu cầu 2 tham số"; return true; }
        return vietvm::helpers::runLowLevelHttpReqField(vietvm::helpers::argToRawString(args[0]), vietvm::constants::kReqFieldPathSuffix, vietvm::helpers::argToRawString(args[1]), result, err);
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnHttpServerSend)) {
        if (args.size() != 3) { err = fn + " yêu cầu 3 tham số"; return true; }
        int status = 200;
        if (!vietvm::helpers::parseIntArgFromStack(args[1], fn, vietvm::constants::kArgLabelStatus, status, err)) return true;
        return vietvm::helpers::runLowLevelHttpServerSend(vietvm::helpers::argToRawString(args[0]), status, vietvm::helpers::argToRawString(args[2]), result, err);
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnHttpServerClose)) {
        if (args.size() != 1) { err = fn + " yêu cầu 1 tham số"; return true; }
        int serverId = 0;
        if (!vietvm::helpers::parseIntArgFromStack(args[0], fn, vietvm::constants::kArgLabelServerId, serverId, err)) return true;
        return vietvm::helpers::runLowLevelHttpServerClose(serverId, result, err);
    }

    return false;
}

static bool executeNativeStdlibFunction(int hamIdOrName,
                                        const std::vector<StackValue> &args,
                                        const std::vector<std::string> &stringPool,
                                        const std::unordered_map<int, int> &functionTableByNameIndex,
                                        const std::unordered_map<int, std::vector<Instruction>> &functionBytecode,
                                        StackValue &result,
                                        std::string &err) {
    std::string fn;
    if (hamIdOrName < 0) {
        int nameIdx = -(hamIdOrName + 1);
        if (nameIdx >= 0 && nameIdx < (int)stringPool.size()) fn = stringPool[nameIdx];
    } else {
        for (const auto &entry : functionTableByNameIndex) {
            if (entry.second == hamIdOrName) {
                int nameIdx = entry.first;
                if (nameIdx >= 0 && nameIdx < (int)stringPool.size()) fn = stringPool[nameIdx];
                break;
            }
        }
    }
    if (fn.empty() && hamIdOrName >= 0 && hamIdOrName < (int)stringPool.size()) {
        // Compatibility path: only treat positive value as a nameIndex
        // when it is not already a concrete function id.
        if (functionBytecode.find(hamIdOrName) == functionBytecode.end()) {
            fn = stringPool[hamIdOrName];
        }
    }

    if (fn.empty()) return false;

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnIoReadFile)) {
        if (args.size() != 1) { err = fn + " yêu cầu 1 tham số"; return true; }
        std::ifstream ifs(vietvm::helpers::argToRawString(args[0]));
        if (!ifs.is_open()) { err = fn + ": không thể mở file"; return true; }
        std::ostringstream ss;
        ss << ifs.rdbuf();
        result = make_string_value(ss.str());
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnIoWriteFile)) {
        if (args.size() != 2) { err = fn + " yêu cầu 2 tham số"; return true; }
        std::ofstream ofs(vietvm::helpers::argToRawString(args[0]));
        if (!ofs.is_open()) { err = fn + ": không thể mở file để ghi"; return true; }
        std::string content = vietvm::helpers::argToRawString(args[1]);
        ofs << vietvm::helpers::decodeSimpleEscapes(content);
        if (!ofs.good()) { err = fn + ": ghi file thất bại"; return true; }
        result = make_int_value(1);
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnNow)) {
        std::time_t now = std::time(nullptr);
    #if defined(_MSC_VER)
        std::tm tmBuf{};
        std::tm *tmNow = (localtime_s(&tmBuf, &now) == 0) ? &tmBuf : nullptr;
    #else
        std::tm *tmNow = std::localtime(&now);
    #endif
        char buf[32] = {0};
        if (!tmNow || std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tmNow) == 0) {
            err = "lay_thoi_gian_hien_tai: format thời gian thất bại";
            return true;
        }
        result = make_string_value(buf);
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnReadConfig)) {
        if (args.size() != 1) { err = fn + " yêu cầu 1 tham số"; return true; }
        std::ifstream ifs(vietvm::helpers::argToRawString(args[0]));
        if (!ifs.is_open()) { err = fn + ": không thể mở file"; return true; }

        MapValue cfg;
        std::string line;
        while (std::getline(ifs, line)) {
            std::string t = vietvm::helpers::trimCopy(line);
            if (t.empty() || t[0] == '#') continue;
            size_t eq = t.find('=');
            if (eq == std::string::npos) continue;

            std::string key = vietvm::helpers::trimCopy(t.substr(0, eq));
            std::string val = vietvm::helpers::trimCopy(t.substr(eq + 1));
            if (key.empty()) continue;

            if (val == "đúng") cfg[key] = 1;
            else if (val == "sai") cfg[key] = 0;
            else if (val == "rỗng") cfg[key] = std::monostate{};
            else {
                bool parsed = false;
                try {
                    size_t p = 0;
                    int iv = std::stoi(val, &p);
                    if (p == val.size()) { cfg[key] = iv; parsed = true; }
                } catch (...) {}
                if (!parsed) {
                    try {
                        size_t p = 0;
                        double dv = std::stod(val, &p);
                        if (p == val.size()) { cfg[key] = dv; parsed = true; }
                    } catch (...) {}
                }
                if (!parsed) cfg[key] = val;
            }
        }

        result = make_map_value(cfg);
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnReadConfigKey)) {
        if (args.size() != 3) { err = fn + " yêu cầu 3 tham số"; return true; }
        std::string filePath = vietvm::helpers::argToRawString(args[0]);
        std::string key = vietvm::helpers::argToRawString(args[1]);
        std::string fallback = vietvm::helpers::argToRawString(args[2]);
        result = make_string_value(vietvm::helpers::readPropertyByKey(filePath, key, fallback));
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnDbConnect)) {
        if (args.size() != 4) { err = "db_native_connect yêu cầu 4 tham số"; return true; }
        return vietvm::helpers::runDbConnect(vietvm::helpers::argToRawString(args[0]),
                    vietvm::helpers::argToRawString(args[1]),
                    vietvm::helpers::argToRawString(args[2]),
                    vietvm::helpers::argToRawString(args[3]),
                            result,
                            err);
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnDbQuery)) {
        if (args.size() != 5) { err = "db_native_query yêu cầu 5 tham số"; return true; }
        return vietvm::helpers::runDbQuery(vietvm::helpers::argToRawString(args[0]),
                  vietvm::helpers::argToRawString(args[1]),
                  vietvm::helpers::argToRawString(args[2]),
                  vietvm::helpers::argToRawString(args[3]),
                  vietvm::helpers::argToRawString(args[4]),
                          result,
                          err);
    }

    if (handleNativeHttpClientFunction(fn, args, result, err)) return true;
    if (handleNativeJsonFunction(fn, args, result, err)) return true;
    if (handleNativeLowLevelHttpFunction(fn, args, result, err)) return true;

    return false;
}

static std::string decodeEscaped(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char n = s[i + 1];
            if (n == 'n') out.push_back('\n');
            else if (n == 'r') out.push_back('\r');
            else if (n == 't') out.push_back('\t');
            else if (n == 'e') out.push_back('\x1e');
            else if (n == 'f') out.push_back('\x1f');
            else out.push_back(n);
            ++i;
            continue;
        }
        out.push_back(s[i]);
    }
    return out;
}

static MapValue decodeMapFromStringPool(const std::string &encoded) {
    constexpr char RS = '\x1e';
    constexpr char FS = '\x1f';

    MapValue m;
    if (encoded.empty()) return m;

    size_t start = 0;
    while (start <= encoded.size()) {
        size_t end = encoded.find(RS, start);
        if (end == std::string::npos) end = encoded.size();
        std::string record = encoded.substr(start, end - start);
        if (!record.empty()) {
            size_t p1 = record.find(FS);
            size_t p2 = (p1 == std::string::npos) ? std::string::npos : record.find(FS, p1 + 1);
            if (p1 == std::string::npos || p2 == std::string::npos) {
                throw std::runtime_error(vietvm::constants::kErrMapLiteralEncodeInvalid);
            }

            std::string key = decodeEscaped(record.substr(0, p1));
            std::string typeTag = record.substr(p1 + 1, p2 - p1 - 1);
            std::string valRaw = decodeEscaped(record.substr(p2 + 1));

            if (typeTag == "i") {
                m[key] = std::stoi(valRaw);
            } else if (typeTag == "d") {
                m[key] = std::stod(valRaw);
            } else if (typeTag == "s") {
                m[key] = valRaw;
            } else if (typeTag == "n") {
                m[key] = std::monostate{};
            } else {
                throw std::runtime_error(vietvm::constants::kErrMapLiteralTypeTagInvalid);
            }
        }

        if (end == encoded.size()) break;
        start = end + 1;
    }

    return m;
}

static StackValue decodeDefaultParamValue(const std::string &encoded) {
    size_t colon = encoded.find(':');
    if (colon == std::string::npos) {
        throw std::runtime_error(vietvm::constants::kErrDefaultParamEncodeInvalid);
    }
    std::string tag = encoded.substr(0, colon);
    std::string payload = encoded.substr(colon + 1);

    if (tag == "i") return make_int_value(std::stoi(payload));
    if (tag == "d") return make_float_value(std::stod(payload));
    if (tag == "s") return make_string_value(payload);
    if (tag == "n") return make_null_value();
    throw std::runtime_error(vietvm::constants::kErrDefaultParamTypeInvalid);
}

/**
 * VM ctor
 * (Keep existing constructors in vm.h / vm.cpp; this file assumes members:
 *  std::vector<Instruction> bytecode;
 *  std::vector<std::string> stringPool;
 *  std::vector<StackValue> stack;
 *  std::unordered_map<int, StackValue> variables;  // or map
 *  std::unordered_map<int, std::vector<Instruction>> hamBytecodeMap;
 *  int pc;
 *  etc.)
 */
VM::VM(const std::vector<Instruction>& code, const std::vector<std::string>& pool)
    : bytecode(code), stringPool(pool), pc(0) {}

// Convert StackValue to boolean
bool toBool(const StackValue& value) {
    if (std::holds_alternative<int>(value))    return std::get<int>(value) != 0;
    if (std::holds_alternative<double>(value)) return std::get<double>(value) != 0.0;
    if (std::holds_alternative<std::string>(value)) return !std::get<std::string>(value).empty();
    if (std::holds_alternative<std::monostate>(value)) return false;
    if (std::holds_alternative<MapValue>(value)) return !std::get<MapValue>(value).empty();
    return false;
}

void VM::collectGarbage() {
    // MVP GC: compact runtime containers to release unused memory back to allocator.
    // This is intentionally conservative and does not alter value semantics.
    stack.shrink_to_fit();
    callStack.shrink_to_fit();
    loopStartStack.shrink_to_fit();
    ifElseStack.shrink_to_fit();
    blockStack.shrink_to_fit();
    switchStack.shrink_to_fit();
    tryStack.shrink_to_fit();

    for (auto &f : callStack) {
        f.args.shrink_to_fit();
        f.localsVec.shrink_to_fit();
    }

    variables.rehash(variables.size());
}

bool VM::runJitCompiledLinear() {
    // MVP JIT: compile linear, non-control-flow bytecode into executable lambdas.
    // Unsupported opcodes fall back to the normal interpreter.
    for (const auto &ins : bytecode) {
        switch (ins.op) {
            case OP_BIEN_SO:
            case OP_BIEN_SO_FLOAT:
            case OP_CHUOI:
            case OP_RONG_GIA_TRI:
            case OP_CONG:
            case OP_TRU:
            case OP_NHAN:
            case OP_CHIA:
            case OP_MODULO:
            case OP_Logic_VA:
            case OP_Logic_HOAC:
            case OP_SO_SANH_BANG:
            case OP_KHAC_BANG:
            case OP_LON_HON:
            case OP_NHO_HON:
            case OP_LON_HON_HOAC_BANG:
            case OP_NHO_HON_HOAC_BANG:
            case OP_PHU_DINH:
            case OP_IN:
            case OP_DONG_LENH:
            case OP_MO_NGOAC:
            case OP_DONG_NGOAC:
            case OP_DUNG_CHUONG_TRINH:
                break;
            default:
                return false;
        }
    }

    using JitFn = std::function<void()>;
    std::vector<JitFn> program;
    program.reserve(bytecode.size());

    for (const auto &instr : bytecode) {
        switch (instr.op) {
            case OP_BIEN_SO: {
                int v = instr.operand;
                program.push_back([this, v]() { stack.emplace_back(v); });
                break;
            }
            case OP_BIEN_SO_FLOAT: {
                int idx = instr.operandIndex;
                program.push_back([this, idx]() {
                    if (idx < 0 || idx >= (int)stringPool.size())
                        throw runtime_error_op(vietvm::constants::kErrInvalidFloatIndex, OP_BIEN_SO_FLOAT, (int)pc);
                    stack.push_back(make_float_value(std::stod(stringPool[idx])));
                });
                break;
            }
            case OP_CHUOI: {
                int idx = instr.operandIndex;
                program.push_back([this, idx]() {
                    if (idx < 0 || idx >= (int)stringPool.size())
                        throw runtime_error_op(vietvm::constants::kErrInvalidStringIndex, OP_CHUOI, (int)pc);
                    stack.push_back(stringPool[idx]);
                });
                break;
            }
            case OP_RONG_GIA_TRI:
                program.push_back([this]() { stack.push_back(make_null_value()); });
                break;

            case OP_CONG:
            case OP_TRU:
            case OP_NHAN:
            case OP_CHIA:
            case OP_Logic_VA:
            case OP_Logic_HOAC:
            case OP_SO_SANH_BANG:
            case OP_KHAC_BANG:
            case OP_LON_HON:
            case OP_NHO_HON:
            case OP_LON_HON_HOAC_BANG:
            case OP_NHO_HON_HOAC_BANG: {
                Opcode op = instr.op;
                program.push_back([this, op]() {
                    if (stack.size() < 2) throw runtime_error_op(vietvm::constants::kErrNotEnoughOperands, op, (int)pc);
                    StackValue b = stack.back(); stack.pop_back();
                    StackValue a = stack.back(); stack.pop_back();

                    switch (op) {
                        case OP_CONG:
                            if (isNumeric(a) && isNumeric(b)) stack.push_back(numAdd(a, b));
                            else stack.emplace_back(sv_to_string(a) + sv_to_string(b));
                            break;
                        case OP_TRU:
                        case OP_NHAN:
                        case OP_CHIA:
                            if (!isNumeric(a) || !isNumeric(b))
                                throw runtime_error_op(vietvm::constants::kErrNumericOnlyOperator, op, (int)pc);
                            if (op == OP_TRU) stack.push_back(numSub(a, b));
                            else if (op == OP_NHAN) stack.push_back(numMul(a, b));
                            else stack.push_back(numDiv(a, b, op, (int)pc));
                            break;
                        case OP_Logic_VA:
                        case OP_Logic_HOAC: {
                            if (!isNumeric(a) || !isNumeric(b))
                                throw runtime_error_op(vietvm::constants::kErrLogicOnlyOperator, op, (int)pc);
                            int ia = as_int(a, op, (int)pc);
                            int ib = as_int(b, op, (int)pc);
                            stack.push_back((op == OP_Logic_VA) ? ((ia && ib) ? 1 : 0) : ((ia || ib) ? 1 : 0));
                            break;
                        }
                        case OP_SO_SANH_BANG:
                        case OP_KHAC_BANG:
                        case OP_LON_HON:
                        case OP_NHO_HON:
                        case OP_LON_HON_HOAC_BANG:
                        case OP_NHO_HON_HOAC_BANG: {
                            int result = 0;
                            if (isNumeric(a) && isNumeric(b)) {
                                double da = toDouble(a), db = toDouble(b);
                                if (op == OP_SO_SANH_BANG) result = (da == db) ? 1 : 0;
                                else if (op == OP_KHAC_BANG) result = (da != db) ? 1 : 0;
                                else if (op == OP_LON_HON) result = (da > db) ? 1 : 0;
                                else if (op == OP_NHO_HON) result = (da < db) ? 1 : 0;
                                else if (op == OP_LON_HON_HOAC_BANG) result = (da >= db) ? 1 : 0;
                                else result = (da <= db) ? 1 : 0;
                            } else if (std::holds_alternative<std::string>(a) && std::holds_alternative<std::string>(b)) {
                                const std::string &sa = std::get<std::string>(a);
                                const std::string &sb = std::get<std::string>(b);
                                if (op == OP_SO_SANH_BANG) result = (sa == sb) ? 1 : 0;
                                else if (op == OP_KHAC_BANG) result = (sa != sb) ? 1 : 0;
                                else if (op == OP_LON_HON) result = (sa > sb) ? 1 : 0;
                                else if (op == OP_NHO_HON) result = (sa < sb) ? 1 : 0;
                                else if (op == OP_LON_HON_HOAC_BANG) result = (sa >= sb) ? 1 : 0;
                                else result = (sa <= sb) ? 1 : 0;
                            } else {
                                throw runtime_error_op(vietvm::constants::kErrCannotCompareDifferentTypes, op, (int)pc);
                            }
                            stack.push_back(result);
                            break;
                        }
                        default:
                            throw runtime_error_op(vietvm::constants::kErrUnknownOperator, op, (int)pc);
                    }
                });
                break;
            }

            case OP_MODULO:
                program.push_back([this]() {
                    if (stack.size() < 2) throw runtime_error_op(vietvm::constants::kErrMissingModuloOperands, OP_MODULO, (int)pc);
                    StackValue b = stack.back(); stack.pop_back();
                    StackValue a = stack.back(); stack.pop_back();
                    int ib = as_int(b, OP_MODULO, (int)pc);
                    if (ib == 0) throw runtime_error_op(vietvm::constants::kErrModuloByZero, OP_MODULO, (int)pc);
                    stack.emplace_back(as_int(a, OP_MODULO, (int)pc) % ib);
                });
                break;

            case OP_PHU_DINH:
                program.push_back([this]() {
                    if (stack.empty()) throw runtime_error_op(vietvm::constants::kErrMissingNegationOperand, OP_PHU_DINH, (int)pc);
                    StackValue operand = stack.back(); stack.pop_back();
                    stack.push_back(toBool(operand) ? 0 : 1);
                });
                break;

            case OP_IN:
                program.push_back([this]() {
                    if (stack.empty()) throw runtime_error_op(vietvm::constants::kErrEmptyStackWhenPrint, OP_IN, (int)pc);
                    StackValue value = stack.back(); stack.pop_back();
                    std::cout << vietvm::constants::kOutputPrefixIn << sv_to_string(value) << std::endl;
                });
                break;

            case OP_DONG_LENH:
            case OP_MO_NGOAC:
            case OP_DONG_NGOAC:
                program.push_back([]() {});
                break;

            case OP_DUNG_CHUONG_TRINH:
                program.push_back([this]() { pc = bytecode.size(); });
                break;

            default:
                return false;
        }
    }

    pc = 0;
    while (pc < program.size()) {
        program[pc]();
        ++pc;
    }
    return true;
}

void VM::run() {
    const bool gcEnabled = vietvm::helpers::hasEnvVar(vietvm::constants::kEnvVppEnableGc)
                        || vietvm::helpers::hasEnvVar(vietvm::constants::kEnvVietvmEnableGc);
    int gcInterval = vietvm::constants::kDefaultGcInterval;
    std::optional<std::string> gcEnv = vietvm::helpers::getEnvVar(vietvm::constants::kEnvVppGcInterval);
    if (!gcEnv) gcEnv = vietvm::helpers::getEnvVar(vietvm::constants::kEnvVietvmGcInterval);
    if (gcEnv) {
        try {
            int parsed = std::stoi(*gcEnv);
            if (parsed > 0) gcInterval = parsed;
        } catch (...) {}
    }

    if (vietvm::helpers::hasEnvVar(vietvm::constants::kEnvVppEnableJit)
     || vietvm::helpers::hasEnvVar(vietvm::constants::kEnvVietvmEnableJit)) {
        if (runJitCompiledLinear()) {
            return;
        }
    }

    int executedSinceGc = 0;

    // Build function name→id table if not already populated (inherited from parent in recursive calls)
    if (functionTableByNameIndex.empty()) {
        for (size_t i = 0; i < bytecode.size(); ++i) {
            const Instruction &ci = bytecode[i];
            if (ci.op == OP_HAM && ci.operand >= 0) {
                functionTableByNameIndex[ci.operand] = ci.operandIndex;
            }
        }
    }

    auto invokeFunction = [&](int argc, int hamIdOrName, Opcode op, int curPc) {
        // 1. Thu thập Đối số (Args) từ stack của VM mẹ
        std::vector<StackValue> args;
        args.reserve(argc);
        for (int i = 0; i < argc; ++i) {
            if (stack.empty()) {
                args.emplace_back(0);
            } else {
                args.push_back(stack.back());
                stack.pop_back();
            }
        }
        std::reverse(args.begin(), args.end());

        // Native stdlib hooks (io/config/time/http)
        StackValue nativeResult = make_int_value(0);
        std::string nativeErr;
        if (executeNativeStdlibFunction(hamIdOrName, args, stringPool,
                                        functionTableByNameIndex, hamBytecodeMap,
                                        nativeResult, nativeErr)) {
            if (!nativeErr.empty()) {
                throw runtime_error_op(nativeErr, op, curPc);
            }
            stack.push_back(nativeResult);
            return;
        }

        // 2. Tạo Khung Gọi (Call Frame) cho VM con
        CallFrame frame;
        frame.args = args;
        frame.localsIndexed = true;
        frame.returnPc = static_cast<int>(curPc + 1);
        callStack.push_back(frame);

        // 3. Tra cứu Hàm
        auto it = hamBytecodeMap.find(hamIdOrName);
        if (it == hamBytecodeMap.end()) {
            int nameIndex = (hamIdOrName < 0) ? -(hamIdOrName + 1) : hamIdOrName;
            auto ftIt = functionTableByNameIndex.find(nameIndex);
            if (ftIt != functionTableByNameIndex.end()) {
                int foundHamId = ftIt->second;
                it = hamBytecodeMap.find(foundHamId);
            } else {
                for (size_t i = 0; i < bytecode.size(); ++i) {
                    const Instruction &hinst = bytecode[i];
                    if (hinst.op == OP_HAM && hinst.operand == nameIndex) {
                        int foundHamId = hinst.operandIndex;
                        auto it2 = hamBytecodeMap.find(foundHamId);
                        if (it2 != hamBytecodeMap.end()) { it = it2; break; }
                    }
                }
            }
        }

        if (it == hamBytecodeMap.end()) {
            if (!callStack.empty()) callStack.pop_back();
            int nameIndex = (hamIdOrName < 0) ? -(hamIdOrName + 1) : hamIdOrName;
            std::string fnName = "?";
            if (nameIndex >= 0 && nameIndex < static_cast<int>(stringPool.size())) {
                fnName = stringPool[nameIndex];
            }
            throw runtime_error_op(
                "OP_GOI: hàm không tồn tại (id/nameIndex=" + std::to_string(hamIdOrName) + ", name='" + fnName + "')",
                op,
                curPc
            );
        }

        // 4. Chạy VM con và Chia sẻ Trạng thái
        VM funcVM(it->second, this->stringPool);
        funcVM.variables = this->variables;
        funcVM.callStack.clear();
        funcVM.callStack.push_back(callStack.back());
        funcVM.hamBytecodeMap = this->hamBytecodeMap;
        funcVM.functionTableByNameIndex = this->functionTableByNameIndex;
        funcVM.run();

        // 5. Cập nhật Trạng thái về VM mẹ
        this->variables = funcVM.variables;
        if (!funcVM.stack.empty()) {
            stack.push_back(funcVM.stack.back());
        }
        if (!callStack.empty()) callStack.pop_back();
    };

    while (pc < bytecode.size()) {
        if (gcEnabled) {
            ++executedSinceGc;
            if (executedSinceGc >= gcInterval) {
                collectGarbage();
                executedSinceGc = 0;
            }
        }

        const Instruction &instr = bytecode[pc];
        switch (instr.op) {
            case OP_HAM: {
                break;
            }

            case OP_GOI: {
                int argc = instr.operand;
                int hamIdOrName = instr.operandIndex;
                invokeFunction(argc, hamIdOrName, instr.op, (int)pc);
                break;
            }

            case OP_GOI_GIAN_TIEP: {
                int argc = instr.operand;
                if (stack.empty()) {
                    throw runtime_error_op(vietvm::constants::kErrIndirectCallMissingRef, instr.op, pc);
                }

                StackValue calleeVal = stack.back();
                stack.pop_back();

                int hamIdOrName = -1;
                if (std::holds_alternative<int>(calleeVal)) {
                    hamIdOrName = std::get<int>(calleeVal);
                } else if (std::holds_alternative<std::string>(calleeVal)) {
                    const auto &s = std::get<std::string>(calleeVal);
                    try {
                        hamIdOrName = std::stoi(s);
                    } catch (...) {
                        auto it = std::find(stringPool.begin(), stringPool.end(), s);
                        if (it != stringPool.end()) {
                            hamIdOrName = -static_cast<int>(std::distance(stringPool.begin(), it)) - 1;
                        } else {
                            throw runtime_error_op(vietvm::constants::kErrIndirectCallInvalidRef, instr.op, pc);
                        }
                    }
                } else {
                    throw runtime_error_op(vietvm::constants::kErrIndirectCallUnsupportedRefType, instr.op, pc);
                }

                invokeFunction(argc, hamIdOrName, instr.op, (int)pc);
                break;
            }

            case OP_BIEN_SO: {
                int val = instr.operand;
                stack.emplace_back(val);
                break;
            }

            case OP_TEN_BIEN_ID: {
                int varId = instr.operandIndex;
                stack.emplace_back(varId);
                break;
            }

            case OP_NEU: {
                // Marker opcode - no runtime action here.
                break;
            }

            case OP_MODULO: {
                if (stack.size() < 2) throw runtime_error_op(vietvm::constants::kErrMissingModuloOperands, instr.op, pc);
                StackValue b = stack.back(); stack.pop_back();
                StackValue a = stack.back(); stack.pop_back();
                int int_b = as_int(b, instr.op, pc);
                if (int_b == 0) throw runtime_error_op(vietvm::constants::kErrModuloByZero, instr.op, pc);
                stack.emplace_back(as_int(a, instr.op, pc) % int_b);
                break;
            }

            case OP_CONG: case OP_TRU: case OP_NHAN: case OP_CHIA:
            case OP_Logic_VA: case OP_Logic_HOAC:
            case OP_SO_SANH_BANG: case OP_KHAC_BANG:
            case OP_LON_HON: case OP_NHO_HON:
            case OP_LON_HON_HOAC_BANG: case OP_NHO_HON_HOAC_BANG: {
                if (stack.size() < 2) throw runtime_error_op(vietvm::constants::kErrNotEnoughOperands, instr.op, pc);

                StackValue b = stack.back(); stack.pop_back();
                StackValue a = stack.back(); stack.pop_back();

                switch (instr.op) {
                    case OP_CONG: {
                        if (isNumeric(a) && isNumeric(b)) {
                            stack.push_back(numAdd(a, b));
                        } else {
                            stack.emplace_back(sv_to_string(a) + sv_to_string(b));
                        }
                        break;
                    }

                    case OP_TRU: case OP_NHAN: case OP_CHIA: {
                        if (!isNumeric(a) || !isNumeric(b))
                            throw runtime_error_op(vietvm::constants::kErrNumericOnlyOperator, instr.op, pc);
                        if (instr.op == OP_TRU)  stack.push_back(numSub(a, b));
                        else if (instr.op == OP_NHAN) stack.push_back(numMul(a, b));
                        else stack.push_back(numDiv(a, b, instr.op, pc));
                        break;
                    }

                    case OP_Logic_VA: case OP_Logic_HOAC: {
                        if (!isNumeric(a) || !isNumeric(b))
                            throw runtime_error_op(vietvm::constants::kErrLogicOnlyOperator, instr.op, pc);
                        int ia = as_int(a, instr.op, pc);
                        int ib = as_int(b, instr.op, pc);
                        if (instr.op == OP_Logic_VA) stack.push_back((ia && ib) ? 1 : 0);
                        else stack.push_back((ia || ib) ? 1 : 0);
                        break;
                    }

                    case OP_SO_SANH_BANG: case OP_KHAC_BANG:
                    case OP_LON_HON: case OP_NHO_HON:
                    case OP_LON_HON_HOAC_BANG: case OP_NHO_HON_HOAC_BANG: {
                        int result = 0;
                        if (isNumeric(a) && isNumeric(b)) {
                            double da = toDouble(a), db = toDouble(b);
                            switch (instr.op) {
                                case OP_SO_SANH_BANG: result = (da == db) ? 1 : 0; break;
                                case OP_KHAC_BANG:    result = (da != db) ? 1 : 0; break;
                                case OP_LON_HON:      result = (da >  db) ? 1 : 0; break;
                                case OP_NHO_HON:      result = (da <  db) ? 1 : 0; break;
                                case OP_LON_HON_HOAC_BANG: result = (da >= db) ? 1 : 0; break;
                                case OP_NHO_HON_HOAC_BANG: result = (da <= db) ? 1 : 0; break;
                                default: break;
                            }
                        } else if (std::holds_alternative<std::string>(a) && std::holds_alternative<std::string>(b)) {
                            std::string sa = std::get<std::string>(a), sb = std::get<std::string>(b);
                            switch (instr.op) {
                                case OP_SO_SANH_BANG: result = (sa == sb) ? 1 : 0; break;
                                case OP_KHAC_BANG:    result = (sa != sb) ? 1 : 0; break;
                                case OP_LON_HON:      result = (sa >  sb) ? 1 : 0; break;
                                case OP_NHO_HON:      result = (sa <  sb) ? 1 : 0; break;
                                case OP_LON_HON_HOAC_BANG: result = (sa >= sb) ? 1 : 0; break;
                                case OP_NHO_HON_HOAC_BANG: result = (sa <= sb) ? 1 : 0; break;
                                default: break;
                            }
                        } else {
                            throw runtime_error_op(vietvm::constants::kErrCannotCompareDifferentTypes, instr.op, pc);
                        }
                        stack.push_back(result);
                        break;
                    }

                    default:
                        throw runtime_error_op(vietvm::constants::kErrUnknownOperator, instr.op, pc);
                }
                break;
            }

            case OP_KHONG: {
                if (stack.empty()) throw runtime_error_op(vietvm::constants::kErrMissingNotOperands, instr.op, pc);
                StackValue a = stack.back(); stack.pop_back();
                stack.push_back((as_int(a, instr.op, pc) == 0) ? 1 : 0);
                break;
            }

            case OP_KHOI_TAO: {
                int varId = instr.operandIndex;
                // If we are inside a call frame, initialize local storage there.
                if (!callStack.empty()) {
                    CallFrame &frame = callStack.back();
                    if (frame.localsIndexed) {
                        if (varId >= 0 && varId >= (int)frame.localsVec.size()) {
                            frame.localsVec.resize(varId + 1, make_int_value(0));
                        } else if (varId >= 0 && varId < (int)frame.localsVec.size()) {
                            frame.localsVec[varId] = make_int_value(0);
                        }
                    } else {
                        frame.localsMap[varId] = make_int_value(0);
                    }
                } else {
                    if (variables.count(varId) == 0) {
                        variables[varId] = make_int_value(0);
                    }
                }
                break;
            }

            case OP_DIEU_KIEN:
                break;

            case OP_LAP:
                break;

            case OP_CAP_NHAT:
                // metadata label - no runtime action
                break;

            case OP_CHUOI: {
                if (instr.operandIndex < 0 || instr.operandIndex >= (int)stringPool.size()) {
                    throw runtime_error_op(vietvm::constants::kErrInvalidStringIndex, instr.op, pc);
                }
                stack.push_back(stringPool[instr.operandIndex]);
                break;
            }

            case OP_BIEN_SO_FLOAT: {
                if (instr.operandIndex < 0 || instr.operandIndex >= (int)stringPool.size())
                    throw runtime_error_op(vietvm::constants::kErrInvalidFloatIndex, instr.op, pc);
                try {
                    double d = std::stod(stringPool[instr.operandIndex]);
                    stack.push_back(make_float_value(d));
                } catch (...) {
                    throw runtime_error_op(std::string(vietvm::constants::kErrCannotConvertToFloatPrefix) + stringPool[instr.operandIndex] + vietvm::constants::kErrCannotConvertToFloatSuffix, instr.op, pc);
                }
                break;
            }

            case OP_RONG_GIA_TRI: {
                stack.push_back(make_null_value());
                break;
            }

            case OP_MAP_LITERAL: {
                if (instr.operandIndex < 0 || instr.operandIndex >= (int)stringPool.size()) {
                    throw runtime_error_op(vietvm::constants::kErrInvalidMapIndex, instr.op, pc);
                }
                try {
                    MapValue parsed = decodeMapFromStringPool(stringPool[instr.operandIndex]);
                    stack.push_back(make_map_value(parsed));
                } catch (const std::exception &ex) {
                    throw runtime_error_op(std::string(vietvm::constants::kErrParseMapLiteralPrefix) + ex.what(), instr.op, pc);
                }
                break;
            }

            case OP_IN: {
                if (stack.empty()) throw runtime_error_op(vietvm::constants::kErrEmptyStackWhenPrint, instr.op, pc);
                StackValue value = stack.back(); stack.pop_back();
                if (!switchStack.empty() && switchStack.back().skippingCase) {
                    break;
                }

                std::cout << vietvm::constants::kOutputPrefixIn << sv_to_string(value) << std::endl;
                break;
            }

            case OP_TRA_VE: {
                if (stack.empty()) {
                    stack.push_back(make_int_value(0));
                }
                return;
            }

            case OP_BO_QUA: {
                // Nhảy đến OP_CAP_NHAT gần nhất (bắt đầu phần update của vòng lặp)
                size_t scanPc = pc + 1;
                while (scanPc < bytecode.size()) {
                    if (bytecode[scanPc].op == OP_CAP_NHAT) {
                        pc = (int)scanPc;
                        goto bo_qua_done;
                    }
                    ++scanPc;
                }
                throw runtime_error_op(vietvm::constants::kErrContinueMissingUpdate, instr.op, pc);
                bo_qua_done:
                break;
            }

            case OP_CHON: {
                if (stack.empty()) throw runtime_error_op(vietvm::constants::kErrSwitchEmptyStack, instr.op, pc);
                SwitchFrame frame;
                frame.switchValue = stack.back();
                stack.pop_back();
                frame.skippingCase = true;
                frame.caseMatched = false;
                frame.blockDepthAtStart = blockStack.size();
                switchStack.push_back(frame);
                break;
            }

            case OP_CA: {
                if (switchStack.empty() || !switchStack.back().switchValue.has_value()) {
                    throw runtime_error_op(vietvm::constants::kErrCaseOutsideSwitch, instr.op, pc);
                }
                auto& ctx = switchStack.back();

                bool match = false;
                if (instr.operandIndex >= 0) {
                    std::string caseStr = stringPool.at(instr.operandIndex);
                    if (std::holds_alternative<std::string>(*ctx.switchValue)) {
                        match = (std::get<std::string>(*ctx.switchValue) == caseStr);
                    } else if (std::holds_alternative<int>(*ctx.switchValue)) {
                        match = (std::to_string(std::get<int>(*ctx.switchValue)) == caseStr);
                    }
                } else if (instr.operandIndex == -1) {
                    if (std::holds_alternative<int>(*ctx.switchValue)) {
                        match = (std::get<int>(*ctx.switchValue) == instr.operand);
                    } else if (std::holds_alternative<std::string>(*ctx.switchValue)) {
                        try {
                            int sv = std::stoi(std::get<std::string>(*ctx.switchValue));
                            match = (sv == instr.operand);
                        } catch (...) { match = false; }
                    }
                } else if (instr.operandIndex == -2) {
                    int varId = instr.operand;
                    int varValue = 0;
                    if (variables.count(varId)) {
                        varValue = as_int(variables[varId], instr.op, pc);
                    }
                    if (std::holds_alternative<int>(*ctx.switchValue)) {
                        match = (std::get<int>(*ctx.switchValue) == varValue);
                    } else if (std::holds_alternative<std::string>(*ctx.switchValue)) {
                        try {
                            int sv = std::stoi(std::get<std::string>(*ctx.switchValue));
                            match = (sv == varValue);
                        } catch (...) { match = false; }
                    }
                } else {
                    throw runtime_error_op(vietvm::constants::kErrCaseInvalidOperandFormat, instr.op, pc);
                }

                if (!ctx.caseMatched && match) {
                    ctx.skippingCase = false;
                    ctx.caseMatched = true;
                } else {
                    ctx.skippingCase = true;
                }
                break;
            }

            case OP_MAC_DINH: {
                if (switchStack.empty()) throw runtime_error_op(vietvm::constants::kErrDefaultOutsideSwitch, instr.op, pc);
                auto& ctx = switchStack.back();
                if (!ctx.caseMatched) {
                    ctx.skippingCase = false;
                    ctx.caseMatched = true;
                } else {
                    ctx.skippingCase = true;
                }
                break;
            }

            case OP_THOAT: {
                if (switchStack.empty()) throw runtime_error_op(vietvm::constants::kErrBreakOutsideSwitch, instr.op, pc);
                auto& ctx = switchStack.back();
                ctx.skippingCase = true;

                while (pc < bytecode.size()) {
                    if (bytecode[pc].op == OP_DONG_KHOI) {
                        ++pc;
                        break;
                    }
                    ++pc;
                }
                continue;
            }

            case OP_TEN_BIEN_GIA_TRI: {
                int varId = instr.operandIndex;

                // Prefer locals if inside a call frame AND local has been initialized
                if (!callStack.empty()) {
                    CallFrame &frame = callStack.back();
                    if (frame.localsIndexed) {
                        // Only use local if it exists (was set by OP_PARAM/OP_KHOI_TAO)
                        if (varId >= 0 && varId < (int)frame.localsVec.size()) {
                            stack.push_back(frame.localsVec[varId]);
                            break;
                        }
                        // Otherwise fall through to global variables
                    } else {
                        auto itloc = frame.localsMap.find(varId);
                        if (itloc != frame.localsMap.end()) {
                            // Local exists, use it
                            stack.push_back(itloc->second);
                            break;
                        }
                        // Otherwise fall through to global variables
                    }
                }

                // Fallback to global variables
                if (variables.count(varId) == 0) {
                    // Silently initialize to 0 - this can happen for:
                    // 1. Variables that are implicitly declared
                    // 2. First access before explicit assignment
                    variables[varId] = make_int_value(0);
                }
                stack.push_back(variables[varId]);
                break;
            }

            case OP_GAN: {
                if (stack.size() < 2) throw runtime_error_op(vietvm::constants::kErrAssignNotEnoughOperands, instr.op, pc);

                StackValue top = stack.back();
                StackValue second = stack[stack.size() - 2];

                StackValue varIdVal;
                StackValue valueVal;

                if (std::holds_alternative<int>(top) && std::holds_alternative<std::string>(second)) {
                    // compiler convention might differ; detect common patterns
                    varIdVal = top;
                    valueVal = second;
                } else if (std::holds_alternative<std::string>(top) && std::holds_alternative<int>(second)) {
                    varIdVal = second;
                    valueVal = top;
                } else {
                    // fallback based on convention: top = varId, second = value
                    varIdVal = top;
                    valueVal = second;
                }

                // pop both
                stack.pop_back();
                stack.pop_back();

                if (!std::holds_alternative<int>(varIdVal))
                    throw runtime_error_op(vietvm::constants::kErrVarIdMustBeInt, instr.op, pc);

                int varId = std::get<int>(varIdVal);

                // Prefer writing to locals if inside a call frame
                // This ensures consistency with OP_TEN_BIEN_GIA_TRI which reads from locals first
                if (!callStack.empty()) {
                    CallFrame &frame = callStack.back();
                    if (frame.localsIndexed) {
                        // Existing local slot -> write local
                        if (varId >= 0 && varId < (int)frame.localsVec.size()) {
                            frame.localsVec[varId] = valueVal;
                            break;
                        }

                        // Existing global variable -> keep writing global for shared-global semantics
                        if (variables.count(varId) != 0) {
                            variables[varId] = valueVal;
                            break;
                        }

                        // Implicit local assignment inside function: create local slot on demand.
                        if (varId >= 0) {
                            frame.localsVec.resize(varId + 1, make_int_value(0));
                            frame.localsVec[varId] = valueVal;
                            break;
                        }
                    } else {
                        auto itloc = frame.localsMap.find(varId);
                        if (itloc != frame.localsMap.end()) {
                            frame.localsMap[varId] = valueVal;
                            break;
                        }

                        if (variables.count(varId) != 0) {
                            variables[varId] = valueVal;
                            break;
                        }

                        // Implicit local assignment inside function.
                        frame.localsMap[varId] = valueVal;
                        break;
                    }
                }

                // Fallback: store in global variables map
                variables[varId] = valueVal;
                break;
            }

            case OP_JUMP: {
                int jump_address = instr.operand;
                if (jump_address < 0 || jump_address >= (int)bytecode.size()) {
                    throw runtime_error_op(vietvm::constants::kErrJumpAddressOutOfRange, instr.op, pc);
                }
                pc = jump_address;
                continue;
            }

            case OP_JUMP_IF_FALSE: {
                int jump_address = instr.operand;
                if (stack.empty()) throw runtime_error_op(vietvm::constants::kErrJumpIfFalseEmptyStack, instr.op, pc);
                StackValue condition = stack.back(); stack.pop_back();

                if (!std::holds_alternative<int>(condition))
                    throw runtime_error_op(vietvm::constants::kErrJumpConditionMustBeInt, instr.op, pc);

                int condition_value = as_int(condition, instr.op, pc);
                if (condition_value == 0) {
                    if (jump_address < 0 || jump_address >= (int)bytecode.size())
                        throw runtime_error_op(vietvm::constants::kErrJumpAddressOutOfRange, instr.op, pc);
                    pc = jump_address;
                    continue;
                }
                break;
            }

            case OP_DUNG_CHUONG_TRINH:
                return;

            case OP_MO_KHOI: {
                blockStack.push_back(pc);
                ++blockDepth;
                break;
            }

            case OP_DONG_KHOI: {
                if (blockStack.empty()) throw runtime_error_op(vietvm::constants::kErrNoOpenBlock, instr.op, pc);
                blockStack.pop_back();

                if (blockDepth > 0) --blockDepth;

                if (!switchStack.empty()) {
                    if (blockStack.size() == switchStack.back().blockDepthAtStart - 1) {
                        switchStack.pop_back();
                    }
                }
                break;
            }

            case OP_DONG_LENH: case OP_MO_NGOAC: case OP_DONG_NGOAC:
                break;

            case OP_PHU_DINH: {
                if (stack.empty()) throw runtime_error_op(vietvm::constants::kErrMissingNegationOperand, instr.op, pc);
                StackValue operand = stack.back(); stack.pop_back();
                bool b = toBool(operand);
                int result = b ? 0 : 1;
                stack.push_back(result);
                break;
            }
            case OP_PARAM: {
                int localId = instr.operandIndex;
                int argIndex = instr.operandValue;

                if (callStack.empty()) {
                    variables[localId] = make_int_value(0);
                    break;
                }
                CallFrame &frame = callStack.back();
                StackValue v;
                if (argIndex >= 0 && argIndex < (int)frame.args.size()) {
                    v = frame.args[argIndex];
                } else {
                    v = make_int_value(0);
                    vmLog(vietvm::constants::kWarnParamArgIndexOutOfRange);
                }
                if (frame.localsIndexed) {
                    if (localId >= (int)frame.localsVec.size()) frame.localsVec.resize(localId + 1, make_int_value(0));
                    frame.localsVec[localId] = v;
                } else {
                    frame.localsMap[localId] = v;
                }
                break;
            }
            case OP_PARAM_MAC_DINH: {
                int defaultIndex = instr.operand;
                int localId = instr.operandIndex;
                int argIndex = instr.operandValue;

                if (defaultIndex < 0 || defaultIndex >= (int)stringPool.size()) {
                    throw runtime_error_op(vietvm::constants::kErrDefaultParamIndexInvalid, instr.op, pc);
                }

                StackValue v;
                if (!callStack.empty() && argIndex >= 0 && argIndex < (int)callStack.back().args.size()) {
                    v = callStack.back().args[argIndex];
                } else {
                    v = decodeDefaultParamValue(stringPool[defaultIndex]);
                }

                if (callStack.empty()) {
                    variables[localId] = v;
                    break;
                }

                CallFrame &frame = callStack.back();
                if (frame.localsIndexed) {
                    if (localId >= (int)frame.localsVec.size()) frame.localsVec.resize(localId + 1, make_int_value(0));
                    frame.localsVec[localId] = v;
                } else {
                    frame.localsMap[localId] = v;
                }
                break;
            }
            case OP_CONG_MOT: {
                if (stack.empty()) throw runtime_error_op(vietvm::constants::kErrIncEmptyStack, instr.op, pc);
                StackValue top = stack.back(); stack.pop_back();
                auto push_int = [&](int v) { stack.push_back(make_int_value(v)); };
                if (std::holds_alternative<int>(top)) {
                    int idOrVal = std::get<int>(top);
                    bool updated = false;
                    if (!callStack.empty()) {
                        CallFrame &frame = callStack.back();
                        if (frame.localsIndexed) {
                            // Only use localsVec if slot already exists (do NOT resize)
                            if (idOrVal >= 0 && idOrVal < (int)frame.localsVec.size()) {
                                int cur = as_int(frame.localsVec[idOrVal], instr.op, pc);
                                frame.localsVec[idOrVal] = make_int_value(cur + 1);
                                push_int(cur + 1); updated = true;
                            }
                        } else {
                            auto itLoc = frame.localsMap.find(idOrVal);
                            if (itLoc != frame.localsMap.end()) {
                                int cur = as_int(itLoc->second, instr.op, pc);
                                itLoc->second = make_int_value(cur + 1);
                                push_int(cur + 1); updated = true;
                            }
                        }
                    }
                    if (!updated) {
                        int cur = variables.count(idOrVal) ? as_int(variables[idOrVal], instr.op, pc) : 0;
                        variables[idOrVal] = make_int_value(cur + 1);
                        push_int(cur + 1);
                    }
                } else if (std::holds_alternative<std::string>(top)) {
                    try { push_int(std::stoi(std::get<std::string>(top)) + 1); }
                    catch (...) { throw runtime_error_op(vietvm::constants::kErrIncCannotIncreaseString, instr.op, pc); }
                } else { throw runtime_error_op(vietvm::constants::kErrIncUnsupportedType, instr.op, pc); }
                break;
            }
            case OP_THU: {
                // operand = address of OP_BAT_LOI handler, operandIndex = errVarId (-1 = none)
                TryFrame tf;
                tf.catchAddr = instr.operand;
                tf.stackDepth = (int)stack.size();
                tf.errVarId = instr.operandIndex;
                tryStack.push_back(tf);
                break;
            }

            case OP_THU_KET_THUC: {
                // Normal exit from try block: pop tryStack, jump past catch handler
                if (!tryStack.empty()) tryStack.pop_back();
                pc = instr.operand;  // jump past catch
                continue;
            }

            case OP_BAT_LOI: {
                // Catch handler entry: pop tryStack (already popped by OP_NEM)
                // operandIndex = errVarId (-1 if no variable binding)
                // The error value is on top of stack (pushed by OP_NEM)
                int errVarId = instr.operandIndex;
                if (errVarId >= 0 && !stack.empty()) {
                    StackValue errVal = stack.back(); stack.pop_back();
                    variables[errVarId] = errVal;
                } else if (!stack.empty()) {
                    stack.pop_back();  // discard error value
                }
                break;
            }

            case OP_NEM: {
                if (stack.empty()) stack.push_back(make_string_value(vietvm::constants::kErrUnknownThrownValue));
                StackValue errVal = stack.back(); stack.pop_back();
                if (tryStack.empty()) {
                    // No catch handler: propagate as C++ exception
                    throw std::runtime_error(std::string(vietvm::constants::kErrUncaughtPrefix) + sv_to_string(errVal));
                }
                TryFrame tf = tryStack.back(); tryStack.pop_back();
                // Unwind stack to try entry depth
                while ((int)stack.size() > tf.stackDepth) stack.pop_back();
                // Push error value for OP_BAT_LOI to consume
                stack.push_back(errVal);
                pc = tf.catchAddr;  // jump to catch handler
                continue;
            }

            case OP_TRU_MOT: {
                if (stack.empty()) throw runtime_error_op(vietvm::constants::kErrDecEmptyStack, instr.op, pc);
                StackValue top = stack.back(); stack.pop_back();
                auto push_int2 = [&](int v) { stack.push_back(make_int_value(v)); };
                if (std::holds_alternative<int>(top)) {
                    int idOrVal = std::get<int>(top);
                    bool updated = false;
                    if (!callStack.empty()) {
                        CallFrame &frame = callStack.back();
                        if (frame.localsIndexed) {
                            if (idOrVal >= 0 && idOrVal < (int)frame.localsVec.size()) {
                                int cur = as_int(frame.localsVec[idOrVal], instr.op, pc);
                                frame.localsVec[idOrVal] = make_int_value(cur - 1);
                                push_int2(cur - 1); updated = true;
                            }
                        } else {
                            auto itLoc = frame.localsMap.find(idOrVal);
                            if (itLoc != frame.localsMap.end()) {
                                int cur = as_int(itLoc->second, instr.op, pc);
                                itLoc->second = make_int_value(cur - 1);
                                push_int2(cur - 1); updated = true;
                            }
                        }
                    }
                    if (!updated) {
                        int cur = variables.count(idOrVal) ? as_int(variables[idOrVal], instr.op, pc) : 0;
                        variables[idOrVal] = make_int_value(cur - 1);
                        push_int2(cur - 1);
                    }
                } else { throw runtime_error_op(vietvm::constants::kErrDecUnsupportedType, instr.op, pc); }
                break;
            }
            default:
                // If in a skipping switch block, ignore instructions
                if (!switchStack.empty() && switchStack.back().skippingCase) {
                    break;
                }
                throw runtime_error_op(vietvm::constants::kErrUnknownOpcode, instr.op, pc);
        }
        ++pc;
    }
}

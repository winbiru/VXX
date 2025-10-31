#include <iostream>
#include <stdexcept>
#include <vector>
#include <stack>
#include <variant>
#include <string>
#include <map>
#include <unordered_map>
#include "vm.h"

#include <algorithm>
#include <common/Lex_utils.h>
#include "../include/common/vm_utils.h" // adjust include path according to project

using StackValue = std::variant<int, std::string>;

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
    if (std::holds_alternative<int>(value)) {
        return std::get<int>(value) != 0;
    } else if (std::holds_alternative<std::string>(value)) {
        return !std::get<std::string>(value).empty();
    }
    // default false for unknown
    return false;
}

void VM::run() {
    // Precompute function table once (avoid rebuilding each iteration)
    std::unordered_map<std::string,int> functionTable;
    for (size_t i = 0; i < bytecode.size(); ++i) {
        const Instruction &ci = bytecode[i];
        if (ci.op == OP_HAM) {
            if (ci.operandIndex >= 0 && ci.operandIndex < (int)stringPool.size()) {
                std::string fname = stringPool[ci.operandIndex];
                functionTable[fname] = static_cast<int>(i + 1);
            }
        }
    }

    while (pc < bytecode.size()) {
        const Instruction &instr = bytecode[pc];

        // Helpful debug (can be gated behind a debug flag)
        // std::cerr << "[VM] pc=" << pc << " op=" << name_op(instr.op) << " stack=" << stack.size() << "\n";

        switch (instr.op) {
            case OP_HAM: {
                int hamIndex = instr.operand;
                auto it = hamBytecodeMap.find(hamIndex);
                if (it != hamBytecodeMap.end()) {
                    VM hamVM(it->second, this->stringPool);
                    hamVM.run(); // run function as sub-program
                }
                break;
            }

            case OP_GOI: {
                // convention: ideally instr.operand = hamId, instr.operandIndex = argc
                int candidate = instr.operand;
                int argc = instr.operandIndex;

                // Collect argc args from caller stack.
                std::vector<StackValue> args;
                args.reserve(argc);
                for (int i = 0; i < argc; ++i) {
                    if (stack.empty()) {
                        // missing arg -> default 0
                        args.emplace_back(0);
                    } else {
                        args.push_back(stack.back());
                        stack.pop_back();
                    }
                }
                std::reverse(args.begin(), args.end());

                // Push a new CallFrame for the callee
                CallFrame frame;
                frame.args = args;
                frame.localsIndexed = true;
                frame.returnPc = static_cast<int>(pc + 1);
                callStack.push_back(frame);

                // Try to find function by hamId = candidate
                auto it = hamBytecodeMap.find(candidate);

                // Fallback: if not found, treat candidate as nameIndex (string pool index)
                if (it == hamBytecodeMap.end()) {
                    int nameIndex = candidate;
                    // scan current top-level bytecode for OP_HAM entries with operandIndex == nameIndex
                    // and use its operand as hamId (if compiler emitted OP_HAM with operand=hamId and operandIndex=nameIndex)
                    for (size_t i = 0; i < bytecode.size(); ++i) {
                        const Instruction &hinst = bytecode[i];
                        if (hinst.op == OP_HAM) {
                            if (hinst.operandIndex == nameIndex) {
                                int foundHamId = hinst.operand;
                                auto it2 = hamBytecodeMap.find(foundHamId);
                                if (it2 != hamBytecodeMap.end()) {
                                    it = it2;
                                    break;
                                }
                            }
                        }
                    }
                }

                if (it == hamBytecodeMap.end()) {
                    // cleanup and error
                    callStack.pop_back();
                    throw std::runtime_error("OP_GOI: hàm không tồn tại (id=" + std::to_string(candidate) + ")");
                }

                // Run function in sub-VM, copy the top frame so OP_PARAM can read args
                VM funcVM(it->second, this->stringPool);
                funcVM.callStack.clear();
                funcVM.callStack.push_back(callStack.back());
                funcVM.hamBytecodeMap = this->hamBytecodeMap;

                funcVM.run();

                // propagate possible return value from sub-VM
                if (!funcVM.stack.empty()) {
                    StackValue ret = funcVM.stack.back();
                    stack.push_back(ret);
                }

                // pop caller's frame (we added earlier)
                if (!callStack.empty()) callStack.pop_back();

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
                if (stack.size() < 2) throw runtime_error_op("Lỗi: thiếu toán hạng cho MODULO", instr.op, pc);

                StackValue b = stack.back(); stack.pop_back();
                StackValue a = stack.back(); stack.pop_back();

                int int_b = as_int(b, instr.op, pc);
                if (int_b == 0) throw runtime_error_op("Lỗi: chia dư cho 0", instr.op, pc);

                int int_a = as_int(a, instr.op, pc);
                stack.emplace_back(int_a % int_b);
                break;
            }

            case OP_CONG: case OP_TRU: case OP_NHAN: case OP_CHIA:
            case OP_Logic_VA: case OP_Logic_HOAC:
            case OP_SO_SANH_BANG: case OP_KHAC_BANG:
            case OP_LON_HON: case OP_NHO_HON:
            case OP_LON_HON_HOAC_BANG: case OP_NHO_HON_HOAC_BANG: {
                if (stack.size() < 2) throw runtime_error_op("Lỗi: không đủ toán hạng cho toán tử", instr.op, pc);

                StackValue b = stack.back(); stack.pop_back();
                StackValue a = stack.back(); stack.pop_back();

                switch (instr.op) {
                    case OP_CONG: {
                        // int + int or string concat
                        if (std::holds_alternative<int>(a) && std::holds_alternative<int>(b)) {
                            stack.push_back(as_int(a, instr.op, pc) + as_int(b, instr.op, pc));
                        } else {
                            std::string sa = sv_to_string(a);
                            std::string sb = sv_to_string(b);
                            stack.emplace_back(sa + sb);
                        }
                        break;
                    }

                    case OP_TRU: case OP_NHAN: case OP_CHIA: {
                        if (!std::holds_alternative<int>(a) || !std::holds_alternative<int>(b))
                            throw runtime_error_op("Lỗi: toán tử số chỉ áp dụng cho số nguyên", instr.op, pc);

                        int ia = as_int(a, instr.op, pc);
                        int ib = as_int(b, instr.op, pc);

                        if (instr.op == OP_TRU) stack.push_back(ia - ib);
                        else if (instr.op == OP_NHAN) stack.push_back(ia * ib);
                        else {
                            if (ib == 0) throw runtime_error_op("Lỗi: chia cho 0", instr.op, pc);
                            stack.push_back(ia / ib);
                        }
                        break;
                    }

                    case OP_Logic_VA: case OP_Logic_HOAC: {
                        if (!std::holds_alternative<int>(a) || !std::holds_alternative<int>(b))
                            throw runtime_error_op("Lỗi: toán tử logic chỉ áp dụng cho số nguyên", instr.op, pc);

                        int ia = as_int(a, instr.op, pc);
                        int ib = as_int(b, instr.op, pc);
                        if (instr.op == OP_Logic_VA) stack.push_back((ia && ib) ? 1 : 0);
                        else stack.push_back((ia || ib) ? 1 : 0);
                        break;
                    }

                    case OP_SO_SANH_BANG: case OP_KHAC_BANG:
                    case OP_LON_HON: case OP_NHO_HON:
                    case OP_LON_HON_HOAC_BANG: case OP_NHO_HON_HOAC_BANG: {
                        if (a.index() != b.index())
                            throw runtime_error_op("Lỗi: không thể so sánh hai kiểu dữ liệu khác nhau", instr.op, pc);

                        if (std::holds_alternative<int>(a)) {
                            int ia = std::get<int>(a);
                            int ib = std::get<int>(b);
                            switch (instr.op) {
                                case OP_SO_SANH_BANG: stack.emplace_back((ia == ib) ? 1 : 0); break;
                                case OP_KHAC_BANG: stack.emplace_back((ia != ib) ? 1 : 0); break;
                                case OP_LON_HON: stack.emplace_back((ia > ib) ? 1 : 0); break;
                                case OP_NHO_HON: stack.emplace_back((ia < ib) ? 1 : 0); break;
                                case OP_LON_HON_HOAC_BANG: stack.emplace_back((ia >= ib) ? 1 : 0); break;
                                case OP_NHO_HON_HOAC_BANG: stack.emplace_back((ia <= ib) ? 1 : 0); break;
                                default:     break;
                            }
                        } else {
                            std::string sa = std::get<std::string>(a);
                            std::string sb = std::get<std::string>(b);
                            switch (instr.op) {
                                case OP_SO_SANH_BANG: stack.push_back((sa == sb) ? 1 : 0); break;
                                case OP_KHAC_BANG: stack.push_back((sa != sb) ? 1 : 0); break;
                                case OP_LON_HON: stack.push_back((sa > sb) ? 1 : 0); break;
                                case OP_NHO_HON: stack.push_back((sa < sb) ? 1 : 0); break;
                                case OP_LON_HON_HOAC_BANG: stack.push_back((sa >= sb) ? 1 : 0); break;
                                case OP_NHO_HON_HOAC_BANG: stack.push_back((sa <= sb) ? 1 : 0); break;
                                default:     break;
                            }
                        }
                        break;
                    }

                    default:
                        throw runtime_error_op("Toán tử không xác định", instr.op, pc);
                }
                break;
            }

            case OP_KHONG: {
                if (stack.empty()) throw runtime_error_op("Lỗi: không đủ toán hạng cho toán tử phủ định", instr.op, pc);

                StackValue a = stack.back(); stack.pop_back();

                if (!std::holds_alternative<int>(a))
                    throw runtime_error_op("Lỗi: toán tử phủ định chỉ áp dụng cho số nguyên", instr.op, pc);

                int ia = as_int(a, instr.op, pc);
                stack.push_back((!ia) ? 1 : 0);
                break;
            }

            case OP_KHOI_TAO: {
                int varId = instr.operandIndex;
                if (variables.count(varId) == 0) {
                    variables[varId] = 0;
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
                    throw runtime_error_op("Lỗi: chỉ số chuỗi không hợp lệ", instr.op, pc);
                }
                stack.push_back(stringPool[instr.operandIndex]);
                break;
            }

            case OP_IN: {
                if (stack.empty()) throw runtime_error_op("Lỗi: stack rỗng khi IN", instr.op, pc);
                StackValue value = stack.back(); stack.pop_back();
                if (!switchStack.empty() && switchStack.back().skippingCase) {
                    break;
                }

                std::cout << "[IN] " << sv_to_string(value) << std::endl;
                break;
            }

            case OP_CHON: {
                if (stack.empty()) throw runtime_error_op("CHON: Stack rỗng", instr.op, pc);
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
                    throw runtime_error_op("CA: Không nằm trong khối CHON", instr.op, pc);
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
                    throw runtime_error_op("CA: định dạng operand không hợp lệ", instr.op, pc);
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
                if (switchStack.empty()) throw runtime_error_op("MAC_DINH: Không nằm trong khối CHON", instr.op, pc);
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
                if (switchStack.empty()) throw runtime_error_op("THOAT: Không nằm trong khối CHON", instr.op, pc);
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
                if (variables.count(varId) == 0) {
                    std::cerr << "Cảnh báo: biến ID " << varId << " chưa được khởi tạo. Mặc định = 0.\n";
                    variables[varId] = 0;
                }
                stack.push_back(variables[varId]);
                break;
            }

            case OP_GAN: {
                if (stack.size() < 2) throw runtime_error_op("Không đủ toán hạng để GÁN", instr.op, pc);

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
                    throw runtime_error_op("Lỗi: ID biến phải là số nguyên", instr.op, pc);

                int varId = std::get<int>(varIdVal);

                // store variant into variables map
                variables[varId] = valueVal;
                break;
            }

            case OP_JUMP: {
                int jump_address = instr.operand;
                if (jump_address < 0 || jump_address >= (int)bytecode.size()) {
                    throw runtime_error_op("Lỗi: địa chỉ nhảy ngoài phạm vi", instr.op, pc);
                }
                // dispatch loop increments pc manually at end of loop,
                // so set pc directly and continue to avoid extra ++ at end
                pc = jump_address;
                continue;
            }

            case OP_JUMP_IF_FALSE: {
                int jump_address = instr.operand;
                if (stack.empty()) throw runtime_error_op("Lỗi: Stack rỗng khi thực thi OP_JUMP_IF_FALSE", instr.op, pc);
                StackValue condition = stack.back(); stack.pop_back();

                if (!std::holds_alternative<int>(condition))
                    throw runtime_error_op("Lỗi: điều kiện nhảy phải là số nguyên", instr.op, pc);

                int condition_value = as_int(condition, instr.op, pc);
                if (condition_value == 0) {
                    if (jump_address < 0 || jump_address >= (int)bytecode.size())
                        throw runtime_error_op("Lỗi: địa chỉ nhảy ngoài phạm vi", instr.op, pc);
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
                if (blockStack.empty()) throw runtime_error_op("Lỗi: không có khối mở", instr.op, pc);
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
                if (stack.empty()) throw runtime_error_op("Thiếu toán hạng cho toán tử phủ định !", instr.op, pc);
                StackValue operand = stack.back(); stack.pop_back();
                bool b = toBool(operand);
                int result = b ? 0 : 1;
                stack.push_back(result);
                break;
            }
            case OP_PARAM: {
                // instr.operandIndex = localId
                // instr.operandValue = argIndex
                int localId = instr.operandIndex;
                int argIndex = instr.operandValue;

                // If no call frame, fall back to global variables map
                if (callStack.empty()) {
                    // assign default value 0 to global variable slot (variables map in vm.h)
                    variables[localId] = make_int_value(0); // make_int_value from vm_utils.h
                    break;
                }

                CallFrame &frame = callStack.back();
                if (argIndex >= 0 && argIndex < (int)frame.args.size()) {
                    StackValue v = frame.args[argIndex];
                    if (frame.localsIndexed) {
                        if (localId >= (int)frame.localsVec.size()) frame.localsVec.resize(localId + 1);
                        frame.localsVec[localId] = v;
                    } else {
                        frame.localsMap[localId] = v;
                    }
                } else {
                    // out-of-range -> default 0
                    StackValue def = make_int_value(0);
                    if (frame.localsIndexed) {
                        if (localId >= (int)frame.localsVec.size()) frame.localsVec.resize(localId + 1);
                        frame.localsVec[localId] = def;
                    } else {
                        frame.localsMap[localId] = def;
                    }
                    vmLog("Cảnh báo: OP_PARAM argIndex ngoài phạm vi, gán mặc định 0");
                }
                break;
            }
            default:
                // If in a skipping switch block, ignore instructions
                if (!switchStack.empty() && switchStack.back().skippingCase) {
                    break;
                }
                throw runtime_error_op("Opcode không xác định", instr.op, pc);
        }

        // advance program counter (manual dispatch)
        ++pc;
    }
}
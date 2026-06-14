#include <iostream>
#include <stdexcept>
#include <vector>
#include <stack>
#include <variant>
#include <string>
#include <unordered_map>
#include "../../include/vm/vm.h"

#include <algorithm>
#include "../../include/common/vm_utils.h"
#include "common/storeString.h"

using StackValue = std::variant<int, double, std::string>;

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
    return false;
}

void VM::run() {
    // Build function name→id table if not already populated (inherited from parent in recursive calls)
    if (functionTableByNameIndex.empty()) {
        for (size_t i = 0; i < bytecode.size(); ++i) {
            const Instruction &ci = bytecode[i];
            if (ci.op == OP_HAM && ci.operand >= 0) {
                functionTableByNameIndex[ci.operand] = ci.operandIndex;
            }
        }
        // Merge compiler-provided hamNameIndexMap
        for (const auto &p : vietvm::compiler::hamMap::hamNameIndexMap) {
            int hamId = p.first;
            int nameIndex = p.second;
            if (functionTableByNameIndex.find(nameIndex) == functionTableByNameIndex.end()) {
                functionTableByNameIndex[nameIndex] = hamId;
            }
        }
    }
    while (pc < bytecode.size()) {
        const Instruction &instr = bytecode[pc];
        switch (instr.op) {
            case OP_HAM: {
                break;
            }

            case OP_GOI: {
                int argc = instr.operand;
                int hamIdOrName = instr.operandIndex;

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

                // 2. Tạo Khung Gọi (Call Frame) cho VM con
                CallFrame frame;
                frame.args = args;
                frame.localsIndexed = true;
                frame.returnPc = static_cast<int>(pc + 1);
                callStack.push_back(frame); // Đẩy vào stack của VM mẹ (để chuyển cho VM con)

                // 3. Tra cứu Hàm
                auto it = hamBytecodeMap.find(hamIdOrName);

                // Xử lý tra cứu thất bại (Khối này cần được dọn dẹp và sửa lỗi cú pháp)
                if (it == hamBytecodeMap.end()) {
                    int nameIndex = hamIdOrName;
                    auto ftIt = functionTableByNameIndex.find(nameIndex);
                    if (ftIt != functionTableByNameIndex.end()) {
                        int foundHamId = ftIt->second;
                        it = hamBytecodeMap.find(foundHamId);
                    } else {
                        // Fallback scan (Chỉ nên sử dụng nếu cần)
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

                // 4. KIỂM TRA LỖI CUỐI CÙNG & THỰC THI (Đã sửa lỗi cú pháp/logic)
                if (it == hamBytecodeMap.end()) {
                    // cleanup and error
                    if (!callStack.empty()) callStack.pop_back();
                    throw std::runtime_error("OP_GOI: hàm không tồn tại (id/nameIndex=" + std::to_string(hamIdOrName) + ")");
                }

                // 5. Chạy VM con và Chia sẻ Trạng thái
                VM funcVM(it->second, this->stringPool);

                // Sao chép trạng thái hiện tại (Biến toàn cục và khung gọi)
                funcVM.variables = this->variables;
                funcVM.callStack.clear();
                funcVM.callStack.push_back(callStack.back());
                funcVM.hamBytecodeMap = this->hamBytecodeMap;
                funcVM.functionTableByNameIndex = this->functionTableByNameIndex;

                // Do not pre-size callee locals using operandIndex values gathered
                // from bytecode. These ids may come from a global symbol table rather
                // than a function-local index space, so resizing localsVec to
                // maxLocalId + 1 can cause excessive allocation and can also make
                // variable reads incorrectly resolve as locals instead of falling back
                // to globals. Keep local storage sparse/on-demand and let the normal
                // parameter/initialization instructions populate it.

                // Thực thi hàm
                funcVM.run();

                // 6. Cập nhật Trạng thái về VM mẹ
                // CẬP NHẬT BIẾN TOÀN CỤC TỪ VM CON VỀ VM MẸ
                this->variables = funcVM.variables;

                // Cập nhật giá trị trả về
                if (!funcVM.stack.empty()) {
                    stack.push_back(funcVM.stack.back());
                }

                // Pop caller frame
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
                stack.emplace_back(as_int(a, instr.op, pc) % int_b);
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
                        if (isNumeric(a) && isNumeric(b)) {
                            stack.push_back(numAdd(a, b));
                        } else {
                            stack.emplace_back(sv_to_string(a) + sv_to_string(b));
                        }
                        break;
                    }

                    case OP_TRU: case OP_NHAN: case OP_CHIA: {
                        if (!isNumeric(a) || !isNumeric(b))
                            throw runtime_error_op("Lỗi: toán tử số chỉ áp dụng cho số", instr.op, pc);
                        if (instr.op == OP_TRU)  stack.push_back(numSub(a, b));
                        else if (instr.op == OP_NHAN) stack.push_back(numMul(a, b));
                        else stack.push_back(numDiv(a, b, instr.op, pc));
                        break;
                    }

                    case OP_Logic_VA: case OP_Logic_HOAC: {
                        if (!isNumeric(a) || !isNumeric(b))
                            throw runtime_error_op("Lỗi: toán tử logic chỉ áp dụng cho số", instr.op, pc);
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
                            throw runtime_error_op("Lỗi: không thể so sánh hai kiểu dữ liệu khác nhau", instr.op, pc);
                        }
                        stack.push_back(result);
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
                    throw runtime_error_op("Lỗi: chỉ số chuỗi không hợp lệ", instr.op, pc);
                }
                stack.push_back(stringPool[instr.operandIndex]);
                break;
            }

            case OP_BIEN_SO_FLOAT: {
                if (instr.operandIndex < 0 || instr.operandIndex >= (int)stringPool.size())
                    throw runtime_error_op("Lỗi: chỉ số float không hợp lệ", instr.op, pc);
                try {
                    double d = std::stod(stringPool[instr.operandIndex]);
                    stack.push_back(make_float_value(d));
                } catch (...) {
                    throw runtime_error_op("Lỗi: không thể chuyển '" + stringPool[instr.operandIndex] + "' thành số thực", instr.op, pc);
                }
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
                throw runtime_error_op("BO_QUA: không tìm thấy OP_CAP_NHAT trong vòng lặp", instr.op, pc);
                bo_qua_done:
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

                // Prefer writing to locals if inside a call frame
                // This ensures consistency with OP_TEN_BIEN_GIA_TRI which reads from locals first
                if (!callStack.empty()) {
                    CallFrame &frame = callStack.back();
                    if (frame.localsIndexed) {
                        // Check if this varId is within local scope (was initialized by OP_PARAM/OP_KHOI_TAO)
                        if (varId >= 0 && varId < (int)frame.localsVec.size()) {
                            frame.localsVec[varId] = valueVal;
                            break;
                        }
                        // If not in locals, fall through to global
                    } else {
                        auto itloc = frame.localsMap.find(varId);
                        if (itloc != frame.localsMap.end()) {
                            // Local exists, use it
                            frame.localsMap[varId] = valueVal;
                            break;
                        }
                        // If not in locals, fall through to global
                    }
                }

                // Fallback: store in global variables map
                variables[varId] = valueVal;
                break;
            }

            case OP_JUMP: {
                int jump_address = instr.operand;
                if (jump_address < 0 || jump_address >= (int)bytecode.size()) {
                    throw runtime_error_op("Lỗi: địa chỉ nhảy ngoài phạm vi", instr.op, pc);
                }
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
                    vmLog("Cảnh báo: OP_PARAM argIndex ngoài phạm vi, gán mặc định 0");
                }
                if (frame.localsIndexed) {
                    if (localId >= (int)frame.localsVec.size()) frame.localsVec.resize(localId + 1, make_int_value(0));
                    frame.localsVec[localId] = v;
                } else {
                    frame.localsMap[localId] = v;
                }
                break;
            }
            case OP_CONG_MOT: {
                if (stack.empty()) throw runtime_error_op("OP_CONG_MOT: stack rong", instr.op, pc);
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
                    catch (...) { throw runtime_error_op("OP_CONG_MOT: khong the tang chuoi", instr.op, pc); }
                } else { throw runtime_error_op("OP_CONG_MOT: kieu du lieu khong ho tro", instr.op, pc); }
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
                if (stack.empty()) stack.push_back(make_string_value("lỗi không xác định"));
                StackValue errVal = stack.back(); stack.pop_back();
                if (tryStack.empty()) {
                    // No catch handler: propagate as C++ exception
                    throw std::runtime_error("Lỗi không bắt được: " + sv_to_string(errVal));
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
                if (stack.empty()) throw runtime_error_op("OP_TRU_MOT: stack rong", instr.op, pc);
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
                } else { throw runtime_error_op("OP_TRU_MOT: kieu du lieu khong ho tro", instr.op, pc); }
                break;
            }
            default:
                // If in a skipping switch block, ignore instructions
                if (!switchStack.empty() && switchStack.back().skippingCase) {
                    break;
                }
                throw runtime_error_op("Opcode không xác định", instr.op, pc);
        }
        ++pc;
    }
}
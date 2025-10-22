#include "../include/vm.h"
#include <iostream>
#include <stdexcept>
#include <vector>
#include <stack>
#include <variant>
#include <string>
#include <map> // Thêm thư viện map nếu chưa có

#include "common/Lex_utils.h"
using StackValue = std::variant<int, std::string>;

// ✅ THAY ĐỔI CONSTRUCTOR: Nhận string pool và khởi  tạo thành viên
/**
 *
 * @param code
 * @param pool
 */
VM::VM(const std::vector<Instruction>& code, const std::vector<std::string>& pool)
    : bytecode(code), stringPool(pool), pc(0) {}

bool toBool(const StackValue& value) {
    if (std::holds_alternative<int>(value)) {
        return std::get<int>(value) != 0;
    } else if (std::holds_alternative<std::string>(value)) {
        return !std::get<std::string>(value).empty();
    }
    throw std::runtime_error("Không thể chuyển StackValue sang bool");
}

void VM::run() {
    while (pc < bytecode.size()) {
        const Instruction &instr = bytecode[pc];
        StackValue b = 0, a = 0;

        switch (instr.op) {
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
                // if (stack.empty()) throw std::runtime_error("Lỗi: không đủ giá trị cho lệnh NẾU");
                // int condition = stack.back(); stack.pop_back();
                // if (condition == 0) pc += instr.operand;
                // Biến OP_NEU thành lệnh NOP (No-Operation)
                // Chỉ dùng để đánh dấu bắt đầu của cấu trúc IF/WHILE nếu cần cho debug
                // hoặc cho logic xử lý phạm vi (scope) phức tạp hơn.
                // KHÔNG cần kiểm tra Stack hay thay đổi pc.

                break;
            }
            case OP_MODULO: {
                if (stack.size() < 2)
                    throw std::runtime_error("Lỗi: thiếu toán hạng cho MODULO");

                b = stack.back(); stack.pop_back();
                a = stack.back(); stack.pop_back();

                if (!std::holds_alternative<int>(a) || !std::holds_alternative<int>(b))
                    throw std::runtime_error("Lỗi: MODULO chỉ áp dụng cho số nguyên");

                int int_b = std::get<int>(b);
                if (int_b == 0)
                    throw std::runtime_error("Lỗi: chia dư cho 0");

                int int_a = std::get<int>(a);
                stack.emplace_back(int_a % int_b);
                break;
            }

            case OP_CONG: case OP_TRU: case OP_NHAN: case OP_CHIA:
            case OP_Logic_VA: case OP_Logic_HOAC:
            case OP_SO_SANH_BANG: case OP_KHAC_BANG:
            case OP_LON_HON: case OP_NHO_HON:
            case OP_LON_HON_HOAC_BANG: case OP_NHO_HON_HOAC_BANG: {
                if (stack.size() < 2)
                    throw std::runtime_error("Lỗi: không đủ toán hạng cho toán tử " + std::to_string(instr.op));

                StackValue b = stack.back(); stack.pop_back();
                StackValue a = stack.back(); stack.pop_back();

                switch (instr.op) {
                    case OP_CONG: {
                        if (std::holds_alternative<int>(a) && std::holds_alternative<int>(b)) {
                            stack.push_back(std::get<int>(a) + std::get<int>(b));
                        } else {
                            std::string sa = std::holds_alternative<int>(a) ? std::to_string(std::get<int>(a)) : std::get<std::string>(a);
                            std::string sb = std::holds_alternative<int>(b) ? std::to_string(std::get<int>(b)) : std::get<std::string>(b);
                            stack.emplace_back(sa + sb);
                        }
                        break;
                    }

                    case OP_TRU: case OP_NHAN: case OP_CHIA: {
                        if (!std::holds_alternative<int>(a) || !std::holds_alternative<int>(b))
                            throw std::runtime_error("Lỗi: toán tử số chỉ áp dụng cho số nguyên");

                        int ia = std::get<int>(a);
                        int ib = std::get<int>(b);

                        if (instr.op == OP_TRU) stack.push_back(ia - ib);
                        else if (instr.op == OP_NHAN) stack.push_back(ia * ib);
                        else {
                            if (ib == 0) throw std::runtime_error("Lỗi: chia cho 0");
                            stack.push_back(ia / ib);
                        }
                        break;
                    }

                    case OP_Logic_VA: case OP_Logic_HOAC: {
                        if (!std::holds_alternative<int>(a) || !std::holds_alternative<int>(b))
                            throw std::runtime_error("Lỗi: toán tử logic chỉ áp dụng cho số nguyên");

                        int ia = std::get<int>(a);
                        int ib = std::get<int>(b);
                        if (instr.op == OP_Logic_VA) stack.push_back((ia && ib) ? 1 : 0);
                        else stack.push_back((ia || ib) ? 1 : 0);
                        break;
                    }

                    case OP_SO_SANH_BANG: case OP_KHAC_BANG:
                    case OP_LON_HON: case OP_NHO_HON:
                    case OP_LON_HON_HOAC_BANG: case OP_NHO_HON_HOAC_BANG: {
                        if (a.index() != b.index())
                            throw std::runtime_error("Lỗi: không thể so sánh hai kiểu dữ liệu khác nhau");

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
                            }
                        }
                        break;
                    }

                    default:
                        throw std::runtime_error("Toán tử không xác định: " + std::to_string(instr.op));
                }
                break;
            }

            case OP_KHONG: {
                if (stack.empty())
                    throw std::runtime_error("Lỗi: không đủ toán hạng cho toán tử phủ định");

                StackValue a = stack.back(); stack.pop_back();

                if (!std::holds_alternative<int>(a))
                    throw std::runtime_error("Lỗi: toán tử phủ định chỉ áp dụng cho số nguyên");

                int ia = std::get<int>(a);
                stack.push_back((!ia) ? 1 : 0);
                break;
            }
            case OP_KHOI_TAO: {
                int varId = instr.operandIndex;
                if (variables.count(varId) == 0) {
                    variables[varId] = 0;
                    // std::cout << "[VM] Khởi tạo biến ID " << varId << " với giá trị 0\n";
                }
                break;
            }
            case OP_DIEU_KIEN:
                break;
            case OP_LAP:
                break;
            case OP_CAP_NHAT: // Nếu bạn giữ OP_CAP_NHAT làm nhãn
                // Đây là các nhãn (metadata), không phải lệnh thực thi
                break; // Chuyển sang lệnh tiếp theo (biểu thức)
            case OP_CHUOI: {
                if (instr.operandIndex >= stringPool.size()) {
                    throw std::runtime_error("Lỗi: chỉ số chuỗi không hợp lệ");
                }
                stack.push_back(stringPool[instr.operandIndex]);
                break;
            }
            case OP_IN: {
                if (stack.empty()) throw std::runtime_error("Lỗi: stack rỗng khi IN");
                StackValue value = stack.back(); stack.pop_back();

                std::cout << "[IN] ";
                if (std::holds_alternative<int>(value)) {
                    std::cout << std::get<int>(value);
                } else if (std::holds_alternative<std::string>(value)) {
                    std::cout << std::get<std::string>(value);
                }
                std::cout << std::endl;
                break;
            }
            case OP_CHON: {
                if (stack.empty()) throw std::runtime_error("CHON: Stack rỗng");
                SwitchFrame frame;
                frame.switchValue = stack.back();
                stack.pop_back();
                frame.skippingCase = true;
                frame.blockDepthAtStart = blockStack.size();
                switchStack.push_back(frame);
                break;
            }

            case OP_CA: {
                if (switchStack.empty() || !switchStack.back().switchValue.has_value()) {
                    throw std::runtime_error("CA: Không nằm trong khối CHON");
                }
                auto& ctx = switchStack.back();

                bool match = false;
                // 1) case là chuỗi
                if (instr.operandIndex >= 0) {
                    std::string caseStr = stringPool.at(instr.operandIndex);
                    if (std::holds_alternative<std::string>(*ctx.switchValue)) {
                        match = (std::get<std::string>(*ctx.switchValue) == caseStr);
                    } else if (std::holds_alternative<int>(*ctx.switchValue)) {
                        match = (std::to_string(std::get<int>(*ctx.switchValue)) == caseStr);
                    }
                }
                // 2) case là số
                else if (instr.operandIndex == -1) {
                    if (std::holds_alternative<int>(*ctx.switchValue)) {
                        match = (std::get<int>(*ctx.switchValue) == instr.operand);
                    } else if (std::holds_alternative<std::string>(*ctx.switchValue)) {
                        try {
                            int sv = std::stoi(std::get<std::string>(*ctx.switchValue));
                            match = (sv == instr.operand);
                        } catch (...) { match = false; }
                    }
                }
                // 3) case là biến
                else if (instr.operandIndex == -2) {
                    int varId = instr.operand;
                    int varValue = vietvm::compiler::getVarValueInt(varId);
                    if (std::holds_alternative<int>(*ctx.switchValue)) {
                        match = (std::get<int>(*ctx.switchValue) == varValue);
                    } else if (std::holds_alternative<std::string>(*ctx.switchValue)) {
                        try {
                            int sv = std::stoi(std::get<std::string>(*ctx.switchValue));
                            match = (sv == varValue);
                        } catch (...) { match = false; }
                    }
                } else {
                    throw std::runtime_error("CA: định dạng operand không hợp lệ");
                }

                // cập nhật skippingCase trong context của switch-case
                ctx.skippingCase = !match;
                break;
            }
            case OP_MAC_DINH: {
                if (switchStack.empty()) throw std::runtime_error("MAC_DINH: Không nằm trong khối CHON");
                auto& ctx = switchStack.back();
                ctx.skippingCase = false;
                break;
            }

            case OP_THOAT: {
                if (switchStack.empty()) throw std::runtime_error("THOAT: Không nằm trong khối CHON");
                // Chỉ nhảy tới cuối block case hiện tại
                int localBlockDepth = blockStack.size();
                while (pc < bytecode.size()) {
                    if (bytecode[pc].op == OP_MO_KHOI) ++localBlockDepth;
                    if (bytecode[pc].op == OP_DONG_KHOI) {
                        --localBlockDepth;
                        if (localBlockDepth < blockStack.size()) {
                            ++pc;
                            break;
                        }
                    }
                    ++pc;
                }
                skippingCase = false;
                switchValue.reset();
                // KHÔNG pop switchStack ở đây!
                break;
            }



            // Thay thế phần xử lý đọc giá trị biến
            case OP_TEN_BIEN_GIA_TRI: {
                int varId = instr.operandIndex;
                // Nếu biến chưa có, khởi tạo mặc định = 0 (tránh crash)
                if (variables.count(varId) == 0) {
                    std::cerr << "Cảnh báo: biến ID " << varId << " chưa được khởi tạo. Mặc định = 0.\n";
                    variables[varId] = 0;
                }
                stack.push_back(variables[varId]);
                break;
            }

            // Thay thế phần xử lý GÁN: lưu ý thứ tự pop
            case OP_GAN: {
                if (stack.size() < 2)
                    throw std::runtime_error("Không đủ toán hạng để GÁN");

                StackValue varIdVal = stack.back(); stack.pop_back();
                std::vector<std::variant<int, std::string>>::value_type valueVal = stack.back(); stack.pop_back();

                if (!std::holds_alternative<int>(varIdVal))
                    throw std::runtime_error("Lỗi: ID biến phải là số nguyên");

                int varId = std::get<int>(varIdVal);

                // Cho phép gán cả số và chuỗi
                variables[varId] = valueVal;
                break;
            }

            case OP_JUMP:
            {
                // 1. Đọc Operand (Vị trí nhảy)
                // Giả định địa chỉ nhảy được lưu trong instr.operand
                int jump_address = instr.operand;
                // 2. Thực hiện Nhảy
                pc = jump_address;

                // 3. Bỏ qua pc++ ở cuối vòng lặp
                // Quan trọng: dùng 'continue' để bắt đầu vòng lặp VM mới ngay lập tức
                // từ địa chỉ 'jump_address' mà không tăng pc thêm 1.
                continue;
            }
            case OP_JUMP_IF_FALSE: {
                int jump_address = instr.operand;

                if (stack.empty())
                    throw std::runtime_error("Lỗi: Stack rỗng khi thực thi OP_JUMP_IF_FALSE");

                StackValue condition = stack.back(); stack.pop_back();

                if (!std::holds_alternative<int>(condition))
                    throw std::runtime_error("Lỗi: điều kiện nhảy phải là số nguyên");

                int condition_value = std::get<int>(condition);

                if (condition_value == 0) {
                    pc = jump_address;
                    continue; // bỏ qua pc++ ở cuối vòng lặp
                }

                break;
            }

            case OP_DUNG_CHUONG_TRINH:
                return;
            case OP_MO_KHOI:
                blockStack.push(pc);
                ++blockDepth;
                break;
            case OP_DONG_KHOI:
                if (blockStack.empty()) throw std::runtime_error("Lỗi: không có khối mở");
                blockStack.pop();

                // Nếu kết thúc khối CHON, pop switchStack
                if (!switchStack.empty() && blockStack.size() == switchStack.back().blockDepthAtStart - 1) {
                    switchStack.pop_back();
                    skippingCase = false;
                    switchValue.reset();
                }
                break;

            case OP_DONG_LENH: case OP_MO_NGOAC: case OP_DONG_NGOAC:
                break;
            case OP_PHU_DINH: {
                if (stack.empty()) {
                    throw std::runtime_error("Thiếu toán hạng cho toán tử phủ định !");
                }
                StackValue operand = stack.back(); stack.pop_back();

                // Giả sử kiểu Value là bool hoặc có thể chuyển sang bool
                bool result = !toBool(operand);
                stack.push_back(result); // lưu lại dưới dạng int: 0 hoặc 1
                break;
            }
            default:
                if (inSwitchBlock && skippingCase) {
                    // Bỏ qua lệnh trong ca không khớp
                    break;
                }
                throw std::runtime_error("Opcode không xác định: " + std::to_string(instr.op));
        }
        pc++;
    }
}
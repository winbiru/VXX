#include "../include/vm.h"
#include <iostream>
#include <stdexcept>
#include <vector>
#include <stack>

VM::VM(const std::vector<Instruction>& code) : bytecode(code), pc(0) {}

void VM::run() {
    while (pc < bytecode.size()) {
        const Instruction &instr = bytecode[pc];
        switch (instr.op) {
            case OP_BIEN_SO: {
                int val = instr.operand;
                stack.push_back(val);
                // std::cout << "[DEBUG BIEN_SO] Push hằng số: " << val << std::endl;
                break;
            }

            case OP_TEN_BIEN_ID: {
                int varId = instr.operandIndex;
                stack.push_back(varId);
                // std::cout << "[DEBUG TEN_BIEN] Push biến ID: " << varId << std::endl;
                break;
            }

            case OP_NEU: {
                if (stack.empty()) throw std::runtime_error("Lỗi: không đủ giá trị cho lệnh NẾU");
                int condition = stack.back(); stack.pop_back();
                if (condition == 0) pc += instr.operand;
                break;
            }

            case OP_CONG: case OP_TRU: case OP_NHAN: case OP_CHIA:
            case OP_Logic_VA: case OP_Logic_HOAC: case OP_KHONG:
            case OP_SO_SANH_BANG: case OP_KHAC_BANG:
            case OP_LON_HON: case OP_NHO_HON:
            case OP_LON_HON_HOAC_BANG: case OP_NHO_HON_HOAC_BANG: {
                if ((instr.op != OP_KHONG && stack.size() < 2) || (instr.op == OP_KHONG && stack.empty()))
                    throw std::runtime_error("Lỗi: không đủ toán hạng cho toán tử " + std::to_string(instr.op));
                int b = 0, a = 0;
                if (instr.op != OP_KHONG) {
                    b = stack.back(); stack.pop_back();
                    a = stack.back(); stack.pop_back();
                } else {
                    a = stack.back(); stack.pop_back();
                }
                switch (instr.op) {
                    case OP_CONG: stack.push_back(a + b); break;
                    case OP_TRU: stack.push_back(a - b); break;
                    case OP_NHAN: stack.push_back(a * b); break;
                    case OP_CHIA:
                        if (b == 0) throw std::runtime_error("Lỗi: chia cho 0");
                        stack.push_back(a / b);
                        break;
                    case OP_Logic_VA: stack.push_back((a && b) ? 1 : 0); break;
                    case OP_Logic_HOAC: stack.push_back((a || b) ? 1 : 0); break;
                    case OP_KHONG: stack.push_back((!a) ? 1 : 0); break;
                    case OP_SO_SANH_BANG: stack.push_back((a == b) ? 1 : 0); break;
                    case OP_KHAC_BANG: stack.push_back((a != b) ? 1 : 0); break;
                    case OP_LON_HON: stack.push_back((a > b) ? 1 : 0); break;
                    case OP_NHO_HON: stack.push_back((a < b) ? 1 : 0); break;
                    case OP_LON_HON_HOAC_BANG: stack.push_back((a >= b) ? 1 : 0); break;
                    case OP_NHO_HON_HOAC_BANG: stack.push_back((a <= b) ? 1 : 0); break;
                    default: ;
                }
                break;
            }

            case OP_KHOI_TAO: {
                if (stack.empty()) throw std::runtime_error("Stack rỗng khi KHỞI_TẠO");
                int value = stack.back(); stack.pop_back();
                variables[instr.operandIndex] = value;
                break;
            }

            case OP_DIEU_KIEN: {
                vi_tri_dieu_kien = pc;
                if (stack.empty()) throw std::runtime_error("Không đủ toán hạng cho ĐIỀU_KIỆN");
                int cond = stack.back(); stack.pop_back();
                if (cond == 0) {
                    int depth = 0;
                    while (++pc < bytecode.size()) {
                        if (bytecode[pc].op == OP_MO_KHOI) depth++;
                        else if (bytecode[pc].op == OP_DONG_KHOI) {
                            if (depth == 0) break;
                            depth--;
                        }
                    }
                }
                break;
            }

            case OP_LAP: {
                // Kiểm tra bytecode có đủ phần ()
                if (pc + 1 >= bytecode.size() || bytecode[pc + 1].op != OP_MO_NGOAC)
                    throw std::runtime_error("Thiếu '(' sau LẶP, gặp: " + std::to_string(bytecode[pc + 1].op));

                // Tìm tới vị trí )
                int parenStart = pc + 1;
                int parenEnd = parenStart + 1;
                int openParens = 1;
                while (parenEnd < bytecode.size()) {
                    if (bytecode[parenEnd].op == OP_MO_NGOAC) openParens++;
                    else if (bytecode[parenEnd].op == OP_DONG_NGOAC) openParens--;
                    if (openParens == 0) break;
                    parenEnd++;
                }
                if (openParens != 0)
                    throw std::runtime_error("Không tìm thấy ')' cho LẶP");

                // Tìm vị trí các nhãn khởi tạo, điều kiện, cập nhật
                int khoiTaoPos = -1, dieuKienPos = -1, capNhatPos = -1;
                for (int i = parenStart + 1; i < parenEnd; ++i) {
                    if (bytecode[i].op == OP_KHOI_TAO) khoiTaoPos = i;
                    else if (bytecode[i].op == OP_DIEU_KIEN) dieuKienPos = i;
                    else if (bytecode[i].op == OP_CAP_NHAT) capNhatPos = i;
                }
                if (khoiTaoPos == -1 || dieuKienPos == -1 || capNhatPos == -1)
                    throw std::runtime_error("Thiếu KHỞI_TẠO, ĐIỀU_KIỆN hoặc CẬP_NHẬT trong LẶP");

                // Tách các đoạn mã con
                int initStart = khoiTaoPos + 1, initEnd = dieuKienPos;
                int condStart = dieuKienPos + 1, condEnd = capNhatPos;
                int updateStart = capNhatPos + 1, updateEnd = parenEnd;

                // Tìm khối thân { ... }
                if (parenEnd + 1 >= bytecode.size() || bytecode[parenEnd + 1].op != OP_MO_KHOI)
                    throw std::runtime_error("Thiếu '{' sau phần điều kiện LẶP");

                int bodyStart = parenEnd + 1, bodyEnd = bodyStart + 1;
                int openBraces = 1;
                while (bodyEnd < bytecode.size()) {
                    if (bytecode[bodyEnd].op == OP_MO_KHOI) openBraces++;
                    else if (bytecode[bodyEnd].op == OP_DONG_KHOI) openBraces--;
                    if (openBraces == 0) break;
                    bodyEnd++;
                }
                if (openBraces != 0)
                    throw std::runtime_error("Không tìm thấy '}' cho thân LẶP");

                // Lấy các đoạn code con
                const std::vector<Instruction> initCode(bytecode.begin() + initStart, bytecode.begin() + initEnd);
                const std::vector<Instruction> condCode(bytecode.begin() + condStart, bytecode.begin() + condEnd);
                const std::vector<Instruction> updateCode(bytecode.begin() + updateStart, bytecode.begin() + updateEnd);
                const std::vector<Instruction> bodyCode(bytecode.begin() + bodyStart + 1, bytecode.begin() + bodyEnd);

                // Chạy khởi tạo
                VM initVM(initCode); initVM.variables = variables; initVM.run(); variables = initVM.variables;

                // Bắt đầu vòng lặp
                while (true) {
                    // Kiểm tra điều kiện
                    VM condVM(condCode); condVM.variables = variables; condVM.run(); variables = condVM.variables;
                    if (condVM.stack.empty()) break;
                    int cond = condVM.stack.back(); condVM.stack.pop_back();
                    if (!cond) break;

                    // Thân
                    VM bodyVM(bodyCode); bodyVM.variables = variables; bodyVM.run(); variables = bodyVM.variables;

                    // Cập nhật
                    VM updateVM(updateCode); updateVM.variables = variables; updateVM.run(); variables = updateVM.variables;

                    // // std::cout << "[DEBUG UPDATE] Variables after update:";
                    // for (const auto& [k, val] : variables)
                    //     std::cout << " Biến " << k << "=" << val << ";";
                    // std::cout << std::endl;
                }

                // Nhảy qua phần thân đã xử lý
                pc = bodyEnd + 1;
                continue;
            }


            case OP_IN: {
                if (stack.empty()) throw std::runtime_error("Lỗi: stack rỗng khi IN");
                int value = stack.back(); stack.pop_back();
                std::cout << "[IN] " << value << std::endl;
                break;
            }


            case OP_TEN_BIEN_GIA_TRI: {
                int id = instr.operandIndex;
                if (variables.find(id) == variables.end()) {
                    std::cerr << "Cảnh báo: biến ID " << id << " chưa được khởi tạo. Mặc định = 0.\n";
                    variables[id] = 0;
                }
                stack.push_back(variables[id]);
                break;
            }

            case OP_GAN: {
                // std::cout << "[DEBUG GÁN] Stack trước khi gán:";
                for (auto v : stack)
                    // std::cout << " " << v;
                // std::cout << std::endl;

                if (stack.size() < 2) throw std::runtime_error("Không đủ toán hạng để GÁN");

                int varId = stack.back(); stack.pop_back();
                int value = stack.back(); stack.pop_back();

                variables[varId] = value;
                // std::cout << "[DEBUG GÁN] Biến " << varId << " = " << value << std::endl;

                // // std::cout << "[DEBUG VARIABLES] ";
                // for (const auto& [k, val] : variables)
                //     std::cout << "Biến " << k << "=" << val << "; ";
                // std::cout << std::endl;

                break;
            }

            case OP_DUNG_CHUONG_TRINH:
                return;
            case OP_MO_KHOI:
                blockStack.push(pc);
                break;
            case OP_DONG_KHOI:
                if (blockStack.empty()) throw std::runtime_error("Lỗi: không có khối mở");
                blockStack.pop();
                break;
            case OP_DONG_LENH: case OP_MO_NGOAC: case OP_DONG_NGOAC:
                break;

            default:
                throw std::runtime_error("Opcode không xác định: " + std::to_string(instr.op));
        }
        pc++;
    }
}
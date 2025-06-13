// vm.cpp
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
                stack.push_back(instr.operand);
                break;
            }
            case OP_NEU: {
                if (stack.empty()) throw std::runtime_error("Lỗi: không đủ giá trị cho lệnh NẾu");
                int condition = stack.back(); stack.pop_back();
                if (condition == 0) pc += instr.operand;
                break;
            }
            case OP_CONG: case OP_TRU: case OP_NHAN: case OP_CHIA:
            case OP_VA: case OP_HOAC: case OP_KHONG:
            case OP_SO_SANH_BANG: case OP_KHAC_BANG:
            case OP_LON_HON: case OP_NHO_HON:
            case OP_LON_HON_HOAC_BANG: case OP_NHO_HON_HOAC_BANG: {
                if ((instr.op != OP_KHONG && stack.size() < 2) || (instr.op == OP_KHONG && stack.empty()))
                    throw std::runtime_error("Lỗi: không đủ toán hạng");
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
                    case OP_CHIA: if (b == 0) throw std::runtime_error("Lỗi: chia cho 0"); stack.push_back(a / b); break;
                    case OP_VA: stack.push_back((a && b) ? 1 : 0); break;
                    case OP_HOAC: stack.push_back((a || b) ? 1 : 0); break;
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
                // Gán giá trị từ stack vào biến mới
                if (stack.empty()) throw std::runtime_error("Stack rỗng khi KHỞI_TẠO");
                int value = stack.back(); stack.pop_back();

                variables[instr.operandIndex] = value;
                break;
            }
            case OP_DIEU_KIEN: {
                if (stack.empty()) throw std::runtime_error("Không đủ toán hạng cho ĐIỀU_KIỆN");
                int cond = stack.back(); stack.pop_back();

                if (cond == 0) {
                    // Nhảy đến sau khối lặp (bỏ qua khối giữa MỞ_KHỐI và ĐÓNG_KHỐI)
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

            case OP_KIEM_TRA_SAU: {
                if (stack.empty()) throw std::runtime_error("Stack rỗng khi KIỂM_TRA_SAU");
                int cond = stack.back(); stack.pop_back();
                if (!cond) {
                    // Nhảy đến địa chỉ lưu trong operand
                    pc = instr.operand;
                    continue; // bỏ qua tăng IP ở cuối vòng lặp
                }
                break;
            }
            case OP_CAP_NHAT: {
                // Tăng biến i lên 1
                variables[instr.operandIndex] += instr.operand;
                break;
            }

            case OP_LAP: {
                // Bắt đầu kiểm tra dấu '(' sau LẶP
                if (pc + 1 >= bytecode.size() || bytecode[pc + 1].op != OP_MO_NGOAC)
                    throw std::runtime_error("Thiếu '(' sau LẶP, gặp: " + std::to_string(bytecode[pc + 1].op));
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
                    throw std::runtime_error("Không tìm thấy ')' cho phần điều kiện LẶP");

                // Giả sử trong () là: KHỞI_TẠO ; ĐIỀU_KIỆN ; CẬP_NHẬT
                int semicolon1 = -1, semicolon2 = -1;
                for (int i = parenStart + 1; i < parenEnd; ++i) {
                    if (bytecode[i].op == OP_DONG_LENH) {
                        if (semicolon1 == -1) semicolon1 = i;
                        else {
                            semicolon2 = i;
                            break;
                        }
                    }
                }
                if (semicolon1 == -1 || semicolon2 == -1)
                    throw std::runtime_error("Thiếu dấu ';' trong phần đầu của LẶP");

                int initStart = parenStart + 1;
                int initEnd = semicolon1;
                int condStart = semicolon1 + 1;
                int condEnd = semicolon2;
                int updateStart = semicolon2 + 1;
                int updateEnd = parenEnd;

                // Sau ')' phải là '{' => OP_MO_KHOI
                if (parenEnd + 1 >= bytecode.size() || bytecode[parenEnd + 1].op != OP_MO_KHOI)
                    throw std::runtime_error("Thiếu '{' sau phần điều kiện của LẶP");

                int bodyStart = parenEnd + 1;
                int bodyEnd = bodyStart + 1;
                int openBraces = 1;
                while (bodyEnd < bytecode.size()) {
                    if (bytecode[bodyEnd].op == OP_MO_KHOI) openBraces++;
                    else if (bytecode[bodyEnd].op == OP_DONG_KHOI) openBraces--;
                    if (openBraces == 0) break;
                    bodyEnd++;
                }
                if (openBraces != 0)
                    throw std::runtime_error("Không tìm thấy '}' cho thân LẶP");

                // Thực thi phần khởi tạo
                VM initVM(std::vector<Instruction>(bytecode.begin() + initStart, bytecode.begin() + initEnd));
                initVM.variables = this->variables;
                initVM.run();
                this->variables = initVM.variables;

                while (true) {
                    // Thực thi điều kiện
                    VM condVM(std::vector<Instruction>(bytecode.begin() + condStart, bytecode.begin() + condEnd));
                    condVM.variables = this->variables;
                    condVM.run();
                    if (condVM.stack.empty() || condVM.stack.back() == 0) break;

                    // Thực thi thân vòng lặp
                    VM bodyVM(std::vector<Instruction>(bytecode.begin() + bodyStart + 1, bytecode.begin() + bodyEnd));
                    bodyVM.variables = this->variables;
                    bodyVM.run();
                    this->variables = bodyVM.variables;

                    // Thực thi cập nhật
                    VM updateVM(std::vector<Instruction>(bytecode.begin() + updateStart, bytecode.begin() + updateEnd));
                    updateVM.variables = this->variables;
                    updateVM.run();
                    this->variables = updateVM.variables;
                }

                pc = bodyEnd + 1;
                continue;
            }
            case OP_IN: {
                if (stack.empty()) throw std::runtime_error("Lỗi: stack rỗng, không có gì để in");
                std::cout << stack.back() << std::endl; stack.pop_back();
                break;
            }
            case OP_TEN_BIEN: {
                if (variables.find(instr.operand) == variables.end()) {
                    std::cerr << "Cảnh báo: biến ID " << instr.operand << " chưa được khởi tạo. Mặc định = 0.\n";
                    variables[instr.operand] = 0;
                }
                stack.push_back(variables[instr.operand]); break;
            }
            case OP_GAN: {
                if (stack.empty()) throw std::runtime_error("Stack rỗng khi GÁN");
                int value = stack.back(); stack.pop_back();
                int varID = instr.operandIndex; // Lấy ID biến từ operand của lệnh
                variables[varID] = value;
                // Nếu bạn có map hoặc vector đánh dấu biến đã khởi tạo, update ở đây
                // initialized[varID] = true; // nếu có
                break;
            }
            case OP_DUNG_CHUONG_TRINH: return;
            case OP_MO_KHOI: blockStack.push(pc); break;
            case OP_DONG_KHOI: if (blockStack.empty()) throw std::runtime_error("Lỗi: không có khối mở"); blockStack.pop(); break;
            case OP_DONG_LENH: case OP_MO_NGOAC: case OP_DONG_NGOAC: break;
            default: throw std::runtime_error("Opcode không xác định " + std::to_string(instr.op));
        }
        pc++;
    }
}

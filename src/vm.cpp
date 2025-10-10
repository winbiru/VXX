#include "../include/vm.h"
#include <iostream>
#include <stdexcept>
#include <vector>
#include <stack>

VM::VM(const std::vector<Instruction>& code) : bytecode(code), pc(0) {}

void VM::run() {
    while (pc < bytecode.size()) {
        const Instruction &instr = bytecode[pc];
        int b = 0, a = 0;
        switch (instr.op) {
            case OP_BIEN_SO: {
                int val = instr.operand;
                stack.push_back(val);
                break;
            }

            case OP_TEN_BIEN_ID: {
                int varId = instr.operandIndex;
                stack.push_back(varId);
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

            case OP_CONG: case OP_TRU: case OP_NHAN: case OP_CHIA:
            case OP_MODULO:
                if (stack.size() < 2)
                    throw std::runtime_error("Lỗi: thiếu toán hạng cho MODULO");
                b = stack.back(); stack.pop_back();
                a = stack.back(); stack.pop_back();
                if (b == 0)
                    throw std::runtime_error("Lỗi: chia dư cho 0");
                stack.push_back(a % b);
                break;
            case OP_Logic_VA: case OP_Logic_HOAC: case OP_KHONG:
            case OP_SO_SANH_BANG: case OP_KHAC_BANG:
            case OP_LON_HON: case OP_NHO_HON:
            case OP_LON_HON_HOAC_BANG: case OP_NHO_HON_HOAC_BANG: {
                if ((instr.op != OP_KHONG && stack.size() < 2) || (instr.op == OP_KHONG && stack.empty()))
                    throw std::runtime_error("Lỗi: không đủ toán hạng cho toán tử " + std::to_string(instr.op));
                if (instr.op != OP_KHONG) {
                    b = stack.back(); stack.pop_back();
                    a = stack.back(); stack.pop_back();
                } else {
                    a = stack.back(); stack.pop_back();
                }
                switch (instr.op) {
                    case OP_CONG: stack.push_back(a + b);
                        std::cout << "[DEBUG CỘNG] " << a << " + " << b << " = " << (a + b) << std::endl;
                        break;
                    case OP_TRU: stack.push_back(a - b); break;
                    case OP_NHAN: stack.push_back(a * b); break;
                    case OP_CHIA:
                        if (b == 0) throw std::runtime_error("Lỗi: chia cho 0");
                        stack.push_back(a / b);
                        break;
                    case OP_MODULO:
                        if (b == 0) throw std::runtime_error("Lỗi: chia dư cho 0");
                        stack.push_back(a % b);
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
            case OP_KHOI_TAO:
            case OP_DIEU_KIEN:
            case OP_LAP:
            case OP_CAP_NHAT: // Nếu bạn giữ OP_CAP_NHAT làm nhãn
                // Đây là các nhãn (metadata), không phải lệnh thực thi
                break; // Chuyển sang lệnh tiếp theo (biểu thức)

            case OP_IN: {
                if (stack.empty()) throw std::runtime_error("Lỗi: stack rỗng khi IN");
                int value = stack.back(); stack.pop_back();
                std::cout << "[IN] " << value << std::endl;
                break;
            }


            case OP_TEN_BIEN_GIA_TRI: {
                int varId = instr.operandIndex;
                if (variables.count(varId) == 0)
                    throw std::runtime_error("Lỗi: biến chưa được khởi tạo");
                stack.push_back(variables[varId]);
                break;
            }

            case OP_GAN: {
                // std::cout << "[DEBUG GÁN] Stack trước khi gán:";
                if (stack.size() < 2) throw std::runtime_error("Không đủ toán hạng để GÁN");

                int value = stack.back(); stack.pop_back();   // rồi lấy giá trị
                int varId = stack.back(); stack.pop_back();   // lấy ID biến trước

                variables[varId] = value;
                // std::cout << "[DEBUG GÁN] Biến " << varId << " = " << value << std::endl;
                //
                // for (const auto& [k, val] : variables)
                //     std::cout << "Biến " << k << "=" << val << "; ";
                // std::cout << std::endl;
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
            case OP_JUMP_IF_FALSE:
                {
                // 1. Đọc Operand (Vị trí nhảy)
                // Sử dụng instr.operand1 (hoặc instr.operand nếu bạn dùng nó cho địa chỉ)
                // Dựa trên cách bạn dùng instruction trong OP_NEU, tôi giả định dùng operand:
                int jump_address = instr.operand;

                // 2. Kiểm tra Stack và Lấy giá trị điều kiện
                if (stack.empty())
                    throw std::runtime_error("Lỗi: Stack rỗng khi thực thi OP_JUMP_IF_FALSE");

                int condition_value = stack.back();
                stack.pop_back();

                // 3. Kiểm tra giá trị (logic isFalse)
                // Trong VM này, 0 là FALSE, bất kỳ thứ gì khác là TRUE.
                if (condition_value == 0) {
                    // 4. Thực hiện Nhảy
                    // Cập nhật con trỏ lệnh (pc) đến địa chỉ mới.
                    pc = jump_address;

                    // Dùng 'continue' để bỏ qua pc++ ở cuối vòng lặp while,
                    // vì pc đã được đặt đến lệnh đích.
                    continue;
                }
                // Nếu điều kiện TRUE, VM sẽ tiếp tục lệnh tiếp theo (pc++ ở cuối vòng lặp while)

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
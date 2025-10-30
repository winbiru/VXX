#include "../include/vm.h"
#include <iostream>
#include <stdexcept>
#include <vector>
#include <stack>
#include <variant>
#include <string>
#include <map> // Thêm thư viện map nếu chưa có

#include "../include/common/Lex_utils.h"
using StackValue = std::variant<int, std::string>;
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
        std::unordered_map<std::string,int> functionTable;
        for (size_t i = 0; i < bytecode.size(); ++i) {
            const Instruction &instr = bytecode[i];
            if (instr.op == OP_HAM) {
                if (instr.operandIndex >= 0 && instr.operandIndex < (int)stringPool.size()) {
                    std::string fname = stringPool[instr.operandIndex];
                    functionTable[fname] = static_cast<int>(i + 1);
                }
            }
        }
        switch (instr.op) {
            case OP_HAM: {
                int hamIndex = instr.operand;
                auto it = hamBytecodeMap.find(hamIndex);
                if (it != hamBytecodeMap.end()) {
                    VM hamVM;
                    hamVM.bytecode = it->second;
                    hamVM.run(); // chạy hàm như một chương trình con
                }

                break;
            }
            case OP_GOI: {
                int hamIndex = instr.operand;
                auto it = hamBytecodeMap.find(hamIndex);
                if (it != hamBytecodeMap.end()) {
                    VM hamVM;
                    hamVM.bytecode = it->second;
                    hamVM.stringPool = this->stringPool; // kế thừa bảng chuỗi nếu cần
                    hamVM.run();
                }
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
                if (!switchStack.empty() && switchStack.back().skippingCase) {
                    break; // bỏ qua in
                }

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
                frame.skippingCase = true;      // mặc định bỏ qua
                frame.caseMatched = false;      // chưa có case nào match
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
                // Cập nhật skippingCase dựa trên caseMatched và match
                if (!ctx.caseMatched && match) {
                    ctx.skippingCase = false;   // thực hiện case
                    ctx.caseMatched = true;     // đánh dấu đã match
                } else {
                    ctx.skippingCase = true;    // bỏ qua case này
                }

                break;
            }
            case OP_MAC_DINH: {
                if (switchStack.empty()) throw std::runtime_error("MAC_DINH: Không nằm trong khối CHON");
                auto& ctx = switchStack.back();
                // Default chỉ thực hiện khi chưa match case nào
                if (!ctx.caseMatched) {
                    ctx.skippingCase = false;
                    ctx.caseMatched = true;
                } else {
                    ctx.skippingCase = true;
                }
                break;
            }

            case OP_THOAT: {
                if (switchStack.empty())
                    throw std::runtime_error("THOAT: Không nằm trong khối CHON");

                auto& ctx = switchStack.back();
                // Đánh dấu case hiện tại đã thoát, sẽ bỏ qua các case còn lại
                ctx.skippingCase = true;

                // Tăng pc cho đến hết block CHON (OP_DONG_KHOI)
                while (pc < bytecode.size()) {
                    if (bytecode[pc].op == OP_DONG_KHOI) {
                        ++pc;
                        break;
                    }
                    ++pc;
                }
                continue; // bỏ qua pc++ ở cuối vòng lặp
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

                // Không pop ngay — đọc top hai phần tử để quyết định thứ tự an toàn
                StackValue top = stack.back();
                StackValue second = stack[stack.size() - 2];

                StackValue varIdVal;
                StackValue valueVal;

                // Nếu một trong hai là string thì chắc chắn đó là value
                if (std::holds_alternative<std::string>(top) && std::holds_alternative<int>(second)) {
                    varIdVal = second;
                    valueVal = top;
                } else if (std::holds_alternative<std::string>(second) && std::holds_alternative<int>(top)) {
                    varIdVal = top;
                    valueVal = second;
                } else {
                    // Trường hợp cả hai đều int hoặc cả hai đều int/string: theo chuẩn compiler hiện tại
                    // compiler push value trước, sau đó push varId => top = varId, second = value.
                    // Do đó ưu tiên coi top là varId trong trường hợp mơ hồ.
                    varIdVal = top;
                    valueVal = second;
                }

                // Bây giờ pop hai phần tử
                stack.pop_back();
                stack.pop_back();

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
            case OP_MO_KHOI: {
                blockStack.push_back(pc);
                ++blockDepth;
                break;
            }
            // --- OP_DONG_KHOI (đóng khối) ---
            case OP_DONG_KHOI: {
                if (blockStack.empty()) throw std::runtime_error("Lỗi: không có khối mở");
                // Lấy vị trí bắt đầu khối (nếu cần đối chiếu với switch)
                int blockStartPc = blockStack.back();
                blockStack.pop_back();

                // Giảm blockDepth khi đóng khối (fix off-by-one)
                if (blockDepth > 0) --blockDepth;

                // Nếu kết thúc khối CHON, pop switchStack
                // Lưu ý: logic so sánh dựa trên blockDepthAtStart trước đây — sau khi sửa giảm blockDepth ở đúng chỗ,
                // điều kiện dựa trên kích thước stack hay giá trị blockDepthAtStart sẽ chính xác hơn.
                if (!switchStack.empty()) {
                    // Nếu switchStack lưu blockDepthAtStart, điều kiện ban đầu có thể dùng:
                    if (blockStack.size() == switchStack.back().blockDepthAtStart - 1) {
                        switchStack.pop_back();
                        skippingCase = false;
                        switchValue.reset();
                    }
                    // Nếu bạn đã mở rộng SwitchContext để lưu blockStartPc thì có thể dùng:
                    // if (switchStack.back().blockStartPc == blockStartPc) { ... }
                }
                break;
            }
            case OP_DONG_LENH: case OP_MO_NGOAC: case OP_DONG_NGOAC:
                break;
            case OP_PHU_DINH: {
                if (stack.empty()) {
                    throw std::runtime_error("Thiếu toán hạng cho toán tử phủ định !");
                }
                StackValue operand = stack.back(); stack.pop_back();

                // Chuyển sang int 0/1 thay vì push bool trực tiếp
                bool b = false;
                // Nếu có hàm tiện ích toBool, dùng nó; nếu không, chuyển thủ công
                #ifdef HAS_TOBOOL_HELPER
                b = toBool(operand);
                #else
                if (std::holds_alternative<int>(operand)) {
                    b = (std::get<int>(operand) != 0);
                } else if (std::holds_alternative<std::string>(operand)) {
                    b = (!std::get<std::string>(operand).empty());
                } else {
                    // an toàn: coi như false
                    b = false;
                }
                #endif
                int result = b ? 0 : 1;
                stack.push_back(result); // luôn push int (0/1)
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
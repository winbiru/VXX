#pragma once

#include <cstddef>
#include <stdexcept>
#include <utility>

#include "vpp/runtime/vm.h"

// Cung cấp bề mặt kiểm thử nội bộ cho VM; fixture cho test push stack, đặt frame/class và gọi trực tiếp từng opcode handler mà không chạy cả chương trình.
class VMRuntimeFixture {
public:
    // Khởi tạo fixture kiểm thử VM; constructor tạo trạng thái VM tối thiểu và cung cấp hook để test gọi trực tiếp từng opcode handler.
    explicit VMRuntimeFixture(VM &vm) : vm_(vm) {}

    // Thiết lập đầu ra sink; hàm ghi giá trị đầu vào vào trạng thái đích và thay thế giá trị cũ nếu đã tồn tại.
    void setOutputSink(VM::OutputSink sink) { vm_.setOutputSink(std::move(sink)); }

    // Đưa vào push; hàm thêm phần tử vào ngăn xếp hoặc ngữ cảnh hiện tại để được dùng trước khi rời phạm vi.
    void push(const StackValue &value) { vm_.stack.push_back(value); }
    // Trả tham chiếu tới stack hiện tại của fixture/VM; test dùng kết quả để kiểm tra trạng thái sau khi chạy opcode.
    const std::vector<StackValue> &stack() const { return vm_.stack; }

    // Chuyển top; hàm chuyển giá trị đầu vào sang kiểu/biểu diễn đích và trả kết quả đã chuẩn hóa.
    const StackValue &top() const {
        if (vm_.stack.empty()) throw std::logic_error("VM fixture stack is empty");
        return vm_.stack.back();
    }

    // Trả program counter hiện tại của fixture/VM để test kiểm tra opcode nhảy đã cập nhật đúng vị trí.
    std::size_t pc() const { return vm_.pc; }
    // Thiết lập pc; hàm ghi giá trị đầu vào vào trạng thái đích và thay thế giá trị cũ nếu đã tồn tại.
    void setPc(std::size_t value) { vm_.pc = value; }

    // Kiểm tra điều kiện của `hasVariable`.
    bool hasVariable(int id) const { return vm_.variables.count(id) != 0; }

    // Trả giá trị của biến trong fixture theo slot/tên đã đăng ký; accessor dùng để test kiểm tra tác động của opcode biến.
    const StackValue &variable(int id) const {
        const auto it = vm_.variables.find(id);
        if (it == vm_.variables.end()) throw std::logic_error("VM fixture variable is missing");
        return it->second;
    }

    // Ghi trực tiếp một biến root cho kiểm thử GC; fixture dùng API này để dựng
    // graph sống mà không cần phát bytecode assignment chỉ để tạo root.
    void setVariable(int id, StackValue value) { vm_.variables[id] = std::move(value); }
    // Xóa một biến root để test xác nhận object graph không còn reachable được sweep.
    void eraseVariable(int id) { vm_.variables.erase(id); }
    // Xóa data stack để test mô phỏng object rời khỏi root stack giữa hai chu kỳ GC.
    void clearStack() { vm_.stack.clear(); }

    // Đăng ký graph dựng tay vào heap của VM; production allocation tự đăng ký qua
    // active RuntimeHeapScope, còn test có thể tạo handle trước khi gọi `run()`.
    void trackHeapValue(const StackValue &value) { vm_.runtimeHeap->trackValue(value); }
    // Chạy trực tiếp một chu kỳ tracing GC trên trạng thái fixture hiện tại.
    void collectGarbage() { vm_.collectGarbage(false); }
    // Yêu cầu explicit capacity trim sau GC để test policy major collection.
    void collectGarbageAndTrim() { vm_.collectGarbage(true); }
    // Trả capacity của data stack để hardening test kiểm tra periodic GC không shrink.
    std::size_t stackCapacity() const { return vm_.stack.capacity(); }
    // Dự trữ stack capacity mà không tạo root sống giả.
    void reserveStack(std::size_t capacity) { vm_.stack.reserve(capacity); }
    // Trả thống kê chu kỳ GC gần nhất để test xác nhận mark/sweep đã thật sự chạy.
    const vietvm::runtime::RuntimeHeapStats &gcStats() const { return vm_.lastGcStats; }
    // Trả số heap object còn sống trong registry sau khi loại weak entry hết hạn.
    std::size_t trackedHeapObjects() { return vm_.runtimeHeap->trackedObjectCount(); }

    // Đưa vào lời gọi khung; hàm thêm phần tử vào ngăn xếp hoặc ngữ cảnh hiện tại để được dùng trước khi rời phạm vi.
    void pushCallFrame(const CallFrame &frame) { vm_.callStack.push_back(frame); }

    // Trả số call frame đang hoạt động để test xác nhận unwind sau lời gọi thành công hoặc lỗi.
    std::size_t callDepth() const { return vm_.callStack.size(); }

    // Reset execution-only state giữa các lượt benchmark. Globals/module/class state
    // được giữ nguyên; caller chỉ dùng khi không còn function context hoạt động.
    void resetExecutionForBenchmark() {
        if (!vm_.executionStack.empty() || !vm_.callStack.empty()) {
            throw std::logic_error("VM benchmark reset requires an idle VM");
        }
        vm_.stack.clear();
        vm_.loopStartStack.clear();
        vm_.ifElseStack.clear();
        vm_.blockStack.clear();
        vm_.switchStack.clear();
        vm_.tryStack.clear();
        vm_.blockDepth = 0;
        vm_.callDepthFromRoot = 0;
        vm_.pc = 0;
    }

    // Chạy interpreter trực tiếp sau khi benchmark đã verify bytecode ngoài timer.
    // Đường này cố ý bỏ qua VM::run() để timing không gồm construction/verification.
    void runInterpreterPreverifiedForBenchmark() { vm_.runInterpreterLoop(); }

    // Hạ/nâng giới hạn độ sâu lời gọi trong test để kiểm tra guard đệ quy mà
    // không cần tạo hàng trăm native stack frame.
    void setMaxCallDepth(std::size_t value) { vm_.maxCallDepth = value; }

    // Trả call frame đang hoạt động; hàm đọc frame trên cùng để handler/test truy cập tham số, local, receiver và địa chỉ quay về.
    const CallFrame &currentCallFrame() const {
        if (vm_.callStack.empty()) throw std::logic_error("VM fixture call stack is empty");
        return vm_.callStack.back();
    }

    // Thiết lập lớp; hàm ghi giá trị đầu vào vào trạng thái đích và thay thế giá trị cũ nếu đã tồn tại.
    void setClass(std::string name, ClassHandle klass) {
        vm_.classTable[std::move(name)] = std::move(klass);
    }

    // Trả số `SwitchFrame` đang mở; fixture dùng kích thước stack frame để kiểm tra việc vào/rời câu lệnh chọn.
    std::size_t switchDepth() const { return vm_.switchStack.size(); }
    // Cho biết nhánh `chọn` hiện tại đang bị bỏ qua hay không; hàm đọc cờ `skipping` của frame trên cùng.
    bool switchSkipping() const {
        if (vm_.switchStack.empty()) throw std::logic_error("VM fixture switch stack is empty");
        return vm_.switchStack.back().skippingCase;
    }
    // Cho biết một nhánh của `chọn` đã khớp; hàm đọc cờ `matched` để test xác nhận các nhánh sau bị xử lý đúng.
    bool switchMatched() const {
        if (vm_.switchStack.empty()) throw std::logic_error("VM fixture switch stack is empty");
        return vm_.switchStack.back().caseMatched;
    }

    // Trả độ sâu block runtime hiện tại; test dùng giá trị này để xác nhận opcode mở/đóng khối cân bằng.
    int blockDepth() const { return vm_.blockDepth; }
    // Trả số block frame đang giữ để test khóa contract unwind của `ném`.
    std::size_t blockStackDepth() const { return vm_.blockStack.size(); }
    // Trả số `TryFrame` đang hoạt động; fixture dùng để kiểm tra phạm vi bắt lỗi được đẩy/pop đúng.
    std::size_t tryDepth() const { return vm_.tryStack.size(); }

    // Thực thi lời gọi; hàm đọc trạng thái VM/opcode đầu vào, cập nhật stack/frame/program counter và trả quyền điều khiển về vòng chạy chính.
    void executeCall(const Instruction &instruction) {
        const std::size_t executionDepth = vm_.executionStack.size();
        vm_.executeCallOpcode(instruction);
        if (vm_.executionStack.size() > executionDepth) {
            vm_.runInterpreterLoop(executionDepth);
        }
    }
    // Thực thi giá trị; hàm đọc trạng thái VM/opcode đầu vào, cập nhật stack/frame/program counter và trả quyền điều khiển về vòng chạy chính.
    void executeValue(const Instruction &instruction) { vm_.executeValueOpcode(instruction); }
    // Thực thi chỉ số; hàm đọc trạng thái VM/opcode đầu vào, cập nhật stack/frame/program counter và trả quyền điều khiển về vòng chạy chính.
    void executeIndex(const Instruction &instruction) { vm_.executeIndexOpcode(instruction); }
    // Thực thi đối tượng; hàm đọc trạng thái VM/opcode đầu vào, cập nhật stack/frame/program counter và trả quyền điều khiển về vòng chạy chính.
    void executeObject(const Instruction &instruction) {
        const std::size_t executionDepth = vm_.executionStack.size();
        vm_.executeObjectOpcode(instruction);
        if (vm_.executionStack.size() > executionDepth) {
            vm_.runInterpreterLoop(executionDepth);
        }
    }
    // Thực thi variable; hàm đọc trạng thái VM/opcode đầu vào, cập nhật stack/frame/program counter và trả quyền điều khiển về vòng chạy chính.
    void executeVariable(const Instruction &instruction) { vm_.executeVariableOpcode(instruction); }
    // Thực thi khối chọn; hàm đọc trạng thái VM/opcode đầu vào, cập nhật stack/frame/program counter và trả quyền điều khiển về vòng chạy chính.
    bool executeSwitch(const Instruction &instruction) { return vm_.executeSwitchOpcode(instruction); }
    // Thực thi vòng lặp control; hàm đọc trạng thái VM/opcode đầu vào, cập nhật stack/frame/program counter và trả quyền điều khiển về vòng chạy chính.
    void executeLoopControl(const Instruction &instruction) { vm_.executeLoopControlOpcode(instruction); }
    // Thực thi khối; hàm đọc trạng thái VM/opcode đầu vào, cập nhật stack/frame/program counter và trả quyền điều khiển về vòng chạy chính.
    void executeBlock(const Instruction &instruction) { vm_.executeBlockOpcode(instruction); }
    // Thực thi exception; hàm đọc trạng thái VM/opcode đầu vào, cập nhật stack/frame/program counter và trả quyền điều khiển về vòng chạy chính.
    bool executeException(const Instruction &instruction) { return vm_.executeExceptionOpcode(instruction); }
    // Thực thi nhánh; hàm đọc trạng thái VM/opcode đầu vào, cập nhật stack/frame/program counter và trả quyền điều khiển về vòng chạy chính.
    bool executeBranch(const Instruction &instruction) { return vm_.executeBranchOpcode(instruction); }
    // Thực thi đầu ra; hàm đọc trạng thái VM/opcode đầu vào, cập nhật stack/frame/program counter và trả quyền điều khiển về vòng chạy chính.
    void executeOutput(const Instruction &instruction) { vm_.executeOutputOpcode(instruction); }

private:
    VM &vm_;
};

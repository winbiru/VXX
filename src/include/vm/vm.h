#ifndef VM_H
#define VM_H

#include <vector>
#include <stack>
#include <unordered_map>
#include <string>
#include <string_view>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <variant>

#include "instruction.h"
#include "../common/vm_callframe.h"
#include "vpp/runtime/heap.h"
#include "vpp/runtime/value.h"
#include "vpp/runtime/module.h"

// Cung cấp bề mặt kiểm thử nội bộ cho VM; fixture cho test push stack, đặt frame/class và gọi trực tiếp từng opcode handler mà không chạy cả chương trình.
class VMRuntimeFixture;

// Thực thi bytecode V++; lớp sở hữu stack, call frame, module/class table và dispatch opcode tới các handler theo program counter.
class VM {
public:
    using OutputSink = std::function<void(const std::string&)>;

    // Tạo VM từ bytecode đã biên dịch; constructor lưu code và chuẩn bị trạng thái thực thi ban đầu trước khi `run()` được gọi.
    explicit VM(const std::vector<Instruction>& code);
    // Tạo VM rỗng để test/helper có thể nạp trực tiếp bytecode, StringPool hoặc bảng hàm trước khi thực thi.
    VM() = default;
    // Chạy vòng lặp VM từ bytecode hiện tại; mỗi bước đọc opcode tại program counter và chuyển tới handler tương ứng cho tới khi dừng.
    void run();
    // Tạo VM từ bytecode và `StringPool` đi kèm; constructor giữ cùng chỉ số chuỗi mà compiler đã mã hóa trong operand.
    VM(const std::vector<Instruction>& code, const std::vector<std::string>& pool);
    // Thay callback nhận output của opcode `in`; VM gọi sink này thay vì ghi trực tiếp ra stdout để CLI/test có thể định tuyến kết quả.
    void setOutputSink(OutputSink sink);
    // Đăng ký initializer bytecode cho một module theo identity; hàm từ chối identity trùng và lưu code để `initializeModules()` chạy đúng một lần.
    bool addModuleInitializer(std::string identity,
                              std::vector<Instruction> initializer);
    // Trả trạng thái khởi tạo của module được yêu cầu; hàm tra `ModuleTable`/tracker hiện tại và không tự chạy initializer.
    std::optional<vietvm::runtime::ModuleState> moduleState(
        std::string_view identity) const noexcept;

    std::unordered_map<int, std::vector<Instruction>> hamBytecodeMap;
    // nameIndex → hamId mapping for function name lookup (shared with child VMs for recursion)
    std::unordered_map<int, int> functionTableByNameIndex;

private:
    // Cung cấp bề mặt kiểm thử nội bộ cho VM; fixture cho test push stack, đặt frame/class và gọi trực tiếp từng opcode handler mà không chạy cả chương trình.
    friend class VMRuntimeFixture;

    std::vector<Instruction> bytecode;              // Mã bytecode
    std::vector<std::string> stringPool;

    std::vector<StackValue> stack;                  // data stack (values)

    std::unordered_map<int, StackValue> variables;  // fallback global var store
    OutputSink outputSink;
    vietvm::runtime::ModuleTable moduleTable;
    std::unordered_map<std::string, ClassHandle> classTable;
    std::shared_ptr<vietvm::runtime::RuntimeHeap> runtimeHeap =
        std::make_shared<vietvm::runtime::RuntimeHeap>();
    vietvm::runtime::RuntimeHeapStats lastGcStats;
    std::vector<StackValue> inheritedGcRoots;

    // Call stack for function calls
    std::vector<CallFrame> callStack;

    // helper stacks for control-flow
    std::vector<size_t> loopStartStack;
    std::vector<size_t> ifElseStack;
    std::vector<size_t> blockStack;

    size_t pc = 0;                                  // Program counter
    bool running = true;
    int vi_tri_dieu_kien = -1;

    // Lưu trạng thái của một câu lệnh `chọn` đang chạy, gồm giá trị so sánh và các cờ matched/skipping để điều khiển nhánh kế tiếp.
    struct SwitchFrame {
        std::optional<StackValue> switchValue;
        bool skippingCase{};
        bool caseMatched{};
        size_t blockDepthAtStart{};
    };
    std::vector<SwitchFrame> switchStack;
    int blockDepth = 0;

    // Lưu phạm vi bắt lỗi đang hoạt động, gồm vị trí handler và trạng thái stack/frame cần phục hồi khi opcode `ném` xảy ra.
    struct TryFrame {
        int catchAddr;        // PC of OP_BAT_LOI
        int stackDepth;       // stack size when try started
        int errVarId;         // variable id to bind error (-1 = none)
        std::size_t blockStackDepth{};
        std::size_t switchStackDepth{};
        std::size_t loopStackDepth{};
        std::size_t ifElseStackDepth{};
        int blockDepth{};
    };
    std::vector<TryFrame> tryStack;

    // Các hàm phụ trợ
    void execute(const Instruction& inst);
    // Lấy ra khỏi số nguyên; hàm loại bỏ phần tử/ngữ cảnh trên cùng và khôi phục trạng thái trước đó.
    int popInt();                // helper pop int from stack (or throw)
    // Đưa vào số nguyên; hàm thêm phần tử vào ngăn xếp hoặc ngữ cảnh hiện tại để được dùng trước khi rời phạm vi.
    void pushInt(int value);
    // Lấy ra khỏi giá trị; hàm loại bỏ phần tử/ngữ cảnh trên cùng và khôi phục trạng thái trước đó.
    StackValue popValue();
    // Đưa vào giá trị; hàm thêm phần tử vào ngăn xếp hoặc ngữ cảnh hiện tại để được dùng trước khi rời phạm vi.
    void pushValue(const StackValue &v);

    // Lấy arg from hiện tại khung; hàm đọc dữ liệu từ trạng thái hiện tại và trả về cho caller mà không chủ động thay đổi dữ liệu.
    StackValue getArgFromCurrentFrame(int argIndex) const;
    // Thiết lập cục bộ in hiện tại khung; hàm ghi giá trị đầu vào vào trạng thái đích và thay thế giá trị cũ nếu đã tồn tại.
    void setLocalInCurrentFrame(int localId, const StackValue& value);

    // Tạo và đẩy call frame mới khi vào hàm; frame giữ tham số, biến cục bộ, receiver và địa chỉ quay về của caller.
    void enterFunctionFrame(const std::vector<StackValue>& args, int returnPc);
    // Rời call frame hiện tại sau khi hàm kết thúc; hàm khôi phục frame caller và trạng thái điều khiển trước lời gọi.
    void leaveCurrentFrame();
    // Tạo call frame và thực thi bytecode của một function id với danh sách đối số/receiver đã chuẩn bị, sau đó trả kết quả về caller.
    void invokeFunction(int argc,
                        int hamIdOrName,
                        Opcode op,
                        int curPc,
                        InstanceHandle receiver = nullptr,
                        ClassHandle methodOwnerClass = nullptr,
                        ClosureHandle closure = nullptr);
    // Lấy hoặc tạo ô nhớ chia sẻ cho slot bị closure capture trong frame hiện tại.
    CellHandle captureCellForSlot(int varId);
    // Xử lý nhóm opcode gọi hàm/phương thức; hàm lấy đối số từ stack, xác định đích gọi và chuyển quyền điều khiển sang function tương ứng.
    void executeCallOpcode(const Instruction& instr);
    // Xử lý opcode tạo hoặc biến đổi giá trị trên stack, bao gồm literal và các phép toán số/chuỗi.
    void executeValueOpcode(const Instruction& instr);
    // Xử lý truy cập theo chỉ số cho list/map/string; hàm đọc toán hạng trên stack và áp dụng quy tắc kiểm tra biên/khóa runtime.
    void executeIndexOpcode(const Instruction& instr);
    // Xử lý opcode object model như tạo lớp, tạo instance, đọc/ghi field và gọi phương thức có receiver.
    void executeObjectOpcode(const Instruction& instr);
    // Xử lý opcode biến cục bộ/tham số bằng cách đọc hoặc ghi slot trong call frame hiện tại.
    void executeVariableOpcode(const Instruction& instr);
    // Điều khiển trạng thái của câu lệnh `chọn`, theo dõi nhánh đã khớp và việc bỏ qua các nhánh còn lại.
    bool executeSwitchOpcode(const Instruction& instr);
    // Xử lý `thoát` và `tiếp tục` bằng metadata điều khiển vòng lặp đã được codegen gắn vào bytecode.
    void executeLoopControlOpcode(const Instruction& instr);
    // Cập nhật trạng thái khi VM đi vào hoặc rời block, phục vụ các cấu trúc điều khiển và exception scope.
    void executeBlockOpcode(const Instruction& instr);
    // Xử lý `thử`, `bắt lỗi` và `ném`; hàm quản lý try frame và chuyển điều khiển tới handler phù hợp.
    bool executeExceptionOpcode(const Instruction& instr);
    // Chuyển một giá trị `ném` tới handler gần nhất và phục hồi các control stack về trạng thái lúc bắt đầu `thử`.
    bool transferThrownValue(const StackValue &value);
    // Xử lý opcode nhảy có điều kiện/không điều kiện bằng cách cập nhật program counter dựa trên giá trị trên stack.
    bool executeBranchOpcode(const Instruction& instr);
    // Xử lý opcode xuất dữ liệu, chuyển `StackValue` thành chuỗi rồi gửi tới output sink đã cấu hình.
    void executeOutputOpcode(const Instruction& instr);
    // Phát mã cho đầu ra; hàm duyệt biểu diễn đầu vào và sinh opcode/metadata tương ứng vào buffer bytecode đích.
    void emitOutput(const StackValue& value);
    // Khởi tạo các module runtime theo thứ tự phụ thuộc; tracker bảo đảm mỗi initializer chỉ chạy một lần và ghi trạng thái thành công/thất bại.
    void initializeModules();

    // Chụp toàn bộ root StackValue mà tracing GC phải giữ sống, gồm stack, biến,
    // call frame, class table, switch value và root kế thừa từ VM caller.
    std::vector<StackValue> gcRoots() const;

    // Chạy JIT đã biên dịch tuyến tính; hàm điều phối toàn bộ luồng xử lý của tác vụ, gọi các bước con theo thứ tự và trả mã/kết quả cuối cùng.
    bool runJitCompiledLinear();
    // Thực hiện chu kỳ thu gom bộ nhớ runtime theo cơ chế GC hiện tại, duyệt các root đang sống trước khi giải phóng đối tượng không còn tham chiếu.
    void collectGarbage();
};

#endif // VM_H

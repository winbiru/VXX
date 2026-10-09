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
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "instruction.h"
#include "../common/vm_callframe.h"
#include "vpp/bytecode/intrinsic.h"
#include "vpp/bytecode/foreign.h"
#include "vpp/runtime/foreign.h"
#include "vpp/runtime/heap.h"
#include "vpp/runtime/value.h"
#include "vpp/runtime/module.h"
#include "vpp/runtime/debug.h"
#include "vpp/runtime/error.h"

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
    ~VM();
    VM(const VM &) = delete;
    VM &operator=(const VM &) = delete;
    // Chạy vòng lặp VM từ bytecode hiện tại; mỗi bước đọc opcode tại program counter và chuyển tới handler tương ứng cho tới khi dừng.
    void run();
    // Tạo VM từ bytecode và `StringPool` đi kèm; constructor giữ cùng chỉ số chuỗi mà compiler đã mã hóa trong operand.
    VM(const std::vector<Instruction>& code, const std::vector<std::string>& pool);
    // Thay callback nhận output của opcode `in`; VM gọi sink này thay vì ghi trực tiếp ra stdout để CLI/test có thể định tuyến kết quả.
    void setOutputSink(OutputSink sink);
    // Đăng ký initializer bytecode cho một module theo identity; hàm từ chối identity trùng và lưu code để `initializeModules()` chạy đúng một lần.
    bool addModuleInitializer(std::string identity,
                              std::vector<Instruction> initializer,
                              std::vector<vietvm::runtime::RuntimeSourceLocation> debugInfo = {});
    // Gắn debug metadata song song với root/function bytecode. Metadata không
    // tham gia semantics bytecode và chỉ được dùng khi dựng stack trace lỗi.
    void setDebugInfo(
        std::vector<vietvm::runtime::RuntimeSourceLocation> rootDebugInfo,
        std::unordered_map<int, std::vector<vietvm::runtime::RuntimeSourceLocation>>
            functionDebugInfo);
    // Nạp toàn bộ function bytecode và bảng tên → function id như một snapshot.
    // Mọi thay đổi qua API này đều làm bẩn cache verifier trước lần chạy kế tiếp.
    void setFunctions(
        std::unordered_map<int, std::vector<Instruction>> functionBytecode,
        std::unordered_map<int, int> functionTableByNameIndex = {});
    void setForeignFunctions(
        std::vector<vietvm::bytecode::ForeignFunctionDescriptor> descriptors);
    void setForeignCapabilities(std::unordered_set<std::string> capabilities);
    // Bảo đảm snapshot bytecode hiện tại đã qua verifier. Nhiều lần gọi trên cùng
    // generation là O(1); mutation bytecode/module/function sẽ tạo generation mới.
    void ensureBytecodeVerified();
    // Đặt lại execution state để chạy lại cùng snapshot mà không làm mất globals/module state.
    // API này chủ yếu phục vụ host/test/benchmark lặp lại cùng chương trình.
    void resetExecution();
    // Số generation đã thực sự đi vào verifier (kể cả generation bị từ chối).
    // Cache hit trên cùng generation không tăng counter này.
    std::size_t verificationPassCount() const noexcept { return verificationPassCount_; }
    // Số opcode đã chạy qua fast path JIT ở lần chạy gần nhất (0 khi JIT tắt).
    std::size_t jitFastInstructionCount() const noexcept { return jitFastInstructionCount_; }
    // Số opcode cần dispatcher đầy đủ (call, branch, intrinsic, exception...).
    std::size_t jitInterpreterInstructionCount() const noexcept { return jitInterpreterInstructionCount_; }
    // Trả trạng thái khởi tạo của module được yêu cầu; hàm tra `ModuleTable`/tracker hiện tại và không tự chạy initializer.
    std::optional<vietvm::runtime::ModuleState> moduleState(
        std::string_view identity) const noexcept;

private:
    // Cung cấp bề mặt kiểm thử nội bộ cho VM; fixture cho test push stack, đặt frame/class và gọi trực tiếp từng opcode handler mà không chạy cả chương trình.
    friend class VMRuntimeFixture;

    std::vector<Instruction> bytecode;              // Mã bytecode
    std::vector<std::string> stringPool;
    std::unordered_map<int, std::vector<Instruction>> hamBytecodeMap;
    // nameIndex → hamId mapping for function name lookup across execution contexts.
    std::unordered_map<int, int> functionTableByNameIndex;
    // Reverse lookup derived once when function tables are installed.
    std::unordered_map<int, int> functionNameIndexById_;
    std::vector<vietvm::bytecode::ForeignFunctionDescriptor> foreignFunctions_;
    std::unordered_set<std::string> foreignCapabilities_;
    int lastForeignPosixError_ = 0;
    vietvm::runtime::ForeignFileState foreignFileState_;
    std::size_t programGeneration_ = 1;
    std::size_t verifiedGeneration_ = 0;
    std::size_t verificationPassCount_ = 0;
    // Cache vi lệnh đã biên dịch cho root và mọi function; chỉ sống trong một
    // lượt chạy JIT và được truy cập bằng identity của bytecode vector.
    using JitOperation = std::function<void()>;
    std::unordered_map<const std::vector<Instruction> *, std::vector<JitOperation>> jitPrograms_;
    bool jitActive_ = false;
    std::size_t jitFastInstructionCount_ = 0;
    std::size_t jitInterpreterInstructionCount_ = 0;
    std::vector<vietvm::runtime::RuntimeSourceLocation> bytecodeDebugInfo;
    std::unordered_map<int, std::vector<vietvm::runtime::RuntimeSourceLocation>>
        functionDebugInfo;

    // Root bytecode/debug metadata are owned by the fields above. While a V++
    // function is running these pointers select the immutable function vectors
    // stored in the function tables, avoiding a full vector copy on every call.
    // nullptr means the root program is active.
    const std::vector<Instruction> *activeBytecode_ = nullptr;
    const std::vector<vietvm::runtime::RuntimeSourceLocation>
        *activeBytecodeDebugInfo_ = nullptr;

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
    // Theo dõi độ sâu lời gọi V++; việc thực thi hàm dùng explicit execution
    // contexts nên độ sâu này không còn tiêu thụ native C++ stack.
    std::size_t callDepthFromRoot = 0;
    // Giới hạn độ sâu lời gọi để đệ quy không dừng được báo có kiểm soát và không
    // tăng vô hạn lượng trạng thái interpreter. Fixture có thể hạ giới hạn này cho regression test.
    std::size_t maxCallDepth = 256;

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
        size_t tryDepthAtStart{};
        size_t endPc{}; // points at OP_KET_THUC_CHUYEN for structured switch
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

    // Cách caller xử lý giá trị khi callee kết thúc. Constructor bỏ giá trị trả
    // về của `khởi tạo` và thay bằng instance vừa được tạo.
    enum class CallReturnMode {
        Value,
        ConstructorInstance,
    };

    // Snapshot trạng thái thực thi của caller khi VM chuyển sang bytecode của
    // callee. Đây là call stack ở cấp interpreter, thay cho việc gọi lồng
    // `VM::run()` bằng native C++ recursion.
    struct ExecutionContext {
        const std::vector<Instruction> *bytecode = nullptr;
        const std::vector<vietvm::runtime::RuntimeSourceLocation>
            *bytecodeDebugInfo = nullptr;
        std::vector<StackValue> stack;
        std::vector<size_t> loopStartStack;
        std::vector<size_t> ifElseStack;
        std::vector<size_t> blockStack;
        std::vector<SwitchFrame> switchStack;
        std::vector<TryFrame> tryStack;
        size_t pc = 0;
        int blockDepth = 0;
        CallReturnMode returnMode = CallReturnMode::Value;
        InstanceHandle constructorInstance;
    };
    std::vector<ExecutionContext> executionStack;

    enum class WorkerStatus {
        Running = 0,
        Done = 1,
        Cancelled = 2,
        Failed = 3,
    };

    struct WorkerTask {
        std::mutex mutex;
        std::condition_variable cv;
        std::atomic<bool> cancelRequested{false};
        WorkerStatus status = WorkerStatus::Running;
        StackValue result = make_null_value();
        std::string error;
        std::thread worker;
        bool joinClaimed = false;
    };

    struct ThreadRuntimeState {
        std::mutex registryMutex;
        std::unordered_map<int, std::shared_ptr<WorkerTask>> tasks;
        std::atomic<int> nextId{1};
        std::atomic<bool> stopping{false};
        std::shared_ptr<std::mutex> outputMutex = std::make_shared<std::mutex>();
    };

    std::shared_ptr<ThreadRuntimeState> threadRuntime_ =
        std::make_shared<ThreadRuntimeState>();
    bool threadRuntimeOwner_ = true;
    std::atomic<bool> *cancellationRequested_ = nullptr;
    WorkerTask *currentWorkerTask_ = nullptr;

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
                        int op,
                        int curPc,
                        InstanceHandle receiver = nullptr,
                        ClassHandle methodOwnerClass = nullptr,
                        ClosureHandle closure = nullptr,
                        CallReturnMode returnMode = CallReturnMode::Value,
                        InstanceHandle constructorInstance = nullptr);
    // Khôi phục caller sau khi function bytecode kết thúc; giá trị trả về được
    // chuyển sang caller theo return mode đã lưu trong execution context.
    bool completeFunctionCall();
    // Unwind một giá trị `ném` qua các execution context cho tới `thử/bắt lỗi`
    // gần nhất mà không dùng native exception recursion qua nhiều VM con.
    bool unwindLanguageException(const StackValue &value);
    // Bổ sung source frame của từng caller rồi phục hồi trạng thái gốc trước khi
    // RuntimeError thoát khỏi `run()`.
    void unwindRuntimeError(vietvm::runtime::RuntimeError &error);
    // Khôi phục snapshot caller gần nhất và pop call frame của callee hiện tại.
    void restoreCallerExecutionContext();
    // Lấy hoặc tạo ô nhớ chia sẻ cho slot bị closure capture trong frame hiện tại.
    CellHandle captureCellForSlot(int varId);
    // Xử lý nhóm opcode gọi hàm/phương thức; hàm lấy đối số từ stack, xác định đích gọi và chuyển quyền điều khiển sang function tương ứng.
    void executeCallOpcode(const Instruction& instr);
    // Thực thi intrinsic VM dành riêng cho thư viện chuẩn; opcode mang sẵn
    // identity/arity nên không cần tra tên hàm động qua StringPool.
    void executeIntrinsicOpcode(const Instruction& instr);
    void executeForeignCallOpcode(const Instruction& instr);
    // Cùng đường thực thi primitive cho opcode hiện hành và lời gọi legacy.
    // runtimeFailure phân biệt lỗi VM/IO không bắt được với lỗi V++ có thể bắt.
    bool dispatchRegisteredIntrinsic(const vietvm::bytecode::IntrinsicDescriptor &intrinsic,
                                     const std::vector<StackValue> &args,
                                     StackValue &result,
                                     std::string &err,
                                     bool &runtimeFailure);
    bool executeThreadIntrinsic(const vietvm::bytecode::IntrinsicDescriptor &intrinsic,
                                const std::vector<StackValue> &args,
                                StackValue &result,
                                std::string &err);
    StackValue runWorkerCallable(const StackValue &callable,
                                 const std::vector<StackValue> &args);
    void shutdownThreadRuntime() noexcept;
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
    // Trả source frame ứng với PC hiện tại nếu compiler đã cung cấp debug metadata.
    vietvm::runtime::RuntimeSourceLocation sourceLocationForPc(std::size_t value) const;

    // Chụp toàn bộ root StackValue mà tracing GC phải giữ sống, gồm stack, biến,
    // call frame, class table, switch value và root kế thừa từ VM caller.
    std::vector<StackValue> gcRoots() const;

    // Biên dịch các vi lệnh thuần giá trị ở root và mọi function. Dispatcher VM
    // xử lý call/branch/intrinsic/exception để giữ nguyên execution contexts.
    void runJitCompiled();
    // Chạy interpreter trên context hiện tại. Khi `stopExecutionDepth` có giá trị,
    // hàm dừng ngay sau khi lời gọi trực tiếp của fixture đã quay về độ sâu đó.
    void runInterpreterLoop(std::optional<std::size_t> stopExecutionDepth = std::nullopt);
    // Thực hiện chu kỳ thu gom bộ nhớ runtime theo cơ chế GC hiện tại, duyệt các root đang sống trước khi giải phóng đối tượng không còn tham chiếu.
    // Thu gom tracing roots. `trimCapacity=true` chỉ dùng cho explicit/major trim;
    // periodic/final collection giữ capacity để tránh allocate/free thrashing.
    void collectGarbage(bool trimCapacity = false);
    void invalidateBytecodeVerification() noexcept;
    void trimExecutionCapacity();
    const std::vector<Instruction> &currentBytecode() const noexcept;
    const std::vector<vietvm::runtime::RuntimeSourceLocation> &
    currentBytecodeDebugInfo() const noexcept;
};

#endif // VM_H

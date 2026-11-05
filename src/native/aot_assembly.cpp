#include "vpp/native/aot.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>

#include "vpp/bytecode/intrinsic.h"
#include "common/bit_ops.h"
#include "vpp/native/raw_codegen.h"

namespace vietvm::native::aot {
namespace {
// Fold only VM i32 operations whose results are unconditional and precisely
// defined. Bit reconstruction makes signed overflow wrapping portable.
bool foldI32(int operation, std::int32_t left, std::int32_t right,
             std::int32_t &result) {
    std::uint32_t bits=0;
    switch(operation) {
        case OP_BIT_VA:
        case OP_BIT_HOAC:
        case OP_BIT_XOR:
        case OP_DICH_TRAI:
        case OP_DICH_PHAI:
            result=evaluateI32Bits(operation,left,right);return true;
        case OP_CONG:bits=std::uint32_t(left)+std::uint32_t(right);break;
        case OP_TRU:bits=std::uint32_t(left)-std::uint32_t(right);break;
        case OP_NHAN:bits=std::uint32_t(left)*std::uint32_t(right);break;
        case OP_SO_SANH_BANG:result=left==right;return true;
        case OP_KHAC_BANG:result=left!=right;return true;
        case OP_LON_HON:result=left>right;return true;
        case OP_NHO_HON:result=left<right;return true;
        case OP_LON_HON_HOAC_BANG:result=left>=right;return true;
        case OP_NHO_HON_HOAC_BANG:result=left<=right;return true;
        case OP_Logic_VA:result=(left!=0 && right!=0);return true;
        case OP_Logic_HOAC:result=(left!=0 || right!=0);return true;
        default:return false;
    }
    std::memcpy(&result,&bits,sizeof(result));
    return true;
}
std::string quoted(const std::string &text) {
    std::ostringstream out; out << '"';
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') out << '\\' << c;
        else if (c >= 32 && c < 127) out << c;
        else out << '\\' << std::oct << std::setw(3) << std::setfill('0') << unsigned(c) << std::dec;
    }
    return out.str() + '"';
}
#if defined(__APPLE__)
constexpr const char *prefix = "_";
#else
constexpr const char *prefix = "";
#endif
#if !defined(__aarch64__) && !defined(__arm64__) && !defined(__x86_64__) && !defined(_M_X64)
#error Full native AOT needs an AArch64 or x86-64 host
#endif

struct CountedScalarLoop {
    int counter, bound, value, step, threshold, exit;
};
struct NumericAddLoop {
    int counter, bound, left, right, result, exit;
};
struct IndirectI32FoldLoop {
    int counter, bound, checksum, callable, data, seed;
    int cycle, divisor, exit;
};

std::optional<MachineOpcode> i32Opcode(int op) {
    switch(op) {
        case OP_BIT_VA:return MachineOpcode::BitAnd;
        case OP_BIT_HOAC:return MachineOpcode::BitOr;
        case OP_BIT_XOR:return MachineOpcode::BitXor;
        case OP_DICH_TRAI:return MachineOpcode::ShiftLeft;
        case OP_DICH_PHAI:return MachineOpcode::ShiftRight;
        case OP_CONG:return MachineOpcode::Add;
        case OP_TRU:return MachineOpcode::Sub;
        case OP_NHAN:return MachineOpcode::Mul;
        case OP_SO_SANH_BANG:return MachineOpcode::Equal;
        case OP_KHAC_BANG:return MachineOpcode::NotEqual;
        case OP_NHO_HON:return MachineOpcode::Less;
        case OP_NHO_HON_HOAC_BANG:return MachineOpcode::LessEqual;
        case OP_LON_HON:return MachineOpcode::Greater;
        case OP_LON_HON_HOAC_BANG:return MachineOpcode::GreaterEqual;
        default:return std::nullopt;
    }
}

// Biểu thức hậu tố thuần; không vượt nhánh vào giữa hoặc phép toán có lỗi.
std::size_t i32ExpressionEnd(const std::vector<Instruction> &code,
                             const std::vector<bool> &targets,std::size_t start) {
    std::size_t depth=0,operations=0,end=start;
    for(auto j=start;j<code.size() && j-start<64;++j) {
        if(j>start && targets[j])break;
        const auto &v=code[j];
        if(v.op==OP_BIEN_SO || (v.op==OP_TEN_BIEN_GIA_TRI && v.operandIndex>=0))++depth;
        else if(i32Opcode(v.op) && depth>=2) {
            --depth;++operations;
            if(depth==1 && operations>=2)end=j+1;
        } else break;
    }
    return end;
}

// Recognize the side-effect-free counted loop produced by the bytecode
// compiler for: for(i = ...; i < bound; i++) {
//     x += step; if(x >= threshold) x -= threshold;
// }
// Keep the complete original region as a deoptimization path. Never match a
// region with another entry point: a jump/handler can otherwise bypass the
// scalar guards or enter in the middle of an optimized expression.
std::optional<CountedScalarLoop> countedScalarLoop(
    const std::vector<Instruction> &code, const std::vector<bool> &targets,
    std::size_t i) {
    if (i + 28 > code.size()) return std::nullopt;
    const auto op=[&](std::size_t offset,int opcode) {return code[i+offset].op==opcode;};
    if (!op(0,OP_DIEU_KIEN) || !op(1,OP_TEN_BIEN_GIA_TRI) ||
        !op(2,OP_BIEN_SO) || !op(3,OP_NHO_HON) ||
        !op(4,OP_JUMP_IF_FALSE) || !op(5,OP_MO_KHOI) ||
        !op(6,OP_TEN_BIEN_GIA_TRI) || !op(7,OP_BIEN_SO) ||
        !op(8,OP_CONG) || !op(9,OP_TEN_BIEN_ID) ||
        !op(10,OP_GAN) || !op(11,OP_NEU) ||
        !op(12,OP_TEN_BIEN_GIA_TRI) || !op(13,OP_BIEN_SO) ||
        !op(14,OP_LON_HON_HOAC_BANG) || !op(15,OP_JUMP_IF_FALSE) ||
        !op(16,OP_MO_KHOI) || !op(17,OP_TEN_BIEN_GIA_TRI) ||
        !op(18,OP_BIEN_SO) || !op(19,OP_TRU) ||
        !op(20,OP_TEN_BIEN_ID) || !op(21,OP_GAN) ||
        !op(22,OP_DONG_KHOI) || !op(23,OP_DONG_KHOI) ||
        !op(24,OP_CAP_NHAT) || !op(25,OP_TEN_BIEN_ID) ||
        !op(26,OP_CONG_MOT) || !op(27,OP_JUMP)) return std::nullopt;
    const int counter=code[i+1].operandIndex;
    const int value=code[i+6].operandIndex;
    if (counter < 0 || value < 0 || counter == value ||
        code[i+9].operandIndex != value ||
        code[i+12].operandIndex != value ||
        code[i+17].operandIndex != value ||
        code[i+20].operandIndex != value ||
        code[i+25].operandIndex != counter ||
        code[i+13].operand != code[i+18].operand ||
        code[i+4].operand != static_cast<int>(i+28) ||
        code[i+15].operand != static_cast<int>(i+23) ||
        code[i+27].operand != static_cast<int>(i)) return std::nullopt;
    for (std::size_t offset=1;offset<28;++offset)
        if (targets[i+offset] && offset!=23) return std::nullopt;
    return CountedScalarLoop{counter,code[i+2].operand,value,
                             code[i+7].operand,code[i+13].operand,
                             static_cast<int>(i+28)};
}

// Simpler arithmetic induction, e.g. lặp(i=0;i<bound;i++){x=x+3;}.
// Only the exact single-entry 16-opcode region can skip all dynamic ABI calls.
std::optional<CountedScalarLoop> simpleScalarLoop(
    const std::vector<Instruction> &code, const std::vector<bool> &targets,
    std::size_t i) {
    if(i+16>code.size())return std::nullopt;
    const auto op=[&](std::size_t offset,int opcode){return code[i+offset].op==opcode;};
    if(!op(0,OP_DIEU_KIEN)||!op(1,OP_TEN_BIEN_GIA_TRI)||
       !op(2,OP_BIEN_SO)||!op(3,OP_NHO_HON)||
       !op(4,OP_JUMP_IF_FALSE)||!op(5,OP_MO_KHOI)||
       !op(6,OP_TEN_BIEN_GIA_TRI)||!op(7,OP_BIEN_SO)||
       !(op(8,OP_CONG)||op(8,OP_TRU)||op(8,OP_NHAN))||
       !op(9,OP_TEN_BIEN_ID)||!op(10,OP_GAN)||
       !op(11,OP_DONG_KHOI)||!op(12,OP_CAP_NHAT)||
       !op(13,OP_TEN_BIEN_ID)||!op(14,OP_CONG_MOT)||
       !op(15,OP_JUMP))return std::nullopt;
    const int counter=code[i+1].operandIndex;
    const int value=code[i+6].operandIndex;
    if(counter<0||value<0||counter==value||
       code[i+9].operandIndex!=value||code[i+13].operandIndex!=counter||
       code[i+4].operand!=static_cast<int>(i+16)||
       code[i+15].operand!=static_cast<int>(i))return std::nullopt;
    for(std::size_t offset=1;offset<16;++offset)
        if(targets[i+offset])return std::nullopt;
    return CountedScalarLoop{counter,code[i+2].operand,value,
                             code[i+7].operand,code[i+8].op,
                             static_cast<int>(i+16)};
}

// i < bound; result = left + right; i++: promote numeric operands once in
// the runtime, then perform all iterations without repeated ABI transitions.
// Source slots may alias the result, but the induction slot must not be the
// destination. An incoming branch into the region disables the shortcut.
std::optional<NumericAddLoop> numericAddLoop(
    const std::vector<Instruction> &code, const std::vector<bool> &targets,
    std::size_t i) {
    if(i+16>code.size())return std::nullopt;
    const auto op=[&](std::size_t offset,int opcode){return code[i+offset].op==opcode;};
    if(!op(0,OP_DIEU_KIEN)||!op(1,OP_TEN_BIEN_GIA_TRI)||
       !op(2,OP_BIEN_SO)||!op(3,OP_NHO_HON)||
       !op(4,OP_JUMP_IF_FALSE)||!op(5,OP_MO_KHOI)||
       !op(6,OP_TEN_BIEN_GIA_TRI)||!op(7,OP_TEN_BIEN_GIA_TRI)||
       !op(8,OP_CONG)||!op(9,OP_TEN_BIEN_ID)||!op(10,OP_GAN)||
       !op(11,OP_DONG_KHOI)||!op(12,OP_CAP_NHAT)||
       !op(13,OP_TEN_BIEN_ID)||!op(14,OP_CONG_MOT)||
       !op(15,OP_JUMP))return std::nullopt;
    const int counter=code[i+1].operandIndex;
    const int result=code[i+9].operandIndex;
    if(counter<0||result<0||counter==result||
       code[i+6].operandIndex<0||code[i+7].operandIndex<0||
       code[i+13].operandIndex!=counter||
       code[i+4].operand!=static_cast<int>(i+16)||
       code[i+15].operand!=static_cast<int>(i))return std::nullopt;
    for(std::size_t offset=1;offset<16;++offset)
        if(targets[i+offset])return std::nullopt;
    return NumericAddLoop{counter,code[i+2].operand,
                          code[i+6].operandIndex,code[i+7].operandIndex,
                          result,static_cast<int>(i+16)};
}

// Vòng map/fold động phổ biến:
//   checksum += fn(seed + i % cycle, data) % divisor
// Callee vẫn là giá trị động. Runtime chỉ dùng đường nhanh khi giá trị đó
// trỏ đến một hàm i32 đã được compiler chứng minh và mọi kiểu đầu vào khớp.
std::optional<IndirectI32FoldLoop> indirectI32FoldLoop(
    const std::vector<Instruction> &code, const std::vector<bool> &targets,
    std::size_t i) {
    if(i+25>code.size())return std::nullopt;
    const auto op=[&](std::size_t offset,int opcode){return code[i+offset].op==opcode;};
    if(!op(0,OP_DIEU_KIEN)||!op(1,OP_TEN_BIEN_GIA_TRI)||
       !op(2,OP_TEN_BIEN_GIA_TRI)||!op(3,OP_NHO_HON)||
       !op(4,OP_JUMP_IF_FALSE)||!op(5,OP_MO_KHOI)||
       !op(6,OP_TEN_BIEN_GIA_TRI)||!op(7,OP_TEN_BIEN_GIA_TRI)||
       !op(8,OP_TEN_BIEN_GIA_TRI)||!op(9,OP_BIEN_SO)||
       !op(10,OP_MODULO)||!op(11,OP_CONG)||
       !op(12,OP_TEN_BIEN_GIA_TRI)||!op(13,OP_TEN_BIEN_GIA_TRI)||
       !op(14,OP_GOI_GIAN_TIEP)||!op(15,OP_BIEN_SO)||
       !op(16,OP_MODULO)||!op(17,OP_CONG)||
       !op(18,OP_TEN_BIEN_ID)||!op(19,OP_GAN)||
       !op(20,OP_DONG_KHOI)||!op(21,OP_CAP_NHAT)||
       !op(22,OP_TEN_BIEN_ID)||!op(23,OP_CONG_MOT)||
       !op(24,OP_JUMP))return std::nullopt;
    const int counter=code[i+1].operandIndex;
    const int bound=code[i+2].operandIndex;
    const int checksum=code[i+6].operandIndex;
    const int seed=code[i+7].operandIndex;
    const int data=code[i+12].operandIndex;
    const int callable=code[i+13].operandIndex;
    const int cycle=code[i+9].operand;
    const int divisor=code[i+15].operand;
    const int exit=static_cast<int>(i+25);
    if(counter<0||bound<0||checksum<0||seed<0||data<0||callable<0||
       cycle<=0||cycle>0xffff||divisor<=0||divisor>0xffff||
       code[i+8].operandIndex!=counter||code[i+14].operand!=2||
       code[i+18].operandIndex!=checksum||code[i+22].operandIndex!=counter||
       code[i+4].operand!=exit||code[i+24].operand!=static_cast<int>(i))
        return std::nullopt;
    for(std::size_t offset=1;offset<25;++offset)
        if(targets[i+offset])return std::nullopt;
    if(static_cast<std::size_t>(exit+1)>=code.size() ||
       code[exit].op!=OP_TEN_BIEN_GIA_TRI ||
       code[exit].operandIndex!=checksum || code[exit+1].op!=OP_TRA_VE)
        return std::nullopt;
    return IndirectI32FoldLoop{counter,bound,checksum,callable,data,seed,
                               cycle,divisor,exit};
}

// Chỉ bỏ kết quả i++ nếu sau vòng lặp chỉ còn thao tác đóng khối
// và lệnh trả về nạp giá trị riêng. Không có lệnh đọc ngăn xếp trung gian.
bool deadLoopCounters(const std::vector<Instruction> &code,
                      const std::vector<bool> &targets, int exit) {
    if (exit < 0 || static_cast<std::size_t>(exit) >= code.size()) return false;
    std::size_t i = static_cast<std::size_t>(exit);
    while (i < code.size() &&
           (code[i].op == OP_DONG_KHOI || code[i].op == OP_DONG_LENH))
        ++i;
    if (i+1 >= code.size() ||
        (code[i].op != OP_TEN_BIEN_GIA_TRI && code[i].op != OP_BIEN_SO) ||
        code[i+1].op != OP_TRA_VE) return false;
    for (std::size_t j = static_cast<std::size_t>(exit)+1; j <= i+1; ++j)
        if (targets[j]) return false;
    return true;
}

// Chỉ chuyên biệt hóa hàm thuần có tham số bắt buộc và đúng một biểu thức
// trả về i32. Lời gọi con phải trỏ đến hàm đã được chứng minh ở lượt trước,
// để tránh chu trình đệ quy và mọi lời gọi động chưa kiểm chứng.
struct StaticI32Function {
    std::vector<int> parameterSlots;
    MachineFunction machine;
};

// Metadata đủ để bỏ phân giải động cho lời gọi `mình.privateMethod(...)`.
// Method riêng tư không thể bị lớp con override theo lookup quan sát được,
// nên function id của nó là đích chính xác trong lớp đang thực thi.
struct DirectMethodTable {
    std::map<int,int> ownerByFunction;
    std::map<std::pair<int,int>,int> privateMethodByOwnerAndName;
    std::map<int,int> superclassByClass;
    std::set<std::pair<int,int>> methodByOwnerAndName;
};

int decodedSignedIndex(int value) {
    return value < 0 ? -(value + 1) : value;
}

void collectDirectMethods(const std::vector<Instruction> &code,
                          DirectMethodTable &table) {
    for(const auto &instruction:code) {
        if(instruction.op==OP_TAO_LOP) {
            table.superclassByClass[instruction.operandIndex]=
                instruction.operandValue>0 ? instruction.operandValue-1 : -1;
            continue;
        }
        if(instruction.op!=OP_THEM_PHUONG_THUC)continue;
        const int owner=decodedSignedIndex(instruction.operand);
        const bool isPrivate=instruction.operandValue<0;
        const int function=decodedSignedIndex(instruction.operandValue);
        table.ownerByFunction.emplace(function,owner);
        table.methodByOwnerAndName.emplace(owner,instruction.operandIndex);
        if(isPrivate)
            table.privateMethodByOwnerAndName[{owner,instruction.operandIndex}]=function;
    }
}

// `mình.private()` vẫn đi qua lookup động trong VM. Nếu một lớp con khai báo
// cùng tên, lookup có thể chọn method của lớp con (và sau đó kiểm tra quyền
// truy cập). Chỉ bỏ lookup khi toàn bộ cây lớp đã đóng tại AOT chứng minh rằng
// không descendant nào có thể che tên private method của lớp hiện tại.
bool privateMethodHasDescendantShadow(const DirectMethodTable &table,
                                      int owner,int name) {
    for(const auto &[candidate,parent]:table.superclassByClass) {
        if(candidate==owner ||
           table.methodByOwnerAndName.find({candidate,name})==table.methodByOwnerAndName.end())
            continue;
        int current=parent;
        std::set<int> seen;
        while(current>=0 && seen.insert(current).second) {
            if(current==owner)return true;
            const auto next=table.superclassByClass.find(current);
            if(next==table.superclassByClass.end())break;
            current=next->second;
        }
    }
    return false;
}

// Các phép so sánh trả về boolean động; đóng hộp chúng thành số nguyên
// sẽ thay đổi kiểu quan sát được của hàm gọi.
std::optional<MachineOpcode> pureI32Arithmetic(int op) {
    switch (op) {
        case OP_CONG: case OP_TRU: case OP_NHAN:
        case OP_BIT_VA: case OP_BIT_HOAC: case OP_BIT_XOR:
        case OP_DICH_TRAI: case OP_DICH_PHAI: return i32Opcode(op);
        default: return std::nullopt;
    }
}

std::optional<StaticI32Function> staticI32Function(
    const std::vector<Instruction> &code,
    const std::map<int,StaticI32Function> &proven) {
    if(code.empty() || code.front().op!=OP_MO_KHOI)return std::nullopt;
    StaticI32Function result;
    std::size_t i=1;
    // Trình biên dịch khởi tạo các ô cục bộ trước danh sách tham số.
    // Các ô này không được phép xuất hiện trong biểu thức đã chuyên biệt hóa.
    while(i<code.size() && code[i].op==OP_KHOI_TAO &&
          (i+1>=code.size() || code[i+1].op!=OP_PARAM))++i;
    while(i+1<code.size() && code[i].op==OP_KHOI_TAO &&
          code[i+1].op==OP_PARAM) {
        const int slot=code[i].operandIndex;
        if(slot<0 || code[i+1].operandIndex!=slot ||
           code[i+1].operandValue!=static_cast<int>(result.parameterSlots.size()) ||
           std::find(result.parameterSlots.begin(),result.parameterSlots.end(),slot)!=result.parameterSlots.end() ||
           result.parameterSlots.size()>=4)return std::nullopt;
        result.parameterSlots.push_back(slot);
        i+=2;
    }
    auto &fn=result.machine;
    fn.parameterCount=result.parameterSlots.size();
    fn.slotCount=fn.parameterCount;
    std::size_t depth=0;
    bool returned=false;
    for(;i<code.size();++i) {
        const auto &v=code[i];
        if(returned) {
            if(v.op!=OP_DONG_KHOI && v.op!=OP_DONG_LENH)return std::nullopt;
            continue;
        }
        if(v.op==OP_MO_KHOI || v.op==OP_DONG_KHOI || v.op==OP_DONG_LENH)continue;
        MachineInstruction op;
        op.depthBefore=depth;
        if(v.op==OP_BIEN_SO) {op.opcode=MachineOpcode::PushConstant;op.immediate=v.operand;++depth;}
        else if(v.op==OP_TEN_BIEN_GIA_TRI) {
            const auto found=std::find(result.parameterSlots.begin(),result.parameterSlots.end(),v.operandIndex);
            if(found==result.parameterSlots.end())return std::nullopt;
            op.opcode=MachineOpcode::LoadSlot;
            op.operand=static_cast<std::size_t>(found-result.parameterSlots.begin());++depth;
        } else if(const auto binary=pureI32Arithmetic(v.op);binary && depth>=2) {
            op.opcode=*binary;--depth;
        } else if(v.op==OP_GOI) {
            const auto callee=proven.find(v.operandIndex);
            if(callee==proven.end() || v.operand<0 ||
               static_cast<std::size_t>(v.operand)!=callee->second.parameterSlots.size() ||
               depth<static_cast<std::size_t>(v.operand))return std::nullopt;
            op.opcode=MachineOpcode::Call;
            op.operand=static_cast<std::size_t>(v.operand);
            op.symbol="vpp_aot_i32_"+std::to_string(v.operandIndex);
            depth=depth-op.operand+1;
        } else if(v.op==OP_TRA_VE && depth==1) {
            op.opcode=MachineOpcode::Return;--depth;returned=true;
        } else return std::nullopt;
        fn.instructions.push_back(std::move(op));
        fn.maxStackDepth=std::max(fn.maxStackDepth,depth);
        if(fn.instructions.size()>128)return std::nullopt;
    }
    if(!returned)return std::nullopt;
    return result;
}

class Assembly {
    std::ostringstream &out;
    std::string stem;
    const std::vector<Instruction> &code;
    const std::map<int,StaticI32Function> &scalarFunctions;
    const DirectMethodTable &directMethods;
    int currentFunctionId=-1;
    bool allowDirectMethods=true;
    std::set<int> receiverSlots;
    // Các biến được bind đúng một lần ở đầu hàm có vị trí ô nội tuyến ổn định.
    // Lưu trường khóa và byte offset để runtime có thể kiểm tra rồi đọc thẳng.
    std::map<int,std::pair<int,int>> inlineLocals;
    MachineFunction fn;
    std::vector<std::size_t> locations;
    struct Fixup { std::size_t instruction; int target; };
    std::vector<Fixup> fixups;
    std::vector<std::size_t> errors;
    std::vector<int> switchEnds, handlers;
    std::vector<std::size_t> returns;
    std::size_t depth=0;
    std::size_t fallbackEnd=0;
    void emit(MachineOpcode op,int immediate=0,std::size_t operand=0,std::string symbol={}) {
        fn.instructions.push_back({op,immediate,operand,depth,std::move(symbol)});
        switch(op) {
            case MachineOpcode::PushConstant: case MachineOpcode::LoadSlot: ++depth;break;
            case MachineOpcode::Call: depth=depth-operand+1;break;
            case MachineOpcode::Drop: case MachineOpcode::BranchZero: case MachineOpcode::Return: --depth;break;
            case MachineOpcode::BitAnd:
            case MachineOpcode::BitOr:
            case MachineOpcode::BitXor:
            case MachineOpcode::ShiftLeft:
            case MachineOpcode::ShiftRight:
            case MachineOpcode::Add:case MachineOpcode::Sub:case MachineOpcode::Mul:
            case MachineOpcode::Equal:case MachineOpcode::NotEqual:
            case MachineOpcode::Less:case MachineOpcode::LessEqual:
            case MachineOpcode::Greater:case MachineOpcode::GreaterEqual:--depth;break;
            default:break;
        }
        fn.maxStackDepth=std::max(fn.maxStackDepth,depth);
    }
    void rawCall(const std::string &name,std::initializer_list<int> args={}) {
        emit(MachineOpcode::LoadSlot,0,0);
        for(int value:args)emit(MachineOpcode::PushConstant,value);
        emit(MachineOpcode::Call,0,args.size()+1,"vpp_native_dynamic_"+name);
    }
    void call(const std::string &name,std::initializer_list<int> args={}) {
        // Most dynamic helpers return no language value. Their C ABI return
        // now signals failure directly: zero for success, one for error.
        // Check it with a single call, preserving the existing exception path.
        // Conditions/cases return booleans and must keep the value in slot 1.
        if(name!="condition"&&name!="case"&&name!="default") {
            rawCall(name,args);
            const auto success=fn.instructions.size();emit(MachineOpcode::BranchZero);
            errors.push_back(fn.instructions.size());emit(MachineOpcode::Jump);
            fn.instructions[success].operand=fn.instructions.size();
            return;
        }
        // Machine StoreSlot keeps the value on the evaluation stack.
        // Consume it explicitly so both sides of every error branch join at
        // the same stack depth (including backward branches in loops).
        rawCall(name,args);emit(MachineOpcode::StoreSlot,0,1);emit(MachineOpcode::Drop);
        rawCall("failed");
        const auto skip=fn.instructions.size();emit(MachineOpcode::BranchZero);
        errors.push_back(fn.instructions.size());emit(MachineOpcode::Jump);
        fn.instructions[skip].operand=fn.instructions.size();
    }

    void collectInlineLocals() {
        if(currentFunctionId<0 || code.empty())return;
        // Hàm tạo closure có thể đẩy ô nội tuyến lên heap ở giữa luồng chạy.
        // Giữ đường tối ưu bảo thủ cho tới khi có metadata capture chính xác.
        for(const auto &instruction:code)
            if(instruction.op==OP_TAO_DONG_BAO)return;

        std::map<int,int> initializeCount;
        for(const auto &instruction:code)
            if(instruction.op==OP_KHOI_TAO && instruction.operandIndex>=0)
                ++initializeCount[instruction.operandIndex];

        static constexpr int keyFields[16]={
            15,43,40,44,47,50,53,56,60,63,66,69,72,75,78,81};
        static constexpr int cellOffsets[16]={
            144,152,164,180,192,204,216,228,
            244,256,268,280,292,304,316,328};

        std::size_t i=code.front().op==OP_MO_KHOI?1:0;
        std::size_t ordinal=0;
        while(i<code.size() && ordinal<16 && code[i].op==OP_KHOI_TAO) {
            const int slot=code[i].operandIndex;
            const bool parameter=i+1<code.size() &&
                (code[i+1].op==OP_PARAM || code[i+1].op==OP_PARAM_MAC_DINH) &&
                code[i+1].operandIndex==slot;
            if(slot>=0 && initializeCount[slot]==1)
                inlineLocals.emplace(slot,
                    std::pair<int,int>{keyFields[ordinal],cellOffsets[ordinal]});
            ++ordinal;
            i+=parameter?2:1;
        }
    }

    // Gộp receiver + tối đa hai đối số đơn giản + private method thành một
    // ABI call. Kiểu đối số dùng hai bit: 0=i32, 1=slot, 2=StringPool.
    std::size_t directPrivateMethodCall(std::size_t start,
                                        const std::vector<bool> &targets) {
        if(!allowDirectMethods || currentFunctionId<0 || start>=code.size())return 0;
        const auto owner=directMethods.ownerByFunction.find(currentFunctionId);
        if(owner==directMethods.ownerByFunction.end())return 0;
        const auto &receiver=code[start];
        if(receiver.op!=OP_TEN_BIEN_GIA_TRI ||
           receiverSlots.find(receiver.operandIndex)==receiverSlots.end())return 0;
        for(int count=0;count<=2;++count) {
            const std::size_t callIndex=start+1+static_cast<std::size_t>(count);
            if(callIndex>=code.size())break;
            bool contiguous=true;
            for(std::size_t j=start+1;j<=callIndex;++j)
                if(targets[j]){contiguous=false;break;}
            if(!contiguous)continue;
            const auto &invoke=code[callIndex];
            if(invoke.op!=OP_GOI_PHUONG_THUC || invoke.operand!=count ||
               invoke.operandValue!=0)continue;
            const auto target=directMethods.privateMethodByOwnerAndName.find(
                {owner->second,invoke.operandIndex});
            if(target==directMethods.privateMethodByOwnerAndName.end())continue;
            if(privateMethodHasDescendantShadow(
                   directMethods,owner->second,invoke.operandIndex))continue;
            int values[2]={0,0};
            int kinds=0;
            bool simple=true;
            for(int n=0;n<count;++n) {
                const auto &argument=code[start+1+static_cast<std::size_t>(n)];
                int kind=0;
                if(argument.op==OP_BIEN_SO)values[n]=argument.operand;
                else if(argument.op==OP_TEN_BIEN_GIA_TRI) {
                    kind=1;values[n]=argument.operandIndex;
                } else if(argument.op==OP_CHUOI) {
                    kind=2;values[n]=argument.operandIndex;
                } else {simple=false;break;}
                kinds|=kind<<(n*2);
            }
            if(!simple)continue;
            call("method_direct_small",{target->second,receiver.operandIndex,count,
                                         values[0],values[1],kinds});
            for(std::size_t j=start+1;j<=callIndex;++j)locations[j]=locations[start];
            return callIndex-start+1;
        }
        return 0;
    }
    void jump(int target,std::size_t from) {
        if(target<0||static_cast<std::size_t>(target)>code.size())throw std::runtime_error("AOT: invalid branch target");
        if(static_cast<std::size_t>(target)<=from)call("safepoint");
        fixups.push_back({fn.instructions.size(),target});emit(MachineOpcode::Jump);
    }
    void conditional(int target) {
        emit(MachineOpcode::LoadSlot,0,1);
        fixups.push_back({fn.instructions.size(),target});emit(MachineOpcode::BranchZero);
    }
    int continueTarget(std::size_t from) const {
        int depth=0;
        for(auto i=from+1;i<code.size();++i) {
            if(code[i].op==OP_LAP)++depth;
            if(code[i].op==OP_CAP_NHAT){if(!depth)return static_cast<int>(i);--depth;}
        }
        throw std::runtime_error("AOT: continue outside loop");
    }
    void i32Expression(std::size_t start,std::size_t end) {
        std::map<int,std::size_t> slots;
        std::vector<int> sources;
        for(auto j=start;j<end;++j)if(code[j].op==OP_TEN_BIEN_GIA_TRI) {
            const int slot=code[j].operandIndex;
            if(slots.emplace(slot,sources.size()+2).second)sources.push_back(slot);
        }
        fn.slotCount=std::max(fn.slotCount,sources.size()+2);
        std::vector<std::size_t> fallback;
        for(int slot:sources) {
            rawCall("is_i32",{slot});
            fallback.push_back(fn.instructions.size());emit(MachineOpcode::BranchZero);
            rawCall("read_i32",{slot});
            emit(MachineOpcode::StoreSlot,0,slots.at(slot));emit(MachineOpcode::Drop);
        }
        emit(MachineOpcode::LoadSlot,0,0);
        for(auto j=start;j<end;++j) {
            const auto &v=code[j];
            if(v.op==OP_BIEN_SO)emit(MachineOpcode::PushConstant,v.operand);
            else if(v.op==OP_TEN_BIEN_GIA_TRI)emit(MachineOpcode::LoadSlot,0,slots.at(v.operandIndex));
            else emit(*i32Opcode(v.op));
        }
        emit(MachineOpcode::Call,0,2,"vpp_native_dynamic_integer");
        const auto success=fn.instructions.size();emit(MachineOpcode::BranchZero);
        errors.push_back(fn.instructions.size());emit(MachineOpcode::Jump);
        fn.instructions[success].operand=fn.instructions.size();
        fixups.push_back({fn.instructions.size(),static_cast<int>(end)});emit(MachineOpcode::Jump);
        for(auto branch:fallback)fn.instructions[branch].operand=fn.instructions.size();
        fallbackEnd=end;
    }
    std::size_t directI32Call(std::size_t start,const std::vector<bool> &targets) {
        std::size_t end=start;
        while(end<code.size() && end-start<4 &&
              (code[end].op==OP_BIEN_SO || code[end].op==OP_TEN_BIEN_GIA_TRI)) {
            if(end>start && targets[end])return 0;
            ++end;
        }
        if(end>=code.size() || code[end].op!=OP_GOI ||
           code[end].operand!=static_cast<int>(end-start) ||
           (end>start && targets[end]))return 0;
        const auto found=scalarFunctions.find(code[end].operandIndex);
        if(found==scalarFunctions.end() ||
           found->second.parameterSlots.size()!=end-start)return 0;
        // Không nạp/tiêu thụ đối số trước khi mọi guard thành công.
        // Nhánh chậm dùng đúng lệnh gọi ban đầu và không bị mất dữ liệu.
        std::vector<std::size_t> fallback;
        std::set<int> checkedSlots;
        for(std::size_t j=start;j<end;++j)if(code[j].op==OP_TEN_BIEN_GIA_TRI) {
            if(code[j].operandIndex<0)return 0;
            if(!checkedSlots.insert(code[j].operandIndex).second)continue;
            rawCall("is_i32",{code[j].operandIndex});
            fallback.push_back(fn.instructions.size());emit(MachineOpcode::BranchZero);
        }
        for(std::size_t j=start;j<end;++j) {
            const auto &v=code[j];
            if(v.op==OP_BIEN_SO)emit(MachineOpcode::PushConstant,v.operand);
            else rawCall("read_i32",{v.operandIndex});
        }
        emit(MachineOpcode::Call,0,end-start,
             "vpp_aot_i32_"+std::to_string(code[end].operandIndex));
        // Lời gọi i32 chỉ nhận W0..W3 là đối số, không nhận mã khung.
        // Lưu kết quả tạm rồi đóng hộp qua ABI động (frame, value).
        fn.slotCount=std::max<std::size_t>(fn.slotCount,3);
        emit(MachineOpcode::StoreSlot,0,2);emit(MachineOpcode::Drop);
        emit(MachineOpcode::LoadSlot,0,0);
        emit(MachineOpcode::LoadSlot,0,2);
        emit(MachineOpcode::Call,0,2,"vpp_native_dynamic_integer");
        const auto success=fn.instructions.size();emit(MachineOpcode::BranchZero);
        errors.push_back(fn.instructions.size());emit(MachineOpcode::Jump);
        fn.instructions[success].operand=fn.instructions.size();
        if(!fallback.empty()) {
            const auto skip=fn.instructions.size();emit(MachineOpcode::Jump);
            const auto slow=fn.instructions.size();
            for(const auto branch:fallback)fn.instructions[branch].operand=slow;
            const int target=code[end].operandIndex;
            const int count=static_cast<int>(end-start);
            if(count<=2) {
                int args[2]={},mask=0;
                for(int n=0;n<count;++n) {
                    const auto &v=code[start+n];
                    if(v.op==OP_TEN_BIEN_GIA_TRI)mask|=1<<n;
                    args[n]=v.op==OP_BIEN_SO?v.operand:v.operandIndex;
                }
                call("call_small",{target,count,args[0],args[1],mask});
            } else {
                int args[4]={},mask=0;
                for(int n=0;n<count;++n) {
                    const auto &v=code[start+n];
                    if(v.op==OP_TEN_BIEN_GIA_TRI)mask|=1<<n;
                    args[n]=v.op==OP_BIEN_SO?v.operand:v.operandIndex;
                }
                call("call_quad",{target,count,args[0],args[1],args[2],args[3],mask});
            }
            fn.instructions[skip].operand=fn.instructions.size();
        }
        for(std::size_t j=start+1;j<=end;++j)locations[j]=locations[start];
        return end-start+1;
    }
    // Nhận biểu thức hậu tố i32, kể cả lời gọi lồng nhau đến các hàm thuần
    // đã chứng minh. Giữ bytecode gốc cho nhánh chậm: số thực và lỗi khi
    // đọc biến vẫn đi qua runtime với đúng thứ tự đánh giá.
    std::size_t expressionI32Call(std::size_t start,const std::vector<bool> &targets) {
        if(start>=code.size())return 0;
        const auto &first=code[start];
        const auto firstCall=scalarFunctions.find(first.operandIndex);
        if(first.op!=OP_BIEN_SO && first.op!=OP_TEN_BIEN_GIA_TRI &&
           !(first.op==OP_GOI && first.operand==0 && firstCall!=scalarFunctions.end() &&
             firstCall->second.parameterSlots.empty()))return 0;
        std::size_t depth=0,operations=0,invokeIndex=code.size();
        const bool trace=std::getenv("VPP_AOT_I32_TRACE")!=nullptr && start==5;
        for(std::size_t j=start;j<code.size() && j-start<24;++j) {
            if(trace)std::cerr<<"TRACE i="<<start<<" j="<<j<<" op="<<code[j].op<<" argc="<<code[j].operand<<" id="<<code[j].operandIndex<<" depth="<<depth<<" ops="<<operations<<" target="<<targets[j]<<"\n";
            if(j>start && targets[j])break;
            const auto &v=code[j];
            if(v.op==OP_BIEN_SO || (v.op==OP_TEN_BIEN_GIA_TRI && v.operandIndex>=0))++depth;
            else if(pureI32Arithmetic(v.op) && depth>=2) {--depth;++operations;}
            else if(v.op==OP_GOI && v.operand>=0 && v.operand<=4) {
                const auto callee=scalarFunctions.find(v.operandIndex);
                if(callee==scalarFunctions.end() ||
                   callee->second.parameterSlots.size()!=static_cast<std::size_t>(v.operand) ||
                   depth<static_cast<std::size_t>(v.operand))break;
                // Đối số đã chiếm trọn ngăn xếp của đoạn hậu tố: đây có
                // thể là lời gọi ngoài cùng. Lưu ứng viên cuối cùng để
                // tiếp tục nhận diện khi còn một lời gọi cha bao quanh.
                if(depth==static_cast<std::size_t>(v.operand) && operations>0)
                    invokeIndex=j;
                depth=depth-static_cast<std::size_t>(v.operand)+1;
                ++operations;
            }
            else break;
            if(depth>8)return 0;
        }
        if(trace)std::cerr<<"TRACE end="<<invokeIndex<<"\n";
        if(invokeIndex==code.size())return 0;
        const std::size_t end=invokeIndex;
        const auto &invoke=code[end];
        if(invoke.op!=OP_GOI || invoke.operand<0 || invoke.operand>4)return 0;
        const auto found=scalarFunctions.find(invoke.operandIndex);
        if(found==scalarFunctions.end() ||
           found->second.parameterSlots.size()!=static_cast<std::size_t>(invoke.operand))return 0;

        std::vector<int> slots;
        std::vector<std::size_t> fallback;
        for(std::size_t i=start;i<end;++i)if(code[i].op==OP_TEN_BIEN_GIA_TRI &&
           std::find(slots.begin(),slots.end(),code[i].operandIndex)==slots.end())
            slots.push_back(code[i].operandIndex);
        // Mỗi ô chỉ được kiểm tra và đọc một lần. Các toán hạng lặp lại
        // dùng ô máy tạm, không tạo thêm giá trị động hay lời gọi ABI.
        std::map<int,std::size_t> cached;
        for(std::size_t index=0;index<slots.size();++index)
            cached.emplace(slots[index],index+2);
        // Giữ riêng ô kết quả: ô 2 trở đi đang lưu các toán hạng đã đọc.
        // Không ghi đè toán hạng trước khi hoàn tất việc đóng hộp kết quả.
        const std::size_t resultSlot=slots.size()+2;
        fn.slotCount=std::max(fn.slotCount,resultSlot+1);
        for(const int slot:slots) {
            rawCall("is_i32",{slot});
            fallback.push_back(fn.instructions.size());emit(MachineOpcode::BranchZero);
        }
        for(const int slot:slots) {
            rawCall("read_i32",{slot});
            emit(MachineOpcode::StoreSlot,0,cached.at(slot));emit(MachineOpcode::Drop);
        }
        for(std::size_t i=start;i<end;++i) {
            const auto &v=code[i];
            if(v.op==OP_BIEN_SO)emit(MachineOpcode::PushConstant,v.operand);
            else if(v.op==OP_TEN_BIEN_GIA_TRI)emit(MachineOpcode::LoadSlot,0,cached.at(v.operandIndex));
            else if(v.op==OP_GOI)
                emit(MachineOpcode::Call,0,static_cast<std::size_t>(v.operand),
                     "vpp_aot_i32_"+std::to_string(v.operandIndex));
            else emit(*pureI32Arithmetic(v.op));
        }
        emit(MachineOpcode::Call,0,static_cast<std::size_t>(invoke.operand),
             "vpp_aot_i32_"+std::to_string(invoke.operandIndex));
        emit(MachineOpcode::StoreSlot,0,resultSlot);emit(MachineOpcode::Drop);
        emit(MachineOpcode::LoadSlot,0,0);emit(MachineOpcode::LoadSlot,0,resultSlot);
        emit(MachineOpcode::Call,0,2,"vpp_native_dynamic_integer");
        const auto success=fn.instructions.size();emit(MachineOpcode::BranchZero);
        errors.push_back(fn.instructions.size());emit(MachineOpcode::Jump);
        fn.instructions[success].operand=fn.instructions.size();
        const auto skip=fn.instructions.size();emit(MachineOpcode::Jump);
        const auto slow=fn.instructions.size();
        for(const auto branch:fallback)fn.instructions[branch].operand=slow;
        for(std::size_t i=start;i<end;++i) {
            const auto &v=code[i];
            if(v.op==OP_BIEN_SO)call("integer",{v.operand});
            else if(v.op==OP_TEN_BIEN_GIA_TRI)call("load",{v.operandIndex});
            else if(v.op==OP_GOI)call("call",{v.operandIndex,v.operand});
            else call("binary",{v.op});
        }
        call("call",{invoke.operandIndex,invoke.operand});
        fn.instructions[skip].operand=fn.instructions.size();
        for(std::size_t i=start+1;i<=end;++i)locations[i]=locations[start];
        return end-start+1;
    }
public:
    Assembly(std::ostringstream &stream,std::string symbol,const std::vector<Instruction> &instructions,
             const std::map<int,StaticI32Function> &specialized,
             const DirectMethodTable &methods,int functionId=-1,
             bool enableDirectMethods=true)
        :out(stream),stem(std::move(symbol)),code(instructions),scalarFunctions(specialized),
         directMethods(methods),currentFunctionId(functionId),
         allowDirectMethods(enableDirectMethods) {
        fn.parameterCount=1;fn.slotCount=2;
        for(const auto &instruction:code)
            if(instruction.op==OP_PARAM && instruction.operandValue==-1 &&
               instruction.operandIndex>=0)
                receiverSlots.insert(instruction.operandIndex);
        collectInlineLocals();
    }
    void emit() {
        locations.resize(code.size()+1);
        // AOT peepholes cannot cross an incoming bytecode edge. Build the
        // destination bitmap once; scanning it per instruction is quadratic.
        std::vector<bool> branchTargets(code.size()+1,false);
        const auto mark=[&](int target) {
            if(target>=0 && static_cast<std::size_t>(target)<branchTargets.size())
                branchTargets[static_cast<std::size_t>(target)]=true;
        };
        for(std::size_t j=0;j<code.size();++j) {
            const auto &inst=code[j];
            switch(inst.op) {
                case OP_JUMP:case OP_JUMP_IF_FALSE:case OP_THOAT:
                case OP_THU:case OP_THU_KET_THUC:case OP_CHON:mark(inst.operand);break;
                case OP_CA:case OP_MAC_DINH:mark(inst.operandValue);break;
                case OP_BO_QUA:mark(continueTarget(j));break;
                default:break;
            }
            if(inst.op==OP_BAT_LOI)branchTargets[j]=true;
        }
        for(std::size_t i=0;i<code.size();++i) {
            locations[i]=fn.instructions.size();const auto &v=code[i];
            if(const auto consumed=directPrivateMethodCall(i,branchTargets)) {
                i+=consumed-1;
                continue;
            }
            // Thử gộp cả chuỗi lời gọi lồng nhau trước khi chọn một lời
            // gọi đơn lẻ (kể cả hàm không đối số) làm đường nhanh.
            if(const auto consumed=expressionI32Call(i,branchTargets)) {
                i+=consumed-1;
                continue;
            }
            // Lời gọi thuần nhận tối đa bốn đối số đơn giản, không có nhánh
            // đi vào giữa vùng được gộp. Giữ nguyên đường động khi đổi kiểu.
            if(const auto consumed=directI32Call(i,branchTargets)) {
                i+=consumed-1;
                continue;
            }
            // fn(...): nếu callee là một ô biến đứng ngay trước lời gọi gián
            // tiếp, runtime có thể đọc thẳng ô đó. Các đối số đã nằm trên
            // stack theo đúng thứ tự đánh giá; chỉ bỏ lần load/push rồi pop
            // lại chính callee. Không gộp khi có cạnh nhảy vào lệnh gọi.
            if(v.op==OP_TEN_BIEN_GIA_TRI && i+1<code.size() &&
               code[i+1].op==OP_GOI_GIAN_TIEP && !branchTargets[i+1]) {
                call("indirect_slot",{v.operandIndex,code[i+1].operand});
                locations[i+1]=locations[i];
                ++i;
                continue;
            }
            if(i>=fallbackEnd && (v.op==OP_BIEN_SO || v.op==OP_TEN_BIEN_GIA_TRI)) {
                const auto end=i32ExpressionEnd(code,branchTargets,i);
                if(end>i)i32Expression(i,end);
            }
            if(const auto fold=indirectI32FoldLoop(code,branchTargets,i)) {
                const int packed=(fold->cycle<<16)|fold->divisor;
                rawCall("indirect_i32_fold_loop",
                        {fold->counter,fold->bound,fold->checksum,fold->callable,
                         fold->data,fold->seed,packed});
                emit(MachineOpcode::StoreSlot,0,1);emit(MachineOpcode::Drop);
                emit(MachineOpcode::LoadSlot,0,1);emit(MachineOpcode::PushConstant,2);
                emit(MachineOpcode::Equal);
                const auto valid=fn.instructions.size();emit(MachineOpcode::BranchZero);
                errors.push_back(fn.instructions.size());emit(MachineOpcode::Jump);
                fn.instructions[valid].operand=fn.instructions.size();
                emit(MachineOpcode::LoadSlot,0,1);
                fixups.push_back({fn.instructions.size(),fold->exit});
                emit(MachineOpcode::BranchZero);
                // Khi guard thất bại, mọi cạnh lùi phải vào bytecode gốc và
                // không thử lại specialization ở mỗi lượt.
                locations[i]=fn.instructions.size();
            }
            const auto loop=countedScalarLoop(code,branchTargets,i);
            const auto simple=loop?std::optional<CountedScalarLoop>{}:
                                    simpleScalarLoop(code,branchTargets,i);
            const auto numeric=(loop||simple)?std::optional<NumericAddLoop>{}:
                                              numericAddLoop(code,branchTargets,i);
            if (loop||simple||numeric) {
                // One C ABI transition, then scalar CPU arithmetic in the
                // runtime's registers; no boxed values in the loop body.
                // ABI return: 0=optimized, 1=deopt, 2=pending exception.
                const int loopExit=numeric?numeric->exit:(loop?loop->exit:simple->exit);
                const bool discard=deadLoopCounters(code,branchTargets,loopExit);
                if(numeric)
                    rawCall(discard?"numeric_add_loop_tail":"numeric_add_loop",
                            {numeric->counter,numeric->bound,
                            numeric->left,numeric->right,numeric->result});
                else {
                    const auto &region=loop?*loop:*simple;
                    rawCall(loop?(discard?"counted_scalar_loop_tail":"counted_scalar_loop"):
                                 (discard?"simple_scalar_loop_tail":"simple_scalar_loop"),
                            {region.counter,region.bound,region.value,
                             region.step,region.threshold});
                }
                emit(MachineOpcode::StoreSlot,0,1);emit(MachineOpcode::Drop);
                emit(MachineOpcode::LoadSlot,0,1);emit(MachineOpcode::PushConstant,2);
                emit(MachineOpcode::Equal);
                const auto valid=fn.instructions.size();emit(MachineOpcode::BranchZero);
                errors.push_back(fn.instructions.size());emit(MachineOpcode::Jump);
                fn.instructions[valid].operand=fn.instructions.size();
                emit(MachineOpcode::LoadSlot,0,1);
                fixups.push_back({fn.instructions.size(),loopExit});
                emit(MachineOpcode::BranchZero);
                // Back edges must go directly to the original slow header,
                // so an unsupported type doesn't retry the guard 256 times.
                locations[i]=fn.instructions.size();
            }
            // Compile-time i32/bool folding avoids three dynamic ABI calls
            // and all temporary tagged values. Never cross incoming edges:
            // even a pure expression can be a control-flow destination.
            if(v.op==OP_BIEN_SO && i+2<code.size() &&
               code[i+1].op==OP_BIEN_SO &&
               !branchTargets[i+1] && !branchTargets[i+2]) {
                std::int32_t result=0;
                if(foldI32(code[i+2].op,v.operand,code[i+1].operand,result)) {
                    call("integer",{result});
                    locations[i+1]=locations[i];locations[i+2]=locations[i];
                    i+=2;continue;
                }
            }
            // A known integer assignment has no observable intermediate
            // values. Preserve branches into either instruction as barriers.
            if(v.op==OP_BIEN_SO && i+2<code.size() &&
               code[i+1].op==OP_TEN_BIEN_ID && code[i+2].op==OP_GAN &&
               !branchTargets[i+1] && !branchTargets[i+2]) {
                call("store_integer",{code[i+1].operandIndex,v.operand});
                locations[i+1]=locations[i];locations[i+2]=locations[i];
                i+=2;continue;
            }
            // 0 - x; trả về: bỏ giá trị tạm và phép trừ động.
            if(v.op==OP_BIEN_SO && v.operand==0 && i+3<code.size() &&
               code[i+1].op==OP_TEN_BIEN_GIA_TRI && code[i+2].op==OP_TRU &&
               code[i+3].op==OP_TRA_VE && !branchTargets[i+1] &&
               !branchTargets[i+2] && !branchTargets[i+3]) {
                call("return_negated_slot",{code[i+1].operandIndex});
                returns.push_back(fn.instructions.size());emit(MachineOpcode::Jump);
                for(std::size_t k=1;k<=3;++k)locations[i+k]=locations[i];
                i+=3;continue;
            }
            // Return a known scalar directly without materializing an
            // operand-stack value or calling the dynamic load helper.
            if((v.op==OP_BIEN_SO || v.op==OP_TEN_BIEN_GIA_TRI) &&
               i+1<code.size() && code[i+1].op==OP_TRA_VE &&
               !branchTargets[i+1]) {
                call(v.op==OP_BIEN_SO?"return_integer":"return_slot",
                     {v.op==OP_BIEN_SO?v.operand:v.operandIndex});
                returns.push_back(fn.instructions.size());emit(MachineOpcode::Jump);
                locations[i+1]=locations[i];++i;continue;
            }
            // A contiguous load/immediate/op/assign sequence has no
            // observable intermediate values. Fold it to one checked native
            // call, retaining dynamic type fallback in the callee. Do not
            // fold across jump destinations or exception handler entries.
            if(v.op==OP_TEN_BIEN_GIA_TRI && i+4<code.size() &&
               code[i+1].op==OP_BIEN_SO &&
               (code[i+2].op==OP_CONG || code[i+2].op==OP_TRU || code[i+2].op==OP_NHAN) &&
               code[i+3].op==OP_TEN_BIEN_ID && code[i+4].op==OP_GAN &&
               !branchTargets[i+1] && !branchTargets[i+2] &&
               !branchTargets[i+3] && !branchTargets[i+4]) {
                call("update_integer_slot",{v.operandIndex,code[i+1].operand,
                    code[i+2].op,code[i+3].operandIndex});
                for(std::size_t k=1;k<=4;++k)locations[i+k]=locations[i];
                i+=4;continue;
            }
            // x = a (+,-,*) b: avoid five runtime calls and temporary boxed
            // stack values, with a checked dynamic fallback for non-numbers.
            if(v.op==OP_TEN_BIEN_GIA_TRI && i+4<code.size() &&
               code[i+1].op==OP_TEN_BIEN_GIA_TRI &&
               (code[i+2].op==OP_CONG || code[i+2].op==OP_TRU || code[i+2].op==OP_NHAN) &&
               code[i+3].op==OP_TEN_BIEN_ID && code[i+4].op==OP_GAN &&
               !branchTargets[i+1] && !branchTargets[i+2] &&
               !branchTargets[i+3] && !branchTargets[i+4]) {
                call("update_numeric_slots",{v.operandIndex,code[i+1].operandIndex,
                    code[i+2].op,code[i+3].operandIndex});
                for(std::size_t k=1;k<=4;++k)locations[i+k]=locations[i];
                i+=4;continue;
            }
            if(v.op==OP_TEN_BIEN_ID && i+1<code.size() &&
               code[i+1].op==OP_GAN && !branchTargets[i+1]) {
                call("assign_slot",{v.operandIndex});
                locations[i+1]=locations[i];++i;continue;
            }
            // x = container[index]: giữ container/index trong các ô nguồn và
            // ghi thẳng kết quả vào ô đích. Runtime có thể giữ phần tử i32 ở
            // dạng thô, tránh push/pop giá trị động và lời gọi assign riêng.
            if(v.op==OP_TEN_BIEN_GIA_TRI && i+4<code.size() &&
               (code[i+1].op==OP_BIEN_SO ||
                code[i+1].op==OP_TEN_BIEN_GIA_TRI) &&
               code[i+2].op==OP_DOC_CHI_SO &&
               code[i+3].op==OP_TEN_BIEN_ID &&
               code[i+4].op==OP_GAN &&
               !branchTargets[i+1] && !branchTargets[i+2] &&
               !branchTargets[i+3] && !branchTargets[i+4]) {
                const auto &index=code[i+1];
                call("index_store_slot",
                     {v.operandIndex,
                      index.op==OP_TEN_BIEN_GIA_TRI?index.operandIndex:index.operand,
                      index.op==OP_TEN_BIEN_GIA_TRI?1:0,
                      code[i+3].operandIndex});
                for(std::size_t k=1;k<=4;++k)locations[i+k]=locations[i];
                i+=4;continue;
            }
            // container[index] = source op hằng: giữ phép tính i32 trong
            // thanh ghi và bỏ các giá trị tạm trên stack ở cập nhật trạng thái.
            if(v.op==OP_TEN_BIEN_GIA_TRI && i+5<code.size() &&
               code[i+1].op==OP_BIEN_SO &&
               code[i+2].op==OP_TEN_BIEN_GIA_TRI && code[i+3].op==OP_BIEN_SO &&
               (code[i+4].op==OP_CONG || code[i+4].op==OP_TRU || code[i+4].op==OP_NHAN) &&
               code[i+5].op==OP_GAN_CHI_SO &&
               !branchTargets[i+1] && !branchTargets[i+2] && !branchTargets[i+3] &&
               !branchTargets[i+4] && !branchTargets[i+5]) {
                call("index_store_integer_op",
                     {v.operandIndex,code[i+1].operand,code[i+2].operandIndex,
                      code[i+3].operand,code[i+4].op});
                for(std::size_t k=1;k<=5;++k)locations[i+k]=locations[i];
                i+=5;continue;
            }
            // Cùng một phần tử được kiểm tra < low || > high. Đọc phần tử
            // trực tiếp một lần ở đường i32 và giữ fallback eager khi cần.
            if(v.op==OP_TEN_BIEN_GIA_TRI && i+11<code.size() &&
               (code[i+1].op==OP_BIEN_SO || code[i+1].op==OP_TEN_BIEN_GIA_TRI) &&
               code[i+2].op==OP_DOC_CHI_SO && code[i+3].op==OP_BIEN_SO &&
               code[i+4].op==OP_NHO_HON &&
               code[i+5].op==OP_TEN_BIEN_GIA_TRI &&
               code[i+5].operandIndex==v.operandIndex &&
               code[i+6].op==code[i+1].op &&
               (code[i+1].op==OP_BIEN_SO ? code[i+6].operand==code[i+1].operand
                                         : code[i+6].operandIndex==code[i+1].operandIndex) &&
               code[i+7].op==OP_DOC_CHI_SO && code[i+8].op==OP_BIEN_SO &&
               code[i+9].op==OP_LON_HON && code[i+10].op==OP_Logic_HOAC &&
               code[i+11].op==OP_JUMP_IF_FALSE) {
                bool contiguous=true;
                for(std::size_t k=1;k<=11;++k)if(branchTargets[i+k]){contiguous=false;break;}
                if(contiguous) {
                    const auto &index=code[i+1];
                    rawCall("index_compare_integer_outside_range",
                            {v.operandIndex,
                             index.op==OP_TEN_BIEN_GIA_TRI?index.operandIndex:index.operand,
                             index.op==OP_TEN_BIEN_GIA_TRI?1:0,
                             code[i+3].operand,code[i+8].operand});
                    emit(MachineOpcode::StoreSlot,0,1);emit(MachineOpcode::Drop);
                    emit(MachineOpcode::LoadSlot,0,1);emit(MachineOpcode::PushConstant,2);
                    emit(MachineOpcode::Equal);
                    const auto good=fn.instructions.size();emit(MachineOpcode::BranchZero);
                    errors.push_back(fn.instructions.size());emit(MachineOpcode::Jump);
                    fn.instructions[good].operand=fn.instructions.size();
                    conditional(code[i+11].operand);
                    for(std::size_t k=1;k<=11;++k)locations[i+k]=locations[i];
                    i+=11;continue;
                }
            }
            // Cùng một phần tử được so với hai hằng số bằng == và ||.
            if(v.op==OP_TEN_BIEN_GIA_TRI && i+11<code.size() &&
               (code[i+1].op==OP_BIEN_SO || code[i+1].op==OP_TEN_BIEN_GIA_TRI) &&
               code[i+2].op==OP_DOC_CHI_SO && code[i+3].op==OP_BIEN_SO &&
               code[i+4].op==OP_SO_SANH_BANG &&
               code[i+5].op==OP_TEN_BIEN_GIA_TRI &&
               code[i+5].operandIndex==v.operandIndex &&
               code[i+6].op==code[i+1].op &&
               (code[i+1].op==OP_BIEN_SO ? code[i+6].operand==code[i+1].operand
                                         : code[i+6].operandIndex==code[i+1].operandIndex) &&
               code[i+7].op==OP_DOC_CHI_SO && code[i+8].op==OP_BIEN_SO &&
               code[i+9].op==OP_SO_SANH_BANG && code[i+10].op==OP_Logic_HOAC &&
               code[i+11].op==OP_JUMP_IF_FALSE) {
                bool contiguous=true;
                for(std::size_t k=1;k<=11;++k)if(branchTargets[i+k]){contiguous=false;break;}
                if(contiguous) {
                    const auto &index=code[i+1];
                    rawCall("index_compare_integer_pair",
                            {v.operandIndex,
                             index.op==OP_TEN_BIEN_GIA_TRI?index.operandIndex:index.operand,
                             index.op==OP_TEN_BIEN_GIA_TRI?1:0,
                             code[i+3].operand,code[i+8].operand});
                    emit(MachineOpcode::StoreSlot,0,1);emit(MachineOpcode::Drop);
                    emit(MachineOpcode::LoadSlot,0,1);emit(MachineOpcode::PushConstant,2);
                    emit(MachineOpcode::Equal);
                    const auto good=fn.instructions.size();emit(MachineOpcode::BranchZero);
                    errors.push_back(fn.instructions.size());emit(MachineOpcode::Jump);
                    fn.instructions[good].operand=fn.instructions.size();
                    conditional(code[i+11].operand);
                    for(std::size_t k=1;k<=11;++k)locations[i+k]=locations[i];
                    i+=11;continue;
                }
            }
            // container[index] <op> immediate in a branch is common in byte
            // parsers. Keep the container and index in their slots and let
            // the runtime compare the indexed i32 directly.
            if(v.op==OP_TEN_BIEN_GIA_TRI && i+5<code.size() &&
               (code[i+1].op==OP_BIEN_SO || code[i+1].op==OP_TEN_BIEN_GIA_TRI) &&
               code[i+2].op==OP_DOC_CHI_SO && code[i+3].op==OP_BIEN_SO &&
               (code[i+4].op==OP_SO_SANH_BANG || code[i+4].op==OP_KHAC_BANG ||
                code[i+4].op==OP_LON_HON || code[i+4].op==OP_NHO_HON ||
                code[i+4].op==OP_LON_HON_HOAC_BANG || code[i+4].op==OP_NHO_HON_HOAC_BANG) &&
               code[i+5].op==OP_JUMP_IF_FALSE &&
               !branchTargets[i+1] && !branchTargets[i+2] && !branchTargets[i+3] &&
               !branchTargets[i+4] && !branchTargets[i+5]) {
                const auto &index=code[i+1];
                rawCall("index_compare_integer",
                        {v.operandIndex,
                         index.op==OP_TEN_BIEN_GIA_TRI?index.operandIndex:index.operand,
                         index.op==OP_TEN_BIEN_GIA_TRI?1:0,
                         code[i+3].operand,code[i+4].op});
                emit(MachineOpcode::StoreSlot,0,1);emit(MachineOpcode::Drop);
                emit(MachineOpcode::LoadSlot,0,1);emit(MachineOpcode::PushConstant,2);
                emit(MachineOpcode::Equal);
                const auto good=fn.instructions.size();emit(MachineOpcode::BranchZero);
                errors.push_back(fn.instructions.size());emit(MachineOpcode::Jump);
                fn.instructions[good].operand=fn.instructions.size();
                conditional(code[i+5].operand);
                for(std::size_t k=1;k<=5;++k)locations[i+k]=locations[i];
                i+=5;continue;
            }
            if(v.op==OP_TEN_BIEN_ID && i+1<code.size() &&
               (code[i+1].op==OP_CONG_MOT || code[i+1].op==OP_TRU_MOT) &&
               !branchTargets[i+1]) {
                call("increment_slot",{v.operandIndex,
                    code[i+1].op==OP_CONG_MOT?1:-1});
                locations[i+1]=locations[i];++i;continue;
            }
            // Truyền trực tiếp tối đa bốn đối số đơn giản bằng thanh ghi.
            // Bỏ các thao tác tạo số động, đẩy/lấy ngăn xếp và cấp phát
            // danh sách đối số. Không gộp qua đích nhảy hoặc điểm bắt lỗi.
            const auto simpleArg=[](const Instruction &arg) {
                return arg.op==OP_BIEN_SO || arg.op==OP_TEN_BIEN_GIA_TRI;
            };
            if(v.op==OP_GOI && v.operand==0) {
                call("call_small",{v.operandIndex,0,0,0,0});
                continue;
            }
            if(simpleArg(v) && i+1<code.size() &&
               code[i+1].op==OP_GOI && code[i+1].operand==1 &&
               !branchTargets[i+1]) {
                const int mask=v.op==OP_TEN_BIEN_GIA_TRI?1:0;
                call("call_small",{code[i+1].operandIndex,1,
                                   mask?v.operandIndex:v.operand,0,mask});
                locations[i+1]=locations[i];++i;continue;
            }
            if(simpleArg(v) && i+2<code.size() &&
               simpleArg(code[i+1]) && code[i+2].op==OP_GOI &&
               code[i+2].operand==2 &&
               !branchTargets[i+1] && !branchTargets[i+2]) {
                const auto &second=code[i+1];
                const int mask=(v.op==OP_TEN_BIEN_GIA_TRI?1:0) |
                               (second.op==OP_TEN_BIEN_GIA_TRI?2:0);
                call("call_small",{code[i+2].operandIndex,2,
                                   (mask&1)?v.operandIndex:v.operand,
                                   (mask&2)?second.operandIndex:second.operand,
                                   mask});
                locations[i+1]=locations[i];locations[i+2]=locations[i];
                i+=2;continue;
            }
            if(simpleArg(v) && i+3<code.size() &&
               simpleArg(code[i+1]) && simpleArg(code[i+2]) &&
               code[i+3].op==OP_GOI && code[i+3].operand==3 &&
               !branchTargets[i+1] && !branchTargets[i+2] && !branchTargets[i+3]) {
                const auto &second=code[i+1], &third=code[i+2];
                const int mask=(v.op==OP_TEN_BIEN_GIA_TRI?1:0) |
                               (second.op==OP_TEN_BIEN_GIA_TRI?2:0) |
                               (third.op==OP_TEN_BIEN_GIA_TRI?4:0);
                call("call_quad",{code[i+3].operandIndex,3,
                                  (mask&1)?v.operandIndex:v.operand,
                                  (mask&2)?second.operandIndex:second.operand,
                                  (mask&4)?third.operandIndex:third.operand,
                                  0,mask});
                for(std::size_t k=1;k<=3;++k)locations[i+k]=locations[i];
                i+=3;continue;
            }
            if(simpleArg(v) && i+4<code.size() &&
               simpleArg(code[i+1]) && simpleArg(code[i+2]) &&
               simpleArg(code[i+3]) && code[i+4].op==OP_GOI &&
               code[i+4].operand==4 &&
               !branchTargets[i+1] && !branchTargets[i+2] &&
               !branchTargets[i+3] && !branchTargets[i+4]) {
                const auto &second=code[i+1], &third=code[i+2], &fourth=code[i+3];
                const int mask=(v.op==OP_TEN_BIEN_GIA_TRI?1:0) |
                               (second.op==OP_TEN_BIEN_GIA_TRI?2:0) |
                               (third.op==OP_TEN_BIEN_GIA_TRI?4:0) |
                               (fourth.op==OP_TEN_BIEN_GIA_TRI?8:0);
                call("call_quad",{code[i+4].operandIndex,4,
                                  (mask&1)?v.operandIndex:v.operand,
                                  (mask&2)?second.operandIndex:second.operand,
                                  (mask&4)?third.operandIndex:third.operand,
                                  (mask&8)?fourth.operandIndex:fourth.operand,
                                  mask});
                for(std::size_t k=1;k<=4;++k)locations[i+k]=locations[i];
                i+=4;continue;
            }
            // Four bytecodes and two runtime temporaries become one checked
            // scalar comparison. The result ABI is 0=false, 1=true, 2=error.
            // Keep incoming edges and catch targets as hard barriers.
            if(v.op==OP_TEN_BIEN_GIA_TRI && i+7<code.size() &&
               code[i+1].op==OP_BIEN_SO && code[i+2].op==OP_LON_HON_HOAC_BANG &&
               code[i+3].op==OP_TEN_BIEN_GIA_TRI &&
               code[i+3].operandIndex==v.operandIndex &&
               code[i+4].op==OP_BIEN_SO && code[i+5].op==OP_NHO_HON_HOAC_BANG &&
               code[i+6].op==OP_Logic_VA && code[i+7].op==OP_JUMP_IF_FALSE &&
               !branchTargets[i+1] && !branchTargets[i+2] && !branchTargets[i+3] &&
               !branchTargets[i+4] && !branchTargets[i+5] && !branchTargets[i+6] &&
               !branchTargets[i+7]) {
                rawCall("compare_integer_range",{v.operandIndex,code[i+1].operand,code[i+4].operand});
                emit(MachineOpcode::StoreSlot,0,1);emit(MachineOpcode::Drop);
                emit(MachineOpcode::LoadSlot,0,1);emit(MachineOpcode::PushConstant,2);
                emit(MachineOpcode::Equal);
                const auto good=fn.instructions.size();emit(MachineOpcode::BranchZero);
                errors.push_back(fn.instructions.size());emit(MachineOpcode::Jump);
                fn.instructions[good].operand=fn.instructions.size();
                conditional(code[i+7].operand);
                for(std::size_t k=1;k<=7;++k)locations[i+k]=locations[i];
                i+=7;continue;
            }
            // x < low || x > high is a frequent parser guard. Keep V++ eager
            // || semantics in the runtime fallback while avoiding three dynamic
            // temporaries when x is a proven i32 local.
            if(v.op==OP_TEN_BIEN_GIA_TRI && i+7<code.size() &&
               code[i+1].op==OP_BIEN_SO && code[i+2].op==OP_NHO_HON &&
               code[i+3].op==OP_TEN_BIEN_GIA_TRI &&
               code[i+3].operandIndex==v.operandIndex &&
               code[i+4].op==OP_BIEN_SO && code[i+5].op==OP_LON_HON &&
               code[i+6].op==OP_Logic_HOAC && code[i+7].op==OP_JUMP_IF_FALSE &&
               !branchTargets[i+1] && !branchTargets[i+2] && !branchTargets[i+3] &&
               !branchTargets[i+4] && !branchTargets[i+5] && !branchTargets[i+6] &&
               !branchTargets[i+7]) {
                rawCall("compare_integer_outside_range",
                        {v.operandIndex,code[i+1].operand,code[i+4].operand});
                emit(MachineOpcode::StoreSlot,0,1);emit(MachineOpcode::Drop);
                emit(MachineOpcode::LoadSlot,0,1);emit(MachineOpcode::PushConstant,2);
                emit(MachineOpcode::Equal);
                const auto good=fn.instructions.size();emit(MachineOpcode::BranchZero);
                errors.push_back(fn.instructions.size());emit(MachineOpcode::Jump);
                fn.instructions[good].operand=fn.instructions.size();
                conditional(code[i+7].operand);
                for(std::size_t k=1;k<=7;++k)locations[i+k]=locations[i];
                i+=7;continue;
            }
            // x == a || (x >= low && x <= high): phân loại ký tự/số ASCII
            // mà không dựng năm giá trị boolean động. V++ vẫn đánh giá eager.
            if(v.op==OP_TEN_BIEN_GIA_TRI && i+11<code.size() &&
               code[i+1].op==OP_BIEN_SO && code[i+2].op==OP_SO_SANH_BANG &&
               code[i+3].op==OP_TEN_BIEN_GIA_TRI &&
               code[i+3].operandIndex==v.operandIndex &&
               code[i+4].op==OP_BIEN_SO && code[i+5].op==OP_LON_HON_HOAC_BANG &&
               code[i+6].op==OP_TEN_BIEN_GIA_TRI &&
               code[i+6].operandIndex==v.operandIndex &&
               code[i+7].op==OP_BIEN_SO && code[i+8].op==OP_NHO_HON_HOAC_BANG &&
               code[i+9].op==OP_Logic_VA && code[i+10].op==OP_Logic_HOAC &&
               code[i+11].op==OP_JUMP_IF_FALSE) {
                bool contiguous=true;
                for(std::size_t k=1;k<=11;++k)if(branchTargets[i+k]){contiguous=false;break;}
                if(contiguous) {
                    rawCall("compare_integer_or_range",
                            {v.operandIndex,code[i+1].operand,code[i+4].operand,code[i+7].operand});
                    emit(MachineOpcode::StoreSlot,0,1);emit(MachineOpcode::Drop);
                    emit(MachineOpcode::LoadSlot,0,1);emit(MachineOpcode::PushConstant,2);
                    emit(MachineOpcode::Equal);
                    const auto good=fn.instructions.size();emit(MachineOpcode::BranchZero);
                    errors.push_back(fn.instructions.size());emit(MachineOpcode::Jump);
                    fn.instructions[good].operand=fn.instructions.size();
                    conditional(code[i+11].operand);
                    for(std::size_t k=1;k<=11;++k)locations[i+k]=locations[i];
                    i+=11;continue;
                }
            }
            // x <op> K || (x == K && y <op> z): overflow/bounds guards in
            // parsers evaluate eagerly in V++, but need no temporary dynamic
            // values when all participating locals are i32.
            if(v.op==OP_TEN_BIEN_GIA_TRI && i+11<code.size() &&
               code[i+1].op==OP_BIEN_SO &&
               (code[i+2].op==OP_SO_SANH_BANG || code[i+2].op==OP_KHAC_BANG ||
                code[i+2].op==OP_LON_HON || code[i+2].op==OP_NHO_HON ||
                code[i+2].op==OP_LON_HON_HOAC_BANG || code[i+2].op==OP_NHO_HON_HOAC_BANG) &&
               code[i+3].op==OP_TEN_BIEN_GIA_TRI && code[i+3].operandIndex==v.operandIndex &&
               code[i+4].op==OP_BIEN_SO && code[i+4].operand==code[i+1].operand &&
               code[i+5].op==OP_SO_SANH_BANG &&
               code[i+6].op==OP_TEN_BIEN_GIA_TRI && code[i+7].op==OP_TEN_BIEN_GIA_TRI &&
               (code[i+8].op==OP_SO_SANH_BANG || code[i+8].op==OP_KHAC_BANG ||
                code[i+8].op==OP_LON_HON || code[i+8].op==OP_NHO_HON ||
                code[i+8].op==OP_LON_HON_HOAC_BANG || code[i+8].op==OP_NHO_HON_HOAC_BANG) &&
               code[i+9].op==OP_Logic_VA && code[i+10].op==OP_Logic_HOAC &&
               code[i+11].op==OP_JUMP_IF_FALSE) {
                bool contiguous=true;
                for(std::size_t k=1;k<=11;++k)if(branchTargets[i+k]){contiguous=false;break;}
                if(contiguous) {
                    rawCall("compare_integer_or_and",{v.operandIndex,code[i+1].operand,code[i+2].op,
                                                       code[i+6].operandIndex,code[i+7].operandIndex,
                                                       code[i+8].op});
                    emit(MachineOpcode::StoreSlot,0,1);emit(MachineOpcode::Drop);
                    emit(MachineOpcode::LoadSlot,0,1);emit(MachineOpcode::PushConstant,2);
                    emit(MachineOpcode::Equal);
                    const auto good=fn.instructions.size();emit(MachineOpcode::BranchZero);
                    errors.push_back(fn.instructions.size());emit(MachineOpcode::Jump);
                    fn.instructions[good].operand=fn.instructions.size();
                    conditional(code[i+11].operand);
                    for(std::size_t k=1;k<=11;++k)locations[i+k]=locations[i];
                    i+=11;continue;
                }
            }
            // Membership in a small i32 set is common in parsers: x==a ||
            // x==b [|| x==c || x==d]. Keep eager evaluation semantics while
            // loading the stable local cell only once on the fast path.
            if(v.op==OP_TEN_BIEN_GIA_TRI) {
                int setCount=0, setTarget=0, setConsumed=0;
                std::array<int,4> setValues{};
                for(int count=4;count>=2 && setCount==0;--count) {
                    const auto jump=i+static_cast<std::size_t>(4*count-1);
                    if(jump>=code.size())continue;
                    bool match=true;
                    for(std::size_t k=1;k<=static_cast<std::size_t>(4*count-1);++k) {
                        if(branchTargets[i+k]) {match=false;break;}
                    }
                    if(!match)continue;
                    if(code[i+1].op!=OP_BIEN_SO || code[i+2].op!=OP_SO_SANH_BANG)continue;
                    setValues[0]=code[i+1].operand;
                    for(int term=1;term<count && match;++term) {
                        const auto base=i+static_cast<std::size_t>(4*term-1);
                        if(code[base].op!=OP_TEN_BIEN_GIA_TRI ||
                           code[base].operandIndex!=v.operandIndex ||
                           code[base+1].op!=OP_BIEN_SO || code[base+2].op!=OP_SO_SANH_BANG ||
                           code[base+3].op!=OP_Logic_HOAC) match=false;
                        else setValues[term]=code[base+1].operand;
                    }
                    if(!match || code[jump].op!=OP_JUMP_IF_FALSE)continue;
                    setCount=count;setTarget=code[jump].operand;setConsumed=4*count-1;
                }
                if(setCount) {
                    rawCall("compare_integer_set",{v.operandIndex,setCount,setValues[0],setValues[1],setValues[2],setValues[3]});
                    emit(MachineOpcode::StoreSlot,0,1);emit(MachineOpcode::Drop);
                    emit(MachineOpcode::LoadSlot,0,1);emit(MachineOpcode::PushConstant,2);
                    emit(MachineOpcode::Equal);
                    const auto good=fn.instructions.size();emit(MachineOpcode::BranchZero);
                    errors.push_back(fn.instructions.size());emit(MachineOpcode::Jump);
                    fn.instructions[good].operand=fn.instructions.size();
                    conditional(setTarget);
                    for(int k=1;k<=setConsumed;++k)locations[i+static_cast<std::size_t>(k)]=locations[i];
                    i+=static_cast<std::size_t>(setConsumed);continue;
                }
            }
            if(v.op==OP_TEN_BIEN_GIA_TRI && i+3<code.size() &&
               code[i+1].op==OP_BIEN_SO &&
               (code[i+2].op==OP_SO_SANH_BANG || code[i+2].op==OP_KHAC_BANG ||
                code[i+2].op==OP_LON_HON || code[i+2].op==OP_NHO_HON ||
                code[i+2].op==OP_LON_HON_HOAC_BANG || code[i+2].op==OP_NHO_HON_HOAC_BANG) &&
               code[i+3].op==OP_JUMP_IF_FALSE &&
               !branchTargets[i+1] && !branchTargets[i+2] && !branchTargets[i+3]) {
                rawCall("compare_integer_slot",{v.operandIndex,code[i+1].operand,code[i+2].op});
                emit(MachineOpcode::StoreSlot,0,1);emit(MachineOpcode::Drop);
                emit(MachineOpcode::LoadSlot,0,1);emit(MachineOpcode::PushConstant,2);
                emit(MachineOpcode::Equal);
                const auto good=fn.instructions.size();emit(MachineOpcode::BranchZero);
                errors.push_back(fn.instructions.size());emit(MachineOpcode::Jump);
                fn.instructions[good].operand=fn.instructions.size();
                conditional(code[i+3].operand);
                for(std::size_t k=1;k<=3;++k)locations[i+k]=locations[i];
                i+=3;continue;
            }
            // !a && b; jump. V++ đánh giá eager, helper vẫn tính cả hai vế
            // theo thứ tự trước khi kết hợp kết quả truthiness.
            if(v.op==OP_TEN_BIEN_GIA_TRI && i+4<code.size() &&
               code[i+1].op==OP_PHU_DINH &&
               code[i+2].op==OP_TEN_BIEN_GIA_TRI &&
               code[i+3].op==OP_Logic_VA && code[i+4].op==OP_JUMP_IF_FALSE &&
               !branchTargets[i+1] && !branchTargets[i+2] &&
               !branchTargets[i+3] && !branchTargets[i+4]) {
                rawCall("not_slot_and",{v.operandIndex,code[i+2].operandIndex});
                emit(MachineOpcode::StoreSlot,0,1);emit(MachineOpcode::Drop);
                emit(MachineOpcode::LoadSlot,0,1);emit(MachineOpcode::PushConstant,2);
                emit(MachineOpcode::Equal);
                const auto good=fn.instructions.size();emit(MachineOpcode::BranchZero);
                errors.push_back(fn.instructions.size());emit(MachineOpcode::Jump);
                fn.instructions[good].operand=fn.instructions.size();
                conditional(code[i+4].operand);
                for(std::size_t k=1;k<=4;++k)locations[i+k]=locations[i];
                i+=4;continue;
            }
            // Compare two loop-carried variables without creating temporary
            // StackValues. 0=false, 1=true and 2=an exception to propagate.
            if(v.op==OP_TEN_BIEN_GIA_TRI && i+5<code.size() &&
               code[i+1].op==OP_TEN_BIEN_GIA_TRI &&
               (code[i+2].op==OP_SO_SANH_BANG || code[i+2].op==OP_KHAC_BANG ||
                code[i+2].op==OP_LON_HON || code[i+2].op==OP_NHO_HON ||
                code[i+2].op==OP_LON_HON_HOAC_BANG || code[i+2].op==OP_NHO_HON_HOAC_BANG) &&
               code[i+3].op==OP_TEN_BIEN_GIA_TRI && code[i+4].op==OP_Logic_VA &&
               code[i+5].op==OP_JUMP_IF_FALSE &&
               !branchTargets[i+1] && !branchTargets[i+2] && !branchTargets[i+3] &&
               !branchTargets[i+4] && !branchTargets[i+5]) {
                rawCall("compare_numeric_slots_and",{v.operandIndex,code[i+1].operandIndex,
                                                      code[i+2].op,code[i+3].operandIndex});
                emit(MachineOpcode::StoreSlot,0,1);emit(MachineOpcode::Drop);
                emit(MachineOpcode::LoadSlot,0,1);emit(MachineOpcode::PushConstant,2);
                emit(MachineOpcode::Equal);
                const auto good=fn.instructions.size();emit(MachineOpcode::BranchZero);
                errors.push_back(fn.instructions.size());emit(MachineOpcode::Jump);
                fn.instructions[good].operand=fn.instructions.size();
                conditional(code[i+5].operand);
                for(std::size_t k=1;k<=5;++k)locations[i+k]=locations[i];
                i+=5;continue;
            }
            if(v.op==OP_TEN_BIEN_GIA_TRI && i+3<code.size() &&
               code[i+1].op==OP_TEN_BIEN_GIA_TRI &&
               (code[i+2].op==OP_SO_SANH_BANG || code[i+2].op==OP_KHAC_BANG ||
                code[i+2].op==OP_LON_HON || code[i+2].op==OP_NHO_HON ||
                code[i+2].op==OP_LON_HON_HOAC_BANG || code[i+2].op==OP_NHO_HON_HOAC_BANG) &&
               code[i+3].op==OP_JUMP_IF_FALSE &&
               !branchTargets[i+1] && !branchTargets[i+2] && !branchTargets[i+3]) {
                rawCall("compare_numeric_slots",{v.operandIndex,code[i+1].operandIndex,code[i+2].op});
                emit(MachineOpcode::StoreSlot,0,1);emit(MachineOpcode::Drop);
                emit(MachineOpcode::LoadSlot,0,1);emit(MachineOpcode::PushConstant,2);
                emit(MachineOpcode::Equal);
                const auto good=fn.instructions.size();emit(MachineOpcode::BranchZero);
                errors.push_back(fn.instructions.size());emit(MachineOpcode::Jump);
                fn.instructions[good].operand=fn.instructions.size();
                conditional(code[i+3].operand);
                for(std::size_t k=1;k<=3;++k)locations[i+k]=locations[i];
                i+=3;continue;
            }
            switch(v.op) {
                case OP_HAM:case OP_NEU:case OP_DIEU_KIEN:case OP_LAP:case OP_CAP_NHAT:
                case OP_DONG_LENH:case OP_MO_NGOAC:case OP_DONG_NGOAC:case OP_MO_KHOI:case OP_DONG_KHOI:break;
                case OP_BIEN_SO:call("integer",{v.operand});break;
                case OP_TEN_BIEN_ID:call("integer",{v.operandIndex});break;
                case OP_TEN_BIEN_GIA_TRI: {
                    const auto local=inlineLocals.find(v.operandIndex);
                    if(local!=inlineLocals.end())
                        call("load_inline",{v.operandIndex,
                            local->second.first,local->second.second});
                    else call("load",{v.operandIndex});
                    break;
                }
                case OP_CHUOI:call("string",{v.operandIndex});break;
                case OP_BIEN_SO_FLOAT:call("floating",{v.operandIndex});break;
                case OP_RONG_GIA_TRI:call("null");break;
                case OP_DUNG_GIA_TRI:call("integer",{1});break;
                case OP_SAI_GIA_TRI:call("integer",{0});break;
                case OP_KHOI_TAO:
                    // PARAM immediately binds this slot. Pre-initializing it
                    // creates an unused cell and disables inline i32 binding.
                    // Keep the separate initialization at branch targets.
                    if(i+1>=code.size() || branchTargets[i+1] ||
                       (code[i+1].op!=OP_PARAM && code[i+1].op!=OP_PARAM_MAC_DINH) ||
                       code[i+1].operandIndex!=v.operandIndex)
                        call("initialize",{v.operandIndex});
                    break;
                case OP_GAN:call("assign");break;
                case OP_PARAM:call("parameter",{v.operandIndex,v.operandValue,-1});break;
                case OP_PARAM_MAC_DINH:call("parameter",{v.operandIndex,v.operandValue,v.operand});break;
                case OP_CONG_MOT:case OP_TRU_MOT:call("increment",{v.op==OP_CONG_MOT?1:-1});break;
                case OP_BIT_VA:
                case OP_BIT_HOAC:
                case OP_BIT_XOR:
                case OP_DICH_TRAI:
                case OP_DICH_PHAI:
                case OP_CONG:case OP_TRU:case OP_NHAN:case OP_CHIA:case OP_MODULO:
                case OP_Logic_VA:case OP_Logic_HOAC:case OP_SO_SANH_BANG:case OP_KHAC_BANG:
                case OP_LON_HON:case OP_NHO_HON:case OP_LON_HON_HOAC_BANG:case OP_NHO_HON_HOAC_BANG:call("binary",{v.op});break;
                case OP_KHONG:case OP_PHU_DINH:call("negate");break;
                case OP_LIST_LITERAL:call("list",{v.operand,v.operandIndex});break;
                case OP_MAP_LITERAL:call("map",{v.operand,v.operandIndex});break;
                case OP_DOC_CHI_SO:call("index");break;
                case OP_GAN_CHI_SO:call("set_index");break;
                case OP_GOI:call("call",{v.operandIndex,v.operand});break;
                case OP_GOI_LOI_BAN_DIA:call("native_core",{v.operandIndex,v.operand});break;
                case OP_GOI_GIAN_TIEP:call("indirect",{v.operand});break;
                case OP_TAO_DONG_BAO:call("closure",{v.operand,v.operandIndex});break;
                case OP_TAO_LOP:call("define_class",{v.operandIndex,v.operandValue});break;
                case OP_THEM_PHUONG_THUC:call("define_method",{v.operand,v.operandIndex,v.operandValue});break;
                case OP_TAO_DOI_TUONG:call("construct",{v.operandIndex,v.operand});break;
                case OP_DOC_THUOC_TINH:case OP_GAN_THUOC_TINH:call("property",{v.operandIndex,v.op==OP_GAN_THUOC_TINH?1:0});break;
                case OP_GOI_PHUONG_THUC:call("method",{v.operandIndex,v.operand,v.operandValue});break;
                case OP_IN:call("print");break;
                case OP_TRA_VE:case OP_DUNG_CHUONG_TRINH:
                    call("return_value");returns.push_back(fn.instructions.size());emit(MachineOpcode::Jump);break;
                case OP_JUMP:jump(v.operand,i);break;
                case OP_JUMP_IF_FALSE:call("condition");conditional(v.operand);break;
                case OP_BO_QUA:jump(continueTarget(i),i);break;
                case OP_THOAT:
                    if(v.operandIndex<0)jump(v.operand,i);
                    else {if(switchEnds.empty())throw std::runtime_error("AOT: break outside switch");jump(switchEnds.back(),i);}break;
                case OP_CHON:switchEnds.push_back(v.operand);call("switch_begin");break;
                case OP_CA:call("case",{v.operand,v.operandIndex});conditional(v.operandValue);break;
                case OP_MAC_DINH:call("default");conditional(v.operandValue);break;
                case OP_KET_THUC_CHUYEN:
                    if(switchEnds.empty())throw std::runtime_error("AOT: switch end");switchEnds.pop_back();call("switch_end");break;
                case OP_THU:handlers.push_back(v.operand);call("try_begin",{v.operand,v.operandIndex});break;
                case OP_THU_KET_THUC:call("try_end");jump(v.operand,i);break;
                case OP_BAT_LOI:call("catch_value",{v.operandIndex});break;
                case OP_NEM:call("throw_value");break;
                case OP_FFI_CALL:call("foreign",{v.operandIndex,v.operand});break;
                default:
                    if(bytecode::intrinsicByOpcode(v.op))call("intrinsic",{v.op});
                    else throw std::runtime_error("AOT: unsupported opcode "+std::to_string(v.op));
            }
        }
        locations[code.size()]=fn.instructions.size();
        returns.push_back(fn.instructions.size());emit(MachineOpcode::Jump);
        const auto error=fn.instructions.size();rawCall("handle_error");emit(MachineOpcode::StoreSlot,0,1);emit(MachineOpcode::Drop);
        for(int handler:handlers) {
            emit(MachineOpcode::LoadSlot,0,1);emit(MachineOpcode::PushConstant,handler);emit(MachineOpcode::Equal);
            const auto skip=fn.instructions.size();emit(MachineOpcode::BranchZero);
            fixups.push_back({fn.instructions.size(),handler});emit(MachineOpcode::Jump);
            fn.instructions[skip].operand=fn.instructions.size();
        }
        const auto done=fn.instructions.size();emit(MachineOpcode::PushConstant,0);emit(MachineOpcode::Return);
        for(const auto &fix:fixups) {
            if(fix.target<0||static_cast<std::size_t>(fix.target)>code.size())throw std::runtime_error("AOT: branch target");
            fn.instructions[fix.instruction].operand=locations[fix.target];
        }
        for(auto fix:errors)fn.instructions[fix].operand=error;
        for(auto fix:returns)fn.instructions[fix].operand=done;
        const auto machine=emitMachineFunction(hostTarget(),fn);
        if(!machine)throw std::runtime_error("AOT machine lowering: "+machine.error);
        const auto assembly=emitMachineAssembly(hostTarget(),machine,stem);
        if(!assembly)throw std::runtime_error("AOT assembly: "+assembly.error);
        auto text=assembly.text;
        std::size_t position=0;
        while((position=text.find(".L_vpp_",position))!=std::string::npos){text.replace(position,7,".L"+stem+"_");position+=stem.size()+3;}
        out<<text;
    }
};
const std::vector<Value> &array(const Value &value) {
    const auto *list=std::get_if<runtime::ListHandle>(&value);
    if(!list||!*list)throw std::runtime_error("AOT: wire array required");return (*list)->elements;
}
int number(const Value &value) {
    if(!std::holds_alternative<int>(value))throw std::runtime_error("AOT: wire i32 required");return std::get<int>(value);
}
std::vector<Instruction> instructions(const Value &value) {
    std::vector<Instruction> result;
    for(const auto &item:array(value)) {
        const auto &fields=array(item);if(fields.size()!=4)throw std::runtime_error("AOT: instruction requires four fields");
        result.push_back({number(fields[0]),number(fields[1]),number(fields[2]),number(fields[3])});
    }
    return result;
}
std::string string(const Value &value) {
    const auto *result=std::get_if<std::string>(&value);
    if(!result)throw std::runtime_error("AOT: foreign descriptor string required");
    return *result;
}
bytecode::ForeignAbiType foreignType(const Value &value,bool parameter) {
    const int type=number(value);
    if(type<0||type>static_cast<int>(bytecode::ForeignAbiType::SocketHandle)||
       (parameter&&type==static_cast<int>(bytecode::ForeignAbiType::Void))||
       (!parameter&&(type==static_cast<int>(bytecode::ForeignAbiType::BufferIn)||
                     type==static_cast<int>(bytecode::ForeignAbiType::BufferOut))))
        throw std::runtime_error("AOT: invalid foreign ABI type");
    return static_cast<bytecode::ForeignAbiType>(type);
}
bool foreignInteger(bytecode::ForeignAbiType type) {
    return type==bytecode::ForeignAbiType::I32||type==bytecode::ForeignAbiType::U32||
           type==bytecode::ForeignAbiType::I64||type==bytecode::ForeignAbiType::U64;
}
void validateForeignCalls(const std::vector<Instruction> &code,
                          const std::vector<bytecode::ForeignFunctionDescriptor> &foreign) {
    for(const auto &instruction:code) {
        if(instruction.op!=OP_FFI_CALL)continue;
        if(instruction.operandIndex<0||static_cast<std::size_t>(instruction.operandIndex)>=foreign.size()||
           instruction.operand<0||instruction.operandValue!=0||
           static_cast<std::size_t>(instruction.operand)!=foreign[instruction.operandIndex].parameters.size())
            throw std::runtime_error("AOT: foreign call descriptor or argument count invalid");
    }
}
} // namespace

Snapshot snapshotFromWire(const std::vector<Value> &wire) {
    if(wire.size()!=4&&wire.size()!=5)throw std::runtime_error("AOT: four or five wire regions required");
    Snapshot snapshot;
    snapshot.entry=instructions(wire[0]);
    for(const auto &text:array(wire[1]))snapshot.strings.push_back(std::get<std::string>(text));
    std::set<int> functionNames;
    int nextFunctionId=0;
    for(const auto &item:array(wire[2])) {
        const auto &fields=array(item);if(fields.size()!=3)throw std::runtime_error("AOT: function record");
        const int id=number(fields[0]);
        const int name=number(fields[1]);
        if(id!=nextFunctionId++)throw std::runtime_error("bytecode_chay_vm: function ID phải liên tiếp từ 0");
        if(name < -1 || (name >= 0 && static_cast<std::size_t>(name) >= snapshot.strings.size()))
            throw std::runtime_error("bytecode_chay_vm: function name index ngoài StringPool");
        if(name >= 0 && !functionNames.insert(name).second)
            throw std::runtime_error("bytecode_chay_vm: trùng function runtime name index");
        snapshot.functions.emplace(id,FunctionCode{name,instructions(fields[2])});
    }
    std::set<std::string> initializerNames;
    for(const auto &item:array(wire[3])) {
        const auto &fields=array(item);if(fields.size()!=2)throw std::runtime_error("AOT: initializer record");
        const std::string identity=string(fields[0]);
        if(identity.empty())throw std::runtime_error("bytecode_chay_vm: định danh initializer rỗng");
        if(!initializerNames.insert(identity).second)
            throw std::runtime_error("bytecode_chay_vm: trùng định danh initializer '"+identity+"'");
        snapshot.initializers.push_back(instructions(fields[1]));
    }
    if(wire.size()==5) {
        for(const auto &item:array(wire[4])) {
            const auto &fields=array(item);
            if(fields.size()!=8)throw std::runtime_error("AOT: foreign descriptor requires eight fields");
            bytecode::ForeignFunctionDescriptor descriptor;
            descriptor.id=number(fields[0]);
            if(descriptor.id!=static_cast<int>(snapshot.foreignFunctions.size()))
                throw std::runtime_error("AOT: foreign descriptor IDs must be consecutive");
            descriptor.library=string(fields[1]);descriptor.symbol=string(fields[2]);
            descriptor.result=foreignType(fields[5],false);
            descriptor.abi=string(fields[6]);descriptor.capability=string(fields[7]);
            if(descriptor.library.empty()||descriptor.symbol.empty()||
               descriptor.abi!="c"||descriptor.capability.empty())
                throw std::runtime_error("AOT: invalid foreign library, symbol, ABI or capability");
            for(const auto &type:array(fields[3]))descriptor.parameters.push_back(foreignType(type,true));
            std::vector<bool> seen(descriptor.parameters.size(),false);
            for(const auto &itemExtent:array(fields[4])) {
                const auto &extent=array(itemExtent);
                if(extent.size()!=3)throw std::runtime_error("AOT: foreign buffer extent requires three fields");
                const int parameter=number(extent[0]);
                const int length=number(extent[1]);
                const int fixed=number(extent[2]);
                if(parameter<0||static_cast<std::size_t>(parameter)>=descriptor.parameters.size()||
                   seen[parameter]||
                   (descriptor.parameters[parameter]!=bytecode::ForeignAbiType::BufferOut&&
                    descriptor.parameters[parameter]!=bytecode::ForeignAbiType::BufferIn)||
                   length < -1||
                   (length==-1&&(fixed<=0||fixed>65536))||
                   (length>=0&&(fixed!=0||static_cast<std::size_t>(length)>=descriptor.parameters.size()||
                                !foreignInteger(descriptor.parameters[length]))))
                    throw std::runtime_error("AOT: invalid foreign buffer extent");
                seen[parameter]=true;
                descriptor.bufferExtents.push_back({static_cast<std::size_t>(parameter),length,
                                                    static_cast<std::size_t>(fixed)});
            }
            for(std::size_t i=0;i<descriptor.parameters.size();++i) {
                if((descriptor.parameters[i]==bytecode::ForeignAbiType::BufferOut||
                    descriptor.parameters[i]==bytecode::ForeignAbiType::BufferIn)&&!seen[i])
                    throw std::runtime_error("AOT: foreign buffer parameter missing extent");
            }
            snapshot.foreignFunctions.push_back(std::move(descriptor));
        }
    }
    validateForeignCalls(snapshot.entry,snapshot.foreignFunctions);
    for(const auto &[id,function]:snapshot.functions)validateForeignCalls(function.instructions,snapshot.foreignFunctions);
    for(const auto &initializer:snapshot.initializers)validateForeignCalls(initializer,snapshot.foreignFunctions);
    return snapshot;
}

std::string emitAssembly(const Snapshot &snapshot,bool compiler) {
    std::ostringstream out;out << ".text\n";
    std::map<int,StaticI32Function> scalarFunctions;
    DirectMethodTable directMethods;
    collectDirectMethods(snapshot.entry,directMethods);
    for(const auto &[id,function]:snapshot.functions)
        collectDirectMethods(function.instructions,directMethods);
    for(const auto &initializer:snapshot.initializers)
        collectDirectMethods(initializer,directMethods);
    // Mỗi lượt chỉ dùng hàm đã chứng minh ở lượt trước. Đồ thị lời gọi được
    // sinh theo thứ tự phụ thuộc, và hàm đệ quy tự động giữ đường runtime.
    for(std::size_t pass=0;pass<snapshot.functions.size();++pass) {
        bool changed=false;
        for(const auto &[id,function]:snapshot.functions)if(function.nameIndex>=0 &&
            scalarFunctions.find(id)==scalarFunctions.end()) {
            if(auto scalar=staticI32Function(function.instructions,scalarFunctions)) {
                scalarFunctions.emplace(id,std::move(*scalar));
                changed=true;
            }
        }
        if(!changed)break;
    }
    for(const auto &[id,scalar]:scalarFunctions) {
        const auto machine=emitMachineFunction(hostTarget(),scalar.machine);
        if(!machine)throw std::runtime_error("AOT i32 machine: "+machine.error);
        const auto assembly=emitMachineAssembly(hostTarget(),machine,"vpp_aot_i32_"+std::to_string(id));
        if(!assembly)throw std::runtime_error("AOT i32 assembly: "+assembly.error);
        auto text=assembly.text;
        const std::string stem="vpp_aot_i32_"+std::to_string(id);
        std::size_t position=0;
        while((position=text.find(".L_vpp_",position))!=std::string::npos) {
            text.replace(position,7,".L"+stem+"_");
            position+=stem.size()+3;
        }
        out<<text;
    }
    for(const auto &[id,function]:snapshot.functions)
        Assembly(out,"vpp_aot_function_"+std::to_string(id),function.instructions,
                 scalarFunctions,directMethods,id,!compiler).emit();
    for(std::size_t i=0;i<snapshot.initializers.size();++i)
        Assembly(out,"vpp_aot_init_"+std::to_string(i),snapshot.initializers[i],
                 scalarFunctions,directMethods).emit();
    Assembly(out,"vpp_aot_entry",snapshot.entry,scalarFunctions,directMethods).emit();
#if defined(__APPLE__)
    out << ".section __DATA,__const\n";
#elif defined(_WIN32)
    out << ".section .rdata,\"dr\"\n";
#else
    out << ".section .data.rel.ro,\"aw\",@progbits\n";
#endif
    for(std::size_t i=0;i<snapshot.strings.size();++i)out << ".Laot_string_" << i << ": .asciz " << quoted(snapshot.strings[i]) << "\n";
    out << ".p2align 3\n.Laot_strings:\n";
    for(std::size_t i=0;i<snapshot.strings.size();++i)out << ".quad .Laot_string_" << i << "," << snapshot.strings[i].size() << "\n";
    const int functionCount=snapshot.functions.empty()?0:snapshot.functions.rbegin()->first+1;
    out << ".p2align 3\n.Laot_functions:\n";
    for(int id=0;id<functionCount;++id) {
        auto found=snapshot.functions.find(id);
        if(found==snapshot.functions.end()){out << ".long -1,0,0,0\n.quad 0\n";continue;}
        int required=0,maximum=0;
        for(const auto &v:found->second.instructions)if((v.op==OP_PARAM||v.op==OP_PARAM_MAC_DINH)&&v.operandValue>=0) {
            maximum=std::max(maximum,v.operandValue+1);if(v.op==OP_PARAM)required=std::max(required,v.operandValue+1);
        }
        out << ".long " << found->second.nameIndex << "," << required << "," << maximum << ",0\n.quad " << prefix << "vpp_aot_function_" << id << "\n";
    }
    out << ".Laot_initializers:\n";
    for(std::size_t i=0;i<snapshot.initializers.size();++i)out << ".quad " << prefix << "vpp_aot_init_" << i << "\n";
    for(std::size_t i=0;i<snapshot.foreignFunctions.size();++i) {
        const auto &d=snapshot.foreignFunctions[i];
        out << ".Laot_foreign_lib_" << i << ":.asciz " << quoted(d.library) << "\n.Laot_foreign_sym_" << i << ":.asciz " << quoted(d.symbol)
            << "\n.Laot_foreign_abi_" << i << ":.asciz " << quoted(d.abi) << "\n.Laot_foreign_cap_" << i << ":.asciz " << quoted(d.capability) << "\n.p2align 3\n.Laot_foreign_params_" << i << ":\n";
        for(auto type:d.parameters)out << ".long " << static_cast<int>(type) << "\n";
        out << ".p2align 3\n.Laot_foreign_extents_" << i << ":\n";
        for(const auto &e:d.bufferExtents)out << ".quad " << e.parameterIndex << "\n.long " << e.lengthParameterIndex << ",0\n.quad " << e.fixedLength << "\n";
    }
    out << ".p2align 3\n.Laot_foreign:\n";
    for(std::size_t i=0;i<snapshot.foreignFunctions.size();++i) {
        const auto &d=snapshot.foreignFunctions[i];
        out << ".long " << d.id << "," << static_cast<int>(d.result) << "\n.quad .Laot_foreign_lib_" << i << ",.Laot_foreign_sym_" << i << ",.Laot_foreign_abi_" << i << ",.Laot_foreign_cap_" << i
            << "\n.quad " << d.parameters.size() << ",.Laot_foreign_params_" << i << "," << d.bufferExtents.size() << ",.Laot_foreign_extents_" << i << "\n";
    }
    out << ".p2align 3\n.Laot_i32_functions:\n";
    for(int id=0;id<functionCount;++id) {
        const auto found=scalarFunctions.find(id);
        if(found==scalarFunctions.end()) {
            out << ".quad 0\n.long 0,0\n";
            continue;
        }
        int usedMask=0;
        for(const auto &instruction:found->second.machine.instructions)
            if(instruction.opcode==MachineOpcode::LoadSlot &&
               instruction.operand<found->second.parameterSlots.size())
                usedMask|=1<<static_cast<int>(instruction.operand);
        out << ".quad " << prefix << "vpp_aot_i32_" << id << "\n.long "
            << found->second.parameterSlots.size() << "," << usedMask << "\n";
    }
    out << ".p2align 3\n" << prefix << "vpp_aot_spec:\n.quad " << snapshot.strings.size() << ",.Laot_strings," << functionCount << ",.Laot_functions,"
        << snapshot.initializers.size() << ",.Laot_initializers," << prefix << "vpp_aot_entry\n.long " << int(compiler) << ",1\n.quad " << snapshot.foreignFunctions.size() << ",.Laot_foreign\n.quad "
        << functionCount << ",.Laot_i32_functions\n";
out << "\n#define VPP_NATIVE_DYNAMIC_RUNTIME 1\n#include \"common/vpp_aot_dynamic_runtime.inc\"\n";
    return out.str();
}
} // namespace vietvm::native::aot

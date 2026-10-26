#include "vpp/native/aot.h"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>

#include "vpp/bytecode/intrinsic.h"
#include "vpp/native/raw_codegen.h"

namespace vietvm::native::aot {
namespace {
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
#if defined(__aarch64__) || defined(__arm64__)
constexpr bool arm = true;
#elif defined(__x86_64__) || defined(_M_X64)
constexpr bool arm = false;
#else
#error Full native AOT needs an AArch64 or x86-64 host
#endif

class Assembly {
    std::ostringstream &out;
    std::string stem;
    const std::vector<Instruction> &code;
    MachineFunction fn;
    std::vector<std::size_t> locations;
    struct Fixup { std::size_t instruction; int target; };
    std::vector<Fixup> fixups;
    std::vector<std::size_t> errors;
    std::vector<int> switchEnds, handlers;
    std::vector<std::size_t> returns;
    std::size_t depth=0;
    void emit(MachineOpcode op,int immediate=0,std::size_t operand=0,std::string symbol={}) {
        fn.instructions.push_back({op,immediate,operand,depth,std::move(symbol)});
        switch(op) {
            case MachineOpcode::PushConstant: case MachineOpcode::LoadSlot: ++depth;break;
            case MachineOpcode::Call: depth=depth-operand+1;break;
            case MachineOpcode::Drop: case MachineOpcode::BranchZero: case MachineOpcode::Return: --depth;break;
            case MachineOpcode::Equal: --depth;break;
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
public:
    Assembly(std::ostringstream &stream,std::string symbol,const std::vector<Instruction> &instructions)
        :out(stream),stem(std::move(symbol)),code(instructions){fn.parameterCount=1;fn.slotCount=2;}
    void emit() {
        locations.resize(code.size()+1);
        for(std::size_t i=0;i<code.size();++i) {
            locations[i]=fn.instructions.size();const auto &v=code[i];
            switch(v.op) {
                case OP_HAM:case OP_NEU:case OP_DIEU_KIEN:case OP_LAP:case OP_CAP_NHAT:
                case OP_DONG_LENH:case OP_MO_NGOAC:case OP_DONG_NGOAC:case OP_MO_KHOI:case OP_DONG_KHOI:break;
                case OP_BIEN_SO:call("integer",{v.operand});break;
                case OP_TEN_BIEN_ID:call("integer",{v.operandIndex});break;
                case OP_TEN_BIEN_GIA_TRI:call("load",{v.operandIndex});break;
                case OP_CHUOI:call("string",{v.operandIndex});break;
                case OP_BIEN_SO_FLOAT:call("floating",{v.operandIndex});break;
                case OP_RONG_GIA_TRI:call("null");break;
                case OP_DUNG_GIA_TRI:call("integer",{1});break;
                case OP_SAI_GIA_TRI:call("integer",{0});break;
                case OP_KHOI_TAO:call("initialize",{v.operandIndex});break;
                case OP_GAN:call("assign");break;
                case OP_PARAM:call("parameter",{v.operandIndex,v.operandValue,-1});break;
                case OP_PARAM_MAC_DINH:call("parameter",{v.operandIndex,v.operandValue,v.operand});break;
                case OP_CONG_MOT:case OP_TRU_MOT:call("increment",{v.op==OP_CONG_MOT?1:-1});break;
                case OP_CONG:case OP_TRU:case OP_NHAN:case OP_CHIA:case OP_MODULO:
                case OP_Logic_VA:case OP_Logic_HOAC:case OP_SO_SANH_BANG:case OP_KHAC_BANG:
                case OP_LON_HON:case OP_NHO_HON:case OP_LON_HON_HOAC_BANG:case OP_NHO_HON_HOAC_BANG:call("binary",{v.op});break;
                case OP_KHONG:case OP_PHU_DINH:call("negate");break;
                case OP_LIST_LITERAL:call("list",{v.operand,v.operandIndex});break;
                case OP_MAP_LITERAL:call("map",{v.operand,v.operandIndex});break;
                case OP_DOC_CHI_SO:call("index");break;
                case OP_GAN_CHI_SO:call("set_index");break;
                case OP_GOI:call("call",{v.operandIndex,v.operand});break;
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
    for(const auto &item:array(wire[2])) {
        const auto &fields=array(item);if(fields.size()!=3)throw std::runtime_error("AOT: function record");
        if(!snapshot.functions.emplace(number(fields[0]),FunctionCode{number(fields[1]),instructions(fields[2])}).second)
            throw std::runtime_error("AOT: duplicate function ID");
    }
    for(const auto &item:array(wire[3])) {
        const auto &fields=array(item);if(fields.size()!=2)throw std::runtime_error("AOT: initializer record");
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
    for(const auto &[id,function]:snapshot.functions)Assembly(out,"vpp_aot_function_"+std::to_string(id),function.instructions).emit();
    for(std::size_t i=0;i<snapshot.initializers.size();++i)Assembly(out,"vpp_aot_init_"+std::to_string(i),snapshot.initializers[i]).emit();
    Assembly(out,"vpp_aot_entry",snapshot.entry).emit();
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
    out << ".p2align 3\n" << prefix << "vpp_aot_spec:\n.quad " << snapshot.strings.size() << ",.Laot_strings," << functionCount << ",.Laot_functions,"
        << snapshot.initializers.size() << ",.Laot_initializers," << prefix << "vpp_aot_entry\n.long " << int(compiler) << ",0\n.quad " << snapshot.foreignFunctions.size() << ",.Laot_foreign\n";
out << "\n#define VPP_NATIVE_DYNAMIC_RUNTIME 1\n#include \"vpp_aot_dynamic_runtime.inc\"\n";
    return out.str();
}
} // namespace vietvm::native::aot

// Independent full-language lowering oracle. The V++ backend receives the
// exact same wire arrays; compare assembled objects, not just source text.
#include "vpp/native/aot.h"
#include "vpp/native/target.h"
#include "vpp/bytecode/intrinsic.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace vietvm::native;
using namespace vietvm::native::aot;

namespace {
std::string literal(const std::string &text) {
    std::string result="chuỗi từ byte máy ảo([";
    bool first=true;
    for(unsigned char byte:text) {
        if(!first)result+=',';first=false;
        result+=std::to_string(byte);
    }
    return result+"])";
}
void writeCode(std::ostream &out,const std::vector<Instruction> &code) {
    out<<'[';bool first=true;
    for(const auto &op:code) {
        if(!first)out<<',';first=false;
        out<<'['<<op.op<<','<<op.operand<<','<<op.operandIndex<<','<<op.operandValue<<']';
    }
    out<<']';
}
Snapshot fixture() {
    Snapshot snapshot;
    snapshot.strings={"oracle_function","i:7",u8"tiếng Việt\n\"\\123"};
    snapshot.strings.back().push_back('\0');snapshot.strings.back()+="9tail";
    auto &code=snapshot.entry;
    for(int op:{OP_HAM,OP_NEU,OP_DIEU_KIEN,OP_LAP,OP_CAP_NHAT,
                OP_DONG_LENH,OP_MO_NGOAC,OP_DONG_NGOAC,OP_MO_KHOI,OP_DONG_KHOI})
        code.push_back({op,0,0,0});
    for(int op:{OP_BIEN_SO,OP_TEN_BIEN_ID,OP_TEN_BIEN_GIA_TRI,OP_CHUOI,
                OP_BIEN_SO_FLOAT,OP_RONG_GIA_TRI,OP_DUNG_GIA_TRI,OP_SAI_GIA_TRI,
                OP_KHOI_TAO,OP_GAN,OP_PARAM,OP_PARAM_MAC_DINH,OP_CONG_MOT,OP_TRU_MOT,
                OP_CONG,OP_TRU,OP_NHAN,OP_CHIA,OP_MODULO,OP_Logic_VA,OP_Logic_HOAC,
                OP_SO_SANH_BANG,OP_KHAC_BANG,OP_LON_HON,OP_NHO_HON,OP_LON_HON_HOAC_BANG,
                OP_NHO_HON_HOAC_BANG,OP_KHONG,OP_PHU_DINH,OP_LIST_LITERAL,OP_MAP_LITERAL,
                OP_DOC_CHI_SO,OP_GAN_CHI_SO,OP_GOI,OP_GOI_GIAN_TIEP,OP_TAO_DONG_BAO,
                OP_TAO_LOP,OP_THEM_PHUONG_THUC,OP_TAO_DOI_TUONG,OP_DOC_THUOC_TINH,
                OP_GAN_THUOC_TINH,OP_GOI_PHUONG_THUC,OP_IN})
        code.push_back({op,1,2,3});
    const auto branch=code.size();code.push_back({OP_JUMP_IF_FALSE,0,0,0});
    const auto loop=code.size();code.push_back({OP_LAP,0,0,0});
    code.push_back({OP_BO_QUA,0,0,0});code.push_back({OP_CAP_NHAT,0,0,0});
    code.push_back({OP_JUMP,static_cast<int>(loop),0,0});
    code.push_back({OP_THOAT,0,-1,0});const auto breakLoop=code.size()-1;
    const auto beginSwitch=code.size();code.push_back({OP_CHON,0,0,0});
    const auto caseAt=code.size();code.push_back({OP_CA,1,2,0});
    const auto defaultAt=code.size();code.push_back({OP_MAC_DINH,0,0,0});
    code.push_back({OP_THOAT,0,0,0});code.push_back({OP_KET_THUC_CHUYEN,0,0,0});
    const auto beginTry=code.size();code.push_back({OP_THU,0,4,0});
    const auto endTry=code.size();code.push_back({OP_THU_KET_THUC,0,0,0});
    const auto catchAt=code.size();code.push_back({OP_BAT_LOI,0,4,0});
    code.push_back({OP_NEM,0,0,0});code.push_back({OP_FFI_CALL,2,0,0});
    for(const auto &intrinsic:vietvm::bytecode::kVmIntrinsics)
        code.push_back({intrinsic.opcode,0,0,0});
    code.push_back({OP_TRA_VE,0,0,0});code.push_back({OP_DUNG_CHUONG_TRINH,0,0,0});
    const auto end=static_cast<int>(code.size());
    code[branch].operand=static_cast<int>(beginSwitch);
    code[breakLoop].operand=end;code[beginSwitch].operand=end;
    code[caseAt].operandValue=static_cast<int>(defaultAt);
    code[defaultAt].operandValue=static_cast<int>(beginTry);
    code[beginTry].operand=static_cast<int>(catchAt);code[endTry].operand=end;
    snapshot.functions.emplace(0,FunctionCode{0,{
        {OP_PARAM,0,0,0},{OP_PARAM_MAC_DINH,1,1,1},{OP_PARAM,0,2,-1},
        {OP_BIEN_SO,-2147483647-1,0,0},{OP_TRA_VE,0,0,0}}});
    snapshot.functions.emplace(1,FunctionCode{-1,{{OP_RONG_GIA_TRI,0,0,0},{OP_TRA_VE,0,0,0}}});
    snapshot.initializers.push_back({{OP_CHUOI,0,2,0},{OP_IN,0,0,0}});
    vietvm::bytecode::ForeignFunctionDescriptor foreign;
    using T=vietvm::bytecode::ForeignAbiType;
    foreign.id=0;foreign.library="c";foreign.symbol="read";foreign.abi="c";
    foreign.capability="ffi";foreign.result=T::I32;
    foreign.parameters={T::BufferOut,T::I32};foreign.bufferExtents={{0,1,0}};
    snapshot.foreignFunctions.push_back(foreign);
    return snapshot;
}
}

int main(int argc,char **argv) {
    try {
        if(argc!=2)throw std::runtime_error("native AOT oracle needs output directory");
        const auto directory=std::filesystem::absolute(std::filesystem::u8path(argv[1]));
        const auto snapshot=fixture();
        std::ofstream cpp(directory/"cpp.S",std::ios::binary);
        auto assembly=emitAssembly(snapshot,true);
        // Darwin keeps .L labels in the symbol table. Use its private L
        // prefix for the oracle's temporary branch labels, which the byte
        // emitter does not need. Code, relocations and ABI symbols stay intact.
        if(hostTarget().os==OperatingSystem::MacOS) {
            std::size_t position=0;
            while((position=assembly.find(".Lvpp_aot_",position))!=std::string::npos) {
                assembly.erase(position,1);position+=9;
            }
        }
        cpp<<assembly;
        std::ofstream vi(directory/"emitter.vi",std::ios::binary);
        vi<<u8"nhập gói/biên dịch/asm chương trình;\nnhập gói/nhập xuất/tệp;\nhàm chính() { dây = [";
        writeCode(vi,snapshot.entry);vi<<",[";
        for(std::size_t i=0;i<snapshot.strings.size();++i) {
            if(i)vi<<',';vi<<literal(snapshot.strings[i]);
        }
        vi<<"],[";bool first=true;
        for(const auto &[id,function]:snapshot.functions) {
            if(!first)vi<<',';first=false;
            vi<<'['<<id<<','<<function.nameIndex<<',';writeCode(vi,function.instructions);vi<<']';
        }
        vi<<"],[["<<literal("oracle-init")<<',';writeCode(vi,snapshot.initializers[0]);
        vi<<"]],[[0,"<<literal("c")<<','<<literal("read")<<",["
          <<static_cast<int>(snapshot.foreignFunctions[0].parameters[0])<<",1],[[0,1,0]],1,"
          <<literal("c")<<','<<literal("ffi")<<"]]];\n";
        vi<<u8"ghi tệp("<<literal((directory/"vpp.S").u8string())
          <<u8", biên dịch asm chương trình(dây,"<<literal(canonicalTriple(hostTarget()))
          <<u8", đúng)); };\n";
        if(!cpp||!vi)throw std::runtime_error("native AOT oracle output failed");
        std::cout<<"Independent native AOT fixture: all helpers, intrinsics, CFG and ABI metadata\n";
        return 0;
    } catch(const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}

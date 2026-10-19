#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace vietvm::bytecode {

// Các kiểu ABI tối thiểu của FFI 1.0. Đây là kiểu tại biên C ABI, không phải
// hệ kiểu tĩnh cho ngôn ngữ V++.
enum class ForeignAbiType {
    Void,
    I32,
    U32,
    I64,
    U64,
    F64,
    CString,
    BufferOut,
    BufferIn,
    FileHandle,
    DirectoryHandle,
    DirectoryEntry,
    PointerStatus,
    DnsHandle,
    SocketHandle,
};

// Each pointer is a per-call copy owned by the VM. A relation can bind its
// exact byte count to an integer ABI parameter, or use a fixed byte count.
// Negative lengthParameterIndex means fixedLength bytes instead.
struct ForeignBufferExtent {
    std::size_t parameterIndex = 0;
    int lengthParameterIndex = -1;
    std::size_t fixedLength = 0;
};

// Mô tả một foreign function sau semantic/lowering. Descriptor chứa metadata
// bất biến; runtime mới resolve library/symbol thành địa chỉ native.
struct ForeignFunctionDescriptor {
    int id = -1;
    std::string library;
    std::string symbol;
    std::vector<ForeignAbiType> parameters;
    std::vector<ForeignBufferExtent> bufferExtents;
    ForeignAbiType result = ForeignAbiType::Void;
    std::string abi = "c";
    std::string capability;
};

inline const char *foreignAbiTypeName(ForeignAbiType type) noexcept {
    switch (type) {
        case ForeignAbiType::Void: return "void";
        case ForeignAbiType::I32: return "i32";
        case ForeignAbiType::U32: return "u32";
        case ForeignAbiType::I64: return "i64";
        case ForeignAbiType::U64: return "u64";
        case ForeignAbiType::F64: return "f64";
        case ForeignAbiType::CString: return "c_chuỗi";
        case ForeignAbiType::BufferOut: return "c_đệm_ra";
        case ForeignAbiType::BufferIn: return "c_đệm_vào";
        case ForeignAbiType::FileHandle: return "c_tệp";
        case ForeignAbiType::DirectoryHandle: return "c_thư_mục";
        case ForeignAbiType::DirectoryEntry: return "c_mục_thư_mục";
        case ForeignAbiType::PointerStatus: return "c_cờ_con_trỏ";
        case ForeignAbiType::DnsHandle: return "c_dns";
        case ForeignAbiType::SocketHandle: return "c_socket";
    }
    return "unknown";
}

} // namespace vietvm::bytecode

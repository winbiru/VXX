#include <stdexcept>
#include <vector>
#include <stack>
#include <variant>
#include <string>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <cstdio>
#include <stdio.h>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <optional>
#include <limits>
#include <unordered_map>
#include "vm/vm.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <memory>
#include <atomic>
#include "common/vm_utils.h"
#include "common/vm_native_collection_helpers.h"
#include "common/vm_native_compiler_helpers.h"
#include "common/vm_native_m3_helpers.h"
#include "common/vm_native_helpers.h"
#include "common/vm_native_constants.h"
#include "common/vm_native_stdlib_helpers.h"
#include "vpp/bytecode/intrinsic.h"
#include "vpp/bytecode/literal_wire.h"
#include "vpp/bytecode/verifier.h"
#include "vpp/core/message_constants.h"
#include "vpp/core/text.h"
#include "vpp/runtime/vm_fixture.h"
#include "vpp/runtime/object.h"

#if defined(_WIN32) && defined(_MSC_VER)
#ifndef popen
#define popen _popen
#endif
#ifndef pclose
#define pclose _pclose
#endif
#endif

using vietvm::helpers::requireNativeArgumentCount;

enum class NativeFailureDisposition {
    RuntimeError,
    CatchableLanguageError,
};

namespace {

// Tín hiệu nội bộ dùng để dừng cooperative worker. Không dùng LanguageException
// vì mã V++ có thể bắt LanguageException và vô tình nuốt yêu cầu hủy.
struct VmWorkerCancelled final {};

// Sao chép sâu graph StackValue qua biên OS thread. Các allocation được tạo trực
// tiếp bằng shared_ptr (không gọi factory runtime) để snapshot không bị đăng ký
// nhầm vào heap của thread nguồn. Worker/VM đích sẽ track graph sau khi nhận.
class ThreadValueCloner {
public:
    StackValue clone(const StackValue &value) {
        if (std::holds_alternative<int>(value) ||
            std::holds_alternative<double>(value) ||
            std::holds_alternative<std::string>(value) ||
            std::holds_alternative<std::monostate>(value)) {
            return value;
        }
        if (std::holds_alternative<MapHandle>(value)) {
            return StackValue(cloneMap(std::get<MapHandle>(value)));
        }
        if (std::holds_alternative<ListHandle>(value)) {
            return StackValue(cloneList(std::get<ListHandle>(value)));
        }
        if (std::holds_alternative<TupleHandle>(value)) {
            return StackValue(cloneTuple(std::get<TupleHandle>(value)));
        }
        if (std::holds_alternative<ClassHandle>(value)) {
            return StackValue(cloneClass(std::get<ClassHandle>(value)));
        }
        if (std::holds_alternative<InstanceHandle>(value)) {
            return StackValue(cloneInstance(std::get<InstanceHandle>(value)));
        }
        return StackValue(cloneClosure(std::get<ClosureHandle>(value)));
    }

    ClassHandle cloneClassHandle(const ClassHandle &value) { return cloneClass(value); }

private:
    std::unordered_map<const MapValue *, MapHandle> maps_;
    std::unordered_map<const vietvm::runtime::ListValue *, ListHandle> lists_;
    std::unordered_map<const vietvm::runtime::TupleValue *, TupleHandle> tuples_;
    std::unordered_map<const vietvm::runtime::RuntimeClass *, ClassHandle> classes_;
    std::unordered_map<const vietvm::runtime::RuntimeInstance *, InstanceHandle> instances_;
    std::unordered_map<const vietvm::runtime::RuntimeClosure *, ClosureHandle> closures_;
    std::unordered_map<const vietvm::runtime::RuntimeCell *, CellHandle> cells_;

    MapHandle cloneMap(const MapHandle &value) {
        if (value == nullptr) return nullptr;
        const auto found = maps_.find(value.get());
        if (found != maps_.end()) return found->second;
        auto copy = std::make_shared<MapValue>();
        maps_.emplace(value.get(), copy);
        for (const auto &[key, item] : value->entries) {
            copy->entries.emplace(key, clone(item));
        }
        return copy;
    }

    ListHandle cloneList(const ListHandle &value) {
        if (value == nullptr) return nullptr;
        const auto found = lists_.find(value.get());
        if (found != lists_.end()) return found->second;
        auto copy = std::make_shared<vietvm::runtime::ListValue>();
        lists_.emplace(value.get(), copy);
        copy->elements.reserve(value->elements.size());
        for (const StackValue &item : value->elements) copy->elements.push_back(clone(item));
        return copy;
    }

    TupleHandle cloneTuple(const TupleHandle &value) {
        if (value == nullptr) return nullptr;
        const auto found = tuples_.find(value.get());
        if (found != tuples_.end()) return found->second;
        auto copy = std::make_shared<vietvm::runtime::TupleValue>();
        tuples_.emplace(value.get(), copy);
        copy->elements.reserve(value->elements.size());
        for (const StackValue &item : value->elements) copy->elements.push_back(clone(item));
        return copy;
    }

    ClassHandle cloneClass(const ClassHandle &value) {
        if (value == nullptr) return nullptr;
        const auto found = classes_.find(value.get());
        if (found != classes_.end()) return found->second;
        auto copy = std::make_shared<vietvm::runtime::RuntimeClass>();
        classes_.emplace(value.get(), copy);
        copy->name = value->name;
        copy->methods = value->methods;
        copy->superclass = cloneClass(value->superclass);
        return copy;
    }

    InstanceHandle cloneInstance(const InstanceHandle &value) {
        if (value == nullptr) return nullptr;
        const auto found = instances_.find(value.get());
        if (found != instances_.end()) return found->second;
        auto copy = std::make_shared<vietvm::runtime::RuntimeInstance>();
        instances_.emplace(value.get(), copy);
        copy->klass = cloneClass(value->klass);
        for (const auto &[key, item] : value->fields) {
            copy->fields.emplace(key, clone(item));
        }
        return copy;
    }

    CellHandle cloneCell(const CellHandle &value) {
        if (value == nullptr) return nullptr;
        const auto found = cells_.find(value.get());
        if (found != cells_.end()) return found->second;
        auto copy = std::make_shared<vietvm::runtime::RuntimeCell>();
        cells_.emplace(value.get(), copy);
        copy->value = clone(value->value);
        return copy;
    }

    ClosureHandle cloneClosure(const ClosureHandle &value) {
        if (value == nullptr) return nullptr;
        const auto found = closures_.find(value.get());
        if (found != closures_.end()) return found->second;
        auto copy = std::make_shared<vietvm::runtime::RuntimeClosure>();
        closures_.emplace(value.get(), copy);
        copy->functionId = value->functionId;
        for (const auto &[slot, cell] : value->captures) {
            copy->captures.emplace(slot, cloneCell(cell));
        }
        return copy;
    }
};

std::string languageExceptionText(const vietvm::runtime::LanguageException &error) {
    if (std::holds_alternative<std::string>(error.value())) {
        return std::get<std::string>(error.value());
    }
    return sv_to_string(error.value());
}

} // namespace

// Kiểm tra quyền gọi một method runtime dựa trên visibility, lớp đang thực thi
// và lớp sở hữu method. Private chỉ cho chính lớp sở hữu; protected cho cả lớp con.
static bool canAccessRuntimeMethod(
    vietvm::runtime::RuntimeMemberVisibility visibility,
    const ClassHandle &callerClass,
    const ClassHandle &ownerClass) {
    switch (visibility) {
        case vietvm::runtime::RuntimeMemberVisibility::Public:
            return true;
        case vietvm::runtime::RuntimeMemberVisibility::Private:
            return callerClass != nullptr && callerClass == ownerClass;
        case vietvm::runtime::RuntimeMemberVisibility::Protected:
            return callerClass != nullptr &&
                   vietvm::runtime::isSubclassOf(callerClass, ownerClass);
    }
    return false;
}

// Trả message runtime phù hợp khi một method bị visibility chặn; hàm giữ
// diagnostic private/protected ổn định cho cả constructor và bound method call.
static std::string runtimeMethodAccessMessage(
    vietvm::runtime::RuntimeMemberVisibility visibility,
    const std::string &methodName) {
    const auto &message = visibility == vietvm::runtime::RuntimeMemberVisibility::Private
        ? vietvm::messages::kVmObjectPrivateMethodAccess
        : vietvm::messages::kVmObjectProtectedMethodAccess;
    return vietvm::messages::formatMessage(message, {methodName});
}

// Primitive file I/O. C++ chỉ mở/đọc/ghi đúng tệp được yêu cầu; chuẩn hóa
// đường dẫn, kiểm tra byte và các thuật toán xử lý nội dung thuộc thư viện V++.
static bool executeNativeFilePrimitive(Opcode opcode,
                                       const std::vector<StackValue> &args,
                                       StackValue &result,
                                       std::string &err) {
    const bool readText = opcode == OP_VM_IO_DOC_FILE;
    const bool writeText = opcode == OP_VM_IO_GHI_FILE;
    const bool readBytes = opcode == OP_VM_IO_DOC_BYTES;
    const bool writeBytes = opcode == OP_VM_IO_GHI_BYTES;
    const bool appendText = opcode == OP_VM_IO_GHI_TIEP_FILE;
    if (!readText && !writeText && !readBytes && !writeBytes && !appendText) {
        return false;
    }

    const std::string operation = readText ? "đọc tệp"
        : writeText ? "ghi tệp"
        : readBytes ? "đọc bytes tệp"
        : writeBytes ? "ghi bytes tệp"
        : "ghi nối tệp";
    const std::size_t arity = (readText || readBytes) ? 1u : 2u;
    if (!requireNativeArgumentCount(args, operation, arity, err)) return true;
    std::filesystem::path path;
    if (!vietvm::helpers::nativeUtf8Path(args[0], operation, path, err)) return true;

    if (readText) {
        std::ifstream ifs(path);
        if (!ifs.is_open()) {
            err = vietvm::messages::formatMessage(
                vietvm::messages::kNativeFileOpenForReadFailed, {operation});
            return true;
        }
        std::ostringstream ss;
        ss << ifs.rdbuf();
        result = make_string_value(ss.str());
        return true;
    }

    if (readBytes) {
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs.is_open()) {
            err = vietvm::messages::formatMessage(
                vietvm::messages::kNativeFileOpenForReadFailed, {operation});
            return true;
        }
        std::vector<StackValue> bytes;
        char byte = 0;
        while (ifs.get(byte)) {
            bytes.push_back(make_int_value(static_cast<unsigned char>(byte)));
        }
        if (!ifs.eof()) {
            err = vietvm::messages::formatMessage(
                vietvm::messages::kNativeFileOpenForReadFailed, {operation});
            return true;
        }
        result = make_list_value(std::move(bytes));
        return true;
    }

    if (writeBytes) {
        ListHandle list;
        if (!vietvm::helpers::getListArgument(args, 1, operation, list, err)) return true;
        std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
        if (!ofs.is_open()) {
            err = vietvm::messages::formatMessage(
                vietvm::messages::kNativeFileOpenForWriteFailed, {operation});
            return true;
        }
        for (const StackValue &item : list->elements) {
            if (!std::holds_alternative<int>(item)) {
                err = operation + ": mỗi byte phải là số nguyên";
                return true;
            }
            const int value = std::get<int>(item);
            if (value < 0 || value > 255) {
                err = operation + ": byte phải trong 0..255";
                return true;
            }
            ofs.put(static_cast<char>(static_cast<unsigned char>(value)));
        }
        if (!ofs.good()) {
            err = vietvm::messages::formatMessage(
                vietvm::messages::kNativeFileWriteFailed, {operation});
            return true;
        }
        result = make_int_value(1);
        return true;
    }

    const std::ios::openmode mode = appendText
        ? (std::ios::out | std::ios::app)
        : std::ios::out;
    std::ofstream ofs(path, mode);
    if (!ofs.is_open()) {
        err = vietvm::messages::formatMessage(
            vietvm::messages::kNativeFileOpenForWriteFailed, {operation});
        return true;
    }
    ofs << vietvm::helpers::argToRawString(args[1]);
    if (!ofs.good()) {
        err = vietvm::messages::formatMessage(
            vietvm::messages::kNativeFileWriteFailed, {operation});
        return true;
    }
    result = make_int_value(1);
    return true;
}

// Bytecode cũ có thể gọi intrinsic theo tên thay vì opcode. Chỉ giải tên ở
// đây; mọi thực thi (kể cả thread, process, bytes) dùng chung opcode dispatcher.
static const vietvm::bytecode::IntrinsicDescriptor *resolveLegacyIntrinsic(
    int hamIdOrName,
    const std::vector<std::string> &stringPool,
    const std::unordered_map<int, int> &functionTableByNameIndex,
    const std::unordered_map<int, std::vector<Instruction>> &functionBytecode) {
    std::string fn;
    if (hamIdOrName < 0) {
        int nameIdx = -(hamIdOrName + 1);
        if (nameIdx >= 0 && nameIdx < (int)stringPool.size()) fn = stringPool[nameIdx];
    } else {
        for (const auto &entry : functionTableByNameIndex) {
            if (entry.second == hamIdOrName) {
                int nameIdx = entry.first;
                if (nameIdx >= 0 && nameIdx < (int)stringPool.size()) fn = stringPool[nameIdx];
                break;
            }
        }
    }
    if (fn.empty() && hamIdOrName >= 0 && hamIdOrName < (int)stringPool.size()) {
        // Compatibility path: only treat positive value as a nameIndex
        // when it is not already a concrete function id.
        if (functionBytecode.find(hamIdOrName) == functionBytecode.end()) {
            fn = stringPool[hamIdOrName];
        }
    }

    return fn.empty() ? nullptr : vietvm::bytecode::intrinsicByName(fn);
}

// Only representation/OS primitives live here. Library algorithms run as V++ bytecode.
static bool executeVmPrimitive(Opcode opcode,
                               const std::vector<StackValue> &args,
                               StackValue &result,
                               std::string &err,
                               NativeFailureDisposition &failureDisposition) {
    const auto *primitive = vietvm::bytecode::intrinsicByOpcode(opcode);
    if (primitive == nullptr) return false;
    const std::string fn(primitive->name);
    switch (opcode) {
        case OP_VM_LENGTH:
        case OP_VM_LIST_APPEND:
        case OP_VM_LIST_REMOVE:
        case OP_VM_MAP_HAS:
        case OP_VM_MAP_REMOVE:
        case OP_VM_MAP_KEYS:
            return vietvm::helpers::handleNativeCollectionFunction(opcode, args, result, err);
        case OP_VM_TYPE_OF:
        case OP_VM_IDENTITY_HASH:
        case OP_VM_DOC_BIEN_MOI_TRUONG:
        case OP_VM_DONG_HO_DIA_PHUONG:
        case OP_VM_DONG_HO_UTC:
        case OP_VM_DUONG_DAN_TON_TAI:
        case OP_VM_LA_TEP:
        case OP_VM_LA_THU_MUC_KHONG_THEO_LIEN_KET:
        case OP_VM_LA_THU_MUC:
        case OP_VM_LIET_KE_THU_MUC:
        case OP_VM_NGAU_NHIEN_BAO_MAT_BYTES:
        case OP_VM_NGU_MILI_GIAY:
        case OP_VM_TAO_THU_MUC:
        case OP_VM_TEN_NEN_TANG:
        case OP_VM_THOI_GIAN_DON_DIEU_MS:
        case OP_VM_XOA_DUONG_DAN:
            return vietvm::helpers::handleNativeFoundationFunction(opcode, args, result, err);
        case OP_VM_DNS_PHAN_GIAI:
        case OP_VM_SOCKET_CHAP_NHAN:
        case OP_VM_SOCKET_DAT_TIMEOUT:
        case OP_VM_SOCKET_DONG:
        case OP_VM_SOCKET_GUI:
        case OP_VM_SOCKET_NHAN:
        case OP_VM_SOCKET_PHAN_GIAI:
        case OP_VM_SOCKET_TCP_LANG_NGHE:
        case OP_VM_SOCKET_TCP_MO:
        case OP_VM_SOCKET_TLS_NANG_CAP:
        case OP_VM_SOCKET_UDP_MO:
            return vietvm::helpers::handleNativeM3LibraryFunction(opcode, args, result, err);
        case OP_VM_IO_DOC_BYTES:
        case OP_VM_IO_DOC_FILE:
        case OP_VM_IO_GHI_BYTES:
        case OP_VM_IO_GHI_FILE:
        case OP_VM_IO_GHI_TIEP_FILE:
            failureDisposition = NativeFailureDisposition::RuntimeError;
            return executeNativeFilePrimitive(opcode, args, result, err);
        case OP_VM_TIEN_TRINH_CHAY:
            return vietvm::helpers::handleNativeProcessPrimitive(args, result, err);
        case OP_VM_BIEN_DICH_PHAN_TICH:
            return vietvm::helpers::compilerAnalyzePrimitive(args, result, err);
        case OP_VM_KICH_BAN_CHAY:
            return vietvm::helpers::embeddedVmRunPrimitive(args, result, err);
        case OP_VM_TUPLE_FROM_LIST:
            if (std::holds_alternative<ListHandle>(args[0])) {
                const ListHandle &list = std::get<ListHandle>(args[0]);
                result = make_tuple_value(list == nullptr ? std::vector<StackValue>{} : list->elements);
            } else {
                err = "bộ: primitive cần danh sách";
            }
            return true;
        case OP_VM_STRING_BYTES: {
            if (!std::holds_alternative<std::string>(args[0])) {
                err = fn + ": giá trị phải là chuỗi";
                return true;
            }
            const auto &text = std::get<std::string>(args[0]);
            std::vector<StackValue> bytes;
            bytes.reserve(text.size());
            for (unsigned char byte : text) bytes.push_back(make_int_value(byte));
            result = make_list_value(std::move(bytes));
            return true;
        }
        case OP_VM_STRING_FROM_BYTES:
        case OP_VM_FLOAT_FROM_BITS: {
            ListHandle list;
            if (!vietvm::helpers::getFirstListArgument(args, fn, list, err)) return true;
            if (opcode == OP_VM_FLOAT_FROM_BITS && list->elements.size() != 8) {
                err = fn + ": cần đúng 8 byte IEEE-754";
                return true;
            }
            std::string bytes;
            bytes.reserve(list->elements.size());
            for (const auto &item : list->elements) {
                if (!std::holds_alternative<int>(item) || std::get<int>(item) < 0 ||
                    std::get<int>(item) > 255) {
                    err = fn + ": mỗi byte phải là số nguyên trong 0..255";
                    return true;
                }
                bytes.push_back(static_cast<char>(std::get<int>(item)));
            }
            if (opcode == OP_VM_STRING_FROM_BYTES) {
                // Raw storage bridge, including NUL/malformed bytes. UTF-8 policy is in V++.
                result = make_string_value(std::move(bytes));
            } else {
                std::uint64_t bits = 0;
                for (unsigned i = 0; i < 8; ++i) {
                    bits |= std::uint64_t(static_cast<unsigned char>(bytes[i])) << (i * 8);
                }
                double number;
                static_assert(sizeof(number) == sizeof(bits) && std::numeric_limits<double>::is_iec559);
                std::memcpy(&number, &bits, sizeof(number));
                result = make_float_value(number);
            }
            return true;
        }
        case OP_VM_SO_THUC_BITS: {
            if (!isNumeric(args[0])) {
                err = fn + ": giá trị phải là số";
                return true;
            }
            const double number = toDouble(args[0]);
            std::uint64_t bits = 0;
            static_assert(sizeof(bits) == sizeof(number) && std::numeric_limits<double>::is_iec559);
            std::memcpy(&bits, &number, sizeof(bits));
            std::vector<StackValue> bytes;
            bytes.reserve(8);
            for (unsigned shift = 0; shift < 64; shift += 8) {
                bytes.push_back(make_int_value(static_cast<int>((bits >> shift) & 0xffu)));
            }
            result = make_list_value(std::move(bytes));
            return true;
        }
        default:
            return false;
    }
}

// Giải mã ánh xạ from chuỗi bể dữ liệu; hàm đọc biểu diễn đã mã hóa, kiểm tra định dạng và dựng lại giá trị runtime tương ứng.
static MapValue decodeMapFromStringPool(const std::string &encoded);
// Giải mã danh sách from chuỗi bể dữ liệu; hàm đọc biểu diễn đã mã hóa, kiểm tra định dạng và dựng lại giá trị runtime tương ứng.
static std::vector<StackValue> decodeListFromStringPool(const std::string &encoded);

// Giải mã ánh xạ from chuỗi bể dữ liệu; hàm đọc biểu diễn đã mã hóa, kiểm tra định dạng và dựng lại giá trị runtime tương ứng.
static MapValue decodeMapFromStringPool(const std::string &encoded) {
    constexpr char RS = vietvm::bytecode::kLiteralRecordSeparator;
    constexpr char FS = vietvm::bytecode::kLiteralFieldSeparator;

    MapValue m;
    if (encoded.empty()) return m;

    size_t start = 0;
    while (start <= encoded.size()) {
        size_t end = encoded.find(RS, start);
        if (end == std::string::npos) end = encoded.size();
        std::string record = encoded.substr(start, end - start);
        if (!record.empty()) {
            size_t p1 = record.find(FS);
            size_t p2 = (p1 == std::string::npos) ? std::string::npos : record.find(FS, p1 + 1);
            if (p1 == std::string::npos || p2 == std::string::npos) {
                throw std::runtime_error(vietvm::messages::formatMessage(
                    vietvm::messages::kVmMapLiteralEncodeInvalid));
            }

            std::string key = vietvm::bytecode::unescapeLiteralWireField(record.substr(0, p1));
            std::string typeTag = record.substr(p1 + 1, p2 - p1 - 1);
            std::string valRaw = vietvm::bytecode::unescapeLiteralWireField(record.substr(p2 + 1));

            if (typeTag == "i") {
                m.entries[key] = make_int_value(std::stoi(valRaw));
            } else if (typeTag == "d") {
                m.entries[key] = make_float_value(std::stod(valRaw));
            } else if (typeTag == "s") {
                m.entries[key] = make_string_value(valRaw);
            } else if (typeTag == "n") {
                m.entries[key] = make_null_value();
            } else if (typeTag == "l") {
                m.entries[key] = make_list_value(decodeListFromStringPool(valRaw));
            } else if (typeTag == "m") {
                m.entries[key] = make_map_value(decodeMapFromStringPool(valRaw));
            } else {
                throw std::runtime_error(vietvm::messages::formatMessage(
                    vietvm::messages::kVmMapLiteralTypeTagInvalid));
            }
        }

        if (end == encoded.size()) break;
        start = end + 1;
    }

    return m;
}

// Giải mã danh sách from chuỗi bể dữ liệu; hàm đọc biểu diễn đã mã hóa, kiểm tra định dạng và dựng lại giá trị runtime tương ứng.
static std::vector<StackValue> decodeListFromStringPool(const std::string &encoded) {
    constexpr char RS = vietvm::bytecode::kLiteralRecordSeparator;
    constexpr char FS = vietvm::bytecode::kLiteralFieldSeparator;
    std::vector<StackValue> list;
    if (encoded.empty()) return list;

    size_t start = 0;
    while (start <= encoded.size()) {
        size_t end = encoded.find(RS, start);
        if (end == std::string::npos) end = encoded.size();
        const std::string record = encoded.substr(start, end - start);
        const size_t separator = record.find(FS);
        if (separator == std::string::npos) {
            throw std::runtime_error(vietvm::messages::formatMessage(
                vietvm::messages::kVmMapLiteralEncodeInvalid));
        }
        const std::string typeTag = record.substr(0, separator);
        const std::string value = vietvm::bytecode::unescapeLiteralWireField(
            record.substr(separator + 1));
        if (typeTag == "i") list.push_back(make_int_value(std::stoi(value)));
        else if (typeTag == "d") list.push_back(make_float_value(std::stod(value)));
        else if (typeTag == "s") list.push_back(make_string_value(value));
        else if (typeTag == "n") list.push_back(make_null_value());
        else if (typeTag == "l") {
            list.push_back(make_list_value(decodeListFromStringPool(value)));
        }
        else if (typeTag == "m") {
            list.push_back(make_map_value(decodeMapFromStringPool(value)));
        }
        else throw std::runtime_error(vietvm::messages::formatMessage(
            vietvm::messages::kVmMapLiteralTypeTagInvalid));
        if (end == encoded.size()) break;
        start = end + 1;
    }
    return list;
}

// Giải mã mặc định param giá trị; hàm đọc biểu diễn đã mã hóa, kiểm tra định dạng và dựng lại giá trị runtime tương ứng.
static StackValue decodeDefaultParamValue(const std::string &encoded) {
    size_t colon = encoded.find(':');
    if (colon == std::string::npos) {
        throw std::runtime_error(vietvm::messages::formatMessage(
            vietvm::messages::kVmDefaultParamEncodeInvalid));
    }
    std::string tag = encoded.substr(0, colon);
    std::string payload = encoded.substr(colon + 1);

    if (tag == "i") return make_int_value(std::stoi(payload));
    if (tag == "d") return make_float_value(std::stod(payload));
    if (tag == "s") return make_string_value(payload);
    if (tag == "n") return make_null_value();
    throw std::runtime_error(vietvm::messages::formatMessage(
        vietvm::messages::kVmDefaultParamTypeInvalid));
}

// Khởi tạo máy ảo với bytecode hoặc trạng thái runtime được cung cấp; constructor thiết lập stack, frame và các bảng cần cho vòng thực thi.
VM::VM(const std::vector<Instruction>& code)
    : VM(code, std::vector<std::string>{}) {}

VM::VM(const std::vector<Instruction>& code, const std::vector<std::string>& pool)
    : bytecode(code), stringPool(pool), pc(0) {}

VM::~VM() {
    if (threadRuntimeOwner_) shutdownThreadRuntime();
}

// Thiết lập đầu ra sink; hàm ghi giá trị đầu vào vào trạng thái đích và thay thế giá trị cũ nếu đã tồn tại.
void VM::setOutputSink(OutputSink sink) {
    outputSink = std::move(sink);
}

// Thêm mô-đun khởi tạo; hàm chèn dữ liệu mới vào cấu trúc trạng thái hiện tại và duy trì các chỉ mục liên quan.
bool VM::addModuleInitializer(std::string identity,
                              std::vector<Instruction> initializer,
                              std::vector<vietvm::runtime::RuntimeSourceLocation> debugInfo) {
    const bool added = moduleTable.add(std::move(identity), std::move(initializer),
                                       std::move(debugInfo));
    if (added) invalidateBytecodeVerification();
    return added;
}

// Gắn metadata vị trí nguồn cho bytecode gốc và từng hàm; VM giữ metadata tách khỏi
// instruction để dựng stack trace mà không làm thay đổi định dạng bytecode.
void VM::setDebugInfo(
    std::vector<vietvm::runtime::RuntimeSourceLocation> rootDebugInfo,
    std::unordered_map<int, std::vector<vietvm::runtime::RuntimeSourceLocation>>
        functionDebug) {
    bytecodeDebugInfo = std::move(rootDebugInfo);
    functionDebugInfo = std::move(functionDebug);
}

void VM::setFunctions(
    std::unordered_map<int, std::vector<Instruction>> functionBytecode,
    std::unordered_map<int, int> functionNames) {
    functionNameIndexById_.clear();
    functionNameIndexById_.reserve(functionNames.size());
    for (const auto &[nameIndex, functionId] : functionNames) {
        functionNameIndexById_.emplace(functionId, nameIndex);
    }

    hamBytecodeMap = std::move(functionBytecode);
    functionTableByNameIndex = std::move(functionNames);
    invalidateBytecodeVerification();
}

void VM::invalidateBytecodeVerification() noexcept {
    ++programGeneration_;
    if (programGeneration_ == 0) {
        // Wraparound is practically unreachable, but keep 0 reserved for
        // "never verified" so stale cache state can never become valid.
        programGeneration_ = 1;
        verifiedGeneration_ = 0;
    }
}

void VM::ensureBytecodeVerified() {
    if (verifiedGeneration_ == programGeneration_) return;

    ++verificationPassCount_;

    vietvm::bytecode::BytecodeVerificationContext verificationContext;
    verificationContext.stringPoolSize = stringPool.size();
    for (const auto &entry : hamBytecodeMap) {
        verificationContext.functionIds.insert(entry.first);
    }

    const auto verifyOrThrow = [&](const std::vector<Instruction> &code,
                                   const std::string &scope) {
        const auto verification =
            vietvm::bytecode::verifyBytecode(code, verificationContext);
        if (!verification.has_value()) return;
        throw vietvm::runtime::RuntimeError(
            "bytecode không hợp lệ trong " + scope + " tại lệnh " +
            std::to_string(verification->instructionIndex) + ": " +
            verification->message,
            vietvm::runtime::RuntimeErrorKind::VmFault);
    };

    verifyOrThrow(bytecode, "chương trình chính");
    for (const auto &entry : hamBytecodeMap) {
        verifyOrThrow(entry.second, "hàm " + std::to_string(entry.first));
    }
    for (const std::string &identity : moduleTable.order()) {
        const vietvm::runtime::RuntimeModule *module = moduleTable.module(identity);
        if (module != nullptr) {
            verifyOrThrow(module->initializer, "module " + identity);
        }
    }

    verifiedGeneration_ = programGeneration_;
}

void VM::resetExecution() {
    if (!executionStack.empty() || !callStack.empty()) {
        throw std::logic_error(std::string(vietvm::messages::kVmResetWhileCallActive));
    }
    stack.clear();
    loopStartStack.clear();
    ifElseStack.clear();
    blockStack.clear();
    switchStack.clear();
    tryStack.clear();
    blockDepth = 0;
    callDepthFromRoot = 0;
    pc = 0;
    activeBytecode_ = nullptr;
    activeBytecodeDebugInfo_ = nullptr;
}

const std::vector<Instruction> &VM::currentBytecode() const noexcept {
    return activeBytecode_ == nullptr ? bytecode : *activeBytecode_;
}

const std::vector<vietvm::runtime::RuntimeSourceLocation> &
VM::currentBytecodeDebugInfo() const noexcept {
    if (activeBytecode_ == nullptr) return bytecodeDebugInfo;
    if (activeBytecodeDebugInfo_ != nullptr) return *activeBytecodeDebugInfo_;
    static const std::vector<vietvm::runtime::RuntimeSourceLocation> empty;
    return empty;
}

// Tra vị trí nguồn ứng với program counter hiện tại; nếu instruction chưa có
// metadata thì trả frame rỗng để quá trình unwind bỏ qua vị trí không xác định.
vietvm::runtime::RuntimeSourceLocation VM::sourceLocationForPc(
    std::size_t value) const {
    const auto &debugInfo = currentBytecodeDebugInfo();
    if (value >= debugInfo.size()) return {};
    return debugInfo[value];
}

// Trả trạng thái khởi tạo của module được yêu cầu; hàm tra `ModuleTable`/tracker hiện tại và không tự chạy initializer.
std::optional<vietvm::runtime::ModuleState> VM::moduleState(
    std::string_view identity) const noexcept {
    return moduleTable.state(identity);
}

// Khởi tạo các module runtime theo thứ tự phụ thuộc; tracker bảo đảm mỗi initializer chỉ chạy một lần và ghi trạng thái thành công/thất bại.
void VM::initializeModules() {
    for (const std::string &identity : moduleTable.order()) {
        const auto state = moduleTable.state(identity);
        if (!state.has_value()) continue;
        if (*state == vietvm::runtime::ModuleState::Initialized) continue;
        if (*state == vietvm::runtime::ModuleState::Failed) {
            throw vietvm::runtime::RuntimeError(
                vietvm::messages::formatMessage(
                    vietvm::messages::kVmModuleInitializationFailed, {identity}),
                vietvm::runtime::RuntimeErrorKind::ModuleInitialization,
                vietvm::runtime::runtimeModuleFacts(identity, false));
        }
        if (!moduleTable.begin(identity)) {
            throw vietvm::runtime::RuntimeError(
                vietvm::messages::formatMessage(
                    vietvm::messages::kVmModuleInitializationInvalidState, {identity}),
                vietvm::runtime::RuntimeErrorKind::ModuleInitialization,
                vietvm::runtime::runtimeModuleFacts(identity, false));
        }

        const vietvm::runtime::RuntimeModule *module = moduleTable.module(identity);
        try {
            VM initializer(module == nullptr ? std::vector<Instruction>{}
                                             : module->initializer,
                           stringPool);
            initializer.runtimeHeap = runtimeHeap;
            initializer.inheritedGcRoots = gcRoots();
            initializer.outputSink = outputSink;
            initializer.threadRuntime_ = threadRuntime_;
            initializer.threadRuntimeOwner_ = false;
            initializer.cancellationRequested_ = cancellationRequested_;
            initializer.variables = variables;
            initializer.classTable = classTable;
            initializer.setFunctions(hamBytecodeMap, functionTableByNameIndex);

            // `VM::run()` của VM cha đã verify root, toàn bộ function bytecode
            // và toàn bộ module initializer trước khi bất kỳ initializer nào
            // được phép tạo side effect. VM con nhận đúng cùng string pool và
            // function snapshot, còn root của nó chính là initializer vừa được
            // verifier của cha kiểm tra. Đánh dấu generation này đã verified để
            // không quét lại toàn bộ bảng hàm cho từng module.
            initializer.verifiedGeneration_ = initializer.programGeneration_;

            initializer.bytecodeDebugInfo =
                module == nullptr
                    ? std::vector<vietvm::runtime::RuntimeSourceLocation>{}
                    : module->debugInfo;
            initializer.functionDebugInfo = functionDebugInfo;
            initializer.run();
            variables = std::move(initializer.variables);
            classTable = std::move(initializer.classTable);
            (void)moduleTable.complete(identity);
        } catch (...) {
            (void)moduleTable.fail(identity);
            throw;
        }
    }
}

// Phát mã cho đầu ra; hàm duyệt biểu diễn đầu vào và sinh opcode/metadata tương ứng vào buffer bytecode đích.
void VM::emitOutput(const StackValue& value) {
    if (!outputSink) return;
    if (threadRuntime_ != nullptr && threadRuntime_->outputMutex != nullptr) {
        std::lock_guard<std::mutex> lock(*threadRuntime_->outputMutex);
        outputSink(sv_to_string(value) + "\n");
        return;
    }
    outputSink(sv_to_string(value) + "\n");
}

// Chuyển `StackValue` thành điều kiện luận lý theo quy tắc runtime của V++, dùng cho nhánh và vòng lặp.
bool toBool(const StackValue& value) {
    return stackValueTruthy(value);
}

void VM::trimExecutionCapacity() {
    constexpr std::size_t kMinimumSpare = 256;
    const auto shouldTrim = [](std::size_t size, std::size_t capacity) {
        return capacity > size + kMinimumSpare && capacity > size * 2;
    };
    const auto trimVector = [&](auto &values) {
        if (shouldTrim(values.size(), values.capacity())) values.shrink_to_fit();
    };

    trimVector(stack);
    trimVector(callStack);
    trimVector(loopStartStack);
    trimVector(ifElseStack);
    trimVector(blockStack);
    trimVector(switchStack);
    trimVector(tryStack);
    for (auto &frame : callStack) {
        trimVector(frame.args);
        trimVector(frame.localsVec);
    }

    const std::size_t buckets = variables.bucket_count();
    const std::size_t target = variables.empty() ? 0 : variables.size() * 2;
    if (buckets > target + kMinimumSpare && buckets > variables.size() * 2) {
        variables.rehash(target);
    }
}

// Thực hiện chu kỳ thu gom bộ nhớ runtime theo cơ chế GC hiện tại, duyệt các root đang sống trước khi giải phóng đối tượng không còn tham chiếu.
void VM::collectGarbage(bool trimCapacity) {
    if (runtimeHeap != nullptr) {
        lastGcStats = runtimeHeap->collect(gcRoots());
    }
    if (trimCapacity) trimExecutionCapacity();
}

// Chụp root của VM cho tracing GC; các execution context đang tạm dừng vẫn giữ
// stack/control state của caller để object còn sống không bị quét giữa lời gọi lồng nhau.
std::vector<StackValue> VM::gcRoots() const {
    std::vector<StackValue> roots = inheritedGcRoots;
    roots.insert(roots.end(), stack.begin(), stack.end());
    for (const ExecutionContext &context : executionStack) {
        roots.insert(roots.end(), context.stack.begin(), context.stack.end());
        for (const SwitchFrame &frame : context.switchStack) {
            if (frame.switchValue.has_value()) roots.push_back(*frame.switchValue);
        }
        if (context.constructorInstance != nullptr) {
            roots.push_back(make_instance_value(context.constructorInstance));
        }
    }
    for (const auto &entry : variables) roots.push_back(entry.second);
    for (const auto &entry : classTable) roots.push_back(make_class_value(entry.second));

    for (const CallFrame &frame : callStack) {
        roots.insert(roots.end(), frame.args.begin(), frame.args.end());
        roots.insert(roots.end(), frame.localsVec.begin(), frame.localsVec.end());
        for (const auto &entry : frame.localsMap) roots.push_back(entry.second);
        for (const auto &entry : frame.capturedCells) {
            if (entry.second != nullptr) roots.push_back(entry.second->value);
        }
        if (frame.receiver != nullptr) roots.push_back(make_instance_value(frame.receiver));
        if (frame.methodOwnerClass != nullptr) {
            roots.push_back(make_class_value(frame.methodOwnerClass));
        }
    }
    for (const SwitchFrame &frame : switchStack) {
        if (frame.switchValue.has_value()) roots.push_back(*frame.switchValue);
    }
    return roots;
}

// Biên dịch trước vi lệnh của root và từng thân hàm V++. Những opcode làm đổi
// execution context (gọi hàm, nhảy, return, catch, intrinsic...) tiếp tục chạy
// qua dispatcher chung. Nhờ đó phép toán trong thư viện .vi vẫn đi qua JIT,
// nhưng source trace, GC, hủy worker và unwind không bị nhân đôi ở JIT.
void VM::runJitCompiled() {
    jitPrograms_.clear();
    jitFastInstructionCount_ = 0;
    jitInterpreterInstructionCount_ = 0;

    auto compile = [this](const std::vector<Instruction> &code) {
        auto &program = jitPrograms_[&code];
        program.reserve(code.size());
        for (const Instruction &instr : code) {
            switch (instr.op) {
                case OP_BIEN_SO: {
                    const int value = instr.operand;
                    program.emplace_back([this, value]() { stack.emplace_back(value); });
                    break;
                }
                case OP_TEN_BIEN_ID: {
                    const int id = instr.operandIndex;
                    program.emplace_back([this, id]() { stack.emplace_back(id); });
                    break;
                }
                case OP_CHUOI: {
                    const int index = instr.operandIndex;
                    program.emplace_back([this, index]() {
                        if (index < 0 || index >= static_cast<int>(stringPool.size())) {
                            throw runtime_error_op(vietvm::messages::formatMessage(
                                vietvm::messages::kVmInvalidStringIndex), OP_CHUOI,
                                static_cast<int>(pc),
                                vietvm::runtime::runtimeConstantReferenceFacts(false));
                        }
                        stack.push_back(stringPool[index]);
                    });
                    break;
                }
                case OP_BIEN_SO_FLOAT: {
                    const int index = instr.operandIndex;
                    program.emplace_back([this, index]() {
                        if (index < 0 || index >= static_cast<int>(stringPool.size())) {
                            throw runtime_error_op(vietvm::messages::formatMessage(
                                vietvm::messages::kVmInvalidFloatIndex), OP_BIEN_SO_FLOAT,
                                static_cast<int>(pc),
                                vietvm::runtime::runtimeConstantReferenceFacts(false));
                        }
                        try {
                            stack.push_back(make_float_value(std::stod(stringPool[index])));
                        } catch (...) {
                            throw runtime_error_op(vietvm::messages::formatMessage(
                                vietvm::messages::kVmCannotConvertToFloat,
                                {stringPool[index]}), OP_BIEN_SO_FLOAT,
                                static_cast<int>(pc),
                                vietvm::runtime::runtimeConversionFacts(
                                    stringPool[index], "số thực", false));
                        }
                    });
                    break;
                }
                case OP_KHOI_TAO: {
                    const int id = instr.operandIndex;
                    program.emplace_back([this, id]() {
                        if (!callStack.empty()) {
                            CallFrame &frame = callStack.back();
                            if (frame.localsIndexed) {
                                if (id >= 0 && id >= static_cast<int>(frame.localsVec.size())) {
                                    frame.localsVec.resize(id + 1, make_int_value(0));
                                } else if (id >= 0) {
                                    frame.localsVec[id] = make_int_value(0);
                                }
                            } else {
                                frame.localsMap[id] = make_int_value(0);
                            }
                        } else if (variables.count(id) == 0) {
                            variables[id] = make_int_value(0);
                        }
                    });
                    break;
                }
                case OP_TEN_BIEN_GIA_TRI: {
                    const int id = instr.operandIndex;
                    program.emplace_back([this, id]() {
                        if (!callStack.empty()) {
                            CallFrame &frame = callStack.back();
                            const auto captured = frame.capturedCells.find(id);
                            if (captured != frame.capturedCells.end() && captured->second != nullptr) {
                                stack.push_back(captured->second->value);
                                return;
                            }
                            if (frame.localsIndexed) {
                                if (id >= 0 && id < static_cast<int>(frame.localsVec.size())) {
                                    stack.push_back(frame.localsVec[id]);
                                    return;
                                }
                            } else {
                                const auto found = frame.localsMap.find(id);
                                if (found != frame.localsMap.end()) {
                                    stack.push_back(found->second);
                                    return;
                                }
                            }
                        }
                        const auto found = variables.find(id);
                        if (found != variables.end()) {
                            stack.push_back(found->second);
                        } else {
                            variables[id] = make_int_value(0);
                            stack.push_back(variables[id]);
                        }
                    });
                    break;
                }
                case OP_RONG_GIA_TRI:
                    program.emplace_back([this]() { stack.push_back(make_null_value()); });
                    break;
                case OP_DUNG_GIA_TRI:
                    program.emplace_back([this]() { stack.push_back(make_int_value(1)); });
                    break;
                case OP_SAI_GIA_TRI:
                    program.emplace_back([this]() { stack.push_back(make_int_value(0)); });
                    break;
                case OP_CONG:
                case OP_TRU:
                case OP_NHAN:
                case OP_CHIA:
                case OP_Logic_VA:
                case OP_Logic_HOAC:
                case OP_SO_SANH_BANG:
                case OP_KHAC_BANG:
                case OP_LON_HON:
                case OP_NHO_HON:
                case OP_LON_HON_HOAC_BANG:
                case OP_NHO_HON_HOAC_BANG: {
                    const int op = instr.op;
                    program.emplace_back([this, op]() {
                        if (stack.size() < 2) throw runtime_error_op(
                            vietvm::messages::formatMessage(vietvm::messages::kVmNotEnoughOperands),
                            op, static_cast<int>(pc));
                        StackValue b = std::move(stack.back()); stack.pop_back();
                        StackValue a = std::move(stack.back()); stack.pop_back();
                        stack.push_back(evaluateBinaryOperator(op, a, b, static_cast<int>(pc)));
                    });
                    break;
                }
                case OP_MODULO:
                    program.emplace_back([this]() {
                        if (stack.size() < 2) throw runtime_error_op(
                            vietvm::messages::formatMessage(vietvm::messages::kVmMissingModuloOperands),
                            OP_MODULO, static_cast<int>(pc));
                        StackValue b = std::move(stack.back()); stack.pop_back();
                        StackValue a = std::move(stack.back()); stack.pop_back();
                        stack.push_back(evaluateModuloOperator(
                            a, b, OP_MODULO, static_cast<int>(pc)));
                    });
                    break;
                case OP_KHONG:
                    program.emplace_back([this]() {
                        if (stack.empty()) throw runtime_error_op(
                            vietvm::messages::formatMessage(vietvm::messages::kVmMissingNotOperands),
                            OP_KHONG, static_cast<int>(pc));
                        StackValue value = std::move(stack.back()); stack.pop_back();
                        stack.push_back(as_int(value, OP_KHONG, pc) == 0 ? 1 : 0);
                    });
                    break;
                case OP_PHU_DINH:
                    program.emplace_back([this]() {
                        if (stack.empty()) throw runtime_error_op(
                            vietvm::messages::formatMessage(vietvm::messages::kVmMissingNegationOperand),
                            OP_PHU_DINH, static_cast<int>(pc));
                        StackValue value = std::move(stack.back()); stack.pop_back();
                        stack.push_back(toBool(value) ? 0 : 1);
                    });
                    break;
                case OP_IN:
                    program.emplace_back([this, instr]() { executeOutputOpcode(instr); });
                    break;
                case OP_HAM:
                case OP_NEU:
                case OP_DIEU_KIEN:
                case OP_LAP:
                case OP_CAP_NHAT:
                case OP_DONG_LENH:
                case OP_MO_NGOAC:
                case OP_DONG_NGOAC:
                    program.emplace_back([]() {});
                    break;
                default:
                    // Dispatcher chịu trách nhiệm đầy đủ cho control flow,
                    // collection, object model, intrinsic và lỗi ngôn ngữ.
                    program.emplace_back();
                    break;
            }
        }
    };

    compile(bytecode);
    for (const auto &entry : hamBytecodeMap) compile(entry.second);

    jitActive_ = true;
    try {
        runInterpreterLoop();
    } catch (...) {
        jitActive_ = false;
        jitPrograms_.clear();
        throw;
    }
    jitActive_ = false;
    jitPrograms_.clear();
}

// Suy ra biên arity từ bytecode parameter binding. `operandValue` của OP_PARAM/
// OP_PARAM_MAC_DINH là chỉ số đối số nguồn; receiver ngầm dùng -1 nên không tính.
struct RuntimeFunctionArity {
    int minimum = 0;
    int maximum = 0;
};

static RuntimeFunctionArity inferRuntimeFunctionArity(const std::vector<Instruction> &code) {
    RuntimeFunctionArity arity;
    for (const Instruction &instruction : code) {
        if (instruction.op != OP_PARAM && instruction.op != OP_PARAM_MAC_DINH) continue;
        const int argumentIndex = instruction.operandValue;
        if (argumentIndex < 0) continue;
        arity.maximum = std::max(arity.maximum, argumentIndex + 1);
        if (instruction.op == OP_PARAM) {
            arity.minimum = std::max(arity.minimum, argumentIndex + 1);
        }
    }
    return arity;
}

// Trả tên callable dùng trong diagnostic runtime; ưu tiên tên đã đăng ký trong
// StringPool/function table và chỉ dùng id số khi không còn metadata tên hợp lệ.
static std::string runtimeCallTargetName(int hamIdOrName,
                                         const std::vector<std::string> &stringPool,
                                         const std::unordered_map<int, int> &functionTableByNameIndex,
                                         const std::unordered_map<int, int> *functionNameIndexById = nullptr) {
    if (hamIdOrName < 0) {
        const int nameIndex = -(hamIdOrName + 1);
        if (nameIndex >= 0 && nameIndex < static_cast<int>(stringPool.size())) {
            return stringPool[static_cast<std::size_t>(nameIndex)];
        }
    }
    if (functionNameIndexById != nullptr) {
        const auto reverse = functionNameIndexById->find(hamIdOrName);
        if (reverse != functionNameIndexById->end()) {
            const int nameIndex = reverse->second;
            if (nameIndex >= 0 && nameIndex < static_cast<int>(stringPool.size())) {
                return stringPool[static_cast<std::size_t>(nameIndex)];
            }
        }
    }
    for (const auto &[nameIndex, functionId] : functionTableByNameIndex) {
        if (functionId != hamIdOrName) continue;
        if (nameIndex >= 0 && nameIndex < static_cast<int>(stringPool.size())) {
            return stringPool[static_cast<std::size_t>(nameIndex)];
        }
    }
    return std::to_string(hamIdOrName);
}

// Lấy hoặc tạo shared cell cho một slot đang sống trong frame hiện tại. Từ lúc
// slot được capture, mọi đọc/ghi qua frame đều ưu tiên cell để closure và scope
// tạo closure quan sát cùng một giá trị.
CellHandle VM::captureCellForSlot(int varId) {
    if (!callStack.empty()) {
        CallFrame &frame = callStack.back();
        const auto existing = frame.capturedCells.find(varId);
        if (existing != frame.capturedCells.end() && existing->second != nullptr) {
            return existing->second;
        }

        StackValue current = make_int_value(0);
        bool found = false;
        if (frame.localsIndexed) {
            if (varId >= 0 && varId < static_cast<int>(frame.localsVec.size())) {
                current = frame.localsVec[varId];
                found = true;
            }
        } else {
            const auto local = frame.localsMap.find(varId);
            if (local != frame.localsMap.end()) {
                current = local->second;
                found = true;
            }
        }
        if (!found) {
            const auto global = variables.find(varId);
            if (global != variables.end()) current = global->second;
        }

        CellHandle cell = std::make_shared<vietvm::runtime::RuntimeCell>();
        cell->value = std::move(current);
        frame.capturedCells[varId] = cell;
        return cell;
    }

    CellHandle cell = std::make_shared<vietvm::runtime::RuntimeCell>();
    const auto global = variables.find(varId);
    cell->value = global == variables.end() ? make_int_value(0) : global->second;
    return cell;
}

// Tạo call frame và thực thi bytecode của một function id với danh sách đối số/receiver đã chuẩn bị, sau đó trả kết quả về caller.
void VM::invokeFunction(int argc,
                        int hamIdOrName,
                        int op,
                        int curPc,
                        InstanceHandle receiver,
                        ClassHandle methodOwnerClass,
                        ClosureHandle closure,
                        CallReturnMode returnMode,
                        InstanceHandle constructorInstance) {
    if (argc < 0) {
        vietvm::runtime::RuntimeDiagnosticContext context;
        context.internalInvariantChecked = true;
        context.internalInvariantValid = false;
        throw runtime_error_op(
            vietvm::messages::formatMessage(vietvm::messages::kVmNotEnoughOperands),
            op, curPc, std::move(context));
    }
    if (stack.size() < static_cast<std::size_t>(argc)) {
        throw runtime_error_op(
            vietvm::messages::formatMessage(vietvm::messages::kVmNotEnoughOperands),
            op, curPc,
            vietvm::runtime::runtimeStackFacts(
                static_cast<int>(stack.size()), argc));
    }

    std::vector<StackValue> args;
    args.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        args.push_back(stack.back());
        stack.pop_back();
    }
    std::reverse(args.begin(), args.end());

    // Direct bytecode calls already carry a concrete VM function id. Do not
    // resolve a name and walk every native handler first: stdlib-heavy programs
    // can have hundreds of registered V++ functions, making that old path O(n)
    // for every ordinary function call. Only unresolved/name-based calls need
    // to probe the native boundary.
    auto directVmFunction = hamBytecodeMap.find(hamIdOrName);
    if (directVmFunction == hamBytecodeMap.end()) {
        StackValue nativeResult = make_int_value(0);
        std::string nativeErr;
        bool runtimeFailure = false;
        const auto *legacy = resolveLegacyIntrinsic(
            hamIdOrName, stringPool, functionTableByNameIndex, hamBytecodeMap);
        if (legacy != nullptr) {
            if (requireNativeArgumentCount(args, std::string(legacy->name),
                                           static_cast<int>(legacy->arity), nativeErr)) {
                // Old name-based calls now have exactly the same backend as
                // the IR-generated OP_VM_* instructions.
                if (!dispatchRegisteredIntrinsic(
                        *legacy, args, nativeResult, nativeErr, runtimeFailure)) {
                    throw runtime_error_op(
                        vietvm::messages::formatMessage(
                            vietvm::messages::kVmUnknownOpcode), op, curPc);
                }
            }
            if (!nativeErr.empty()) {
                if (!runtimeFailure) {
                    throw vietvm::runtime::LanguageException(
                        make_string_value(nativeErr));
                }
                const std::string nativeName = runtimeCallTargetName(
                    hamIdOrName, stringPool, functionTableByNameIndex);
                vietvm::runtime::RuntimeDiagnosticContext context;
                std::string diagnosticName = nativeName;
                context = vietvm::runtime::runtimeNativeFacts(
                    diagnosticName, nativeErr, false);
                throw runtime_error_op(nativeErr, op, curPc, std::move(context));
            }
            if (returnMode == CallReturnMode::ConstructorInstance) {
                stack.push_back(make_instance_value(std::move(constructorInstance)));
            } else {
                stack.push_back(nativeResult);
            }
            return;
        }
    }

    const std::size_t nextCallDepth = callDepthFromRoot + 1;
    if (nextCallDepth > maxCallDepth) {
        const std::string targetName = runtimeCallTargetName(
            hamIdOrName, stringPool, functionTableByNameIndex,
            &functionNameIndexById_);
        throw vietvm::runtime::RuntimeError(
            vietvm::messages::formatMessage(
                vietvm::messages::kVmCallDepthExceeded,
                {std::to_string(maxCallDepth), targetName}),
            vietvm::runtime::RuntimeErrorKind::CallBoundary,
            vietvm::runtime::runtimeCallDepthFacts(
                targetName, static_cast<int>(nextCallDepth), static_cast<int>(maxCallDepth)));
    }

    auto it = directVmFunction;
    if (it == hamBytecodeMap.end()) {
        const int nameIndex = (hamIdOrName < 0) ? -(hamIdOrName + 1) : hamIdOrName;
        auto ftIt = functionTableByNameIndex.find(nameIndex);
        if (ftIt != functionTableByNameIndex.end()) {
            it = hamBytecodeMap.find(ftIt->second);
        } else {
            for (const Instruction &hinst : currentBytecode()) {
                if (hinst.op == OP_HAM && hinst.operand == nameIndex) {
                    auto resolved = hamBytecodeMap.find(hinst.operandIndex);
                    if (resolved != hamBytecodeMap.end()) {
                        it = resolved;
                        break;
                    }
                }
            }
        }
    }

    if (it == hamBytecodeMap.end()) {
        const int nameIndex = (hamIdOrName < 0) ? -(hamIdOrName + 1) : hamIdOrName;
        std::string fnName = "?";
        if (nameIndex >= 0 && nameIndex < static_cast<int>(stringPool.size())) {
            fnName = stringPool[nameIndex];
        }
        throw runtime_error_op(
            vietvm::messages::formatMessage(vietvm::messages::kVmFunctionNotFound,
                                             {std::to_string(hamIdOrName), fnName}),
            op,
            curPc,
            vietvm::runtime::runtimeTargetLookupFacts(fnName, false)
        );
    }

    const RuntimeFunctionArity arity = inferRuntimeFunctionArity(it->second);
    if (argc < arity.minimum || argc > arity.maximum) {
        const std::string targetName = runtimeCallTargetName(
            hamIdOrName, stringPool, functionTableByNameIndex,
            &functionNameIndexById_);
        throw vietvm::runtime::RuntimeError(
            vietvm::messages::formatMessage(
                vietvm::messages::kVmCallArityMismatch,
                {targetName,
                 std::to_string(argc), std::to_string(arity.minimum),
                 std::to_string(arity.maximum)}),
            vietvm::runtime::RuntimeErrorKind::CallBoundary,
            vietvm::runtime::runtimeCallFacts(
                targetName, argc, arity.minimum, arity.maximum));
    }

    CallFrame frame;
    frame.functionName = runtimeCallTargetName(
        hamIdOrName, stringPool, functionTableByNameIndex,
        &functionNameIndexById_);
    frame.args = std::move(args);
    frame.receiver = std::move(receiver);
    frame.methodOwnerClass = std::move(methodOwnerClass);
    frame.localsIndexed = true;
    frame.returnPc = curPc + 1;
    if (closure != nullptr) frame.capturedCells = closure->captures;

    const auto debugEntry = functionDebugInfo.find(it->first);

    // Chuyển interpreter sang callee bằng explicit execution context thay vì
    // gọi `funcVM.run()` lồng nhau. Nhờ đó recursion V++ không làm sâu native
    // C++ call stack và giới hạn `maxCallDepth` luôn là guard đầu tiên.
    ExecutionContext caller;
    caller.bytecode = activeBytecode_;
    caller.bytecodeDebugInfo = activeBytecodeDebugInfo_;
    caller.stack = std::move(stack);
    caller.loopStartStack = std::move(loopStartStack);
    caller.ifElseStack = std::move(ifElseStack);
    caller.blockStack = std::move(blockStack);
    caller.switchStack = std::move(switchStack);
    caller.tryStack = std::move(tryStack);
    caller.pc = pc;
    caller.blockDepth = blockDepth;
    caller.returnMode = returnMode;
    caller.constructorInstance = std::move(constructorInstance);
    executionStack.push_back(std::move(caller));

    callStack.push_back(std::move(frame));
    callDepthFromRoot = nextCallDepth;
    activeBytecode_ = &it->second;
    activeBytecodeDebugInfo_ =
        debugEntry == functionDebugInfo.end() ? nullptr : &debugEntry->second;
    stack.clear();
    loopStartStack.clear();
    ifElseStack.clear();
    blockStack.clear();
    switchStack.clear();
    tryStack.clear();
    blockDepth = 0;
    pc = 0;
}

// Khôi phục trạng thái interpreter của caller gần nhất sau khi callee kết thúc
// hoặc bị unwind. CallFrame của callee được bỏ cùng lúc với execution context.
void VM::restoreCallerExecutionContext() {
    if (executionStack.empty()) return;

    ExecutionContext caller = std::move(executionStack.back());
    executionStack.pop_back();
    if (!callStack.empty()) callStack.pop_back();
    if (callDepthFromRoot > 0) --callDepthFromRoot;

    activeBytecode_ = caller.bytecode;
    activeBytecodeDebugInfo_ = caller.bytecodeDebugInfo;
    stack = std::move(caller.stack);
    loopStartStack = std::move(caller.loopStartStack);
    ifElseStack = std::move(caller.ifElseStack);
    blockStack = std::move(caller.blockStack);
    switchStack = std::move(caller.switchStack);
    tryStack = std::move(caller.tryStack);
    pc = caller.pc;
    blockDepth = caller.blockDepth;
}

// Hoàn tất function hiện tại rồi tiếp tục caller mà không quay qua native C++
// recursion. Constructor dùng continuation riêng để bỏ return value của hàm
// `khởi tạo` và đưa instance vừa tạo lên caller stack.
bool VM::completeFunctionCall() {
    if (executionStack.empty()) return false;

    const bool hasReturnValue = !stack.empty();
    StackValue returnValue = hasReturnValue ? stack.back() : make_null_value();
    const CallReturnMode returnMode = executionStack.back().returnMode;
    InstanceHandle constructorInstance = executionStack.back().constructorInstance;

    restoreCallerExecutionContext();
    if (returnMode == CallReturnMode::ConstructorInstance) {
        stack.push_back(make_instance_value(std::move(constructorInstance)));
    } else if (hasReturnValue) {
        stack.push_back(std::move(returnValue));
    }
    ++pc;
    return true;
}

// Tìm handler gần nhất xuyên qua explicit execution contexts. Side effect trên
// variables/classTable đã nằm chung trong VM nên không cần copy/move state khi
// unwind như mô hình child VM cũ.
bool VM::unwindLanguageException(const StackValue &value) {
    if (transferThrownValue(value)) return true;
    while (!executionStack.empty()) {
        restoreCallerExecutionContext();
        if (transferThrownValue(value)) return true;
    }
    return false;
}

// RuntimeError không được bắt bởi `thử/bắt lỗi`; gom source frame của callee và
// từng caller trong lúc unwind explicit contexts để giữ nguyên stack trace.
void VM::unwindRuntimeError(vietvm::runtime::RuntimeError &error) {
    error.addFrame(sourceLocationForPc(pc));
    while (!executionStack.empty()) {
        restoreCallerExecutionContext();
        error.addFrame(sourceLocationForPc(pc));
    }
}

// Xử lý nhóm opcode gọi hàm/phương thức; hàm lấy đối số từ stack, xác định đích gọi và chuyển quyền điều khiển sang function tương ứng.
void VM::executeCallOpcode(const Instruction& instr) {
    if (instr.op == OP_GOI) {
        invokeFunction(instr.operand, instr.operandIndex, instr.op, static_cast<int>(pc));
        return;
    }

    if (instr.op != OP_GOI_GIAN_TIEP) {
        throw runtime_error_op(vietvm::messages::formatMessage(
            vietvm::messages::kVmUnknownOpcode), instr.op, pc);
    }
    if (stack.empty()) {
        throw runtime_error_op(vietvm::messages::formatMessage(
            vietvm::messages::kVmIndirectCallMissingReference), instr.op, pc);
    }

    StackValue calleeVal = stack.back();
    stack.pop_back();

    int hamIdOrName = -1;
    if (std::holds_alternative<int>(calleeVal)) {
        hamIdOrName = std::get<int>(calleeVal);
    } else if (std::holds_alternative<std::string>(calleeVal)) {
        const auto &name = std::get<std::string>(calleeVal);
        try {
            hamIdOrName = std::stoi(name);
        } catch (...) {
            auto it = std::find(stringPool.begin(), stringPool.end(), name);
            if (it == stringPool.end()) {
                throw runtime_error_op(vietvm::messages::formatMessage(
                    vietvm::messages::kVmIndirectCallInvalidReference), instr.op, pc);
            }
            hamIdOrName = -static_cast<int>(std::distance(stringPool.begin(), it)) - 1;
        }
    } else if (std::holds_alternative<ClosureHandle>(calleeVal)) {
        const ClosureHandle &closure = std::get<ClosureHandle>(calleeVal);
        if (closure == nullptr || closure->functionId < 0) {
            throw runtime_error_op(vietvm::messages::formatMessage(
                vietvm::messages::kVmIndirectCallInvalidReference), instr.op, pc);
        }
        invokeFunction(instr.operand, closure->functionId, instr.op,
                       static_cast<int>(pc), nullptr, nullptr, closure);
        return;
    } else {
        throw runtime_error_op(vietvm::messages::formatMessage(
            vietvm::messages::kVmIndirectCallUnsupportedReferenceType), instr.op, pc);
    }

    invokeFunction(instr.operand, hamIdOrName, instr.op, static_cast<int>(pc));
}

void VM::shutdownThreadRuntime() noexcept {
    if (threadRuntime_ == nullptr) return;

    std::vector<std::shared_ptr<WorkerTask>> tasks;
    {
        std::lock_guard<std::mutex> registryLock(threadRuntime_->registryMutex);
        threadRuntime_->stopping.store(true, std::memory_order_release);
        tasks.reserve(threadRuntime_->tasks.size());
        for (const auto &entry : threadRuntime_->tasks) tasks.push_back(entry.second);
    }

    for (const auto &task : tasks) {
        if (task == nullptr) continue;
        task->cancelRequested.store(true, std::memory_order_release);
        task->cv.notify_all();
    }

    for (const auto &task : tasks) {
        if (task == nullptr) continue;
        bool shouldJoin = false;
        {
            std::lock_guard<std::mutex> lock(task->mutex);
            if (!task->joinClaimed && task->worker.joinable()) {
                task->joinClaimed = true;
                shouldJoin = true;
            }
        }
        if (!shouldJoin) continue;
        try {
            if (task->worker.get_id() == std::this_thread::get_id()) {
                task->worker.detach();
            } else {
                task->worker.join();
            }
        } catch (...) {
            // Destructors must not throw. The task still owns its completion
            // state, and process teardown will release the remaining handles.
        }
    }

    std::lock_guard<std::mutex> registryLock(threadRuntime_->registryMutex);
    threadRuntime_->tasks.clear();
}

StackValue VM::runWorkerCallable(const StackValue &callable,
                                 const std::vector<StackValue> &args) {
    vietvm::runtime::RuntimeHeapScope heapScope(*runtimeHeap);
    ensureBytecodeVerified();

    for (const auto &entry : variables) runtimeHeap->trackValue(entry.second);
    for (const auto &entry : classTable) {
        runtimeHeap->trackValue(make_class_value(entry.second));
    }
    runtimeHeap->trackValue(callable);
    for (const StackValue &arg : args) runtimeHeap->trackValue(arg);

    stack = args;
    if (std::holds_alternative<ClosureHandle>(callable)) {
        const ClosureHandle &closure = std::get<ClosureHandle>(callable);
        if (closure == nullptr || closure->functionId < 0) {
            throw std::runtime_error("thread_vm_spawn: closure không hợp lệ");
        }
        invokeFunction(static_cast<int>(args.size()), closure->functionId,
                       OP_GOI_GIAN_TIEP, 0, nullptr, nullptr, closure);
    } else {
        int functionIdOrName = -1;
        if (std::holds_alternative<int>(callable)) {
            functionIdOrName = std::get<int>(callable);
        } else if (std::holds_alternative<std::string>(callable)) {
            const std::string &name = std::get<std::string>(callable);
            const auto found = std::find(stringPool.begin(), stringPool.end(), name);
            if (found == stringPool.end()) {
                throw std::runtime_error("thread_vm_spawn: không tìm thấy hàm " + name);
            }
            functionIdOrName =
                -static_cast<int>(std::distance(stringPool.begin(), found)) - 1;
        } else {
            throw std::runtime_error(
                "thread_vm_spawn: công việc phải là hàm hoặc closure");
        }
        invokeFunction(static_cast<int>(args.size()), functionIdOrName,
                       OP_GOI, 0);
    }

    if (!executionStack.empty()) {
        if (vietvm::helpers::hasEnvVar(vietvm::constants::kEnvVppEnableJit) ||
            vietvm::helpers::hasEnvVar(vietvm::constants::kEnvVietvmEnableJit)) {
            runJitCompiled();
        } else {
            runInterpreterLoop();
        }
    }
    if (stack.empty()) return make_null_value();
    return stack.back();
}

bool VM::executeThreadIntrinsic(
    const vietvm::bytecode::IntrinsicDescriptor &intrinsic,
    const std::vector<StackValue> &args,
    StackValue &result,
    std::string &err) {
    const std::string_view fn = intrinsic.name;
    if (intrinsic.opcode != OP_VM_THREAD_SPAWN && intrinsic.opcode != OP_VM_THREAD_WAIT &&
        intrinsic.opcode != OP_VM_THREAD_CANCEL && intrinsic.opcode != OP_VM_THREAD_STATUS &&
        intrinsic.opcode != OP_VM_THREAD_PARK) {
        return false;
    }

    if (threadRuntime_ == nullptr) {
        err = std::string(fn) + ": runtime luồng chưa được khởi tạo";
        return true;
    }

    if (intrinsic.opcode == OP_VM_THREAD_PARK) {
        if (args.size() != 1 || !std::holds_alternative<int>(args[0])) {
            err = std::string(fn) + ": thời gian chờ phải là số nguyên";
            return true;
        }
        const int delayMs = std::get<int>(args[0]);
        if (delayMs < 0 || delayMs > 86400000) {
            err = std::string(fn) + ": thời gian chờ phải trong 0..86400000 ms";
            return true;
        }
        if (currentWorkerTask_ == nullptr) {
            err = std::string(fn) + ": chỉ được dùng bên trong worker V++";
            return true;
        }

        std::unique_lock<std::mutex> lock(currentWorkerTask_->mutex);
        if (currentWorkerTask_->cancelRequested.load(std::memory_order_acquire)) {
            throw VmWorkerCancelled{};
        }
        const bool cancelled = currentWorkerTask_->cv.wait_for(
            lock, std::chrono::milliseconds(delayMs), [&]() {
                return currentWorkerTask_->cancelRequested.load(std::memory_order_acquire);
            });
        if (cancelled) throw VmWorkerCancelled{};
        result = make_null_value();
        return true;
    }

    if (intrinsic.opcode == OP_VM_THREAD_SPAWN) {
        if (args.size() != 2) {
            err = vietvm::helpers::nativeArgumentCountError(std::string(fn), 2);
            return true;
        }
        if (!std::holds_alternative<ListHandle>(args[1]) ||
            std::get<ListHandle>(args[1]) == nullptr) {
            err = std::string(fn) + ": đối số phải là danh sách";
            return true;
        }

        ThreadValueCloner snapshotCloner;
        StackValue callableSnapshot = snapshotCloner.clone(args[0]);
        std::vector<StackValue> argumentSnapshot;
        const ListHandle &argumentList = std::get<ListHandle>(args[1]);
        argumentSnapshot.reserve(argumentList->elements.size());
        for (const StackValue &value : argumentList->elements) {
            argumentSnapshot.push_back(snapshotCloner.clone(value));
        }

        std::unordered_map<int, StackValue> variableSnapshot;
        variableSnapshot.reserve(variables.size());
        for (const auto &[slot, value] : variables) {
            variableSnapshot.emplace(slot, snapshotCloner.clone(value));
        }
        std::unordered_map<std::string, ClassHandle> classSnapshot;
        classSnapshot.reserve(classTable.size());
        for (const auto &[name, klass] : classTable) {
            classSnapshot.emplace(name, snapshotCloner.cloneClassHandle(klass));
        }

        auto task = std::make_shared<WorkerTask>();
        auto state = threadRuntime_;
        auto poolSnapshot = stringPool;
        auto functionSnapshot = hamBytecodeMap;
        auto functionNamesSnapshot = functionTableByNameIndex;
        auto functionDebugSnapshot = functionDebugInfo;
        const std::size_t maxDepthSnapshot = maxCallDepth;
        OutputSink sinkSnapshot = outputSink;

        int id = 0;
        {
            std::lock_guard<std::mutex> registryLock(threadRuntime_->registryMutex);
            if (threadRuntime_->stopping.load(std::memory_order_acquire)) {
                err = std::string(fn) + ": runtime luồng đang dừng";
                return true;
            }
            id = threadRuntime_->nextId.fetch_add(1, std::memory_order_relaxed);
            if (id <= 0) {
                err = std::string(fn) + ": đã hết mã luồng";
                return true;
            }
            try {
                task->worker = std::thread(
                [task, state,
                 callable = std::move(callableSnapshot),
                 callArgs = std::move(argumentSnapshot),
                 pool = std::move(poolSnapshot),
                 functions = std::move(functionSnapshot),
                 functionNames = std::move(functionNamesSnapshot),
                 functionDebug = std::move(functionDebugSnapshot),
                 workerVariables = std::move(variableSnapshot),
                 workerClasses = std::move(classSnapshot),
                 maxDepthSnapshot, sink = std::move(sinkSnapshot)]() mutable {
                    try {
                        VM worker({}, pool);
                        worker.threadRuntime_ = state;
                        worker.threadRuntimeOwner_ = false;
                        worker.cancellationRequested_ = &task->cancelRequested;
                        worker.currentWorkerTask_ = task.get();
                        worker.outputSink = std::move(sink);
                        worker.variables = std::move(workerVariables);
                        worker.classTable = std::move(workerClasses);
                        worker.functionDebugInfo = std::move(functionDebug);
                        worker.maxCallDepth = maxDepthSnapshot;
                        worker.setFunctions(std::move(functions), std::move(functionNames));

                        StackValue workerResult = worker.runWorkerCallable(callable, callArgs);
                        ThreadValueCloner resultCloner;
                        StackValue transportResult = resultCloner.clone(workerResult);
                        {
                            std::lock_guard<std::mutex> lock(task->mutex);
                            task->result = std::move(transportResult);
                            task->status = WorkerStatus::Done;
                        }
                    } catch (const VmWorkerCancelled &) {
                        std::lock_guard<std::mutex> lock(task->mutex);
                        task->status = WorkerStatus::Cancelled;
                    } catch (const vietvm::runtime::LanguageException &failure) {
                        std::lock_guard<std::mutex> lock(task->mutex);
                        task->error = languageExceptionText(failure);
                        task->status = WorkerStatus::Failed;
                    } catch (const std::exception &failure) {
                        std::lock_guard<std::mutex> lock(task->mutex);
                        task->error = failure.what();
                        task->status = WorkerStatus::Failed;
                    } catch (...) {
                        std::lock_guard<std::mutex> lock(task->mutex);
                        task->error = "lỗi worker không xác định";
                        task->status = WorkerStatus::Failed;
                    }
                    task->cv.notify_all();
                });
            } catch (const std::system_error &failure) {
                err = std::string(fn) + ": không tạo được OS thread: " + failure.what();
                return true;
            }
            threadRuntime_->tasks.emplace(id, task);
        }
        result = make_int_value(id);
        return true;
    }

    if (args.empty() || !std::holds_alternative<int>(args[0])) {
        err = std::string(fn) + ": mã luồng phải là số nguyên";
        return true;
    }
    const int id = std::get<int>(args[0]);
    if (id <= 0) {
        err = std::string(fn) + ": mã luồng phải dương";
        return true;
    }

    std::shared_ptr<WorkerTask> task;
    {
        std::lock_guard<std::mutex> lock(threadRuntime_->registryMutex);
        const auto found = threadRuntime_->tasks.find(id);
        if (found == threadRuntime_->tasks.end()) {
            err = std::string(fn) + ": không tìm thấy luồng";
            return true;
        }
        task = found->second;
    }

    if (intrinsic.opcode == OP_VM_THREAD_CANCEL) {
        if (args.size() != 1) {
            err = vietvm::helpers::nativeArgumentCountError(std::string(fn), 1);
            return true;
        }
        {
            std::lock_guard<std::mutex> lock(task->mutex);
            if (task->status != WorkerStatus::Running) {
                result = make_int_value(0);
                return true;
            }
            task->cancelRequested.store(true, std::memory_order_release);
        }
        task->cv.notify_all();
        result = make_int_value(1);
        return true;
    }

    if (intrinsic.opcode == OP_VM_THREAD_STATUS) {
        if (args.size() != 1) {
            err = vietvm::helpers::nativeArgumentCountError(std::string(fn), 1);
            return true;
        }
        std::lock_guard<std::mutex> lock(task->mutex);
        result = make_int_value(static_cast<int>(task->status));
        return true;
    }

    if (args.size() != 2 || !std::holds_alternative<int>(args[1])) {
        err = std::string(fn) + ": thời gian chờ phải là số nguyên";
        return true;
    }
    const int timeoutMs = std::get<int>(args[1]);
    if (timeoutMs < -1) {
        err = std::string(fn) + ": thời gian chờ phải là -1 hoặc không âm";
        return true;
    }

    WorkerStatus status = WorkerStatus::Running;
    StackValue transportResult = make_null_value();
    std::string workerError;
    bool shouldJoin = false;
    {
        std::unique_lock<std::mutex> lock(task->mutex);
        const auto completed = [&]() { return task->status != WorkerStatus::Running; };
        if (timeoutMs < 0) {
            task->cv.wait(lock, completed);
        } else if (timeoutMs > 0) {
            (void)task->cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), completed);
        }
        status = task->status;
        transportResult = task->result;
        workerError = task->error;
        if (status != WorkerStatus::Running && !task->joinClaimed &&
            task->worker.joinable()) {
            task->joinClaimed = true;
            shouldJoin = true;
        }
    }

    if (shouldJoin) task->worker.join();

    StackValue copiedResult = make_null_value();
    if (status == WorkerStatus::Done) {
        ThreadValueCloner resultCloner;
        copiedResult = resultCloner.clone(transportResult);
        runtimeHeap->trackValue(copiedResult);
    }
    result = make_list_value({
        make_int_value(static_cast<int>(status)),
        std::move(copiedResult),
        make_string_value(std::move(workerError)),
    });
    return true;
}

bool VM::dispatchRegisteredIntrinsic(
    const vietvm::bytecode::IntrinsicDescriptor &intrinsic,
    const std::vector<StackValue> &args,
    StackValue &result,
    std::string &err,
    bool &runtimeFailure) {
    NativeFailureDisposition failureDisposition =
        NativeFailureDisposition::CatchableLanguageError;
    const bool handledThread = executeThreadIntrinsic(intrinsic, args, result, err);
    const bool handled = handledThread ||
        executeVmPrimitive(intrinsic.opcode, args, result, err, failureDisposition);
    runtimeFailure = failureDisposition == NativeFailureDisposition::RuntimeError;
    return handled;
}

void VM::executeIntrinsicOpcode(const Instruction &instr) {
    const auto *intrinsic = vietvm::bytecode::intrinsicByOpcode(instr.op);
    if (intrinsic == nullptr) {
        throw runtime_error_op(vietvm::messages::formatMessage(
            vietvm::messages::kVmUnknownOpcode), instr.op, pc);
    }
    if (stack.size() < intrinsic->arity) {
        throw runtime_error_op(
            vietvm::messages::formatMessage(vietvm::messages::kVmNotEnoughOperands),
            instr.op, pc,
            vietvm::runtime::runtimeStackFacts(
                static_cast<int>(stack.size()), static_cast<int>(intrinsic->arity)));
    }

    std::vector<StackValue> args;
    args.reserve(intrinsic->arity);
    for (std::size_t index = 0; index < intrinsic->arity; ++index) {
        args.push_back(stack.back());
        stack.pop_back();
    }
    std::reverse(args.begin(), args.end());

    StackValue result = make_null_value();
    std::string err;
    bool runtimeFailure = false;
    if (!dispatchRegisteredIntrinsic(*intrinsic, args, result, err, runtimeFailure)) {
        throw runtime_error_op(vietvm::messages::formatMessage(
            vietvm::messages::kVmUnknownOpcode), instr.op, pc);
    }
    if (!err.empty()) {
        if (runtimeFailure) {
            std::string diagnosticName(intrinsic->name);
            if (intrinsic->name == "io_doc_file_vm") diagnosticName = "đọc tệp";
            else if (intrinsic->name == "io_ghi_file_vm") diagnosticName = "ghi tệp";
            else if (intrinsic->name == "io_doc_bytes_vm") diagnosticName = "đọc bytes tệp";
            else if (intrinsic->name == "io_ghi_bytes_vm") diagnosticName = "ghi bytes tệp";
            else if (intrinsic->name == "io_ghi_tiep_file_vm") diagnosticName = "ghi tiếp tệp";
            throw runtime_error_op(
                err, instr.op, pc,
                vietvm::runtime::runtimeNativeFacts(diagnosticName, err, false));
        }
        throw vietvm::runtime::LanguageException(make_string_value(err));
    }
    stack.push_back(std::move(result));
}

// Xử lý opcode tạo hoặc biến đổi giá trị trên stack, bao gồm literal và các phép toán số/chuỗi.
void VM::executeValueOpcode(const Instruction& instr) {
    switch (instr.op) {
        case OP_BIEN_SO:
            stack.emplace_back(instr.operand);
            return;
        case OP_TEN_BIEN_ID:
            stack.emplace_back(instr.operandIndex);
            return;
        case OP_TAO_DONG_BAO: {
            const int captureCount = instr.operandIndex;
            if (captureCount < 0 || stack.size() < static_cast<std::size_t>(captureCount)) {
                throw runtime_error_op(vietvm::messages::formatMessage(
                    vietvm::messages::kVmClosureMissingCaptures), instr.op, pc,
                    vietvm::runtime::runtimeClosureFacts("không đủ biến cần giữ lại", false));
            }
            std::vector<int> captureSlots;
            captureSlots.reserve(static_cast<std::size_t>(captureCount));
            for (int index = 0; index < captureCount; ++index) {
                StackValue slot = stack.back();
                stack.pop_back();
                if (!std::holds_alternative<int>(slot)) {
                    throw runtime_error_op(vietvm::messages::formatMessage(
                        vietvm::messages::kVmClosureInvalidCapture), instr.op, pc,
                        vietvm::runtime::runtimeClosureFacts("vị trí biến cần giữ lại không hợp lệ", false));
                }
                captureSlots.push_back(std::get<int>(slot));
            }
            std::reverse(captureSlots.begin(), captureSlots.end());

            ClosureHandle closure = std::make_shared<vietvm::runtime::RuntimeClosure>();
            closure->functionId = instr.operand;
            for (int slot : captureSlots) {
                closure->captures[slot] = captureCellForSlot(slot);
            }
            stack.push_back(make_closure_value(std::move(closure)));
            return;
        }
        case OP_MODULO: {
            if (stack.size() < 2) throw runtime_error_op(vietvm::messages::formatMessage(
                vietvm::messages::kVmMissingModuloOperands), instr.op, pc);
            StackValue b = stack.back(); stack.pop_back();
            StackValue a = stack.back(); stack.pop_back();
            stack.push_back(evaluateModuloOperator(a, b, instr.op, static_cast<int>(pc)));
            return;
        }
        case OP_CONG: case OP_TRU: case OP_NHAN: case OP_CHIA:
        case OP_Logic_VA: case OP_Logic_HOAC:
        case OP_SO_SANH_BANG: case OP_KHAC_BANG:
        case OP_LON_HON: case OP_NHO_HON:
        case OP_LON_HON_HOAC_BANG: case OP_NHO_HON_HOAC_BANG: {
            if (stack.size() < 2) throw runtime_error_op(vietvm::messages::formatMessage(
                vietvm::messages::kVmNotEnoughOperands), instr.op, pc);
            StackValue b = stack.back(); stack.pop_back();
            StackValue a = stack.back(); stack.pop_back();
            stack.push_back(evaluateBinaryOperator(instr.op, a, b, static_cast<int>(pc)));
            return;
        }
        case OP_KHONG: {
            if (stack.empty()) throw runtime_error_op(vietvm::messages::formatMessage(
                vietvm::messages::kVmMissingNotOperands), instr.op, pc);
            StackValue a = stack.back(); stack.pop_back();
            stack.push_back((as_int(a, instr.op, pc) == 0) ? 1 : 0);
            return;
        }
        case OP_CHUOI:
            if (instr.operandIndex < 0 || instr.operandIndex >= static_cast<int>(stringPool.size())) {
                throw runtime_error_op(vietvm::messages::formatMessage(
                    vietvm::messages::kVmInvalidStringIndex), instr.op, pc,
                    vietvm::runtime::runtimeConstantReferenceFacts(false));
            }
            stack.push_back(stringPool[instr.operandIndex]);
            return;
        case OP_BIEN_SO_FLOAT:
            if (instr.operandIndex < 0 || instr.operandIndex >= static_cast<int>(stringPool.size())) {
                throw runtime_error_op(vietvm::messages::formatMessage(
                    vietvm::messages::kVmInvalidFloatIndex), instr.op, pc,
                    vietvm::runtime::runtimeConstantReferenceFacts(false));
            }
            try {
                stack.push_back(make_float_value(std::stod(stringPool[instr.operandIndex])));
            } catch (...) {
                throw runtime_error_op(vietvm::messages::formatMessage(
                    vietvm::messages::kVmCannotConvertToFloat,
                    {stringPool[instr.operandIndex]}), instr.op, pc,
                    vietvm::runtime::runtimeConversionFacts(
                        stringPool[instr.operandIndex], "số thực", false));
            }
            return;
        case OP_RONG_GIA_TRI:
            stack.push_back(make_null_value());
            return;
        case OP_DUNG_GIA_TRI:
            stack.push_back(make_int_value(1));
            return;
        case OP_SAI_GIA_TRI:
            stack.push_back(make_int_value(0));
            return;
        case OP_MAP_LITERAL:
            if (instr.operandIndex == -1) {
                if (instr.operand < 0 ||
                    stack.size() < static_cast<std::size_t>(instr.operand) * 2u) {
                    throw runtime_error_op(vietvm::messages::formatMessage(
                        vietvm::messages::kVmNotEnoughOperands), instr.op, pc,
                        vietvm::runtime::runtimeStackFacts(
                            static_cast<int>(stack.size()), instr.operand * 2));
                }
                std::vector<std::pair<std::string, StackValue>> entries;
                entries.reserve(static_cast<std::size_t>(instr.operand));
                for (int index = 0; index < instr.operand; ++index) {
                    StackValue itemValue = stack.back();
                    stack.pop_back();
                    StackValue keyValue = stack.back();
                    stack.pop_back();
                    if (!std::holds_alternative<std::string>(keyValue)) {
                        throw runtime_error_op(vietvm::messages::formatMessage(
                            vietvm::messages::kVmParseMapLiteral,
                            {"khóa động không phải chuỗi"}), instr.op, pc);
                    }
                    entries.emplace_back(
                        std::get<std::string>(std::move(keyValue)),
                        std::move(itemValue));
                }
                MapValue map;
                for (auto entry = entries.rbegin(); entry != entries.rend(); ++entry) {
                    map.entries[entry->first] = std::move(entry->second);
                }
                stack.push_back(make_map_value(std::move(map)));
                return;
            }
            if (instr.operandIndex < 0 || instr.operandIndex >= static_cast<int>(stringPool.size())) {
                throw runtime_error_op(vietvm::messages::formatMessage(
                    vietvm::messages::kVmInvalidMapIndex), instr.op, pc,
                    vietvm::runtime::runtimeConstantReferenceFacts(false));
            }
            try {
                stack.push_back(make_map_value(decodeMapFromStringPool(stringPool[instr.operandIndex])));
            } catch (const std::exception &ex) {
                throw runtime_error_op(vietvm::messages::formatMessage(
                    vietvm::messages::kVmParseMapLiteral, {ex.what()}), instr.op, pc,
                    vietvm::runtime::runtimeLiteralDecodeFacts(ex.what(), false));
            }
            return;
        case OP_LIST_LITERAL:
            if (instr.operandIndex == -1) {
                if (instr.operand < 0 ||
                    stack.size() < static_cast<std::size_t>(instr.operand)) {
                    throw runtime_error_op(vietvm::messages::formatMessage(
                        vietvm::messages::kVmNotEnoughOperands), instr.op, pc,
                        vietvm::runtime::runtimeStackFacts(
                            static_cast<int>(stack.size()), instr.operand));
                }
                std::vector<StackValue> elements(static_cast<std::size_t>(instr.operand));
                for (int index = instr.operand - 1; index >= 0; --index) {
                    elements[static_cast<std::size_t>(index)] = std::move(stack.back());
                    stack.pop_back();
                }
                stack.push_back(make_list_value(std::move(elements)));
                return;
            }
            if (instr.operandIndex < 0 || instr.operandIndex >= static_cast<int>(stringPool.size())) {
                throw runtime_error_op(vietvm::messages::formatMessage(
                    vietvm::messages::kVmInvalidListIndex), instr.op, pc,
                    vietvm::runtime::runtimeConstantReferenceFacts(false));
            }
            try {
                stack.push_back(make_list_value(decodeListFromStringPool(stringPool[instr.operandIndex])));
            } catch (const std::exception &ex) {
                throw runtime_error_op(vietvm::messages::formatMessage(
                    vietvm::messages::kVmParseMapLiteral, {ex.what()}), instr.op, pc,
                    vietvm::runtime::runtimeLiteralDecodeFacts(ex.what(), false));
            }
            return;
        case OP_PHU_DINH: {
            if (stack.empty()) throw runtime_error_op(vietvm::messages::formatMessage(
                vietvm::messages::kVmMissingNegationOperand), instr.op, pc);
            StackValue operand = stack.back(); stack.pop_back();
            stack.push_back(toBool(operand) ? 0 : 1);
            return;
        }
        default:
            throw runtime_error_op(vietvm::messages::formatMessage(
                vietvm::messages::kVmUnknownOpcode), instr.op, pc);
    }
}

// Xử lý truy cập theo chỉ số cho list/tuple/map/string; map dùng khóa chuỗi theo
// cùng quy tắc chuyển khóa của các primitive collection hiện có.
void VM::executeIndexOpcode(const Instruction& instr) {
    if (instr.op == OP_DOC_CHI_SO) {
        if (stack.size() < 2) {
            throw runtime_error_op(vietvm::messages::formatMessage(
                vietvm::messages::kVmNotEnoughOperands), instr.op, pc,
                vietvm::runtime::runtimeStackFacts(static_cast<int>(stack.size()), 2));
        }
        StackValue indexValue = stack.back(); stack.pop_back();
        StackValue container = stack.back(); stack.pop_back();
        if (std::holds_alternative<MapHandle>(container)) {
            const MapHandle &map = std::get<MapHandle>(container);
            if (map == nullptr) {
                stack.push_back(make_null_value());
                return;
            }
            const std::string key = vietvm::helpers::argToRawString(indexValue);
            const auto found = map->entries.find(key);
            stack.push_back(found == map->entries.end()
                                ? make_null_value()
                                : found->second);
            return;
        }
        if (!std::holds_alternative<int>(indexValue)) {
            throw runtime_error_op(vietvm::messages::formatMessage(
                vietvm::messages::kVmIndexMustBeInteger), instr.op, pc,
                vietvm::runtime::runtimeIndexFacts(
                    0, false, -1, true, runtime_value_type_name(indexValue)));
        }
        const int index = std::get<int>(indexValue);
        if (index < 0) {
            throw runtime_error_op(vietvm::messages::formatMessage(
                vietvm::messages::kVmIndexOutOfRange), instr.op, pc,
                vietvm::runtime::runtimeIndexFacts(index, true, -1, true));
        }
        if (std::holds_alternative<ListHandle>(container)) {
            const ListHandle &list = std::get<ListHandle>(container);
            const long long size = list == nullptr ? 0 : static_cast<long long>(list->elements.size());
            if (index >= size) {
                throw runtime_error_op(vietvm::messages::formatMessage(
                    vietvm::messages::kVmIndexOutOfRange), instr.op, pc,
                    vietvm::runtime::runtimeIndexFacts(index, true, size, true));
            }
            stack.push_back(list->elements[static_cast<std::size_t>(index)]);
            return;
        }
        if (std::holds_alternative<TupleHandle>(container)) {
            const TupleHandle &tuple = std::get<TupleHandle>(container);
            const long long size = tuple == nullptr ? 0 : static_cast<long long>(tuple->elements.size());
            if (index >= size) {
                throw runtime_error_op(vietvm::messages::formatMessage(
                    vietvm::messages::kVmIndexOutOfRange), instr.op, pc,
                    vietvm::runtime::runtimeIndexFacts(index, true, size, true));
            }
            stack.push_back(tuple->elements[static_cast<std::size_t>(index)]);
            return;
        }
        if (std::holds_alternative<std::string>(container)) {
            const std::string &text = std::get<std::string>(container);
            const long long size = static_cast<long long>(
                vietvm::core::utf8CodePointCount(text));
            if (index >= size) {
                throw runtime_error_op(vietvm::messages::formatMessage(
                    vietvm::messages::kVmIndexOutOfRange), instr.op, pc,
                    vietvm::runtime::runtimeIndexFacts(index, true, size, true));
            }
            const auto unit = vietvm::core::utf8CodePointAt(
                text, static_cast<std::size_t>(index));
            if (!unit.has_value()) {
                throw runtime_error_op(vietvm::messages::formatMessage(
                    vietvm::messages::kVmIndexOutOfRange), instr.op, pc,
                    vietvm::runtime::runtimeIndexFacts(index, true, size, true));
            }
            stack.push_back(make_string_value(*unit));
            return;
        }
        throw runtime_error_op(vietvm::messages::formatMessage(
            vietvm::messages::kVmIndexNeedsListOrString), instr.op, pc,
            vietvm::runtime::runtimeIndexFacts(
                index, true, -1, false, runtime_value_type_name(container)));
    }

    if (instr.op != OP_GAN_CHI_SO) {
        vietvm::runtime::RuntimeDiagnosticContext context;
        context.internalInvariantChecked = true;
        context.internalInvariantValid = false;
        throw runtime_error_op(vietvm::messages::formatMessage(
            vietvm::messages::kVmUnknownOpcode), instr.op, pc, std::move(context));
    }
    if (stack.size() < 3) {
        throw runtime_error_op(vietvm::messages::formatMessage(
            vietvm::messages::kVmAssignNotEnoughOperands), instr.op, pc,
            vietvm::runtime::runtimeStackFacts(static_cast<int>(stack.size()), 3));
    }
    StackValue value = stack.back(); stack.pop_back();
    StackValue indexValue = stack.back(); stack.pop_back();
    StackValue container = stack.back(); stack.pop_back();
    if (std::holds_alternative<MapHandle>(container)) {
        const MapHandle &map = std::get<MapHandle>(container);
        if (map == nullptr) {
            throw runtime_error_op(vietvm::messages::formatMessage(
                vietvm::messages::kVmIndexNeedsListOrString), instr.op, pc,
                vietvm::runtime::runtimeIndexFacts(
                    0, false, -1, false, runtime_value_type_name(container)));
        }
        map->entries[vietvm::helpers::argToRawString(indexValue)] = std::move(value);
        return;
    }
    if (!std::holds_alternative<int>(indexValue)) {
        throw runtime_error_op(vietvm::messages::formatMessage(
            vietvm::messages::kVmIndexMustBeInteger), instr.op, pc,
            vietvm::runtime::runtimeIndexFacts(
                0, false, -1, true, runtime_value_type_name(indexValue)));
    }
    const int index = std::get<int>(indexValue);
    if (index < 0) {
        throw runtime_error_op(vietvm::messages::formatMessage(
            vietvm::messages::kVmIndexOutOfRange), instr.op, pc,
            vietvm::runtime::runtimeIndexFacts(index, true, -1, true));
    }
    if (!std::holds_alternative<ListHandle>(container)) {
        throw runtime_error_op(vietvm::messages::formatMessage(
            vietvm::messages::kVmIndexNeedsListOrString), instr.op, pc,
            vietvm::runtime::runtimeIndexFacts(
                index, true, -1, false, runtime_value_type_name(container)));
    }
    const ListHandle &list = std::get<ListHandle>(container);
    const long long size = list == nullptr ? 0 : static_cast<long long>(list->elements.size());
    if (index >= size) {
        throw runtime_error_op(vietvm::messages::formatMessage(
            vietvm::messages::kVmIndexOutOfRange), instr.op, pc,
            vietvm::runtime::runtimeIndexFacts(index, true, size, true));
    }
    list->elements[static_cast<std::size_t>(index)] = std::move(value);
}

// Xử lý opcode object model như tạo lớp, tạo instance, đọc/ghi field và gọi phương thức có receiver.
void VM::executeObjectOpcode(const Instruction& instr) {
    auto stringAt = [&](int index, std::string_view errorMessage) -> const std::string & {
        if (index < 0 || index >= static_cast<int>(stringPool.size())) {
            throw runtime_error_op(vietvm::messages::formatMessage(errorMessage), instr.op, pc);
        }
        return stringPool[static_cast<std::size_t>(index)];
    };

    switch (instr.op) {
        case OP_TAO_LOP: {
            const std::string &className = stringAt(
                instr.operandIndex, vietvm::messages::kVmObjectInvalidClassNameIndex);
            if (classTable.find(className) == classTable.end()) {
                ClassHandle superclass;
                if (instr.operandValue > 0) {
                    const std::string &superclassName = stringAt(
                        instr.operandValue - 1,
                        vietvm::messages::kVmObjectInvalidClassNameIndex);
                    const auto foundSuperclass = classTable.find(superclassName);
                    if (foundSuperclass == classTable.end()) {
                        throw runtime_error_op(vietvm::messages::formatMessage(
                            vietvm::messages::kVmObjectClassNotFound,
                            {superclassName}), instr.op, pc,
                            vietvm::runtime::runtimeTargetLookupFacts(superclassName, false));
                    }
                    superclass = foundSuperclass->second;
                }
                classTable.emplace(
                    className,
                    vietvm::runtime::createClass(className, std::move(superclass)));
            }
            return;
        }
        case OP_THEM_PHUONG_THUC: {
            int classNameIndex = instr.operand;
            int functionId = instr.operandValue;
            vietvm::runtime::RuntimeMemberVisibility visibility =
                vietvm::runtime::RuntimeMemberVisibility::Public;
            if (functionId < 0) {
                visibility = vietvm::runtime::RuntimeMemberVisibility::Private;
                functionId = -(functionId + 1);
            } else if (classNameIndex < 0) {
                visibility = vietvm::runtime::RuntimeMemberVisibility::Protected;
                classNameIndex = -(classNameIndex + 1);
            }
            const std::string &className = stringAt(
                classNameIndex, vietvm::messages::kVmObjectInvalidClassNameIndex);
            const std::string &methodName = stringAt(
                instr.operandIndex, vietvm::messages::kVmObjectInvalidMemberNameIndex);
            const auto foundClass = classTable.find(className);
            if (foundClass == classTable.end()) {
                throw runtime_error_op(vietvm::messages::formatMessage(
                    vietvm::messages::kVmObjectClassNotFound, {className}), instr.op, pc,
                    vietvm::runtime::runtimeTargetLookupFacts(className, false));
            }
            (void)vietvm::runtime::defineMethod(
                foundClass->second, methodName, functionId, visibility);
            return;
        }
        case OP_TAO_DOI_TUONG: {
            const std::string &className = stringAt(
                instr.operandIndex, vietvm::messages::kVmObjectInvalidClassNameIndex);
            const auto foundClass = classTable.find(className);
            if (foundClass == classTable.end()) {
                throw runtime_error_op(vietvm::messages::formatMessage(
                    vietvm::messages::kVmObjectClassNotFound, {className}), instr.op, pc,
                    vietvm::runtime::runtimeTargetLookupFacts(className, false));
            }
            const int argc = instr.operand;
            if (argc < 0 || stack.size() < static_cast<std::size_t>(argc)) {
                throw runtime_error_op(vietvm::messages::formatMessage(
                    vietvm::messages::kVmObjectNotEnoughOperands), instr.op, pc,
                    vietvm::runtime::runtimeStackFacts(static_cast<int>(stack.size()), argc));
            }

            const ClassHandle &klass = foundClass->second;
            const InstanceHandle instance = vietvm::runtime::createInstance(klass);
            const auto constructor = klass->methods.find("khởi tạo");
            if (constructor != klass->methods.end()) {
                const ClassHandle callerClass = callStack.empty()
                    ? nullptr
                    : callStack.back().methodOwnerClass;
                if (!canAccessRuntimeMethod(
                        constructor->second.visibility, callerClass, klass)) {
                    throw runtime_error_op(
                        runtimeMethodAccessMessage(
                            constructor->second.visibility, "khởi tạo"),
                        instr.op, pc,
                        vietvm::runtime::runtimeMemberFacts(
                            "khởi tạo", true, true, true, true, false));
                }
                invokeFunction(argc, constructor->second.functionId,
                               instr.op, static_cast<int>(pc),
                               instance, klass, nullptr,
                               CallReturnMode::ConstructorInstance, instance);
                return;
            } else if (argc != 0) {
                throw vietvm::runtime::RuntimeError(
                    vietvm::messages::formatMessage(
                        vietvm::messages::kVmCallArityMismatch,
                        {className, std::to_string(argc), "0", "0"}),
                    vietvm::runtime::RuntimeErrorKind::CallBoundary,
                    vietvm::runtime::runtimeCallFacts(className, argc, 0, 0));
            }
            stack.push_back(make_instance_value(instance));
            return;
        }
        case OP_DOC_THUOC_TINH: {
            const std::string &memberName = stringAt(
                instr.operandIndex, vietvm::messages::kVmObjectInvalidMemberNameIndex);
            if (stack.empty()) {
                throw runtime_error_op(vietvm::messages::formatMessage(
                    vietvm::messages::kVmObjectNotEnoughOperands), instr.op, pc,
                    vietvm::runtime::runtimeStackFacts(static_cast<int>(stack.size()), 1));
            }
            StackValue receiver = stack.back();
            stack.pop_back();
            if (!std::holds_alternative<InstanceHandle>(receiver)) {
                throw runtime_error_op(vietvm::messages::formatMessage(
                    vietvm::messages::kVmObjectExpectedInstance), instr.op, pc,
                    vietvm::runtime::runtimeMemberFacts(memberName, false, false, true));
            }
            const auto field = vietvm::runtime::getInstanceField(
                std::get<InstanceHandle>(receiver), memberName);
            if (!field.has_value()) {
                throw runtime_error_op(vietvm::messages::formatMessage(
                    vietvm::messages::kVmObjectPropertyNotFound, {memberName}), instr.op, pc,
                    vietvm::runtime::runtimeMemberFacts(memberName, true, true, false));
            }
            stack.push_back(*field);
            return;
        }
        case OP_GAN_THUOC_TINH: {
            const std::string &memberName = stringAt(
                instr.operandIndex, vietvm::messages::kVmObjectInvalidMemberNameIndex);
            if (stack.size() < 2) {
                throw runtime_error_op(vietvm::messages::formatMessage(
                    vietvm::messages::kVmObjectNotEnoughOperands), instr.op, pc,
                    vietvm::runtime::runtimeStackFacts(static_cast<int>(stack.size()), 2));
            }
            StackValue value = stack.back();
            stack.pop_back();
            StackValue receiver = stack.back();
            stack.pop_back();
            if (!std::holds_alternative<InstanceHandle>(receiver)) {
                throw runtime_error_op(vietvm::messages::formatMessage(
                    vietvm::messages::kVmObjectExpectedInstance), instr.op, pc,
                    vietvm::runtime::runtimeMemberFacts(memberName, false, false, true));
            }
            if (!vietvm::runtime::setInstanceField(
                    std::get<InstanceHandle>(receiver), memberName, std::move(value))) {
                throw runtime_error_op(vietvm::messages::formatMessage(
                    vietvm::messages::kVmObjectExpectedInstance), instr.op, pc,
                    vietvm::runtime::runtimeMemberFacts(memberName, false, false, true));
            }
            return;
        }
        case OP_GOI_PHUONG_THUC: {
            const std::string &methodName = stringAt(
                instr.operandIndex, vietvm::messages::kVmObjectInvalidMemberNameIndex);
            const int argc = instr.operand;
            if (argc < 0 || stack.size() < static_cast<std::size_t>(argc + 1)) {
                throw runtime_error_op(vietvm::messages::formatMessage(
                    vietvm::messages::kVmObjectNotEnoughOperands), instr.op, pc,
                    vietvm::runtime::runtimeStackFacts(static_cast<int>(stack.size()), argc + 1));
            }

            std::vector<StackValue> args(static_cast<std::size_t>(argc));
            for (int index = argc - 1; index >= 0; --index) {
                args[static_cast<std::size_t>(index)] = stack.back();
                stack.pop_back();
            }
            StackValue receiver = stack.back();
            stack.pop_back();
            if (!std::holds_alternative<InstanceHandle>(receiver)) {
                throw runtime_error_op(vietvm::messages::formatMessage(
                    vietvm::messages::kVmObjectExpectedInstance), instr.op, pc,
                    vietvm::runtime::runtimeMemberFacts(methodName, false, false, true));
            }
            const InstanceHandle &instance = std::get<InstanceHandle>(receiver);
            ClassHandle dispatchClass;
            if (instance != nullptr) {
                if (instr.operandValue == 1) {
                    if (!callStack.empty() &&
                        callStack.back().methodOwnerClass != nullptr) {
                        dispatchClass = callStack.back().methodOwnerClass->superclass;
                    }
                } else {
                    dispatchClass = instance->klass;
                }
            }
            const auto method = vietvm::runtime::resolveMethod(dispatchClass, methodName);
            if (!method.has_value()) {
                throw runtime_error_op(vietvm::messages::formatMessage(
                    vietvm::messages::kVmObjectMethodNotFound, {methodName}), instr.op, pc,
                    vietvm::runtime::runtimeMemberFacts(methodName, true, true, false));
            }
            const ClassHandle callerClass = callStack.empty()
                ? nullptr
                : callStack.back().methodOwnerClass;
            if (!canAccessRuntimeMethod(
                    method->visibility, callerClass, method->owner)) {
                throw runtime_error_op(
                    runtimeMethodAccessMessage(method->visibility, methodName),
                    instr.op, pc,
                    vietvm::runtime::runtimeMemberFacts(
                        methodName, true, true, true, true, false));
            }
            for (const StackValue &argument : args) stack.push_back(argument);
            invokeFunction(argc, method->functionId, instr.op, static_cast<int>(pc),
                           instance, method->owner);
            return;
        }
        default:
            throw runtime_error_op(vietvm::messages::formatMessage(
                vietvm::messages::kVmUnknownOpcode), instr.op, pc);
    }
}

// Xử lý opcode biến cục bộ/tham số bằng cách đọc hoặc ghi slot trong call frame hiện tại.
void VM::executeVariableOpcode(const Instruction& instr) {
    switch (instr.op) {
        case OP_KHOI_TAO: {
            const int varId = instr.operandIndex;
            if (!callStack.empty()) {
                CallFrame &frame = callStack.back();
                if (frame.localsIndexed) {
                    if (varId >= 0 && varId >= static_cast<int>(frame.localsVec.size())) {
                        frame.localsVec.resize(varId + 1, make_int_value(0));
                    } else if (varId >= 0) {
                        frame.localsVec[varId] = make_int_value(0);
                    }
                } else {
                    frame.localsMap[varId] = make_int_value(0);
                }
            } else if (variables.count(varId) == 0) {
                variables[varId] = make_int_value(0);
            }
            return;
        }
        case OP_TEN_BIEN_GIA_TRI: {
            const int varId = instr.operandIndex;
            if (!callStack.empty()) {
                CallFrame &frame = callStack.back();
                const auto captured = frame.capturedCells.find(varId);
                if (captured != frame.capturedCells.end() && captured->second != nullptr) {
                    stack.push_back(captured->second->value);
                    return;
                }
                if (frame.localsIndexed) {
                    if (varId >= 0 && varId < static_cast<int>(frame.localsVec.size())) {
                        stack.push_back(frame.localsVec[varId]);
                        return;
                    }
                } else {
                    auto itloc = frame.localsMap.find(varId);
                    if (itloc != frame.localsMap.end()) {
                        stack.push_back(itloc->second);
                        return;
                    }
                }
            }
            if (variables.count(varId) == 0) {
                variables[varId] = make_int_value(0);
            }
            stack.push_back(variables[varId]);
            return;
        }
        case OP_GAN: {
            if (stack.size() < 2) throw runtime_error_op(vietvm::messages::formatMessage(
                vietvm::messages::kVmAssignNotEnoughOperands), instr.op, pc);

            StackValue top = stack.back();
            StackValue second = stack[stack.size() - 2];
            StackValue varIdVal;
            StackValue valueVal;
            if (std::holds_alternative<int>(top) && std::holds_alternative<std::string>(second)) {
                varIdVal = top;
                valueVal = second;
            } else if (std::holds_alternative<std::string>(top) && std::holds_alternative<int>(second)) {
                varIdVal = second;
                valueVal = top;
            } else {
                varIdVal = top;
                valueVal = second;
            }
            stack.pop_back();
            stack.pop_back();

            if (!std::holds_alternative<int>(varIdVal)) {
                throw runtime_error_op(vietvm::messages::formatMessage(
                    vietvm::messages::kVmVariableIdMustBeInt), instr.op, pc);
            }
            const int varId = std::get<int>(varIdVal);
            if (!callStack.empty()) {
                CallFrame &frame = callStack.back();
                const auto captured = frame.capturedCells.find(varId);
                if (captured != frame.capturedCells.end() && captured->second != nullptr) {
                    captured->second->value = valueVal;
                    return;
                }
                if (frame.localsIndexed) {
                    if (varId >= 0 && varId < static_cast<int>(frame.localsVec.size())) {
                        frame.localsVec[varId] = valueVal;
                        return;
                    }
                    if (variables.count(varId) != 0) {
                        variables[varId] = valueVal;
                        return;
                    }
                    if (varId >= 0) {
                        frame.localsVec.resize(varId + 1, make_int_value(0));
                        frame.localsVec[varId] = valueVal;
                        return;
                    }
                } else {
                    auto itloc = frame.localsMap.find(varId);
                    if (itloc != frame.localsMap.end()) {
                        frame.localsMap[varId] = valueVal;
                        return;
                    }
                    if (variables.count(varId) != 0) {
                        variables[varId] = valueVal;
                        return;
                    }
                    frame.localsMap[varId] = valueVal;
                    return;
                }
            }
            variables[varId] = valueVal;
            return;
        }
        case OP_PARAM: {
            const int localId = instr.operandIndex;
            const int argIndex = instr.operandValue;
            if (callStack.empty()) {
                variables[localId] = make_int_value(0);
                return;
            }
            CallFrame &frame = callStack.back();
            StackValue value;
            if (argIndex == -1) {
                if (frame.receiver == nullptr) {
                    throw runtime_error_op(vietvm::messages::formatMessage(
                        vietvm::messages::kVmObjectExpectedInstance), instr.op, pc);
                }
                value = make_instance_value(frame.receiver);
            } else if (argIndex >= 0 && argIndex < static_cast<int>(frame.args.size())) {
                value = frame.args[argIndex];
            } else {
                throw vietvm::runtime::RuntimeError(
                    vietvm::messages::formatMessage(
                        vietvm::messages::kVmRequiredArgumentMissing,
                        {std::to_string(argIndex)}),
                    vietvm::runtime::RuntimeErrorKind::CallBoundary,
                    vietvm::runtime::runtimeRequiredArgumentFacts(argIndex));
            }
            if (frame.localsIndexed) {
                if (localId >= static_cast<int>(frame.localsVec.size())) {
                    frame.localsVec.resize(localId + 1, make_int_value(0));
                }
                frame.localsVec[localId] = value;
            } else {
                frame.localsMap[localId] = value;
            }
            return;
        }
        case OP_PARAM_MAC_DINH: {
            const int defaultIndex = instr.operand;
            const int localId = instr.operandIndex;
            const int argIndex = instr.operandValue;
            if (defaultIndex < 0 || defaultIndex >= static_cast<int>(stringPool.size())) {
                throw runtime_error_op(vietvm::messages::formatMessage(
                    vietvm::messages::kVmDefaultParamIndexInvalid), instr.op, pc);
            }
            StackValue value;
            if (!callStack.empty() && argIndex >= 0 && argIndex < static_cast<int>(callStack.back().args.size())) {
                value = callStack.back().args[argIndex];
            } else {
                value = decodeDefaultParamValue(stringPool[defaultIndex]);
            }
            if (callStack.empty()) {
                variables[localId] = value;
                return;
            }
            CallFrame &frame = callStack.back();
            if (frame.localsIndexed) {
                if (localId >= static_cast<int>(frame.localsVec.size())) {
                    frame.localsVec.resize(localId + 1, make_int_value(0));
                }
                frame.localsVec[localId] = value;
            } else {
                frame.localsMap[localId] = value;
            }
            return;
        }
        case OP_CONG_MOT:
        case OP_TRU_MOT: {
            if (stack.empty()) {
                throw runtime_error_op(vietvm::messages::formatMessage(
                    instr.op == OP_CONG_MOT ? vietvm::messages::kVmIncrementEmptyStack
                                           : vietvm::messages::kVmDecrementEmptyStack), instr.op, pc,
                    vietvm::runtime::runtimeStackFacts(0, 1));
            }
            StackValue top = stack.back(); stack.pop_back();
            const int delta = instr.op == OP_CONG_MOT ? 1 : -1;
            auto pushResult = [&](int value) { stack.push_back(make_int_value(value)); };
            if (std::holds_alternative<int>(top)) {
                const int idOrVal = std::get<int>(top);
                bool updated = false;
                if (!callStack.empty()) {
                    CallFrame &frame = callStack.back();
                    const auto captured = frame.capturedCells.find(idOrVal);
                    if (captured != frame.capturedCells.end() && captured->second != nullptr) {
                        const int current = as_int(captured->second->value, instr.op, pc);
                        captured->second->value = make_int_value(current + delta);
                        pushResult(current + delta);
                        updated = true;
                    }
                    if (!updated && frame.localsIndexed) {
                        if (idOrVal >= 0 && idOrVal < static_cast<int>(frame.localsVec.size())) {
                            const int current = as_int(frame.localsVec[idOrVal], instr.op, pc);
                            frame.localsVec[idOrVal] = make_int_value(current + delta);
                            pushResult(current + delta);
                            updated = true;
                        }
                    } else if (!updated) {
                        auto itLoc = frame.localsMap.find(idOrVal);
                        if (itLoc != frame.localsMap.end()) {
                            const int current = as_int(itLoc->second, instr.op, pc);
                            itLoc->second = make_int_value(current + delta);
                            pushResult(current + delta);
                            updated = true;
                        }
                    }
                }
                if (!updated) {
                    const int current = variables.count(idOrVal)
                        ? as_int(variables[idOrVal], instr.op, pc) : 0;
                    variables[idOrVal] = make_int_value(current + delta);
                    pushResult(current + delta);
                }
                return;
            }
            if (instr.op == OP_CONG_MOT && std::holds_alternative<std::string>(top)) {
                try {
                    pushResult(std::stoi(std::get<std::string>(top)) + 1);
                    return;
                } catch (...) {
                    throw runtime_error_op(vietvm::messages::formatMessage(
                        vietvm::messages::kVmIncrementCannotIncreaseString), instr.op, pc,
                        vietvm::runtime::runtimeTypeFacts("chuỗi"));
                }
            }
            throw runtime_error_op(vietvm::messages::formatMessage(
                instr.op == OP_CONG_MOT ? vietvm::messages::kVmIncrementUnsupportedType
                                       : vietvm::messages::kVmDecrementUnsupportedType), instr.op, pc,
                vietvm::runtime::runtimeTypeFacts(runtime_value_type_name(top)));
        }
        default:
            throw runtime_error_op(vietvm::messages::formatMessage(
                vietvm::messages::kVmUnknownOpcode), instr.op, pc);
    }
}

// Điều khiển trạng thái của câu lệnh `chọn`, theo dõi nhánh đã khớp và việc bỏ qua các nhánh còn lại.
bool VM::executeSwitchOpcode(const Instruction& instr) {
    switch (instr.op) {
        case OP_CHON: {
            if (stack.empty()) throw runtime_error_op(vietvm::messages::formatMessage(
                vietvm::messages::kVmSwitchEmptyStack), instr.op, pc,
                vietvm::runtime::runtimeStackFacts(0, 1));
            SwitchFrame frame;
            frame.switchValue = stack.back();
            stack.pop_back();
            frame.skippingCase = true;
            frame.caseMatched = false;
            frame.blockDepthAtStart = blockStack.size();
            switchStack.push_back(frame);
            return false;
        }
        case OP_CA: {
            if (switchStack.empty() || !switchStack.back().switchValue.has_value()) {
                throw runtime_error_op(vietvm::messages::formatMessage(
                    vietvm::messages::kVmCaseOutsideSwitch), instr.op, pc);
            }
            auto &ctx = switchStack.back();
            bool match = false;
            if (instr.operandIndex >= 0) {
                const std::string caseStr = stringPool.at(instr.operandIndex);
                if (std::holds_alternative<std::string>(*ctx.switchValue)) {
                    match = std::get<std::string>(*ctx.switchValue) == caseStr;
                } else if (std::holds_alternative<int>(*ctx.switchValue)) {
                    match = std::to_string(std::get<int>(*ctx.switchValue)) == caseStr;
                }
            } else if (instr.operandIndex == -1) {
                if (std::holds_alternative<int>(*ctx.switchValue)) {
                    match = std::get<int>(*ctx.switchValue) == instr.operand;
                } else if (std::holds_alternative<std::string>(*ctx.switchValue)) {
                    try {
                        match = std::stoi(std::get<std::string>(*ctx.switchValue)) == instr.operand;
                    } catch (...) {
                        match = false;
                    }
                }
            } else if (instr.operandIndex == -2) {
                const int varId = instr.operand;
                int varValue = 0;
                if (variables.count(varId)) {
                    varValue = as_int(variables[varId], instr.op, pc);
                }
                if (std::holds_alternative<int>(*ctx.switchValue)) {
                    match = std::get<int>(*ctx.switchValue) == varValue;
                } else if (std::holds_alternative<std::string>(*ctx.switchValue)) {
                    try {
                        match = std::stoi(std::get<std::string>(*ctx.switchValue)) == varValue;
                    } catch (...) {
                        match = false;
                    }
                }
            } else {
                throw runtime_error_op(vietvm::messages::formatMessage(
                    vietvm::messages::kVmCaseInvalidOperandFormat), instr.op, pc,
                    vietvm::runtime::runtimeControlFacts("dữ liệu của nhánh ca không hợp lệ", false));
            }
            if (!ctx.caseMatched && match) {
                ctx.skippingCase = false;
                ctx.caseMatched = true;
            } else {
                ctx.skippingCase = true;
            }
            return false;
        }
        case OP_MAC_DINH: {
            if (switchStack.empty()) throw runtime_error_op(vietvm::messages::formatMessage(
                vietvm::messages::kVmDefaultOutsideSwitch), instr.op, pc,
                vietvm::runtime::runtimeControlFacts("mặc định nằm ngoài khối chọn", false));
            auto &ctx = switchStack.back();
            if (!ctx.caseMatched) {
                ctx.skippingCase = false;
                ctx.caseMatched = true;
            } else {
                ctx.skippingCase = true;
            }
            return false;
        }
        case OP_THOAT: {
            if (switchStack.empty()) throw runtime_error_op(vietvm::messages::formatMessage(
                vietvm::messages::kVmBreakOutsideSwitch), instr.op, pc,
                vietvm::runtime::runtimeControlFacts("thoát không có khối chọn đang hoạt động", false));
            switchStack.back().skippingCase = true;
            const auto &code = currentBytecode();
            while (pc < code.size()) {
                if (code[pc].op == OP_DONG_KHOI) {
                    ++pc;
                    break;
                }
                ++pc;
            }
            return true;
        }
        default:
            throw runtime_error_op(vietvm::messages::formatMessage(
                vietvm::messages::kVmUnknownOpcode), instr.op, pc);
    }
}

// Xử lý `thoát` và `tiếp tục` bằng metadata điều khiển vòng lặp đã được codegen gắn vào bytecode.
void VM::executeLoopControlOpcode(const Instruction& instr) {
    if (instr.op != OP_BO_QUA) {
        throw runtime_error_op(vietvm::messages::formatMessage(
            vietvm::messages::kVmUnknownOpcode), instr.op, pc);
    }
    size_t scanPc = pc + 1;
    const auto &code = currentBytecode();
    while (scanPc < code.size()) {
        if (code[scanPc].op == OP_CAP_NHAT) {
            pc = scanPc;
            return;
        }
        ++scanPc;
    }
    throw runtime_error_op(vietvm::messages::formatMessage(
        vietvm::messages::kVmContinueMissingUpdate), instr.op, pc);
}

// Cập nhật trạng thái khi VM đi vào hoặc rời block, phục vụ các cấu trúc điều khiển và exception scope.
void VM::executeBlockOpcode(const Instruction& instr) {
    if (instr.op == OP_MO_KHOI) {
        blockStack.push_back(pc);
        ++blockDepth;
        return;
    }
    if (instr.op != OP_DONG_KHOI) {
        throw runtime_error_op(vietvm::messages::formatMessage(
            vietvm::messages::kVmUnknownOpcode), instr.op, pc);
    }
    if (blockStack.empty()) throw runtime_error_op(vietvm::messages::formatMessage(
        vietvm::messages::kVmNoOpenBlock), instr.op, pc);
    blockStack.pop_back();
    if (blockDepth > 0) --blockDepth;
    if (!switchStack.empty()
        && blockStack.size() == switchStack.back().blockDepthAtStart - 1) {
        switchStack.pop_back();
    }
}

// Xử lý `thử`, `bắt lỗi` và `ném`; hàm quản lý try frame và chuyển điều khiển tới handler phù hợp.
bool VM::executeExceptionOpcode(const Instruction& instr) {
    switch (instr.op) {
        case OP_THU: {
            TryFrame frame;
            frame.catchAddr = instr.operand;
            frame.stackDepth = static_cast<int>(stack.size());
            frame.errVarId = instr.operandIndex;
            frame.blockStackDepth = blockStack.size();
            frame.switchStackDepth = switchStack.size();
            frame.loopStackDepth = loopStartStack.size();
            frame.ifElseStackDepth = ifElseStack.size();
            frame.blockDepth = blockDepth;
            tryStack.push_back(frame);
            return false;
        }
        case OP_THU_KET_THUC:
            if (!tryStack.empty()) tryStack.pop_back();
            pc = instr.operand;
            return true;
        case OP_BAT_LOI: {
            const int errVarId = instr.operandIndex;
            if (errVarId >= 0 && !stack.empty()) {
                StackValue errVal = stack.back(); stack.pop_back();
                if (!callStack.empty()) {
                    CallFrame &frame = callStack.back();
                    const auto captured = frame.capturedCells.find(errVarId);
                    if (captured != frame.capturedCells.end() && captured->second != nullptr) {
                        captured->second->value = std::move(errVal);
                    } else if (frame.localsIndexed) {
                        if (errVarId >= static_cast<int>(frame.localsVec.size())) {
                            frame.localsVec.resize(errVarId + 1, make_int_value(0));
                        }
                        frame.localsVec[errVarId] = std::move(errVal);
                    } else {
                        frame.localsMap[errVarId] = std::move(errVal);
                    }
                } else {
                    variables[errVarId] = std::move(errVal);
                }
            } else if (!stack.empty()) {
                stack.pop_back();
            }
            return false;
        }
        case OP_NEM: {
            StackValue errVal = stack.empty()
                ? make_string_value(vietvm::messages::formatMessage(
                      vietvm::messages::kVmUnknownThrownValue))
                : stack.back();
            if (!stack.empty()) stack.pop_back();
            if (transferThrownValue(errVal)) return true;
            throw vietvm::runtime::LanguageException(std::move(errVal));
        }
        default:
            throw runtime_error_op(vietvm::messages::formatMessage(
                vietvm::messages::kVmUnknownOpcode), instr.op, pc);
    }
}

// Chuyển giá trị `ném` tới handler gần nhất và unwind toàn bộ trạng thái điều khiển
// tạm được tạo sau khi `thử` bắt đầu. Giá trị lỗi được giữ nguyên trên stack để
// `OP_BAT_LOI` bind vào biến catch.
bool VM::transferThrownValue(const StackValue &value) {
    if (tryStack.empty()) return false;

    TryFrame frame = tryStack.back();
    tryStack.pop_back();
    while (static_cast<int>(stack.size()) > frame.stackDepth) stack.pop_back();
    if (blockStack.size() > frame.blockStackDepth) blockStack.resize(frame.blockStackDepth);
    if (switchStack.size() > frame.switchStackDepth) switchStack.resize(frame.switchStackDepth);
    if (loopStartStack.size() > frame.loopStackDepth) loopStartStack.resize(frame.loopStackDepth);
    if (ifElseStack.size() > frame.ifElseStackDepth) ifElseStack.resize(frame.ifElseStackDepth);
    blockDepth = frame.blockDepth;
    stack.push_back(value);
    pc = static_cast<std::size_t>(frame.catchAddr);
    return true;
}

// Xử lý opcode nhảy có điều kiện/không điều kiện bằng cách cập nhật program counter dựa trên giá trị trên stack.
bool VM::executeBranchOpcode(const Instruction& instr) {
    const int jumpAddress = instr.operand;
    const auto &code = currentBytecode();
    if (instr.op == OP_JUMP) {
        if (jumpAddress < 0 || jumpAddress >= static_cast<int>(code.size())) {
            throw runtime_error_op(vietvm::messages::formatMessage(
                vietvm::messages::kVmJumpAddressOutOfRange), instr.op, pc,
                vietvm::runtime::runtimeJumpFacts(jumpAddress, false));
        }
        pc = jumpAddress;
        return true;
    }

    if (stack.empty()) throw runtime_error_op(vietvm::messages::formatMessage(
        vietvm::messages::kVmJumpIfFalseEmptyStack), instr.op, pc,
        vietvm::runtime::runtimeStackFacts(0, 1));
    StackValue condition = stack.back(); stack.pop_back();
    if (!std::holds_alternative<int>(condition)) {
        vietvm::runtime::RuntimeDiagnosticContext context;
        context.expectsInteger = true;
        context.actualType = runtime_value_type_name(condition);
        throw runtime_error_op(vietvm::messages::formatMessage(
            vietvm::messages::kVmJumpConditionMustBeInt), instr.op, pc, std::move(context));
    }
    if (as_int(condition, instr.op, pc) != 0) {
        return false;
    }
    if (jumpAddress < 0 || jumpAddress >= static_cast<int>(code.size())) {
        throw runtime_error_op(vietvm::messages::formatMessage(
            vietvm::messages::kVmJumpAddressOutOfRange), instr.op, pc);
    }
    pc = jumpAddress;
    return true;
}

// Xử lý opcode xuất dữ liệu, chuyển `StackValue` thành chuỗi rồi gửi tới output sink đã cấu hình.
void VM::executeOutputOpcode(const Instruction& instr) {
    if (instr.op != OP_IN) {
        throw runtime_error_op(vietvm::messages::formatMessage(
            vietvm::messages::kVmUnknownOpcode), instr.op, pc);
    }
    if (stack.empty()) {
        throw runtime_error_op(vietvm::messages::formatMessage(
            vietvm::messages::kVmEmptyStackWhenPrint), instr.op, pc,
            vietvm::runtime::runtimeStackFacts(0, 1));
    }

    StackValue value = stack.back();
    stack.pop_back();
    if (switchStack.empty() || !switchStack.back().skippingCase) {
        emitOutput(value);
    }
}

// Chạy vòng lặp VM từ bytecode hiện tại; mỗi bước đọc opcode tại program counter và chuyển tới handler tương ứng cho tới khi dừng.
void VM::run() {
    vietvm::runtime::RuntimeHeapScope heapScope(*runtimeHeap);
    ensureBytecodeVerified();

    initializeModules();

    if (functionTableByNameIndex.empty()) {
        for (const Instruction &candidate : bytecode) {
            if (candidate.op == OP_HAM && candidate.operand >= 0) {
                functionTableByNameIndex[candidate.operand] = candidate.operandIndex;
                functionNameIndexById_.emplace(candidate.operandIndex, candidate.operand);
            }
        }
    }

    if (vietvm::helpers::hasEnvVar(vietvm::constants::kEnvVppEnableJit) ||
        vietvm::helpers::hasEnvVar(vietvm::constants::kEnvVietvmEnableJit)) {
        runJitCompiled();
        return;
    }
    jitFastInstructionCount_ = 0;
    jitInterpreterInstructionCount_ = 0;
    runInterpreterLoop();
}

// Thực thi bytecode bằng một vòng lặp duy nhất. Mỗi lời gọi V++ chỉ thay context
// hiện tại và đẩy snapshot caller vào `executionStack`, vì vậy recursion của
// ngôn ngữ không tạo thêm native C++ stack frame.
void VM::runInterpreterLoop(std::optional<std::size_t> stopExecutionDepth) {
    VMRuntimeFixture runtime(*this);
    int gcInterval = vietvm::constants::kDefaultGcInterval;
    std::optional<std::string> gcEnv = vietvm::helpers::getEnvVar(vietvm::constants::kEnvVppGcInterval);
    if (!gcEnv) gcEnv = vietvm::helpers::getEnvVar(vietvm::constants::kEnvVietvmGcInterval);
    if (gcEnv) {
        try {
            int parsed = std::stoi(*gcEnv);
            if (parsed > 0) gcInterval = parsed;
        } catch (...) {}
    }

    int executedSinceGc = 0;
    while (true) {
        if (cancellationRequested_ != nullptr &&
            cancellationRequested_->load(std::memory_order_acquire)) {
            throw VmWorkerCancelled{};
        }
        if (stopExecutionDepth.has_value() &&
            executionStack.size() <= *stopExecutionDepth) {
            return;
        }
        const auto &code = currentBytecode();
        if (pc >= code.size()) {
            if (completeFunctionCall()) continue;
            collectGarbage();
            return;
        }

        ++executedSinceGc;
        if (executedSinceGc >= gcInterval) {
            if (runtimeHeap->needsCollection()) collectGarbage();
            executedSinceGc = 0;
        }

        const Instruction &instr = code[pc];
        try {
            bool compiled = false;
            if (jitActive_) {
                const auto program = jitPrograms_.find(&code);
                if (program != jitPrograms_.end() && pc < program->second.size()) {
                    const JitOperation &operation = program->second[pc];
                    if (operation) {
                        operation();
                        ++jitFastInstructionCount_;
                        compiled = true;
                    }
                }
                if (!compiled) ++jitInterpreterInstructionCount_;
            }
            if (!compiled) {
            switch (instr.op) {
            case OP_HAM:
            case OP_NEU:
            case OP_DIEU_KIEN:
            case OP_LAP:
            case OP_CAP_NHAT:
            case OP_DONG_LENH:
            case OP_MO_NGOAC:
            case OP_DONG_NGOAC:
                break;

            case OP_GOI:
            case OP_GOI_GIAN_TIEP: {
                const std::size_t executionDepth = executionStack.size();
                executeCallOpcode(instr);
                if (executionStack.size() > executionDepth) continue;
                break;
            }

            case OP_VM_BIEN_DICH_PHAN_TICH:
            case OP_VM_DNS_PHAN_GIAI:
            case OP_VM_DOC_BIEN_MOI_TRUONG:
            case OP_VM_DONG_HO_DIA_PHUONG:
            case OP_VM_DONG_HO_UTC:
            case OP_VM_DUONG_DAN_TON_TAI:
            case OP_VM_IO_DOC_BYTES:
            case OP_VM_IO_DOC_FILE:
            case OP_VM_IO_GHI_BYTES:
            case OP_VM_IO_GHI_FILE:
            case OP_VM_IO_GHI_TIEP_FILE:
            case OP_VM_KICH_BAN_CHAY:
            case OP_VM_LA_TEP:
            case OP_VM_LA_THU_MUC_KHONG_THEO_LIEN_KET:
            case OP_VM_LA_THU_MUC:
            case OP_VM_LIET_KE_THU_MUC:
            case OP_VM_NGAU_NHIEN_BAO_MAT_BYTES:
            case OP_VM_NGU_MILI_GIAY:
            case OP_VM_SO_THUC_BITS:
            case OP_VM_SOCKET_CHAP_NHAN:
            case OP_VM_SOCKET_DAT_TIMEOUT:
            case OP_VM_SOCKET_DONG:
            case OP_VM_SOCKET_GUI:
            case OP_VM_SOCKET_NHAN:
            case OP_VM_SOCKET_PHAN_GIAI:
            case OP_VM_SOCKET_TCP_LANG_NGHE:
            case OP_VM_SOCKET_TCP_MO:
            case OP_VM_SOCKET_TLS_NANG_CAP:
            case OP_VM_SOCKET_UDP_MO:
            case OP_VM_TAO_THU_MUC:
            case OP_VM_TEN_NEN_TANG:
            case OP_VM_THOI_GIAN_DON_DIEU_MS:
            case OP_VM_TIEN_TRINH_CHAY:
            case OP_VM_XOA_DUONG_DAN:
            case OP_VM_THREAD_SPAWN:
            case OP_VM_THREAD_WAIT:
            case OP_VM_THREAD_CANCEL:
            case OP_VM_THREAD_STATUS:
            case OP_VM_THREAD_PARK:
            case OP_VM_TUPLE_FROM_LIST:
            case OP_VM_TYPE_OF:
            case OP_VM_IDENTITY_HASH:
            case OP_VM_LENGTH:
            case OP_VM_LIST_APPEND:
            case OP_VM_LIST_REMOVE:
            case OP_VM_MAP_HAS:
            case OP_VM_MAP_REMOVE:
            case OP_VM_MAP_KEYS:
            case OP_VM_STRING_BYTES:
            case OP_VM_STRING_FROM_BYTES:
            case OP_VM_FLOAT_FROM_BITS:
                if (switchStack.empty() || !switchStack.back().skippingCase) {
                    executeIntrinsicOpcode(instr);
                }
                break;

            case OP_BIEN_SO:
            case OP_TEN_BIEN_ID:
            case OP_MODULO:
            case OP_CONG:
            case OP_TRU:
            case OP_NHAN:
            case OP_CHIA:
            case OP_Logic_VA:
            case OP_Logic_HOAC:
            case OP_SO_SANH_BANG:
            case OP_KHAC_BANG:
            case OP_LON_HON:
            case OP_NHO_HON:
            case OP_LON_HON_HOAC_BANG:
            case OP_NHO_HON_HOAC_BANG:
            case OP_KHONG:
            case OP_CHUOI:
            case OP_BIEN_SO_FLOAT:
            case OP_RONG_GIA_TRI:
            case OP_DUNG_GIA_TRI:
            case OP_SAI_GIA_TRI:
            case OP_MAP_LITERAL:
            case OP_LIST_LITERAL:
            case OP_PHU_DINH:
            case OP_TAO_DONG_BAO:
                runtime.executeValue(instr);
                break;

            case OP_KHOI_TAO:
            case OP_TEN_BIEN_GIA_TRI:
            case OP_GAN:
            case OP_PARAM:
            case OP_PARAM_MAC_DINH:
            case OP_CONG_MOT:
            case OP_TRU_MOT:
                runtime.executeVariable(instr);
                break;

            case OP_DOC_CHI_SO:
            case OP_GAN_CHI_SO:
                runtime.executeIndex(instr);
                break;

            case OP_TAO_LOP:
            case OP_THEM_PHUONG_THUC:
            case OP_TAO_DOI_TUONG:
            case OP_DOC_THUOC_TINH:
            case OP_GAN_THUOC_TINH:
            case OP_GOI_PHUONG_THUC: {
                const std::size_t executionDepth = executionStack.size();
                executeObjectOpcode(instr);
                if (executionStack.size() > executionDepth) continue;
                break;
            }

            case OP_IN:
                runtime.executeOutput(instr);
                break;

            case OP_TRA_VE:
                if (stack.empty()) stack.push_back(make_int_value(0));
                if (completeFunctionCall()) continue;
                return;

            case OP_BO_QUA:
                runtime.executeLoopControl(instr);
                break;

            case OP_CHON:
            case OP_CA:
            case OP_MAC_DINH:
            case OP_THOAT:
                if (runtime.executeSwitch(instr)) continue;
                break;

            case OP_JUMP:
            case OP_JUMP_IF_FALSE:
                if (runtime.executeBranch(instr)) continue;
                break;

            case OP_DUNG_CHUONG_TRINH:
                if (completeFunctionCall()) continue;
                collectGarbage();
                return;

            case OP_MO_KHOI:
            case OP_DONG_KHOI:
                runtime.executeBlock(instr);
                break;

            case OP_THU:
            case OP_THU_KET_THUC:
            case OP_BAT_LOI:
            case OP_NEM:
                if (runtime.executeException(instr)) continue;
                break;

            default:
                if (!switchStack.empty() && switchStack.back().skippingCase) {
                    break;
                }
                throw runtime_error_op(vietvm::messages::formatMessage(
                    vietvm::messages::kVmUnknownOpcode), instr.op, pc);
            }
            }
        } catch (const vietvm::runtime::LanguageException &thrown) {
            const std::string thrownText = sv_to_string(thrown.value());
            const bool conversionFailure =
                thrownText == vietvm::messages::messageText(
                    vietvm::messages::kNativeToFloatConversionFailed) ||
                thrownText == vietvm::messages::messageText(
                    vietvm::messages::kNativeToIntegerConversionFailed);
            std::string conversionInput;
            vietvm::runtime::RuntimeSourceLocation conversionCallerFrame;
            if (conversionFailure) {
                const std::string conversionFunction =
                    thrownText == vietvm::messages::messageText(
                        vietvm::messages::kNativeToIntegerConversionFailed)
                        ? "thành số nguyên"
                        : "thành số thực";
                for (std::size_t index = 0; index < callStack.size(); ++index) {
                    const CallFrame &frame = callStack[index];
                    if (frame.functionName != conversionFunction) continue;
                    if (!frame.args.empty()) {
                        conversionInput = sv_to_string(frame.args.front());
                    }
                    if (index < executionStack.size()) {
                        const ExecutionContext &caller = executionStack[index];
                        const auto *callerDebugInfo =
                            caller.bytecode == nullptr
                                ? &bytecodeDebugInfo
                                : caller.bytecodeDebugInfo;
                        if (callerDebugInfo != nullptr &&
                            caller.pc < callerDebugInfo->size()) {
                            conversionCallerFrame = (*callerDebugInfo)[caller.pc];
                        }
                    }
                    break;
                }
                if (conversionInput.empty()) {
                    for (auto frame = callStack.rbegin(); frame != callStack.rend(); ++frame) {
                        if (frame->args.empty()) continue;
                        conversionInput = sv_to_string(frame->args.front());
                        break;
                    }
                }
                if (!conversionCallerFrame.valid()) {
                    conversionCallerFrame = sourceLocationForPc(pc);
                }
            }

            if (unwindLanguageException(thrown.value())) continue;

            // Library conversion is implemented in V++ and therefore signals
            // failure with `ném`.  Keep that value catchable while a handler
            // exists, but recover the structured runtime diagnostic when it
            // reaches the program boundary uncaught.
            if (conversionFailure) {
                const std::string target =
                    thrownText == vietvm::messages::messageText(
                        vietvm::messages::kNativeToIntegerConversionFailed)
                        ? "số nguyên"
                        : "số thực";
                vietvm::runtime::RuntimeError error(
                    thrownText,
                    vietvm::runtime::RuntimeErrorKind::VmFault,
                    vietvm::runtime::runtimeConversionFacts(
                        conversionInput, target, false));
                error.addFrame(std::move(conversionCallerFrame));
                throw error;
            }
            throw;
        } catch (vietvm::runtime::RuntimeError &error) {
            unwindRuntimeError(error);
            throw;
        }
        ++pc;
    }
}

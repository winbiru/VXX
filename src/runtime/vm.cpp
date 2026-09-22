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
#include <unordered_map>
#include <regex>
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
#include "common/vm_native_helpers.h"
#include "common/vm_native_constants.h"
#include "common/vm_native_http_helpers.h"
#include "common/vm_native_json_helpers.h"
#include "common/vm_low_level_http_server.h"
#include "common/vm_native_stdlib_helpers.h"
#include "common/vm_native_text_helpers.h"
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

// Xử lý `native http client function`; hàm dispatch theo loại/tên yêu cầu, đọc đối số cần thiết và ghi kết quả trở lại runtime.
static bool handleNativeHttpClientFunction(const std::string &fn,
                                           const std::vector<StackValue> &args,
                                           StackValue &result,
    std::string &err) {
    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnHttpGet)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        std::string url = vietvm::helpers::argToRawString(args[0]);
        return vietvm::helpers::runCurlHttpRequest(vietvm::constants::kHttpMethodGet, fn, url, std::nullopt, result, err);
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnHttpPost)) {
        if (!requireNativeArgumentCount(args, fn, 2, err)) return true;
        std::string url = vietvm::helpers::argToRawString(args[0]);
        std::string payload = vietvm::helpers::argToRawString(args[1]);
        return vietvm::helpers::runCurlHttpRequest(vietvm::constants::kHttpMethodPost, fn, url, payload, result, err);
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnHttpPut)) {
        if (!requireNativeArgumentCount(args, fn, 2, err)) return true;
        std::string url = vietvm::helpers::argToRawString(args[0]);
        std::string payload = vietvm::helpers::argToRawString(args[1]);
        return vietvm::helpers::runCurlHttpRequest(vietvm::constants::kHttpMethodPut, fn, url, payload, result, err);
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnHttpDelete)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        std::string url = vietvm::helpers::argToRawString(args[0]);
        return vietvm::helpers::runCurlHttpRequest(vietvm::constants::kHttpMethodDelete, fn, url, std::nullopt, result, err);
    }

    return false;
}

// Xử lý `native json function`; hàm dispatch theo loại/tên yêu cầu, đọc đối số cần thiết và ghi kết quả trở lại runtime.
static bool handleNativeJsonFunction(const std::string &fn,
                                     const std::vector<StackValue> &args,
                                     StackValue &result,
    std::string &err) {
    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnJsonEscape)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        const std::string text = vietvm::helpers::argToRawString(args[0]);
        if (!vietvm::core::isValidUtf8(text)) {
            err = fn + ": chuỗi phải là UTF-8 hợp lệ";
            return true;
        }
        result = make_string_value(vietvm::helpers::escapeJsonString(text));
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnJsonString)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        const std::string text = vietvm::helpers::argToRawString(args[0]);
        if (!vietvm::core::isValidUtf8(text)) {
            err = fn + ": chuỗi phải là UTF-8 hợp lệ";
            return true;
        }
        result = make_string_value(std::string("\"") +
                                   vietvm::helpers::escapeJsonString(text) +
                                   "\"");
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnJsonParse)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        vietvm::helpers::parseJson(vietvm::helpers::argToRawString(args[0]), result, err);
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnJsonEncode)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        std::string encoded;
        if (!vietvm::helpers::stringifyJson(args[0], encoded, err)) return true;
        result = make_string_value(encoded);
        return true;
    }

    return false;
}

// Xử lý `native low level http function`; hàm dispatch theo loại/tên yêu cầu, đọc đối số cần thiết và ghi kết quả trở lại runtime.
static bool handleNativeLowLevelHttpFunction(const std::string &fn,
                                             const std::vector<StackValue> &args,
                                             StackValue &result,
                                             std::string &err,
                                             const VM::OutputSink &outputSink) {
    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnHttpServerOpen)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        int port = 0;
        if (!vietvm::helpers::parseIntArgFromStack(args[0], fn, vietvm::constants::kArgLabelPort, port, err)) return true;
        return vietvm::helpers::runLowLevelHttpServerOpen(port, result, err, outputSink);
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnHttpServerNext)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        int serverId = 0;
        if (!vietvm::helpers::parseIntArgFromStack(args[0], fn, vietvm::constants::kArgLabelServerId, serverId, err)) return true;
        return vietvm::helpers::runLowLevelHttpServerNext(serverId, result, err);
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnHttpReqMethod)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        return vietvm::helpers::runLowLevelHttpReqField(vietvm::helpers::argToRawString(args[0]), vietvm::constants::kReqFieldMethod, std::nullopt, result, err);
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnHttpReqPath)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        return vietvm::helpers::runLowLevelHttpReqField(vietvm::helpers::argToRawString(args[0]), vietvm::constants::kReqFieldPath, std::nullopt, result, err);
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnHttpReqQuery)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        return vietvm::helpers::runLowLevelHttpReqField(vietvm::helpers::argToRawString(args[0]), vietvm::constants::kReqFieldQuery, std::nullopt, result, err);
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnHttpReqBody)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        return vietvm::helpers::runLowLevelHttpReqField(vietvm::helpers::argToRawString(args[0]), vietvm::constants::kReqFieldBody, std::nullopt, result, err);
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnHttpReqHeader)) {
        if (!requireNativeArgumentCount(args, fn, 2, err)) return true;
        return vietvm::helpers::runLowLevelHttpReqField(vietvm::helpers::argToRawString(args[0]), vietvm::constants::kReqFieldHeader, vietvm::helpers::argToRawString(args[1]), result, err);
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnHttpReqQueryParam)) {
        if (!requireNativeArgumentCount(args, fn, 2, err)) return true;
        return vietvm::helpers::runLowLevelHttpReqField(vietvm::helpers::argToRawString(args[0]), vietvm::constants::kReqFieldQueryParam, vietvm::helpers::argToRawString(args[1]), result, err);
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnHttpReqJsonField)) {
        if (!requireNativeArgumentCount(args, fn, 2, err)) return true;
        return vietvm::helpers::runLowLevelHttpReqField(vietvm::helpers::argToRawString(args[0]), vietvm::constants::kReqFieldJsonField, vietvm::helpers::argToRawString(args[1]), result, err);
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnHttpReqPathSuffix)) {
        if (!requireNativeArgumentCount(args, fn, 2, err)) return true;
        return vietvm::helpers::runLowLevelHttpReqField(vietvm::helpers::argToRawString(args[0]), vietvm::constants::kReqFieldPathSuffix, vietvm::helpers::argToRawString(args[1]), result, err);
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnHttpServerSend)) {
        if (!requireNativeArgumentCount(args, fn, 3, err)) return true;
        int status = vietvm::constants::kHttpStatusOk;
        if (!vietvm::helpers::parseIntArgFromStack(args[1], fn, vietvm::constants::kArgLabelStatus, status, err)) return true;
        return vietvm::helpers::runLowLevelHttpServerSend(vietvm::helpers::argToRawString(args[0]), status, vietvm::helpers::argToRawString(args[2]), result, err);
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnHttpServerClose)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        int serverId = 0;
        if (!vietvm::helpers::parseIntArgFromStack(args[0], fn, vietvm::constants::kArgLabelServerId, serverId, err)) return true;
        return vietvm::helpers::runLowLevelHttpServerClose(serverId, result, err);
    }

    return false;
}

// Thực thi native thư viện chuẩn hàm; hàm đọc trạng thái VM/opcode đầu vào, cập nhật stack/frame/program counter và trả quyền điều khiển về vòng chạy chính.
static bool executeNativeStdlibFunction(int hamIdOrName,
                                        const std::vector<StackValue> &args,
                                        const std::vector<std::string> &stringPool,
                                        const std::unordered_map<int, int> &functionTableByNameIndex,
                                        const std::unordered_map<int, std::vector<Instruction>> &functionBytecode,
                                        StackValue &result,
                                        std::string &err,
                                        const VM::OutputSink &outputSink) {
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

    if (fn.empty()) return false;

    if (vietvm::helpers::handleNativeCollectionFunction(fn, args, result, err)) return true;

    if (vietvm::helpers::handleNativeTextFunction(fn, args, result, err)) return true;

    if (vietvm::helpers::handleNativeFoundationFunction(fn, args, result, err)) return true;

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnFileLineCount) ||
        vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnFileWordCount)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        std::filesystem::path path;
        if (!vietvm::helpers::nativeUtf8Path(args[0], fn, path, err)) return true;
        std::ifstream input(path);
        if (!input.is_open()) {
            err = vietvm::messages::formatMessage(
                vietvm::messages::kNativeFileOpenForReadFailed, {fn});
            return true;
        }
        if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnFileLineCount)) {
            int count = 0;
            std::string line;
            while (std::getline(input, line)) ++count;
            result = make_int_value(count);
            return true;
        }
        std::ostringstream content;
        content << input.rdbuf();
        result = make_int_value(static_cast<int>(
            vietvm::core::splitAsciiWords(content.str()).size()));
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnIoReadFile)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        std::filesystem::path path;
        if (!vietvm::helpers::nativeUtf8Path(args[0], fn, path, err)) return true;
        std::ifstream ifs(path);
        if (!ifs.is_open()) {
            err = vietvm::messages::formatMessage(
                vietvm::messages::kNativeFileOpenForReadFailed, {fn});
            return true;
        }
        std::ostringstream ss;
        ss << ifs.rdbuf();
        result = make_string_value(ss.str());
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnIoWriteFile)) {
        if (!requireNativeArgumentCount(args, fn, 2, err)) return true;
        std::filesystem::path path;
        if (!vietvm::helpers::nativeUtf8Path(args[0], fn, path, err)) return true;
        std::ofstream ofs(path);
        if (!ofs.is_open()) {
            err = vietvm::messages::formatMessage(
                vietvm::messages::kNativeFileOpenForWriteFailed, {fn});
            return true;
        }
        std::string content = vietvm::helpers::argToRawString(args[1]);
        ofs << content;
        if (!ofs.good()) {
            err = vietvm::messages::formatMessage(
                vietvm::messages::kNativeFileWriteFailed, {fn});
            return true;
        }
        result = make_int_value(1);
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnReadConfig)) {
        if (!requireNativeArgumentCount(args, fn, 1, err)) return true;
        std::filesystem::path path;
        if (!vietvm::helpers::nativeUtf8Path(args[0], fn, path, err)) return true;
        std::ifstream ifs(path);
        if (!ifs.is_open()) {
            err = vietvm::messages::formatMessage(
                vietvm::messages::kNativeFileOpenForReadFailed, {fn});
            return true;
        }

        MapValue cfg;
        std::string line;
        while (std::getline(ifs, line)) {
            const auto assignment = vietvm::helpers::parsePropertyAssignment(line);
            if (!assignment.has_value()) continue;
            const std::string &key = assignment->first;
            const std::string &val = assignment->second;
            if (key.empty()) continue;

            if (val == "đúng") cfg.entries[key] = make_int_value(1);
            else if (val == "sai") cfg.entries[key] = make_int_value(0);
            else if (val == "rỗng") cfg.entries[key] = make_null_value();
            else {
                bool parsed = false;
                try {
                    size_t p = 0;
                    int iv = std::stoi(val, &p);
                    if (p == val.size()) { cfg.entries[key] = make_int_value(iv); parsed = true; }
                } catch (...) {}
                if (!parsed) {
                    try {
                        size_t p = 0;
                        double dv = std::stod(val, &p);
                        if (p == val.size()) { cfg.entries[key] = make_float_value(dv); parsed = true; }
                    } catch (...) {}
                }
                if (!parsed) cfg.entries[key] = make_string_value(val);
            }
        }

        result = make_map_value(cfg);
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnReadConfigKey)) {
        if (!requireNativeArgumentCount(args, fn, 3, err)) return true;
        std::string filePath = vietvm::helpers::argToRawString(args[0]);
        std::filesystem::path path;
        if (!vietvm::helpers::nativeUtf8Path(args[0], fn, path, err)) return true;
        filePath = path.u8string();
        std::string key = vietvm::helpers::argToRawString(args[1]);
        std::string fallback = vietvm::helpers::argToRawString(args[2]);
        result = make_string_value(vietvm::helpers::readPropertyByKey(filePath, key, fallback));
        return true;
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnDbConnect)) {
        if (!requireNativeArgumentCount(args, fn, 4, err)) return true;
        return vietvm::helpers::runDbConnect(vietvm::helpers::argToRawString(args[0]),
                    vietvm::helpers::argToRawString(args[1]),
                    vietvm::helpers::argToRawString(args[2]),
                    vietvm::helpers::argToRawString(args[3]),
                            result,
                            err);
    }

    if (vietvm::constants::matchesAnyName(fn, vietvm::constants::kFnDbQuery)) {
        if (!requireNativeArgumentCount(args, fn, 5, err)) return true;
        return vietvm::helpers::runDbQuery(vietvm::helpers::argToRawString(args[0]),
                  vietvm::helpers::argToRawString(args[1]),
                  vietvm::helpers::argToRawString(args[2]),
                  vietvm::helpers::argToRawString(args[3]),
                  vietvm::helpers::argToRawString(args[4]),
                          result,
                          err);
    }

    if (handleNativeHttpClientFunction(fn, args, result, err)) return true;
    if (handleNativeJsonFunction(fn, args, result, err)) return true;
    if (handleNativeLowLevelHttpFunction(fn, args, result, err, outputSink)) return true;

    return false;
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
        throw std::logic_error("không thể reset VM khi lời gọi vẫn đang hoạt động");
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
}

// Tra vị trí nguồn ứng với program counter hiện tại; nếu instruction chưa có
// metadata thì trả frame rỗng để quá trình unwind bỏ qua vị trí không xác định.
vietvm::runtime::RuntimeSourceLocation VM::sourceLocationForPc(
    std::size_t value) const {
    if (value >= bytecodeDebugInfo.size()) return {};
    return bytecodeDebugInfo[value];
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
            initializer.variables = variables;
            initializer.classTable = classTable;
            initializer.setFunctions(hamBytecodeMap, functionTableByNameIndex);
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

// Chạy JIT đã biên dịch tuyến tính; hàm điều phối toàn bộ luồng xử lý của tác vụ, gọi các bước con theo thứ tự và trả mã/kết quả cuối cùng.
bool VM::runJitCompiledLinear() {
    // MVP JIT: compile linear, non-control-flow bytecode into executable lambdas.
    // Unsupported opcodes fall back to the normal interpreter.
    for (const auto &ins : bytecode) {
        switch (ins.op) {
            case OP_BIEN_SO:
            case OP_BIEN_SO_FLOAT:
            case OP_CHUOI:
            case OP_RONG_GIA_TRI:
            case OP_CONG:
            case OP_TRU:
            case OP_NHAN:
            case OP_CHIA:
            case OP_MODULO:
            case OP_Logic_VA:
            case OP_Logic_HOAC:
            case OP_SO_SANH_BANG:
            case OP_KHAC_BANG:
            case OP_LON_HON:
            case OP_NHO_HON:
            case OP_LON_HON_HOAC_BANG:
            case OP_NHO_HON_HOAC_BANG:
            case OP_PHU_DINH:
            case OP_IN:
            case OP_DONG_LENH:
            case OP_MO_NGOAC:
            case OP_DONG_NGOAC:
            case OP_DUNG_CHUONG_TRINH:
                break;
            default:
                return false;
        }
    }

    using JitFn = std::function<void()>;
    std::vector<JitFn> program;
    program.reserve(bytecode.size());

    for (const auto &instr : bytecode) {
        switch (instr.op) {
            case OP_BIEN_SO: {
                int v = instr.operand;
                program.push_back([this, v]() { stack.emplace_back(v); });
                break;
            }
            case OP_BIEN_SO_FLOAT: {
                int idx = instr.operandIndex;
                program.push_back([this, idx]() {
                    if (idx < 0 || idx >= (int)stringPool.size())
                        throw runtime_error_op(vietvm::messages::formatMessage(
                            vietvm::messages::kVmInvalidFloatIndex), OP_BIEN_SO_FLOAT, (int)pc);
                    stack.push_back(make_float_value(std::stod(stringPool[idx])));
                });
                break;
            }
            case OP_CHUOI: {
                int idx = instr.operandIndex;
                program.push_back([this, idx]() {
                    if (idx < 0 || idx >= (int)stringPool.size())
                        throw runtime_error_op(vietvm::messages::formatMessage(
                            vietvm::messages::kVmInvalidStringIndex), OP_CHUOI, (int)pc);
                    stack.push_back(stringPool[idx]);
                });
                break;
            }
            case OP_RONG_GIA_TRI:
                program.push_back([this]() { stack.push_back(make_null_value()); });
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
                program.push_back([this, op]() {
                    if (stack.size() < 2) throw runtime_error_op(vietvm::messages::formatMessage(
                        vietvm::messages::kVmNotEnoughOperands), op, (int)pc);
                    StackValue b = stack.back(); stack.pop_back();
                    StackValue a = stack.back(); stack.pop_back();
                    stack.push_back(evaluateBinaryOperator(op, a, b, static_cast<int>(pc)));
                });
                break;
            }

            case OP_MODULO:
                program.push_back([this]() {
                    if (stack.size() < 2) throw runtime_error_op(vietvm::messages::formatMessage(
                        vietvm::messages::kVmMissingModuloOperands), OP_MODULO, (int)pc);
                    StackValue b = stack.back(); stack.pop_back();
                    StackValue a = stack.back(); stack.pop_back();
                    stack.push_back(evaluateModuloOperator(
                        a, b, OP_MODULO, static_cast<int>(pc)));
                });
                break;

            case OP_PHU_DINH:
                program.push_back([this]() {
                    if (stack.empty()) throw runtime_error_op(vietvm::messages::formatMessage(
                        vietvm::messages::kVmMissingNegationOperand), OP_PHU_DINH, (int)pc);
                    StackValue operand = stack.back(); stack.pop_back();
                    stack.push_back(toBool(operand) ? 0 : 1);
                });
                break;

            case OP_IN:
                program.push_back([this, instr]() { executeOutputOpcode(instr); });
                break;

            case OP_DONG_LENH:
            case OP_MO_NGOAC:
            case OP_DONG_NGOAC:
                program.push_back([]() {});
                break;

            case OP_DUNG_CHUONG_TRINH:
                program.push_back([this]() { pc = bytecode.size(); });
                break;

            default:
                return false;
        }
    }

    pc = 0;
    while (pc < program.size()) {
        program[pc]();
        ++pc;
    }
    return true;
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
                                         const std::unordered_map<int, int> &functionTableByNameIndex) {
    if (hamIdOrName < 0) {
        const int nameIndex = -(hamIdOrName + 1);
        if (nameIndex >= 0 && nameIndex < static_cast<int>(stringPool.size())) {
            return stringPool[static_cast<std::size_t>(nameIndex)];
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

    StackValue nativeResult = make_int_value(0);
    std::string nativeErr;
    if (executeNativeStdlibFunction(hamIdOrName, args, stringPool,
                                    functionTableByNameIndex, hamBytecodeMap,
                                    nativeResult, nativeErr, outputSink)) {
        if (!nativeErr.empty()) {
            const std::string nativeName = runtimeCallTargetName(
                hamIdOrName, stringPool, functionTableByNameIndex);
            vietvm::runtime::RuntimeDiagnosticContext context;
            if (vietvm::constants::matchesAnyName(
                    nativeName, vietvm::constants::kFnToFloat) && !args.empty()) {
                context = vietvm::runtime::runtimeConversionFacts(
                    sv_to_string(args.front()), "số thực", false);
            } else {
                context = vietvm::runtime::runtimeNativeFacts(
                    nativeName, nativeErr, false);
            }
            throw runtime_error_op(nativeErr, op, curPc, std::move(context));
        }
        if (returnMode == CallReturnMode::ConstructorInstance) {
            stack.push_back(make_instance_value(std::move(constructorInstance)));
        } else {
            stack.push_back(nativeResult);
        }
        return;
    }

    const std::size_t nextCallDepth = callDepthFromRoot + 1;
    if (nextCallDepth > maxCallDepth) {
        const std::string targetName = runtimeCallTargetName(
            hamIdOrName, stringPool, functionTableByNameIndex);
        throw vietvm::runtime::RuntimeError(
            vietvm::messages::formatMessage(
                vietvm::messages::kVmCallDepthExceeded,
                {std::to_string(maxCallDepth), targetName}),
            vietvm::runtime::RuntimeErrorKind::CallBoundary,
            vietvm::runtime::runtimeCallDepthFacts(
                targetName, static_cast<int>(nextCallDepth), static_cast<int>(maxCallDepth)));
    }

    auto it = hamBytecodeMap.find(hamIdOrName);
    if (it == hamBytecodeMap.end()) {
        const int nameIndex = (hamIdOrName < 0) ? -(hamIdOrName + 1) : hamIdOrName;
        auto ftIt = functionTableByNameIndex.find(nameIndex);
        if (ftIt != functionTableByNameIndex.end()) {
            it = hamBytecodeMap.find(ftIt->second);
        } else {
            for (const Instruction &hinst : bytecode) {
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
            hamIdOrName, stringPool, functionTableByNameIndex);
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
    frame.args = std::move(args);
    frame.receiver = std::move(receiver);
    frame.methodOwnerClass = std::move(methodOwnerClass);
    frame.localsIndexed = true;
    frame.returnPc = curPc + 1;
    if (closure != nullptr) frame.capturedCells = closure->captures;

    std::vector<vietvm::runtime::RuntimeSourceLocation> calleeDebugInfo;
    const auto debugEntry = functionDebugInfo.find(it->first);
    if (debugEntry != functionDebugInfo.end()) {
        calleeDebugInfo = debugEntry->second;
    }

    // Chuyển interpreter sang callee bằng explicit execution context thay vì
    // gọi `funcVM.run()` lồng nhau. Nhờ đó recursion V++ không làm sâu native
    // C++ call stack và giới hạn `maxCallDepth` luôn là guard đầu tiên.
    ExecutionContext caller;
    caller.bytecode = std::move(bytecode);
    caller.bytecodeDebugInfo = std::move(bytecodeDebugInfo);
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
    bytecode = it->second;
    bytecodeDebugInfo = std::move(calleeDebugInfo);
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

    bytecode = std::move(caller.bytecode);
    bytecodeDebugInfo = std::move(caller.bytecodeDebugInfo);
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

// Xử lý truy cập theo chỉ số cho list/tuple/string; hàm ghi lại chỉ số, kích
// thước và kiểu dữ liệu thực tế để bộ chẩn đoán tự nhận diện nguyên nhân khi lỗi.
void VM::executeIndexOpcode(const Instruction& instr) {
    if (instr.op == OP_DOC_CHI_SO) {
        if (stack.size() < 2) {
            throw runtime_error_op(vietvm::messages::formatMessage(
                vietvm::messages::kVmNotEnoughOperands), instr.op, pc,
                vietvm::runtime::runtimeStackFacts(static_cast<int>(stack.size()), 2));
        }
        StackValue indexValue = stack.back(); stack.pop_back();
        StackValue container = stack.back(); stack.pop_back();
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
            while (pc < bytecode.size()) {
                if (bytecode[pc].op == OP_DONG_KHOI) {
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
    while (scanPc < bytecode.size()) {
        if (bytecode[scanPc].op == OP_CAP_NHAT) {
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
    if (instr.op == OP_JUMP) {
        if (jumpAddress < 0 || jumpAddress >= static_cast<int>(bytecode.size())) {
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
    if (jumpAddress < 0 || jumpAddress >= static_cast<int>(bytecode.size())) {
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

    if (vietvm::helpers::hasEnvVar(vietvm::constants::kEnvVppEnableJit)
     || vietvm::helpers::hasEnvVar(vietvm::constants::kEnvVietvmEnableJit)) {
        if (runJitCompiledLinear()) {
            return;
        }
    }

    if (functionTableByNameIndex.empty()) {
        for (const Instruction &candidate : bytecode) {
            if (candidate.op == OP_HAM && candidate.operand >= 0) {
                functionTableByNameIndex[candidate.operand] = candidate.operandIndex;
            }
        }
    }

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
        if (stopExecutionDepth.has_value() &&
            executionStack.size() <= *stopExecutionDepth) {
            return;
        }
        if (pc >= bytecode.size()) {
            if (completeFunctionCall()) continue;
            collectGarbage();
            return;
        }

        ++executedSinceGc;
        if (executedSinceGc >= gcInterval) {
            collectGarbage();
            executedSinceGc = 0;
        }

        const Instruction &instr = bytecode[pc];
        try {
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
        } catch (const vietvm::runtime::LanguageException &thrown) {
            if (!unwindLanguageException(thrown.value())) throw;
            continue;
        } catch (vietvm::runtime::RuntimeError &error) {
            unwindRuntimeError(error);
            throw;
        }
        ++pc;
    }
}

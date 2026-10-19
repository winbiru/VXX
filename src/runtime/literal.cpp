#include "vpp/runtime/literal.h"
#include "vpp/bytecode/literal_wire.h"

namespace vietvm::runtime {
MapValue decodeMapFromStringPool(const std::string &encoded);
// Giải mã danh sách from chuỗi bể dữ liệu; hàm đọc biểu diễn đã mã hóa, kiểm tra định dạng và dựng lại giá trị runtime tương ứng.
std::vector<StackValue> decodeListFromStringPool(const std::string &encoded);

// Giải mã ánh xạ from chuỗi bể dữ liệu; hàm đọc biểu diễn đã mã hóa, kiểm tra định dạng và dựng lại giá trị runtime tương ứng.
MapValue decodeMapFromStringPool(const std::string &encoded) {
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
std::vector<StackValue> decodeListFromStringPool(const std::string &encoded) {
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
}

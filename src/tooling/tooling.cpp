#include "vpp/tooling/tooling.h"

#include <filesystem>
#include <iomanip>
#include <sstream>

#include "common/utility.h"
#include "common/storeString.h"
#include "compiler/compiler.h"
#include "compiler/compileRegistry.h"
#include "frontend/keywords.h"
#include "frontend/lexer.h"
#include "vpp/bytecode/opcode.h"
#include "vpp/compiler/pipeline.h"

namespace vietvm::tooling {

namespace {

// Chuyển vị trí nguồn thành chuỗi `dòng:cột..dòng:cột` để AST và IR dump hiển thị chính xác nơi phát sinh nút.
std::string formatSpan(const vietvm::frontend::SourceSpan &span) {
    std::ostringstream out;
    out << span.begin.line << ':' << span.begin.column
        << ".."
        << span.end.line << ':' << span.end.column;
    return out.str();
}

// Ghi phần thụt lề theo độ sâu của cây; mỗi cấp thêm hai khoảng trắng để cấu trúc AST/IR dễ đọc.
void writeIndent(std::ostringstream &out, std::size_t depth) {
    for (std::size_t i = 0; i < depth; ++i) out << "  ";
}

// Ghi danh sách lexeme của token dưới dạng chuỗi có dấu nháy, giữ nguyên thứ tự nguồn để phục vụ debug.
void writeTokenLexemes(std::ostringstream &out,
                       const std::vector<vietvm::frontend::Token> &tokens);

// Chuyển giá trị luận lý thành chữ `có` hoặc `không` để output tooling dùng tiếng Việt nhất quán.
const char *coKhong(bool value) noexcept {
    return value ? "có" : "không";
}

// Ánh xạ `AstStatementKind` sang tên câu lệnh tiếng Việt bằng bảng `switch`, dùng khi in cây AST.
const char *tenLoaiCauLenh(vietvm::frontend::AstStatementKind kind) noexcept {
    using vietvm::frontend::AstStatementKind;
    switch (kind) {
        case AstStatementKind::Empty: return "rỗng";
        case AstStatementKind::Block: return "khối";
        case AstStatementKind::Import: return "nhập";
        case AstStatementKind::Function: return "hàm";
        case AstStatementKind::Class: return "lớp";
        case AstStatementKind::Interface: return "giao diện";
        case AstStatementKind::Conditional: return "điều kiện";
        case AstStatementKind::Loop: return "lặp";
        case AstStatementKind::Switch: return "chọn";
        case AstStatementKind::Return: return "trả về";
        case AstStatementKind::Print: return "in";
        case AstStatementKind::Break: return "thoát";
        case AstStatementKind::Continue: return "bỏ qua";
        case AstStatementKind::Throw: return "ném";
        case AstStatementKind::Try: return "thử";
        case AstStatementKind::Expression: return "biểu thức";
        case AstStatementKind::Unknown: return "không xác định";
    }
    return "không xác định";
}

// Ánh xạ `AstExpressionKind` sang tên biểu thức tiếng Việt để phần dump AST không lộ thuật ngữ enum tiếng Anh.
const char *tenLoaiBieuThuc(vietvm::frontend::AstExpressionKind kind) noexcept {
    using vietvm::frontend::AstExpressionKind;
    switch (kind) {
        case AstExpressionKind::Literal: return "giá trị trực tiếp";
        case AstExpressionKind::Name: return "tên";
        case AstExpressionKind::Unary: return "một ngôi";
        case AstExpressionKind::Binary: return "hai ngôi";
        case AstExpressionKind::Assignment: return "gán";
        case AstExpressionKind::CompoundAssignment: return "gán kết hợp";
        case AstExpressionKind::Postfix: return "hậu tố";
        case AstExpressionKind::Call: return "gọi";
        case AstExpressionKind::Lambda: return "hàm vô danh";
        case AstExpressionKind::MapLiteral: return "ánh xạ trực tiếp";
        case AstExpressionKind::ListLiteral: return "danh sách trực tiếp";
        case AstExpressionKind::Index: return "chỉ số";
    }
    return "không xác định";
}

// Ánh xạ loại literal của AST sang tên tiếng Việt tương ứng như số nguyên, chuỗi, luận lý hoặc rỗng.
const char *tenLoaiGiaTriTrucTiep(vietvm::frontend::AstLiteralKind kind) noexcept {
    using vietvm::frontend::AstLiteralKind;
    switch (kind) {
        case AstLiteralKind::None: return "không";
        case AstLiteralKind::Integer: return "số nguyên";
        case AstLiteralKind::Float: return "số thực";
        case AstLiteralKind::String: return "chuỗi";
        case AstLiteralKind::Boolean: return "luận lý";
        case AstLiteralKind::Null: return "rỗng";
    }
    return "không";
}

// Ánh xạ mức truy cập AST sang tên tiếng Việt như công khai, riêng tư và bảo vệ.
const char *tenPhamVi(vietvm::frontend::AstVisibility visibility) noexcept {
    using vietvm::frontend::AstVisibility;
    switch (visibility) {
        case AstVisibility::Unspecified: return "không chỉ định";
        case AstVisibility::Public: return "công khai";
        case AstVisibility::Private: return "riêng tư";
        case AstVisibility::Protected: return "bảo vệ";
    }
    return "không chỉ định";
}

// Chuyển dạng câu lệnh nhập sang mô tả tiếng Việt để tooling phân biệt import có cấu trúc và chưa cấu trúc.
const char *tenDangNhap(vietvm::frontend::AstImportForm form) noexcept {
    using vietvm::frontend::AstImportForm;
    switch (form) {
        case AstImportForm::Unstructured: return "chưa cấu trúc";
        case AstImportForm::LocalSourceFile: return "tệp nguồn cục bộ";
    }
    return "chưa cấu trúc";
}

// Chuyển dạng khai báo lớp sang mô tả tiếng Việt, hiện phân biệt khối phương thức và dạng chưa cấu trúc.
const char *tenDangLop(vietvm::frontend::AstClassForm form) noexcept {
    using vietvm::frontend::AstClassForm;
    switch (form) {
        case AstClassForm::Unstructured: return "chưa cấu trúc";
        case AstClassForm::MethodBlock: return "khối phương thức";
    }
    return "chưa cấu trúc";
}

// Chuyển dạng khai báo giao diện sang mô tả tiếng Việt để AST dump không lộ tên enum/thuật ngữ tiếng Anh.
const char *tenDangGiaoDien(vietvm::frontend::AstInterfaceForm form) noexcept {
    using vietvm::frontend::AstInterfaceForm;
    switch (form) {
        case AstInterfaceForm::Unstructured: return "chưa cấu trúc";
        case AstInterfaceForm::MethodSignatures: return "các chữ ký phương thức";
    }
    return "chưa cấu trúc";
}

// Chuyển dạng điều kiện AST sang mô tả tiếng Việt, phân biệt khối nếu và khối nếu–hoặc.
const char *tenDangDieuKien(vietvm::frontend::AstConditionalForm form) noexcept {
    using vietvm::frontend::AstConditionalForm;
    switch (form) {
        case AstConditionalForm::Unstructured: return "chưa cấu trúc";
        case AstConditionalForm::IfBlock: return "khối nếu";
        case AstConditionalForm::IfElseBlocks: return "khối nếu hoặc";
    }
    return "chưa cấu trúc";
}

// Chuyển dạng vòng lặp AST sang mô tả tiếng Việt để tooling thể hiện cấu trúc vòng lặp đã được parser nhận diện.
const char *tenDangLap(vietvm::frontend::AstLoopForm form) noexcept {
    using vietvm::frontend::AstLoopForm;
    switch (form) {
        case AstLoopForm::Unstructured: return "chưa cấu trúc";
        case AstLoopForm::ForBlock: return "khối lặp";
    }
    return "chưa cấu trúc";
}

// Chuyển dạng `switch/chọn` sang mô tả tiếng Việt, cho biết cấu trúc đã được parser chuẩn hóa hay chưa.
const char *tenDangChon(vietvm::frontend::AstSwitchForm form) noexcept {
    using vietvm::frontend::AstSwitchForm;
    switch (form) {
        case AstSwitchForm::Unstructured: return "chưa cấu trúc";
        case AstSwitchForm::Structured: return "có cấu trúc";
    }
    return "chưa cấu trúc";
}

// Ánh xạ loại nhánh của câu lệnh chọn thành `ca` hoặc `mặc định` khi in AST/IR.
const char *tenNhanhChon(vietvm::frontend::AstSwitchArmKind kind) noexcept {
    using vietvm::frontend::AstSwitchArmKind;
    switch (kind) {
        case AstSwitchArmKind::Case: return "ca";
        case AstSwitchArmKind::Default: return "mặc định";
    }
    return "ca";
}

// Chuyển dạng khối thử/bắt lỗi sang mô tả tiếng Việt dùng trong output tooling.
const char *tenDangThu(vietvm::frontend::AstTryForm form) noexcept {
    using vietvm::frontend::AstTryForm;
    switch (form) {
        case AstTryForm::Unstructured: return "chưa cấu trúc";
        case AstTryForm::TryCatchBlocks: return "khối thử bắt lỗi";
    }
    return "chưa cấu trúc";
}

// Ánh xạ opcode cấp câu lệnh của IR sang tên tiếng Việt mà không thay đổi enum nội bộ của compiler.
const char *tenLenhIr(vietvm::compiler::IrOpcode opcode) noexcept {
    using vietvm::compiler::IrOpcode;
    switch (opcode) {
        case IrOpcode::NoOp: return "không thao tác";
        case IrOpcode::Block: return "khối";
        case IrOpcode::Import: return "nhập";
        case IrOpcode::DefineFunction: return "định nghĩa hàm";
        case IrOpcode::DefineClass: return "định nghĩa lớp";
        case IrOpcode::Conditional: return "điều kiện";
        case IrOpcode::Loop: return "lặp";
        case IrOpcode::Switch: return "chọn";
        case IrOpcode::Return: return "trả về";
        case IrOpcode::Print: return "in";
        case IrOpcode::Break: return "thoát";
        case IrOpcode::Continue: return "bỏ qua";
        case IrOpcode::Throw: return "ném";
        case IrOpcode::Try: return "thử";
        case IrOpcode::Statement: return "câu lệnh";
    }
    return "câu lệnh";
}

// Ánh xạ opcode cấp giá trị của IR sang tên tiếng Việt như đọc tên, ghi thuộc tính, gọi động hoặc hằng số.
const char *tenGiaTriIr(vietvm::compiler::IrValueOpcode opcode) noexcept {
    using vietvm::compiler::IrValueOpcode;
    switch (opcode) {
        case IrValueOpcode::UnsupportedDirectRegion: return "vùng chưa hỗ trợ trực tiếp";
        case IrValueOpcode::ConstInt: return "hằng số nguyên";
        case IrValueOpcode::ConstFloat: return "hằng số thực";
        case IrValueOpcode::ConstString: return "hằng chuỗi";
        case IrValueOpcode::ConstBool: return "hằng luận lý";
        case IrValueOpcode::ConstNull: return "hằng rỗng";
        case IrValueOpcode::MapLiteral: return "ánh xạ trực tiếp";
        case IrValueOpcode::ListLiteral: return "danh sách trực tiếp";
        case IrValueOpcode::Index: return "chỉ số";
        case IrValueOpcode::StoreIndex: return "ghi chỉ số";
        case IrValueOpcode::LoadName: return "đọc tên";
        case IrValueOpcode::LoadProperty: return "đọc thuộc tính";
        case IrValueOpcode::StoreName: return "ghi tên";
        case IrValueOpcode::StoreProperty: return "ghi thuộc tính";
        case IrValueOpcode::Unary: return "một ngôi";
        case IrValueOpcode::Binary: return "hai ngôi";
        case IrValueOpcode::Call: return "gọi";
        case IrValueOpcode::CallDynamic: return "gọi động";
        case IrValueOpcode::Lambda: return "hàm vô danh";
    }
    return "vùng chưa hỗ trợ trực tiếp";
}

// Ánh xạ loại đích gọi semantic sang mô tả tiếng Việt để phân biệt hàm trực tiếp, phương thức đối tượng, native và tên động.
const char *tenDichGoi(vietvm::compiler::CallTargetKind kind) noexcept {
    using vietvm::compiler::CallTargetKind;
    switch (kind) {
        case CallTargetKind::Invalid: return "không hợp lệ";
        case CallTargetKind::DirectFunction: return "hàm trực tiếp";
        case CallTargetKind::ImportedFunction: return "hàm được nhập";
        case CallTargetKind::ClassConstructor: return "hàm tạo lớp";
        case CallTargetKind::InstanceMethod: return "phương thức đối tượng";
        case CallTargetKind::IndirectValue: return "giá trị gián tiếp";
        case CallTargetKind::Native: return "hàm bản địa";
        case CallTargetKind::DynamicName: return "tên động";
    }
    return "không hợp lệ";
}

// In đệ quy một câu lệnh AST cùng metadata, tham số, nhánh chọn và các nút con; độ sâu quyết định thụt lề của cây.
void writeAstStatement(std::ostringstream &out,
                       const vietvm::frontend::AstStatement &statement,
                       std::size_t depth) {
    writeIndent(out, depth);
    out << tenLoaiCauLenh(statement.kind)
        << " vị trí=" << formatSpan(statement.span)
        << " từ tố=[" << statement.tokenBegin << ", " << statement.tokenEnd << ')';
    if (!statement.declarationName.empty()) {
        out << " khai báo=" << std::quoted(statement.declarationName);
    }
    if (statement.visibility != vietvm::frontend::AstVisibility::Unspecified) {
        out << " phạm vi=" << tenPhamVi(statement.visibility);
    }
    if (statement.kind == vietvm::frontend::AstStatementKind::Import) {
        out << " dạng=" << tenDangNhap(statement.importForm);
        if (statement.importForm ==
            vietvm::frontend::AstImportForm::LocalSourceFile) {
            out << " đích=" << std::quoted(statement.importSpec.target)
                << " có dấu nháy=" << coKhong(statement.importSpec.quoted)
                << " dấu chấm phẩy=" << coKhong(statement.importSpec.hasSemicolon);
            if (!statement.importSpec.alias.empty()) {
                out << " bí danh=" << std::quoted(statement.importSpec.alias);
            }
        }
    } else if (statement.kind == vietvm::frontend::AstStatementKind::Class) {
        out << " dạng=" << tenDangLop(statement.classForm);
        if (!statement.superclassName.empty()) {
            out << " lớp cha=" << std::quoted(statement.superclassName);
        }
        for (const auto &implemented : statement.implementedInterfaces) {
            out << " triển khai=" << std::quoted(implemented.name);
        }
    } else if (statement.kind == vietvm::frontend::AstStatementKind::Interface) {
        out << " dạng=" << tenDangGiaoDien(statement.interfaceForm);
        for (const auto &parent : statement.extendedInterfaces) {
            out << " giao diện cha=" << std::quoted(parent.name);
        }
    } else if (statement.kind == vietvm::frontend::AstStatementKind::Conditional) {
        out << " dạng=" << tenDangDieuKien(statement.conditionalForm);
    } else if (statement.kind == vietvm::frontend::AstStatementKind::Loop) {
        out << " dạng=" << tenDangLap(statement.loopForm);
    } else if (statement.kind == vietvm::frontend::AstStatementKind::Switch) {
        out << " dạng=" << tenDangChon(statement.switchForm);
    } else if (statement.kind == vietvm::frontend::AstStatementKind::Try) {
        out << " dạng=" << tenDangThu(statement.tryForm);
        if (!statement.catchVariable.empty()) {
            out << " biến bắt lỗi=" << std::quoted(statement.catchVariable);
        }
    }
    out << '\n';

    for (vietvm::frontend::ExprId root : statement.expressionRoots) {
        writeIndent(out, depth + 1);
        out << "gốc biểu thức #" << root << '\n';
    }

    for (const vietvm::frontend::AstParameter &parameter : statement.parameters) {
        writeIndent(out, depth + 1);
        out << "tham số " << std::quoted(parameter.name);
        if (parameter.hasDefault) out << " mặc định=#" << parameter.defaultValue;
        out << " vị trí=" << formatSpan(parameter.span) << '\n';
    }

    for (const vietvm::frontend::AstSwitchArm &arm : statement.switchArms) {
        writeIndent(out, depth + 1);
        out << "nhánh " << tenNhanhChon(arm.kind)
            << " vị trí=" << formatSpan(arm.span) << " nhãn=";
        if (arm.label == vietvm::frontend::kInvalidExprId) out << "không có";
        else out << '#' << arm.label;
        out << " thân con=" << arm.bodyChildIndex
            << " dấu hai chấm=" << coKhong(arm.hasColon)
            << " tiền tố ca=" << coKhong(arm.prefixedByCase)
            << '\n';
    }

    for (const vietvm::frontend::AstStatement &child : statement.children) {
        writeAstStatement(out, child, depth + 1);
    }
}

// In đệ quy một lệnh IR cùng symbol, tham số, expression root và token nguồn; đồng thời đánh số lệnh theo thứ tự duyệt.
void writeIrInstruction(std::ostringstream &out,
                        const vietvm::compiler::IrInstruction &instruction,
                        std::size_t depth,
                        std::size_t &index) {
    writeIndent(out, depth);
    out << index++ << "  " << tenLenhIr(instruction.opcode)
        << " vị trí=" << formatSpan(instruction.span)
        << " ký hiệu=" << instruction.symbolId
        << " chưa hỗ trợ trực tiếp=" << coKhong(instruction.unsupportedDirectRegion);
    if (!instruction.declarationName.empty()) {
        out << " khai báo=" << std::quoted(instruction.declarationName);
    }
    if (instruction.visibility != vietvm::frontend::AstVisibility::Unspecified) {
        out << " phạm vi=" << tenPhamVi(instruction.visibility);
    }
    if (instruction.opcode == vietvm::compiler::IrOpcode::DefineClass) {
        out << " dạng=" << tenDangLop(instruction.classForm);
        if (!instruction.superclassName.empty()) {
            out << " lớp cha=" << std::quoted(instruction.superclassName)
                << ":ký hiệu=" << instruction.superclassSymbolId;
        }
    } else if (instruction.opcode == vietvm::compiler::IrOpcode::Conditional) {
        out << " dạng=" << tenDangDieuKien(instruction.conditionalForm);
    } else if (instruction.opcode == vietvm::compiler::IrOpcode::Loop) {
        out << " dạng=" << tenDangLap(instruction.loopForm);
    } else if (instruction.opcode == vietvm::compiler::IrOpcode::Switch) {
        out << " dạng=" << tenDangChon(instruction.switchForm);
    } else if (instruction.opcode == vietvm::compiler::IrOpcode::Try) {
        out << " dạng=" << tenDangThu(instruction.tryForm);
        if (!instruction.catchVariable.empty()) {
            out << " biến bắt lỗi=" << std::quoted(instruction.catchVariable)
                << ":ký hiệu=" << instruction.catchSymbolId;
        }
    }
    out << " tham số=[";
    for (std::size_t parameterIndex = 0;
         parameterIndex < instruction.parameters.size(); ++parameterIndex) {
        if (parameterIndex != 0) out << ", ";
        const vietvm::compiler::IrParameter &parameter =
            instruction.parameters[parameterIndex];
        out << std::quoted(parameter.name) << ":ký hiệu=" << parameter.symbolId;
        if (parameter.hasDefault) out << ":mặc định=#" << parameter.defaultValue;
    }
    out << "] gốc=[";
    for (std::size_t rootIndex = 0; rootIndex < instruction.expressionRoots.size(); ++rootIndex) {
        if (rootIndex != 0) out << ", ";
        out << instruction.expressionRoots[rootIndex];
    }
    out << "] từ tố=";
    writeTokenLexemes(out, instruction.tokens);
    out << '\n';

    for (const vietvm::compiler::IrSwitchArm &arm : instruction.switchArms) {
        writeIndent(out, depth + 1);
        out << "nhánh " << tenNhanhChon(arm.kind)
            << " vị trí=" << formatSpan(arm.span) << " nhãn=";
        if (arm.label == vietvm::compiler::kInvalidIrValueId) out << "không có";
        else out << '#' << arm.label;
        out << " thân con=" << arm.bodyChildIndex
            << " dấu hai chấm=" << coKhong(arm.hasColon)
            << " tiền tố ca=" << coKhong(arm.prefixedByCase)
            << '\n';
    }

    for (const vietvm::compiler::IrInstruction &child : instruction.children) {
        writeIrInstruction(out, child, depth + 1, index);
    }
}

// Ghi danh sách lexeme của token dưới dạng chuỗi có dấu nháy, giữ nguyên thứ tự nguồn để phục vụ debug.
void writeTokenLexemes(std::ostringstream &out,
                       const std::vector<vietvm::frontend::Token> &tokens) {
    out << '[';
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (i != 0) out << ", ";
        out << std::quoted(tokens[i].lexeme);
    }
    out << ']';
}

} // namespace

// Xác định operand nào thật sự tham chiếu `StringPool`; nếu hợp lệ thì trả thêm nhãn chuỗi để disassembler dễ đọc.
static std::string operandLabel(const Instruction &instr,
                                const std::vector<std::string> &stringPool) {
    int poolIndex = -1;
    if (instr.op == OP_HAM) {
        poolIndex = instr.operand;
    } else if (instr.op == OP_CHUOI || instr.op == OP_BIEN_SO_FLOAT ||
               instr.op == OP_MAP_LITERAL) {
        poolIndex = instr.operandIndex;
    } else if (instr.op == OP_PARAM_MAC_DINH) {
        poolIndex = instr.operand;
    } else if (instr.op == OP_GOI && instr.operandIndex < 0) {
        poolIndex = -(instr.operandIndex + 1);
    }
    if (poolIndex >= 0 && poolIndex < static_cast<int>(stringPool.size())) {
        return " bể chuỗi=\"" + stringPool[poolIndex] + "\"";
    }
    return "";
}

// Chuyển dãy bytecode thành từng dòng opcode và operand; chỉ gắn nội dung `StringPool` cho các opcode có operand chuỗi thực sự.
std::string disassembleBytecode(const std::vector<Instruction> &bytecode,
                                const std::vector<std::string> &stringPool) {
    std::ostringstream out;
    for (size_t i = 0; i < bytecode.size(); ++i) {
        const Instruction &instr = bytecode[i];
        out << std::setw(4) << i << "  " << vietvm::bytecode::opcodeName(instr.op)
            << " toán hạng=" << instr.operand
            << " chỉ số=" << instr.operandIndex
            << " giá trị=" << instr.operandValue
            << operandLabel(instr, stringPool)
            << '\n';
    }
    return out.str();
}

// Tạo bản dump toàn bộ AST gồm thống kê chương trình, cây câu lệnh, biểu thức và lambda để kiểm tra kết quả parser.
std::string dumpAst(const vietvm::frontend::AstProgram &program) {
    std::ostringstream out;
    out << "AST từ tố=" << program.tokens.size()
        << " biểu thức=" << program.expressions.size()
        << " hàm vô danh=" << program.lambdas.size()
        << " vị trí=" << formatSpan(program.span) << '\n';
    for (const vietvm::frontend::AstStatement &statement : program.statements) {
        writeAstStatement(out, statement, 0);
    }
    out << "biểu thức:\n";
    for (const vietvm::frontend::AstExpression &expression : program.expressions) {
        out << "  #" << expression.id << ' '
            << tenLoaiBieuThuc(expression.kind)
            << " kiểu trực tiếp=" << tenLoaiGiaTriTrucTiep(expression.literalKind)
            << " vị trí=" << formatSpan(expression.span)
            << " từ tố=[" << expression.tokenBegin << ", " << expression.tokenEnd << ')'
            << " văn bản=" << std::quoted(expression.text);
        if (expression.kind == vietvm::frontend::AstExpressionKind::Lambda) {
            out << " hàm vô danh=#" << expression.lambdaId;
        }
        out << '\n';
    }
    out << "hàm vô danh:\n";
    for (const vietvm::frontend::AstLambda &lambda : program.lambdas) {
        out << "  #" << lambda.id << " biểu thức=#" << lambda.expression
            << " vị trí=" << formatSpan(lambda.span) << " tham số=[";
        for (std::size_t index = 0; index < lambda.parameters.size(); ++index) {
            if (index != 0) out << ", ";
            const vietvm::frontend::AstParameter &parameter = lambda.parameters[index];
            out << std::quoted(parameter.name);
            if (parameter.hasDefault) out << ":mặc định=#" << parameter.defaultValue;
        }
        out << "]\n";
        writeAstStatement(out, lambda.body, 2);
    }
    return out.str();
}

// Tạo bản dump IR sau lowering/optimization, gồm giá trị, lambda và cây lệnh để kiểm tra semantic binding và direct IR.
std::string dumpIr(const vietvm::compiler::IrProgram &program) {
    std::ostringstream out;
    out << "IR câu lệnh=" << program.instructions.size()
        << " giá trị=" << program.values.size()
        << " hàm vô danh=" << program.lambdas.size()
        << " vùng chưa hỗ trợ trực tiếp=" << program.unsupportedDirectRegionCount << '\n';

    out << "giá trị:\n";
    for (const vietvm::compiler::IrValue &value : program.values) {
        out << "  #" << value.id << ' '
            << tenGiaTriIr(value.opcode)
            << " biểu thức=#" << value.sourceExprId
            << " ký hiệu=" << value.symbolId
            << " vị trí=" << formatSpan(value.span)
            << " văn bản=" << std::quoted(value.text)
            << " gọi tường minh=" << coKhong(value.explicitCall);
        if (value.opcode == vietvm::compiler::IrValueOpcode::Call ||
            value.opcode == vietvm::compiler::IrValueOpcode::CallDynamic) {
            out << " đích gọi=" << tenDichGoi(value.callTarget);
        }
        if (value.opcode == vietvm::compiler::IrValueOpcode::Lambda) {
            out << " hàm vô danh=#" << value.lambdaId;
        }
        out << " toán hạng=[";
        for (std::size_t operandIndex = 0; operandIndex < value.operands.size(); ++operandIndex) {
            if (operandIndex != 0) out << ", ";
            out << value.operands[operandIndex];
        }
        out << "]\n";
    }

    out << "hàm vô danh:\n";
    for (const vietvm::compiler::IrLambda &lambda : program.lambdas) {
        out << "  #" << lambda.id << " chủ sở hữu=#" << lambda.ownerValue
            << " biểu thức=#" << lambda.sourceExprId
            << " vị trí=" << formatSpan(lambda.span) << " tham số=[";
        for (std::size_t index = 0; index < lambda.parameters.size(); ++index) {
            if (index != 0) out << ", ";
            const vietvm::compiler::IrParameter &parameter = lambda.parameters[index];
            out << std::quoted(parameter.name) << ":ký hiệu=" << parameter.symbolId;
            if (parameter.hasDefault) out << ":mặc định=#" << parameter.defaultValue;
        }
        out << "] biến bắt giữ=[";
        for (std::size_t index = 0; index < lambda.captures.size(); ++index) {
            if (index != 0) out << ", ";
            out << lambda.captures[index];
        }
        out << "]\n";
        std::size_t lambdaInstructionIndex = 0;
        writeIrInstruction(out, lambda.body, 2, lambdaInstructionIndex);
    }

    out << "câu lệnh:\n";
    std::size_t index = 0;
    for (const vietvm::compiler::IrInstruction &instruction : program.instructions) {
        writeIrInstruction(out, instruction, 1, index);
    }
    return out.str();
}

// Định dạng lại mã nguồn từ chuỗi token; hàm quản lý thụt lề theo dấu ngoặc khối và xuống dòng tại dấu chấm phẩy.
std::string formatSource(const std::string &source) {
    auto tokens = vietvm::compiler::tokenize(source);
    tokens = vietvm::compiler::postProcessTokens(tokens);

    std::ostringstream out;
    int indent = 0;
    bool atLineStart = true;

    auto writeIndent = [&]() {
        for (int i = 0; i < indent; ++i) out << "    ";
    };

    auto newline = [&]() {
        out << '\n';
        atLineStart = true;
    };

    for (size_t i = 0; i < tokens.size(); ++i) {
        const std::string &tk = tokens[i];
        if (tk.empty()) continue;

        if (tk == "}") {
            if (!atLineStart) newline();
            if (indent > 0) --indent;
            writeIndent();
            out << tk;
            atLineStart = false;
            continue;
        }

        if (atLineStart) {
            writeIndent();
            atLineStart = false;
        } else if (tk != ";" && tk != "," && tk != ")" && tk != "]" && tk != "(" && tk != "[") {
            out << ' ';
        }

        out << tk;

        if (tk == "{") {
            ++indent;
            newline();
        } else if (tk == ";") {
            newline();
        } else if (tk == ",") {
            out << ' ';
        }
    }

    std::string formatted = out.str();
    if (!formatted.empty() && formatted.back() != '\n') formatted.push_back('\n');
    return formatted;
}

// Phân tích nguồn để thu thập lỗi lexer/parser/semantic và trả về danh sách chẩn đoán thay vì trực tiếp thực thi chương trình.
bool lintSource(const std::string &source, std::string &errorMessage) {
    try {
        vietvm::compiler::CompilationContext context;
        (void)vietvm::compiler::compilePipeline(context, source, keywordMap, false);
        return true;
    } catch (const std::exception &ex) {
        errorMessage = ex.what();
        return false;
    }
}

} // namespace vietvm::tooling

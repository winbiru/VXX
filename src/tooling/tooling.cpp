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

namespace vietvm::tooling {

namespace {

std::string formatSpan(const vietvm::frontend::SourceSpan &span) {
    std::ostringstream out;
    out << span.begin.line << ':' << span.begin.column
        << ".."
        << span.end.line << ':' << span.end.column;
    return out.str();
}

void writeIndent(std::ostringstream &out, std::size_t depth) {
    for (std::size_t i = 0; i < depth; ++i) out << "  ";
}

void writeAstStatement(std::ostringstream &out,
                       const vietvm::frontend::AstStatement &statement,
                       std::size_t depth) {
    writeIndent(out, depth);
    out << vietvm::frontend::astStatementKindName(statement.kind)
        << " span=" << formatSpan(statement.span)
        << " tokens=[" << statement.tokenBegin << ", " << statement.tokenEnd << ')';
    if (!statement.declarationName.empty()) {
        out << " declaration=" << std::quoted(statement.declarationName);
    }
    out << '\n';

    for (const vietvm::frontend::AstStatement &child : statement.children) {
        writeAstStatement(out, child, depth + 1);
    }
}

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

static std::string operandLabel(const Instruction &instr,
                                const std::vector<std::string> &stringPool) {
    if (instr.op == OP_GOI || instr.op == OP_BIEN_SO || instr.op == OP_BIEN_SO_FLOAT ||
        instr.op == OP_MAP_LITERAL || instr.op == OP_PARAM || instr.op == OP_PARAM_MAC_DINH) {
        if (instr.operandIndex >= 0 && instr.operandIndex < static_cast<int>(stringPool.size())) {
            return " pool=\"" + stringPool[instr.operandIndex] + "\"";
        }
    }
    return "";
}

std::string disassembleBytecode(const std::vector<Instruction> &bytecode,
                                const std::vector<std::string> &stringPool) {
    std::ostringstream out;
    for (size_t i = 0; i < bytecode.size(); ++i) {
        const Instruction &instr = bytecode[i];
        out << std::setw(4) << i << "  " << vietvm::bytecode::opcodeName(instr.op)
            << " op=" << instr.operand
            << " idx=" << instr.operandIndex
            << " val=" << instr.operandValue
            << operandLabel(instr, stringPool)
            << '\n';
    }
    return out.str();
}

std::string dumpAst(const vietvm::frontend::AstProgram &program) {
    std::ostringstream out;
    out << "AST tokens=" << program.tokens.size()
        << " span=" << formatSpan(program.span) << '\n';
    for (const vietvm::frontend::AstStatement &statement : program.statements) {
        writeAstStatement(out, statement, 0);
    }
    return out.str();
}

std::string dumpIr(const vietvm::compiler::IrProgram &program) {
    std::ostringstream out;
    out << "IR instructions=" << program.instructions.size() << '\n';
    for (std::size_t i = 0; i < program.instructions.size(); ++i) {
        const vietvm::compiler::IrInstruction &instruction = program.instructions[i];
        out << i << "  " << vietvm::compiler::irOpcodeName(instruction.opcode)
            << " span=" << formatSpan(instruction.span)
            << " symbol=" << instruction.symbolId
            << " tokens=";
        writeTokenLexemes(out, instruction.tokens);
        out << '\n';
    }
    return out.str();
}

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

bool lintSource(const std::string &source, std::string &errorMessage) {
    try {
        vietvm::compiler::resetCompilationState();
        (void)compileSource(source, keywordMap, false);
        return true;
    } catch (const std::exception &ex) {
        errorMessage = ex.what();
        return false;
    }
}

} // namespace vietvm::tooling

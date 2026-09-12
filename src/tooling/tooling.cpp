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

void writeTokenLexemes(std::ostringstream &out,
                       const std::vector<vietvm::frontend::Token> &tokens);

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
    if (statement.visibility != vietvm::frontend::AstVisibility::Unspecified) {
        out << " visibility="
            << vietvm::frontend::astVisibilityName(statement.visibility);
    }
    if (statement.kind == vietvm::frontend::AstStatementKind::Import) {
        out << " form=" << vietvm::frontend::astImportFormName(statement.importForm);
        if (statement.importForm ==
            vietvm::frontend::AstImportForm::LocalSourceFile) {
            out << " target=" << std::quoted(statement.importSpec.target)
                << " quoted=" << (statement.importSpec.quoted ? "yes" : "no")
                << " semicolon="
                << (statement.importSpec.hasSemicolon ? "yes" : "no");
            if (!statement.importSpec.alias.empty()) {
                out << " alias=" << std::quoted(statement.importSpec.alias);
            }
        }
    } else if (statement.kind == vietvm::frontend::AstStatementKind::Class) {
        out << " form=" << vietvm::frontend::astClassFormName(statement.classForm);
    } else if (statement.kind == vietvm::frontend::AstStatementKind::Conditional) {
        out << " form="
            << vietvm::frontend::astConditionalFormName(statement.conditionalForm);
    } else if (statement.kind == vietvm::frontend::AstStatementKind::Loop) {
        out << " form=" << vietvm::frontend::astLoopFormName(statement.loopForm);
    } else if (statement.kind == vietvm::frontend::AstStatementKind::Switch) {
        out << " form=" << vietvm::frontend::astSwitchFormName(statement.switchForm);
    } else if (statement.kind == vietvm::frontend::AstStatementKind::Try) {
        out << " form=" << vietvm::frontend::astTryFormName(statement.tryForm);
        if (!statement.catchVariable.empty()) {
            out << " catch=" << std::quoted(statement.catchVariable);
        }
    }
    out << '\n';

    for (vietvm::frontend::ExprId root : statement.expressionRoots) {
        writeIndent(out, depth + 1);
        out << "expr-root #" << root << '\n';
    }

    for (const vietvm::frontend::AstParameter &parameter : statement.parameters) {
        writeIndent(out, depth + 1);
        out << "parameter " << std::quoted(parameter.name);
        if (parameter.hasDefault) out << " default=#" << parameter.defaultValue;
        out << " span=" << formatSpan(parameter.span) << '\n';
    }

    for (const vietvm::frontend::AstSwitchArm &arm : statement.switchArms) {
        writeIndent(out, depth + 1);
        out << "arm " << vietvm::frontend::astSwitchArmKindName(arm.kind)
            << " span=" << formatSpan(arm.span) << " label=";
        if (arm.label == vietvm::frontend::kInvalidExprId) out << "none";
        else out << '#' << arm.label;
        out << " body-child=" << arm.bodyChildIndex
            << " colon=" << (arm.hasColon ? "yes" : "no")
            << " case-prefix=" << (arm.prefixedByCase ? "yes" : "no")
            << '\n';
    }

    for (const vietvm::frontend::AstStatement &child : statement.children) {
        writeAstStatement(out, child, depth + 1);
    }
}

void writeIrInstruction(std::ostringstream &out,
                        const vietvm::compiler::IrInstruction &instruction,
                        std::size_t depth,
                        std::size_t &index) {
    writeIndent(out, depth);
    out << index++ << "  " << vietvm::compiler::irOpcodeName(instruction.opcode)
        << " span=" << formatSpan(instruction.span)
        << " symbol=" << instruction.symbolId
        << " unsupported-direct=" << (instruction.unsupportedDirectRegion ? "yes" : "no");
    if (!instruction.declarationName.empty()) {
        out << " declaration=" << std::quoted(instruction.declarationName);
    }
    if (instruction.visibility != vietvm::frontend::AstVisibility::Unspecified) {
        out << " visibility="
            << vietvm::frontend::astVisibilityName(instruction.visibility);
    }
    if (instruction.opcode == vietvm::compiler::IrOpcode::DefineClass) {
        out << " form="
            << vietvm::frontend::astClassFormName(instruction.classForm);
    } else if (instruction.opcode == vietvm::compiler::IrOpcode::Conditional) {
        out << " form="
            << vietvm::frontend::astConditionalFormName(
                   instruction.conditionalForm);
    } else if (instruction.opcode == vietvm::compiler::IrOpcode::Loop) {
        out << " form="
            << vietvm::frontend::astLoopFormName(instruction.loopForm);
    } else if (instruction.opcode == vietvm::compiler::IrOpcode::Switch) {
        out << " form="
            << vietvm::frontend::astSwitchFormName(instruction.switchForm);
    } else if (instruction.opcode == vietvm::compiler::IrOpcode::Try) {
        out << " form=" << vietvm::frontend::astTryFormName(instruction.tryForm);
        if (!instruction.catchVariable.empty()) {
            out << " catch=" << std::quoted(instruction.catchVariable)
                << ":symbol=" << instruction.catchSymbolId;
        }
    }
    out << " params=[";
    for (std::size_t parameterIndex = 0;
         parameterIndex < instruction.parameters.size(); ++parameterIndex) {
        if (parameterIndex != 0) out << ", ";
        const vietvm::compiler::IrParameter &parameter =
            instruction.parameters[parameterIndex];
        out << std::quoted(parameter.name) << ":symbol=" << parameter.symbolId;
        if (parameter.hasDefault) out << ":default=#" << parameter.defaultValue;
    }
    out << "] roots=[";
    for (std::size_t rootIndex = 0; rootIndex < instruction.expressionRoots.size(); ++rootIndex) {
        if (rootIndex != 0) out << ", ";
        out << instruction.expressionRoots[rootIndex];
    }
    out << "] tokens=";
    writeTokenLexemes(out, instruction.tokens);
    out << '\n';

    for (const vietvm::compiler::IrSwitchArm &arm : instruction.switchArms) {
        writeIndent(out, depth + 1);
        out << "arm " << vietvm::frontend::astSwitchArmKindName(arm.kind)
            << " span=" << formatSpan(arm.span) << " label=";
        if (arm.label == vietvm::compiler::kInvalidIrValueId) out << "none";
        else out << '#' << arm.label;
        out << " body-child=" << arm.bodyChildIndex
            << " colon=" << (arm.hasColon ? "yes" : "no")
            << " case-prefix=" << (arm.prefixedByCase ? "yes" : "no")
            << '\n';
    }

    for (const vietvm::compiler::IrInstruction &child : instruction.children) {
        writeIrInstruction(out, child, depth + 1, index);
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
        return " pool=\"" + stringPool[poolIndex] + "\"";
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
        << " expressions=" << program.expressions.size()
        << " lambdas=" << program.lambdas.size()
        << " span=" << formatSpan(program.span) << '\n';
    for (const vietvm::frontend::AstStatement &statement : program.statements) {
        writeAstStatement(out, statement, 0);
    }
    out << "expressions:\n";
    for (const vietvm::frontend::AstExpression &expression : program.expressions) {
        out << "  #" << expression.id << ' '
            << vietvm::frontend::astExpressionKindName(expression.kind)
            << " literal=" << vietvm::frontend::astLiteralKindName(expression.literalKind)
            << " span=" << formatSpan(expression.span)
            << " tokens=[" << expression.tokenBegin << ", " << expression.tokenEnd << ')'
            << " text=" << std::quoted(expression.text);
        if (expression.kind == vietvm::frontend::AstExpressionKind::Lambda) {
            out << " lambda=#" << expression.lambdaId;
        }
        out << '\n';
    }
    out << "lambdas:\n";
    for (const vietvm::frontend::AstLambda &lambda : program.lambdas) {
        out << "  #" << lambda.id << " expr=#" << lambda.expression
            << " span=" << formatSpan(lambda.span) << " params=[";
        for (std::size_t index = 0; index < lambda.parameters.size(); ++index) {
            if (index != 0) out << ", ";
            const vietvm::frontend::AstParameter &parameter = lambda.parameters[index];
            out << std::quoted(parameter.name);
            if (parameter.hasDefault) out << ":default=#" << parameter.defaultValue;
        }
        out << "]\n";
        writeAstStatement(out, lambda.body, 2);
    }
    return out.str();
}

std::string dumpIr(const vietvm::compiler::IrProgram &program) {
    std::ostringstream out;
    out << "IR instructions=" << program.instructions.size()
        << " values=" << program.values.size()
        << " lambdas=" << program.lambdas.size()
        << " unsupported-direct-regions=" << program.unsupportedDirectRegionCount << '\n';

    out << "values:\n";
    for (const vietvm::compiler::IrValue &value : program.values) {
        out << "  #" << value.id << ' '
            << vietvm::compiler::irValueOpcodeName(value.opcode)
            << " expr=#" << value.sourceExprId
            << " symbol=" << value.symbolId
            << " span=" << formatSpan(value.span)
            << " text=" << std::quoted(value.text)
            << " explicit-call=" << (value.explicitCall ? "yes" : "no");
        if (value.opcode == vietvm::compiler::IrValueOpcode::Call ||
            value.opcode == vietvm::compiler::IrValueOpcode::CallDynamic) {
            out << " call-target="
                << vietvm::compiler::callTargetKindName(value.callTarget);
        }
        if (value.opcode == vietvm::compiler::IrValueOpcode::Lambda) {
            out << " lambda=#" << value.lambdaId;
        }
        out << " operands=[";
        for (std::size_t operandIndex = 0; operandIndex < value.operands.size(); ++operandIndex) {
            if (operandIndex != 0) out << ", ";
            out << value.operands[operandIndex];
        }
        out << "]\n";
    }

    out << "lambdas:\n";
    for (const vietvm::compiler::IrLambda &lambda : program.lambdas) {
        out << "  #" << lambda.id << " owner=#" << lambda.ownerValue
            << " expr=#" << lambda.sourceExprId
            << " span=" << formatSpan(lambda.span) << " params=[";
        for (std::size_t index = 0; index < lambda.parameters.size(); ++index) {
            if (index != 0) out << ", ";
            const vietvm::compiler::IrParameter &parameter = lambda.parameters[index];
            out << std::quoted(parameter.name) << ":symbol=" << parameter.symbolId;
            if (parameter.hasDefault) out << ":default=#" << parameter.defaultValue;
        }
        out << "] captures=[";
        for (std::size_t index = 0; index < lambda.captures.size(); ++index) {
            if (index != 0) out << ", ";
            out << lambda.captures[index];
        }
        out << "]\n";
        std::size_t lambdaInstructionIndex = 0;
        writeIrInstruction(out, lambda.body, 2, lambdaInstructionIndex);
    }

    out << "instructions:\n";
    std::size_t index = 0;
    for (const vietvm::compiler::IrInstruction &instruction : program.instructions) {
        writeIrInstruction(out, instruction, 1, index);
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

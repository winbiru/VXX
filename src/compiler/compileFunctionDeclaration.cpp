#include "compiler/compileFunctionDeclaration.h"

#include <stdexcept>
#include <utility>

#include "common/storeString.h"
#include "common/utility.h"
#include "compiler/compileBlock.h"
#include "compiler/compileRegistry.h"
#include "frontend/lexer.h"
#include "vpp/core/message_constants.h"

namespace vietvm::compiler {

void compileFunctionDeclaration(
    const std::vector<std::string> &tokens,
    std::size_t &pos,
    std::vector<Instruction> &bytecode,
    std::unordered_map<std::string, int> &symTab,
    int &nextId,
    const std::unordered_map<std::string, Opcode> &keywordMap,
    const std::string &namePrefix,
    const std::string &classOwner,
    const std::string &visibility) {
    ++pos; // skip 'hàm'
    std::string effectiveVisibility = visibility;
    if (pos < tokens.size() && isVisibilityToken(tokens[pos])) {
        effectiveVisibility = tokens[pos++];
    }

    if (pos >= tokens.size()) {
        throw std::runtime_error(messages::formatMessage(
            messages::kSyntaxMissingFunctionName));
    }
    const std::size_t nameBegin = pos;
    while (pos < tokens.size() && tokens[pos] != "(" &&
           tokens[pos] != "{" && tokens[pos] != ";") {
        if (!isCallableNamePiece(tokens[pos])) break;
        ++pos;
    }
    if (nameBegin == pos) {
        throw std::runtime_error(messages::formatMessage(
            messages::kSyntaxInvalidFunctionName));
    }

    const std::string rawName = joinNameTokens(tokens, nameBegin, pos);
    const std::string fullName =
        namePrefix.empty() ? rawName : (namePrefix + rawName);
    const int nameIndex = StringPool::storeString(fullName);

    int hamId = -1;
    const auto existingFunction = symTab.find(fullName);
    if (existingFunction != symTab.end()) {
        hamId = existingFunction->second;
    } else {
        hamId = hamMap::allocHamId();
        symTab[fullName] = hamId;
    }

    // Keep variable ids in a separate range from function ids.
    if (hamId >= nextId) nextId = hamId + 1;

    if (!classOwner.empty()) {
        registerClassMethodVisibility(
            fullName, classOwner, effectiveVisibility);
    }

    // Register before compiling the body so recursive/self calls resolve.
    hamMap::hamBytecodeMap[hamId] = {};
    hamMap::setHamNameIndex(hamId, nameIndex);

    struct ParamSpec {
        std::string name;
        bool hasDefault = false;
        std::string defaultEncoded;
    };
    std::vector<ParamSpec> params;

    if (pos < tokens.size() && tokens[pos] == "(") {
        const auto parsed = extractParens(tokens, pos);
        pos = parsed.second;

        for (const std::string &item : splitTopLevelArguments(parsed.first)) {
            const std::string token = trim(item);
            if (token.empty()) continue;

            ParamSpec parameter;
            const std::size_t equals = token.find('=');
            if (equals == std::string::npos) {
                parameter.name = trim(token);
            } else {
                parameter.name = trim(token.substr(0, equals));
                const std::string defaultValue = trim(token.substr(equals + 1));
                parameter.hasDefault = true;
                if (isNumber(defaultValue)) {
                    parameter.defaultEncoded = "i:" + defaultValue;
                } else if (isFloat(defaultValue)) {
                    parameter.defaultEncoded = "d:" + defaultValue;
                } else if (isStringLiteral(defaultValue)) {
                    parameter.defaultEncoded = "s:" + stripQuotes(defaultValue);
                } else if (defaultValue == "đúng") {
                    parameter.defaultEncoded = "i:1";
                } else if (defaultValue == "sai") {
                    parameter.defaultEncoded = "i:0";
                } else if (defaultValue == "rỗng") {
                    parameter.defaultEncoded = "n:";
                } else {
                    throw std::runtime_error(messages::formatMessage(
                        messages::kSemanticUnsupportedDefaultParameter,
                        {"hàm"}));
                }
            }
            if (!parameter.name.empty()) params.push_back(std::move(parameter));
        }
    }

    if (pos >= tokens.size() || tokens[pos] != "{") {
        throw std::runtime_error(messages::formatMessage(
            messages::kSyntaxExpectedBlockAtPosition,
            {std::to_string(pos), pos < tokens.size() ? tokens[pos] : "EOF"}));
    }

    std::vector<Instruction> funcCode;
    funcCode.push_back({OP_MO_KHOI, 0, 0, 0});

    for (std::size_t index = 0; index < params.size(); ++index) {
        const ParamSpec &parameter = params[index];
        if (symTab.find(parameter.name) == symTab.end()) {
            symTab[parameter.name] = nextId++;
        }
        const int varId = symTab[parameter.name];
        funcCode.push_back({OP_KHOI_TAO, 0, varId, 0});
        if (parameter.hasDefault) {
            const int defaultIndex = StringPool::storeString(parameter.defaultEncoded);
            funcCode.push_back(
                {OP_PARAM_MAC_DINH, defaultIndex, varId, static_cast<int>(index)});
        } else {
            funcCode.push_back(
                {OP_PARAM, 0, varId, static_cast<int>(index)});
        }
    }

    compileBlock(tokens, pos, funcCode, symTab, nextId, keywordMap);

    funcCode.push_back({OP_DONG_KHOI, 0, 0, 0});
    funcCode.push_back({OP_DONG_LENH, 0, 0, 0});

    hamMap::hamBytecodeMap[hamId] = std::move(funcCode);
    bytecode.push_back({OP_HAM, nameIndex, hamId, 0});
}

} // namespace vietvm::compiler

//
// Created by nx_thang on 10/21/2025.
//

// CompileRegistry.h
#pragma once
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <functional>
#include <vector>
#include "../vm/instruction.h"
#include "vpp/frontend/ast.h"

using CompileFunc = std::function<void(
    const std::vector<std::string>& tokens,
    size_t &pos,
    std::vector<Instruction>& bytecode,
    std::unordered_map<std::string,int>& symTab,
    int& nextId,
    const std::unordered_map<std::string,Opcode>& keywordMap)>;

extern std::unordered_map<std::string, CompileFunc> compileMap;
void initCompileMap();
// Imported files tracking (shared for a single compilation session)
namespace vietvm { namespace compiler {
    extern std::unordered_set<std::string> importedFiles;
    void clearImportedFiles();

    // Class/access-control compile state
    void clearClassAccessState();
    bool isVisibilityToken(const std::string &token);
    void pushClassContext(const std::string &className);
    void popClassContext();
    std::string currentClassContext();
    void registerClassMethodVisibility(const std::string &fullMethodName,
                                       const std::string &ownerClass,
                                       const std::string &visibility);
    std::string resolveCallableNameInContext(const std::string &name,
                                             const std::unordered_map<std::string,int> &symTab);
    void validateCallableAccess(const std::string &resolvedName);
    // Resolve a callable through the local symbol table.  Imported/global
    // fallback is enabled for expression/statement calls and can be disabled
    // for `gọi`, which intentionally emits a name-based VM fallback instead.
    int resolveFunctionIdByName(const std::string &name,
                                const std::unordered_map<std::string,int> &symTab,
                                bool includeGlobalFallback = true);

    void compileImportSpec(
        const vietvm::frontend::AstImportSpec &spec,
        int &nextId,
        const std::unordered_map<std::string,Opcode> &keywordMap);
} }

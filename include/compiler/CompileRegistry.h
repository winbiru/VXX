//
// Created by nx_thang on 10/21/2025.
//

// CompileRegistry.h
#pragma once
#include <unordered_map>
#include <string>
#include <functional>
#include "../instruction.h"

using CompileFunc = std::function<void(
    const std::vector<std::string>& tokens,
    size_t &pos,
    std::vector<Instruction>& bytecode,
    std::unordered_map<std::string,int>& symTab,
    int& nextId,
    const std::unordered_map<std::string,Opcode>& keywordMap)>;

extern std::unordered_map<std::string, CompileFunc> compileMap;

void initCompileMap();
#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

mkdir -p bin

c++ -std=c++17 -Iinclude \
  src/cli/main.cpp \
  src/frontend/keywords.cpp \
  src/frontend/lexer.cpp \
  src/helpers/expression.cpp \
  src/helpers/storeString.cpp \
  src/helpers/symbolTable.cpp \
  src/helpers/tooling.cpp \
  src/helpers/utility.cpp \
  src/compiler/compileBlock.cpp \
  src/compiler/compileCondition.cpp \
  src/compiler/compileFunction.cpp \
  src/compiler/compileLoop.cpp \
  src/compiler/compileRegistry.cpp \
  src/compiler/compileStatement.cpp \
  src/compiler/compileSwitch.cpp \
  src/compiler/compiler.cpp \
  src/compiler/compilerExpr.cpp \
  src/vm/vm.cpp \
  -o bin/vpp-cli

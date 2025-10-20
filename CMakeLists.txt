cmake_minimum_required(VERSION 3.10)
project(VietVM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include_directories(include)

set(SOURCES
        src/main.cpp
        src/compiler.cpp
        src/vm.cpp
        include/name_op.h
        include/common/Utility.h
        include/common/LoopUltil.h
        src/name_op.cpp
        src/Helpers/lex_utils.cpp
        include/common/lex_utils.h
        src/Helpers/Utility.cpp
        src/Helpers/expression.cpp
        include/common/expression.h
        src/Helpers/SymbolTable.cpp
        include/common/SymbolTable.h
        # Các tệp nguồn khác nếu có
)

add_executable(VietVM ${SOURCES}
        include/name_op.h)
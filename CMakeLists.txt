cmake_minimum_required(VERSION 3.10)
project(VietVM LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Tự động thu thập các source .cpp trong thư mục src/
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
        ${CMAKE_SOURCE_DIR}/src/*.cpp
)

add_executable(VietVM ${SOURCES})

# Thêm include/ vào đường dẫn tìm header để các file nguồn có thể dùng
# #include "common/Utility.h", "compiler/compiler.h", v.v.
target_include_directories(VietVM PRIVATE ${CMAKE_SOURCE_DIR}/include)

# Compiler warnings
if (MSVC)
    target_compile_options(VietVM PRIVATE /W4 /permissive-)
else()
    target_compile_options(VietVM PRIVATE -Wall -Wextra -Wpedantic)
endif()

# (Tuỳ chọn) nếu muốn bật debug info cho Debug config mặc định
# set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
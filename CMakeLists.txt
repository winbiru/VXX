cmake_minimum_required(VERSION 3.10)
project(VietVM)

# Thêm thư mục chứa các tệp header
include_directories(include)

# Liệt kê tất cả các tệp nguồn
set(SOURCES
        src/main.cpp
        src/compiler.cpp
        src/vm.cpp
        # Thêm các tệp nguồn khác nếu có
)

# Tạo executable
add_executable(VietVM ${SOURCES})

# VIETVM

## English

This repository contains **VIETVM**, a custom-designed virtual machine and compiler infrastructure for a structured, imperative programming language.

VIETVM is an experimental system focused on **explicit execution semantics**, **transparent control flow**, and **deterministic behavior**.
It is intended for study, experimentation, and long-term reference rather than mass adoption.

Copyright © 2025. All rights reserved by the author.

See the end of this file for copyright and license information.

---

## Contents

* General Information
* Project Scope
* Repository Structure
* Building VIETVM
* Running Programs
* Execution Model
* Testing
* Documentation
* Versioning
* Copyright and License Information

---

## General Information

* Project name: **VIETVM**
* Type: Virtual Machine + Compiler toolchain
* Implementation language: C++
* Status: Experimental / Research-oriented
* Target audience: system programmers, compiler learners, VM designers

This project does not aim to replace existing languages or runtimes.

---

## Project Scope

VIETVM focuses on:

* A custom bytecode format
* A stack-based execution model
* Explicit control flow (blocks, loops, conditions)
* Minimal runtime assumptions
* Clear separation between parsing, compilation, and execution

Out of scope (by design):

* Large standard libraries
* Framework-level abstractions
* Automatic parallelism
* Dynamic reflection-heavy features

---

## Repository Structure

```
/
├── src/            Core VM and compiler source code
├── include/        Public headers
├── examples/       Small example programs
├── docs/           Design notes and specifications
├── tests/          Test cases
└── README.md
```

The layout may evolve as the project stabilizes.

---

## Building VIETVM

### On Unix-like systems (Linux, macOS, BSD)

Requirements:

* C++17-compatible compiler (clang or gcc)
* Make or CMake

Example build:

```bash
mkdir build
cd build
cmake ..
make
```

No system-wide installation is required.

---

## Running Programs

After building, programs can be executed by invoking the VIETVM runtime with compiled bytecode:

```bash
./vietvm example.bytecode
```

---

## Execution Model

VIETVM uses:

* A stack-based virtual machine
* Explicit instructions for variable initialization, condition evaluation, loop control, and block entry/exit
* Deterministic instruction dispatch

There is no hidden control flow or implicit runtime behavior.

See `docs/DESIGN.md` for details.

---

## Testing

```bash
make test
```

Tests focus on instruction correctness and control-flow behavior.

---

## Documentation

Documentation is provided under `docs/`:

* `architecture.md` — project structure and design decisions
* `bytecode.md` — instruction set and bytecode format reference
* `grammar.bnf` — language grammar specification
* `language-comparison-en.md` — comparison with other programming languages (English)
* `language-comparison.md` — so sánh với các ngôn ngữ lập trình khác (Tiếng Việt)

---

## Versioning

VIETVM does not follow a fixed release schedule.
Backward compatibility is not guaranteed during early development.

---

## Copyright and License Information

Copyright © 2025.

Licensing terms are specified in the `LICENSE` file.

This project contains no GPL-licensed code.

---

# VIETVM (Tiếng Việt)

## Giới thiệu chung

Kho mã nguồn này chứa **VIETVM** — một **máy ảo (virtual machine)** và **hạ tầng compiler** được thiết kế riêng cho một ngôn ngữ lập trình có cấu trúc, dạng mệnh lệnh.

VIETVM là một hệ thống **thử nghiệm**, tập trung vào:

* Ngữ nghĩa thực thi tường minh
* Luồng điều khiển rõ ràng
* Hành vi xác định, có thể truy vết

Dự án này được xây dựng cho mục đích **nghiên cứu, học tập và tham khảo lâu dài**, không nhằm mục tiêu phổ cập đại trà.

---

## Phạm vi dự án

VIETVM tập trung vào:

* Định nghĩa bytecode riêng
* Mô hình thực thi dựa trên stack
* Luồng điều khiển tường minh (khối lệnh, vòng lặp, điều kiện)
* Runtime tối giản
* Phân tách rõ ràng giữa: phân tích cú pháp, biên dịch và thực thi

Những thứ **cố tình không làm**:

* Thư viện chuẩn lớn
* Abstraction cấp framework
* Tự động song song hóa
* Cơ chế phản xạ (reflection) động phức tạp

---

## Cấu trúc thư mục

```
/
├── src/            Mã nguồn lõi của VM và compiler
├── include/        Header public
├── examples/       Ví dụ chương trình nhỏ
├── docs/           Tài liệu thiết kế và đặc tả
├── tests/          Các ca kiểm thử
└── README.md
```

---

## Biên dịch VIETVM

### Trên các hệ Unix-like (Linux, macOS, BSD)

Yêu cầu:

* Trình biên dịch C++ hỗ trợ C++17
* Make hoặc CMake

Ví dụ:

```bash
mkdir build
cd build
cmake ..
make
```

Không yêu cầu cài đặt toàn hệ thống.

---

## Thực thi chương trình

Sau khi biên dịch, có thể chạy chương trình thông qua runtime của VIETVM với file bytecode:

```bash
./vietvm example.bytecode
```

---

## Mô hình thực thi

VIETVM sử dụng:

* Máy ảo dựa trên stack
* Tập lệnh tường minh cho:

  * khởi tạo biến
  * kiểm tra điều kiện
  * điều khiển vòng lặp
  * mở/đóng khối lệnh
* Cơ chế dispatch xác định, không ẩn

Không tồn tại luồng điều khiển ngầm hay hành vi runtime khó truy vết.

Chi tiết xem `docs/DESIGN.md`.

---

## Kiểm thử

```bash
make test
```

Các kiểm thử tập trung vào tính đúng đắn của lệnh và luồng điều khiển.

---

## Tài liệu

Tài liệu được đặt trong thư mục `docs/`, bao gồm:

* `architecture.md` — cấu trúc dự án và quyết định thiết kế
* `bytecode.md` — đặc tả tập lệnh bytecode và định dạng bytecode
* `grammar.bnf` — đặc tả ngữ pháp ngôn ngữ
* `language-comparison.md` — so sánh với các ngôn ngữ lập trình khác (Tiếng Việt)
* `language-comparison-en.md` — comparison with other programming languages (English)

Tài liệu mang tính mô tả kỹ thuật, không mang tính quảng bá.

---

## Phiên bản

VIETVM không theo lịch phát hành cố định.
Trong giai đoạn đầu, **không đảm bảo tương thích ngược**.

---

## Bản quyền và giấy phép

Copyright © 2025.

Điều khoản sử dụng được nêu trong file `LICENSE`.

Dự án không chứa mã nguồn GPL và có thể được sử dụng cho mục đích nghiên cứu, giáo dục hoặc cá nhân theo giấy phép tương ứng.



## Liên hệ

- Chủ repo: `winbiru`
- Báo lỗi hoặc thảo luận: tạo Issue tại [Issues](https://github.com/winbiru/VietVM/issues)

---

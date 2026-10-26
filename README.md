<div align="center">
  <img src="assets/icons/vpp-file.svg" width="112" alt="V++ logo" />

# V++

### Ngôn ngữ lập trình tiếng Việt, có compiler, bytecode VM và toolchain riêng.

*Vietnamese-first programming language with its own compiler, bytecode VM and developer toolchain.*

Viết chương trình bằng cú pháp gần gũi với tiếng Việt, nhưng vẫn có những thành phần của một
ngôn ngữ thực tế: semantic analysis, IR, bytecode verifier, tracing GC, module/package system,
LSP, formatter, linter và bộ thư viện chuẩn.

[![C/C++ CI](https://github.com/winbiru/VXX/actions/workflows/c-cpp.yml/badge.svg?branch=developer)](https://github.com/winbiru/VXX/actions/workflows/c-cpp.yml)
[![Release](https://img.shields.io/github/v/release/winbiru/VXX?display_name=tag)](https://github.com/winbiru/VXX/releases)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C.svg)](CMakeLists.txt)

[🚀 Cài đặt](#-cài-v-trong-vài-phút) · [✨ Xem cú pháp](#-v-trông-như-thế-nào) · [🧰 Tooling](#-tooling-đi-kèm) · [📊 Benchmark](#-benchmark) · [🗺️ Roadmap](plans/roadmap-1.0.md) · [🤝 Đóng góp](CONTRIBUTING.md)
</div>

---

## V++ là gì?

V++ là một ngôn ngữ lập trình dynamic-typed được xây dựng từ đầu với **cú pháp tiếng Việt và
Unicode là công dân hạng nhất**.

Mục tiêu của dự án không chỉ là đổi keyword sang tiếng Việt. V++ đang xây dựng đầy đủ một stack:

```text
Mã nguồn V++
    ↓
Lexer → Parser → AST → Semantic Analysis
    ↓
Untyped IR → Optimizer → Bytecode
    ↓
Bytecode Verifier → V++ VM → Tracing GC
```

V++ phù hợp nếu bạn quan tâm tới compiler/VM, thiết kế ngôn ngữ, runtime, hoặc đơn giản muốn thử
một ngôn ngữ mà code tiếng Việt trông tự nhiên hơn.

## ✨ V++ trông như thế nào?

```vi
hàm chào(tên) {
    trả về "Xin chào, " + tên + "!";
};

hàm chính() {
    in chào("Việt Nam");

    lặp(i = 1; i <= 5; i++) {
        nếu (i % 2 == 0) {
            in "Số chẵn: " + i;
        };
    };
};
```

Class, kế thừa và interface cũng dùng cú pháp tiếng Việt:

```vi
giao diện CóTên {
    hàm tên();
}

lớp SảnPhẩm triển khai CóTên {
    hàm khởi tạo(tên) {
        mình.tênSảnPhẩm = tên;
    };

    hàm tên() {
        trả về mình.tênSảnPhẩm;
    };
}

hàm chính() {
    càPhê = SảnPhẩm("Cà phê Việt Nam");
    in càPhê.tên();
};
```

Xem đầy đủ tại [Language Reference](docs/language-reference.md).

## 🔥 Có gì đáng chú ý?

| Thành phần | V++ hiện có |
| --- | --- |
| Ngôn ngữ | Hàm, lambda/closure, list/map, exception, class, inheritance, interface, visibility |
| Compiler | Lexer, parser, semantic analysis, IR, optimizer, bytecode codegen |
| Runtime | Bytecode VM, verifier, stack trace, module lifecycle, tracing GC |
| Package | `vpp.json`, SemVer, deterministic `vpp.lock`, cache/offline, path/Git/registry package |
| Stdlib | Text/Unicode, collection, JSON, file/path, time, HTTP, SQL và nhiều module mở rộng |
| Tooling | Formatter, linter, AST/IR dump, REPL, LSP, VS Code integration |
| Platforms | Windows, macOS và Linux |

Một vài điểm V++ tập trung mạnh:

- **Tiếng Việt thật sự trong source**: identifier có dấu, `mình`, `gốc`, `nếu`, `lặp`, `thử`, `bắt lỗi`…
- **Không chỉ là transpiler**: V++ có bytecode format nội bộ và VM riêng.
- **Runtime có verifier + tracing GC**: bytecode được kiểm tra trước khi thực thi và object graph có cycle collection.
- **Compiler có IR thật**: frontend, semantic, IR và codegen được tách thành các tầng rõ ràng.
- **Package reproducible**: dependency được khóa bằng version + fingerprint trong `vpp.lock`.
- **Developer tooling đi cùng ngôn ngữ**: formatter, linter, REPL và LSP dùng chung semantic model.

## 🚀 Cài V++ trong vài phút

> V++ đang tiến tới mốc 1.0. Build metadata trên nhánh phát triển có thể đi trước release final;
> xem [Roadmap 1.0](plans/roadmap-1.0.md) để biết trạng thái release gate hiện tại.

### macOS

```bash
curl -L https://github.com/winbiru/VXX/releases/latest/download/vpp-macos.tar.gz -o vpp-macos.tar.gz
tar -xzf vpp-macos.tar.gz
./install-vpp.sh
```

### Linux x64

```bash
curl -L https://github.com/winbiru/VXX/releases/latest/download/vpp-linux-x64.tar.gz -o vpp-linux-x64.tar.gz
tar -xzf vpp-linux-x64.tar.gz
./install-vpp.sh
```

### Windows PowerShell

```powershell
Invoke-WebRequest -Uri "https://github.com/winbiru/VXX/releases/latest/download/vpp-windows-x64.zip" -OutFile "vpp-windows-x64.zip"
Expand-Archive -Path "vpp-windows-x64.zip" -DestinationPath ".\vpp-bin" -Force
.\vpp-bin\install-vpp.cmd
```

**Chỉ cần chạy installer một lần.** Installer tự cài binary, stdlib, templates/examples, cấu hình
`PATH`, `VPP_HOME` và UTF-8/locale cho shell phù hợp. Cài lại dùng cùng installer để cập nhật và
không nhân đôi block cấu hình. Sau khi cài, mở terminal mới và chạy:

```text
vpp phiên bản
vpp chẩn đoán
```

Trên Windows, installer ghi block có marker vào PowerShell `CurrentUserAllHosts` profile để các
PowerShell mở sau đó tự dùng code page 65001, UTF-8 input/output và `LANG=vi_VN.UTF-8`. Trên
macOS/Linux, installer ưu tiên `vi_VN.UTF-8`; nếu hệ thống chưa có locale này thì chọn một locale
UTF-8 đang tồn tại. Uninstaller chỉ xóa cấu hình do V++ quản lý.

Chi tiết cài đặt, cập nhật, gỡ cài đặt và troubleshooting nằm trong
[docs/installation.md](docs/installation.md).

## ⚡ Chạy chương trình đầu tiên

Tạo `xin-chao.vi`:

```vi
hàm chính() {
    in "Xin chào V++!";
    in "2 + 3 = " + (2 + 3);
};
```

Sau đó:

```bash
vpp chạy xin-chao.vi
```

Hoặc tạo hẳn một project:

```bash
vpp khởi tạo ứng dụng hello-vpp
cd hello-vpp
vpp dựng src/chính.vi
vpp chạy src/chính.vi
vpp kiểm thử tests
```

## 🧰 Tooling đi kèm

```bash
vpp chạy app.vi                 # chạy source
vpp dựng app.vi                 # chạy compiler pipeline
vpp kiểm thử tests              # chạy test project
vpp --soát-lỗi src/             # linter
vpp --định-dạng src/ --kiểm-tra # formatter check
vpp --dump-ast app.vi           # xem AST
vpp --dump-ir app.vi            # xem IR
vpp --giải-mã app.vi            # disassemble bytecode
vpp --repl                      # REPL
vpp --lsp                       # language server
vpp chẩn đoán                   # kiểm tra môi trường
```

CLI dùng tên tiếng Việt làm contract chính; các alias quen thuộc như `run`, `build`, `test`,
`new` vẫn được giữ để tương thích.

### VS Code

Repo có extension hỗ trợ `.vi` với syntax highlighting và LSP cho:

- diagnostic;
- completion;
- go-to-definition;
- hover;
- rename;
- format.

Cài extension local:

```bash
./scripts/vpp-lang self-install
vpp-lang install --editor vscode
```

Sau đó reload VS Code và mở file `.vi`.

## 📦 Package system

Project V++ dùng `vpp.json` và `vpp.lock`:

```bash
vpp gói cài đặt ./thu-vien-cua-toi
vpp gói cài đặt git+https://github.com/example/pkg.git#main
vpp gói cập nhật
vpp gói khóa
vpp gói phục hồi --ngoại-tuyến
```

Package manager hỗ trợ SemVer range, dependency transitive, deterministic lockfile, cache offline,
local path, Git source và filesystem registry.

Đọc thêm: [Package System](docs/package-system.md).

Gói thử nghiệm `lượng tử` hỗ trợ dựng mạch, mô phỏng vector trạng thái phức,
phép đo/reset và xuất OpenQASM 3.0. Chạy ví dụ Bell bằng
`vpp chạy examples/luong-tu/bell.vi`; xem [API và cầu nối Qiskit](docs/thư-viện-vpp/lượng%20tử/chính.md).

## 🧪 Build từ source

Yêu cầu chính: compiler C++17 và CMake.

```bash
git clone https://github.com/winbiru/VXX.git
cd VXX
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure --no-tests=error
```

Binary nằm tại:

```text
build/bin/vpp-cli
```

Chạy trực tiếp:

```bash
./build/bin/vpp-cli chạy src/tests/program.vi
```

Windows có thể build bằng MSVC/CMake; hướng dẫn chi tiết nằm trong
[docs/installation.md](docs/installation.md).

## 📊 Benchmark

Đo trực tiếp ngày **24/10/2026** trên **Apple M3 Pro, macOS 26.6.2 ARM64**,
commit `b39933e` cùng các thay đổi benchmark được ghi trong report. V++ CLI được
build Release (`-O3 -DNDEBUG`); các executable đều được dựng lại trước khi đo.

### Chạy ứng dụng: V++ VM, V++ AOT và các runtime khác

**Trung vị µs/thao tác; thấp hơn là nhanh hơn.** Mỗi ô gồm **10 tiến trình độc lập**,
mỗi tiến trình có ba batch làm nóng rồi một batch đo. Thứ tự runtime được xáo trộn.
Thời gian trong bảng loại trừ khởi động, biên dịch source, chuẩn bị input và làm nóng;
vẫn tính vòng lặp harness, lời gọi hàm và công việc runtime/GC phát sinh trong batch.

| Workload | V++ VM | V++ AOT ứng dụng | C++ `-O3` | Python | Node.js | Java |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Số học | 1.2702 | 0.9416 | 0.0011 | 0.1090 | 0.0013 | 0.0017 |
| Vòng lặp có nhánh | 32.3707 | 27.0018 | 0.1009 | 2.4580 | 0.0871 | 0.0342 |
| Fibonacci vòng lặp | 9.0677 | 7.5001 | 0.0089 | 0.5475 | 0.0118 | 0.0040* |
| Danh sách 128 phần tử | 81.3476 | 66.4089 | 0.2489 | 6.7473 | 0.5235 | 0.4404 |
| Thay chuỗi Unicode | 2769.3333 | 1766.3047 | 0.1533 | 0.4228 | 0.2692 | 0.1369 |
| Phân tích JSON | 10272.3750 | 7064.9062 | — | 1.8086 | 0.4328 | — |

`*` Java/Fibonacci có MAD/median **6,88%**, vượt mốc nhiễu 5%; không dùng ô này
để khẳng định thứ hạng chính xác. Các ô còn lại có MAD/median dưới 5% trong lần chạy này.

- Toolchain: Apple Clang **21.0.0**, CPython **3.14.3**, Node.js **20.11.1**,
  OpenJDK **21.0.2**. Node/Java dùng JIT mặc định; V++ VM tắt JIT.
- V++ AOT ứng dụng dùng `--emit-native` qua frontend Stage 0 C++; đây là đường
  biên dịch đầy đủ của cùng chương trình V++, có runtime giá trị động.
- Mỗi workload dùng cùng chu kỳ **16 đầu vào**, tránh tràn i32. Kiểm tra từng đầu
  ra trước khi đo và checksum của tất cả batch: **PASS**, tổng cộng **340 mẫu đo**.
  Số lần lặp được hiệu chỉnh riêng theo runtime, theo bội số của 16, hướng tới 100 ms/batch.
- Danh sách tạo 128 số, lọc và cộng bằng vòng lặp. Chuỗi thay toàn bộ `Việt` bằng
  `V++` rồi đếm code point trên văn bản NFC dựng sẵn. JSON parse object có tên tiếng Việt,
  số lượng và mảng 16 số, sau đó đọc trường và tính tổng.
- Mỗi runtime dùng cấu trúc dữ liệu/thư viện của nó: JSON V++ viết bằng V++, Python/Node
  dùng parser tích hợp. Không thêm thư viện JSON cho C++/Java nên hai ô đó không được đo.
  Go và Rust chưa cài trên máy này, không có số liệu để so sánh.

**Ở bộ đo này, V++ AOT ứng dụng nhanh hơn V++ VM, nhưng cả hai còn chậm hơn các
runtime đối chiếu. Chuỗi và JSON là những điểm cần tối ưu rõ nhất.** Đây là các workload
nhỏ trên một máy, không phải kết luận cho mọi chương trình hoặc một chứng nhận phát hành.
Report lưu cả p95 thời gian/thao tác giữa các batch, độ nhiễu, thời gian toàn tiến trình và peak RSS.
RSS gồm cả chuẩn bị input/làm nóng; số vòng lặp và chính sách heap khác nhau giữa runtime,
nên chưa phải phép so sánh hiệu quả bộ nhớ với cùng tổng khối lượng công việc.

[Số liệu thô + môi trường + SHA256](benchmark/reports/languages-2026-10-24/report.json)
· [Bảng kết quả](benchmark/reports/languages-2026-10-24/summary.txt)
· [Mã nguồn các workload](benchmark/languages/)

Chạy lại, dùng một thư mục output mới:

```bash
cmake -S . -B build-performance -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF
cmake --build build-performance --target vpp-cli --parallel 4
python3 benchmark/languages/test_runner.py
python3 benchmark/languages/run.py --vpp build-performance/bin/vpp-cli \
  --samples 10 --target-ms 100 --output benchmark/reports/languages-rerun
```

Runner hiện cần POSIX (`posix_spawn`/`wait4`); lần đo này đã kiểm chứng trên macOS ARM64.

### Kernel i32 native: đối chiếu trực tiếp với C++

Đường `--emit-obj-selfhost` sinh hàm mã máy i32 độc lập, gọi qua cùng ABI C và cùng
harness C++ `-O3`, tắt LTO. Đây là phép đo khác với AOT ứng dụng ở bảng trên.
**Trung vị 15 lượt, ns/lần gọi**, cùng ngày và cùng binary V++ CLI:

| Kernel | C++ `-O3` | V++ native i32 | Thời gian V++ / C++ |
| --- | ---: | ---: | ---: |
| Số học `(a+b)-c*d` | 0.874 | 0.947 | 1.08× |
| Vòng lặp có nhánh | 146.527 | 184.143 | 1.26× |
| Fibonacci vòng lặp | 13.359 | 41.781 | 3.13× |

Kết quả và checksum **PASS**; MAD/median dưới 5% cho mọi backend/kernel.
Không tính biên dịch, liên kết hoặc khởi động; vẫn có overhead harness/lời gọi hàm.
Tỷ lệ lớn hơn 1 nghĩa là V++ mất nhiều thời gian hơn. Kết quả chỉ áp dụng cho ba
kernel i32 này, chưa đại diện cho chuỗi, OOP, GC hoặc I/O.

[Raw samples và môi trường](benchmark/reports/native-2026-10-24/)
· [Phương pháp native](benchmark/NATIVE.md)

```bash
VPP_HOME="$PWD" VPP_EXEC="$PWD/build-performance/bin/vpp-cli" \
  bash benchmark/native/run.sh build-benchmark/native-rerun 15 0.1
```

Benchmark regression giữa hai revision V++ được theo dõi riêng tại
[benchmark/BASELINE.md](benchmark/BASELINE.md): **16 PASS, 3 INCONCLUSIVE, 0 FAIL**;
gate tổng thể vẫn chờ đo lại ba workload nhiễu.

## 🧠 Kiến trúc

V++ chia source C++ thành các module riêng:

```text
vpp-core
   ↓
vpp-bytecode
   ↓
vpp-frontend
   ↓
vpp-compiler ──→ vpp-tooling
   ↓
vpp-runtime
   ↓
vpp-cli
```

Các package viết bằng V++ nằm dưới `gói/`, còn native runtime chỉ giữ những primitive cần tương
tác với hệ điều hành hoặc host runtime.

Xem chi tiết: [Architecture](docs/architecture.md) · [Bytecode](docs/bytecode.md) ·
[Semantics](docs/semantics.md) · [Runtime Errors](docs/runtime-errors.md).

## 🗺️ Trạng thái 1.0

V++ đã có phần lớn nền tảng của 1.0: semantics, object model, compiler pipeline, VM, package system,
toolchain và documentation. Công việc còn lại tập trung vào hardening release:

- same-machine performance baseline/candidate regression gate;
- GC allocation/RSS/p95 telemetry;
- Linux ASan/UBSan/LSan evidence;
- artifact install smoke trên Windows/macOS/Linux;
- audit bề mặt stdlib stable trước final.

Theo dõi checklist đầy đủ tại [Roadmap V++ 1.0](plans/roadmap-1.0.md) và
[Quality Gates](docs/quality.md).

## 🧩 Ví dụ trong repo

- `examples/hoa-don-cua-hang` — module, class/constructor và import tương đối.
- `examples/quan-ly-kho-api` — sample backend/HTTP lớn hơn để kiểm tra package + runtime.
- `src/tests/` — regression corpus của compiler và VM.

## 🤝 Đóng góp

Compiler, runtime, stdlib, tooling và documentation đều có chỗ để đóng góp.

Nếu bạn muốn bắt đầu nhanh:

1. Fork repository.
2. Chọn một issue hoặc một mục trong [Roadmap](plans/roadmap-1.0.md).
3. Build và chạy CTest trước khi sửa.
4. Thêm regression khi thay đổi behavior.
5. Gửi pull request với mô tả rõ trước/sau.

Đọc [CONTRIBUTING.md](CONTRIBUTING.md) để biết quy ước chi tiết.

## ⭐ Nếu bạn thấy V++ thú vị

Hãy **Star** repo để theo dõi quá trình đưa một ngôn ngữ lập trình tiếng Việt từ compiler/VM thử
nghiệm tới mốc 1.0, hoặc chia sẻ V++ cho những người quan tâm tới compiler, VM và language design.

Ý tưởng, bug report và pull request đều được hoan nghênh.

## 📚 Tài liệu

- [Language Reference](docs/language-reference.md)
- [CLI](docs/cli.md)
- [Package System](docs/package-system.md)
- [Testing](docs/testing.md)
- [Installation](docs/installation.md)
- [Architecture](docs/architecture.md)
- [Debugging](docs/debugging.md)
- [Migration to 1.0](docs/migration-1.0.md)
- [Roadmap 1.0](plans/roadmap-1.0.md)
- [Changelog](CHANGELOG.md)
  
## Thông điệp
V++ — Lập trình bằng tiếng Việt, kết nối với công nghệ thế giới.
V++ được phát triển với mong muốn tiếng Việt có thể trở thành một phương tiện biểu đạt tư duy lập trình và xây dựng phần mềm hiện đại.
Chúng tôi không phát triển V++ để phủ nhận Python, Java, C++ hay bất kỳ ngôn ngữ lập trình nào khác. Mỗi ngôn ngữ đều có giá trị, lịch sử và lĩnh vực ứng dụng riêng.
V++ hướng tới việc mở rộng sự lựa chọn: để người Việt có thể học tập, nghiên cứu và sáng tạo phần mềm bằng chính tiếng mẹ đẻ, đồng thời tương tác với các tiêu chuẩn và hệ sinh thái công nghệ quốc tế.
tôi hiểu rằng V++ còn nhiều điều cần hoàn thiện. Mọi phản biện kỹ thuật, góp ý thiết kế và đóng góp mã nguồn đều được trân trọng.
tôi không mong mọi người đồng ý với V++. tôi mong V++ được đánh giá bằng những gì nó thực sự làm được. 
Bản sắc V++ không có nghĩa là khép mình trước thế giới. Đó là khả năng đóng góp vào thế giới bằng bản sắc của chính mình.

## License

V++ được phát hành theo giấy phép [MIT](LICENSE).

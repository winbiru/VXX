# So sánh C++ và V++ chạy mã máy trực tiếp

Đo ngày 16/10/2026 trên macOS 26.6.2, ARM64, Apple Clang 21.0.0
(`clang-2100.1.1.101`), target `aarch64-apple-darwin`.
V++ dùng bộ sinh mã native hiện tại qua `--emit-obj-selfhost`.

Thời gian dưới đây là trung vị của 7 lượt, đơn vị **ns/lần gọi hàm**;
giá trị nhỏ hơn chạy nhanh hơn. Thời gian gồm cùng chi phí gọi hàm, đọc đầu
vào và cộng checksum; không gồm biên dịch, liên kết, khởi động tiến trình
hay sinh đầu vào.

| Bài đo | C++ `-O0` | C++ `-O2` | C++ `-O3` | V++ native | V++ / C++ `-O3` |
| --- | ---: | ---: | ---: | ---: | ---: |
| Số học `(a+b)-c*d` | 1.076 | 0.838 | 0.839 | 2.944 | 3.51× |
| Vòng lặp có nhánh | 216.032 | 140.786 | 143.346 | 652.439 | 4.55× |
| Fibonacci vòng lặp | 49.802 | 13.407 | 13.360 | 162.298 | 12.15× |

Khoảng nhỏ nhất–lớn nhất trong 7 lượt:

| Bài đo | C++ `-O3` (ns/lần gọi) | V++ native (ns/lần gọi) | Số lần gọi/mẫu/backend |
| --- | ---: | ---: | ---: |
| Số học | 0.829–0.845 | 2.934–2.983 | 67,108,864 |
| Vòng lặp có nhánh | 134.183–145.650 | 648.965–657.001 | 262,144 |
| Fibonacci vòng lặp | 12.493–13.635 | 158.355–166.380 | 4,194,304 |

## Cách bảo đảm chạy native

[Mã V++](native/kernels.vi) được xuất thành ba object Mach-O chứa mã máy.
[Mã C++](native/kernels.cpp) triển khai cùng thuật toán và được biên dịch
thành các object riêng ở `-O0`, `-O2`, `-O3`.
[Harness C++](native/harness.cpp) liên kết trực tiếp các object đó, gọi
các hàm qua cùng ABI C với bốn tham số i32 và giá trị trả về i32.
Tắt LTO bằng `-fno-lto`; các hàm không được inline vào harness.

Executable đo chỉ liên kết `libc++.1.dylib` và `libSystem.B.dylib`.
Kiểm tra `otool -L` và `nm -u` không thấy phụ thuộc V++ runtime/VM.
Trong giai đoạn biên dịch, compiler self-host có thể dùng VM; giai đoạn
đó hoàn tất trước khi chạy executable và không nằm trong thời gian đo.

## Đầu vào và kiểm tra kết quả

- Mỗi bài dùng 4,096 đầu vào giả ngẫu nhiên cố định, seed `20261016`.
  Mọi backend nhận cùng đầu vào; miền giá trị tránh tràn số nguyên có dấu.
- Số học: mỗi tham số trong `[-1000, 1000]`.
- Vòng lặp: `seed` trong `[0, 65520]`, `n` trong `[128, 255]`;
  mỗi bước cộng 251, trừ 65521 nếu đạt ngưỡng.
- Fibonacci: tính `F(n)` bằng vòng lặp với `n` trong `[20, 39]`.
- Kiểm tra 49,152 kết quả với hàm tham chiếu trước khi đo; kiểm tra
  checksum của mọi mẫu đo. Tất cả đều **PASS**.
- Warm-up từng backend, chọn cùng số lần gọi/mẫu theo C++ `-O3`
  với mục tiêu 30 ms, rồi xáo thứ tự backend trong mỗi lượt bằng seed
  `20261017`. Dùng đồng hồ `std::chrono::steady_clock`.

## Nhận xét từ mã máy

Hàm số học C++ `-O3` có ba lệnh: `add`, `msub`, `ret` (12 byte).
Object V++ tương ứng có 128 byte, gồm tạo frame, nhiều lệnh `ldr`/`str`
cho biến và giá trị tạm, cùng một lệnh `brk` sau `ret`.
Fibonacci C++ giữ trạng thái trong thanh ghi; V++ còn đọc/ghi stack nhiều
lần mỗi vòng. Đây là bằng chứng về chi phí của bộ sinh mã hiện tại ngay
cả khi đã bỏ VM khỏi đường thực thi.

Các số đo chỉ đại diện ba hàm i32 này, chưa đánh giá toàn bộ thư viện OOP,
cấp phát, chuỗi, I/O hoặc mọi chương trình V++. Chưa ghim CPU affinity;
tải máy và điều phối CPU có thể ảnh hưởng kết quả. C++ `-O2` và `-O3`
có kết quả gần nhau trong các bài này; sai khác nhỏ cần xem cùng khoảng
dao động trước khi kết luận.

## Chạy lại

Driver Python dùng trong lần đo 16/10/2026 đã được gỡ bỏ khi loại bỏ
các công cụ Python khỏi repository. Các số liệu trên là bản ghi lịch sử;
hiện chưa có driver thay thế để tái tạo tự động phép đo. Mã nguồn
V++ và C++ của ba bài đo vẫn được lưu tại `benchmark/native/`.

Artifact của lần đo cũ ở `build-benchmark/native-comparison/` (được git ignore):
`results.json` lưu mọi mẫu, checksum, thứ tự chạy, phiên bản compiler và
SHA-256; `commands.json` lưu lệnh, đầu ra và thời gian từng bước;
`dependencies.txt`, `undefined-symbols.txt`, `disassembly.txt` lưu bằng chứng
liên kết và mã máy. Chạy lại sẽ ghi đè artifact ở thư mục đầu ra đã chọn.

SHA-256 compiler V++ dùng trong lần đo này:
`af67b15853303ea1decbb643daf3618300c6fcb4c065b7570c040bc568aa3c25`.

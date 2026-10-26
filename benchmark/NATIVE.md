# Đo tốc độ C++ và V++ sau khi sinh mã máy

## Xuất object qua CLI V++ native (26/10/2026)

CLI compiler đã bootstrap nhận trực tiếp `build --target <triple>
--emit-obj-selfhost <function> <source.vi> <output.o>`. Phân tích tham số,
đọc nguồn, AST/IR/ISA và ghi object chạy bằng V++, không tạo wrapper qua
bridge Stage 0 ở mỗi lần xuất object. Script ưu tiên `build/native-cli/vpp-native`
nếu đã tạo; có thể chọn compiler của một build khác bằng `VPP_EXEC`:

```sh
cmake --build build-p0-release --target vpp-native-cli -j 6
VPP_EXEC="$PWD/build-p0-release/native-cli/vpp-native" \
  bash benchmark/native/run.sh build-benchmark/native-vpp-cli 15 0.03
```

Compiler native vẫn liên kết runtime C++/ASM và thư viện hệ điều hành;
bootstrap ban đầu và backend ASM tổng quát vẫn dùng C++. Kết quả này chỉ
loại đường bridge Stage 0 khỏi việc xuất object i32. Script lưu riêng
`compiler-dependencies.txt` và `dependencies.txt` của harness để phân biệt.
Thay CLI không tự làm kernel chạy nhanh hơn; byte object và correctness
phải giữ nguyên. Chi phí biên dịch nằm ngoài phần đo ns/lần gọi.

Phép đo cục bộ trên macOS ARM64, năm lượt xen kẽ sau warm-up, xuất một
object `vpp_cycle`: median đường CLI/bridge Stage 0 là **5,295 giây**,
CLI V++ Stage 3 trực tiếp là **24,84 ms** (khoảng 213×). Cả hai đường xuất
object trùng byte; không tính chi phí bootstrap. Mẫu thô, executable hashes
và tham số ở `build-benchmark/native-cli-latency-20261026/results.json`.

## Kết quả mới nhất: 24/10/2026

Đã đo lại với V++ CLI Release tại commit `b39933e` cùng benchmark patch,
Apple M3 Pro, macOS 26.6.2 ARM64, Apple Clang 21.0.0. **15 lượt**, target
0,1 giây, input/checksum/harness như phần phương pháp bên dưới:

| Kernel | C++ `-O3` (ns/lần gọi) | V++ native i32 (ns/lần gọi) | V++ / C++ |
| --- | ---: | ---: | ---: |
| Số học | 0.874 | 0.947 | 1.08× |
| Vòng lặp có nhánh | 146.527 | 184.143 | 1.26× |
| Fibonacci vòng lặp | 13.359 | 41.781 | 3.13× |

Kết quả đúng và checksum **PASS**, MAD/median <5% cho mọi backend/kernel.
[Raw samples, disassembly, môi trường và source hashes](reports/native-2026-10-24/).
SHA256 V++ CLI: `88f8876d2341094c15a7938826128ac91edeab55fd7f98d177d1787989f74c72`.

```sh
VPP_HOME="$PWD" VPP_EXEC="$PWD/build-performance/bin/vpp-cli" \
  bash benchmark/native/run.sh build-benchmark/native-rerun 15 0.1
```

Đây là kernel i32 độc lập; kết quả AOT ứng dụng dùng runtime giá trị động,
V++ VM, Python, Node.js và Java được đo riêng tại
[README](../README.md#-benchmark) và [report đa ngôn ngữ](reports/languages-2026-10-24/report.json).

## Kết quả lịch sử: 18/10/2026

macOS 26.6.2, ARM64, Apple Clang 21.0.0
(`aarch64-apple-darwin`). V++ dùng `build/bin/vpp` và backend
`--emit-obj-selfhost` để xuất object Mach-O chứa mã máy trực tiếp.

Các số dưới đây là **trung vị 7 lượt**, đơn vị **ns/lần gọi hàm**
(thấp hơn là nhanh hơn). Mỗi backend chạy cùng input và được gọi qua
cùng một harness C++ đã biên dịch `-O3`; không tính thời gian khởi động,
biên dịch hoặc liên kết. Các kernel C++ được biên dịch độc lập, tắt LTO.

| Kernel | C++ `-O0` | C++ `-O2` | C++ `-O3` | V++ native | V++ / C++ `-O3` |
| --- | ---: | ---: | ---: | ---: | ---: |
| Số học `(a+b)-c*d` | 1.093 | 0.837 | 0.848 | 0.911 | **1.07×** |
| Vòng lặp có nhánh | 226.058 | 152.803 | 150.355 | 192.030 | **1.28×** |
| Fibonacci vòng lặp | 51.182 | 13.471 | 13.679 | 41.608 | **3.04×** |

Khoảng nhỏ nhất–lớn nhất trong cùng 7 lượt:

| Kernel | C++ `-O3` (ns/lần) | V++ native (ns/lần) | Số lần gọi/lượt/backend |
| --- | ---: | ---: | ---: |
| Số học | 0.837–1.052 | 0.886–0.920 | 67,108,864 |
| Vòng lặp | 142.461–154.034 | 185.487–194.261 | 262,144 |
| Fibonacci | 12.932–13.874 | 41.352–42.771 | 4,194,304 |

**Đúng kết quả: PASS.** Harness kiểm tra cả 4 backend bằng tham chiếu
trước khi đo, rồi kiểm tra checksum trong mỗi lượt. Mỗi kernel dùng 4,096
đầu vào giả ngẫu nhiên cố định (seed `20261016`): số học trong
`[-1000, 1000]`, vòng lặp `seed` trong `[0, 65520]` với 128–255 bước,
Fibonacci `n` trong `[20, 39]`. Miền đầu vào tránh tràn số nguyên có dấu.
Sau warm-up, harness hiệu chỉnh số lần gọi theo C++ `-O3`, xáo trộn thứ
tự các backend, đo bằng `std::chrono::steady_clock`.

## Mã máy thật và nguyên nhân chênh lệch

- Ba object V++ có `.text` lần lượt **40, 124, 112 byte** cho số học,
  vòng lặp và Fibonacci. Kernel số học C++ `-O3` chỉ gồm `add`, `msub`,
  `ret` (12 byte).
- Ở phiên bản V++ hiện tại, Fibonacci đã giữ biến trong thanh ghi. Tuy
  nhiên, mã máy còn nhiều lệnh `mov` thừa và chuỗi `cmp` → `cset` → `cbz`.
  C++ `-O3` tạo vòng lặp ngắn hơn, dùng `subs` và `b.ne`.
- `otool -L` trên executable chỉ có `libc++.1.dylib` và
  `libSystem.B.dylib`; `nm` xác nhận các symbol V++ được liên kết trực
  tiếp. **VM không chạy trong đường đo**. Compiler self-host vẫn có thể
  dùng VM trong quá trình *biên dịch*, ngoài thời gian benchmark.

Chỉ đo ba kernel i32 có lời gọi ABI C và checksum chung. Không thể suy
ra hiệu năng của toàn bộ V++ (cấp phát, OOP, chuỗi, I/O...). Thời gian
mỗi lần gọi còn gồm overhead của harness. Máy không ghim CPU affinity,
nên một phần dao động đến từ scheduler, nhiệt độ và tải hệ thống.

## Chạy lại

Trên máy macOS ARM64 đã build `build/bin/vpp`, chạy tại gốc repo:

```bash
./benchmark/native/run.sh
```

Script xuất object V++ native, biên dịch các phiên bản C++ `-O0`, `-O2`,
`-O3`, liên kết executable rồi chạy 7 lượt. Mọi artifact và JSON kết quả
nằm tại `build-benchmark/native-comparison-current/`. Có thể truyền
`<output-dir> [số-lượt-lẻ] [mục-tiêu-giây]` làm tham số, ví dụ:

```bash
./benchmark/native/run.sh build-benchmark/native-comparison-next 7 0.03
```

Kết quả thực tế ngày **18/10/2026** được lưu tại
`build-benchmark/native-comparison-20261018/results.json`.
SHA-256 compiler trong lần đo:
`e7c2912f1f89feaa8b4835a7cbadaa7707174b68c5b31c4c15fe16fec7a43e83`.

Chạy kiểm chứng thêm cùng ngày với 7 lượt và cùng tham số `0.03` tại
`build-benchmark/native-comparison-selfhost-verify/results.json` cho trung
vị V++ lần lượt **0.876 / 185.635 / 41.084 ns**, so với C++ `-O3`
**0.831 / 149.003 / 13.437 ns**; tỷ lệ xấp xỉ **1.05× / 1.25× / 3.06×**.
Kết quả đúng và checksum của mọi backend đều được harness xác nhận.

## Lịch sử

Lần đo ban đầu ngày **16/10/2026** cho kết quả V++ chậm hơn C++ `-O3`
**3.51× / 4.55× / 12.15×** cho ba kernel theo thứ tự trên. Artifact của
lần đo lại trong cùng ngày (`build-benchmark/native-comparison-rerun-20261016/`)
cho tỷ lệ khoảng **1.06× / 1.24× / 3.05×**. Bộ sinh mã V++ đã thay đổi
đáng kể giữa hai lần đo; so sánh hiệu năng theo từng artifact và compiler
hash, không coi kết quả đầu ngày 16/10 là hiện trạng.

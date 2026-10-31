# Đo V++ và các runtime khác — 30/10/2026

Bộ đo mới trên Apple M3 Pro (ARM64, 12 CPU logic, 36 GiB RAM),
macOS 26.6.2. Source nền: commit
`5ef46b3cba044f27793eae9e275ac96f7357a337`; harness benchmark là phần bổ sung
chưa commit, có bản sao và SHA-256 trong báo cáo.

## Dữ liệu và cách chạy

- [Báo cáo JSON đầy đủ](../benchmark/reports/languages-2026-10-30/report.json)
- [Từng mẫu đo](../benchmark/reports/languages-2026-10-30/samples.jsonl)
- [Hiệu chỉnh số vòng lặp](../benchmark/reports/languages-2026-10-30/pilots.jsonl)
- [Môi trường, lệnh chạy, fingerprint](../benchmark/reports/languages-2026-10-30/manifest.json)
- [Cấu hình phần cứng đã đọc riêng](../benchmark/reports/languages-2026-10-30/host-verified.json)
- [Bản sao source đã đo](../benchmark/reports/languages-2026-10-30/sources/)
- [Runner V++](../benchmark/languages/run.vi)

Runner, kiểm tra kết quả và thống kê viết bằng V++. Shell/CMake chỉ build
và khởi chạy runner. Python là một runtime đối chiếu, chạy source inline
từ `.txt`; không thêm tệp `.py` hoặc dùng Python làm runner.

```sh
cmake -S . -B build-performance -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTS=OFF -DBUILD_BENCHMARKS=ON -DVPP_ENABLE_COVERAGE=OFF
cmake --build build-performance --target vpp-cli --parallel 2
VPP_EXEC="$PWD/build-performance/bin/vpp-cli" \
  VPP_I32_COMPILER="$PWD/build-ci-regression-fix/nativedriver/vpp-native" \
  bash benchmark/languages/run.sh "$PWD/benchmark/reports/languages-rerun"
```

Output phải là thư mục mới; runner từ chối ghi đè bằng chứng cũ. Nếu chưa
có compiler i32, cần bootstrap fixture `vpp-native-stage1-bootstrap` trước
để có thêm backend VM/ASM. Các runtime đối chiếu được phát hiện trên máy;
không tự cài thêm runtime hoặc tải thư viện JSON ngoài.

Ví dụ chuẩn bị compiler leaf trên macOS ARM64, dùng CLI vừa build:

```sh
cmake "-DVPP_EXEC=$PWD/build-performance/bin/vpp-cli" \
  "-DVPP_ROOT=$PWD" -DVPP_TARGET=aarch64-apple-darwin \
  "-DVPP_OUTPUT_DIR=$PWD/build-performance/nativedriver" \
  -DVPP_PREPARE_ONLY=ON -P scripts/quality/checknativedriver.cmake
VPP_I32_COMPILER="$PWD/build-performance/nativedriver/vpp-native" \
  bash benchmark/languages/run.sh "$PWD/benchmark/reports/languages-rerun"
```

Các trường `sysctl` trong manifest tự động bị sandbox chặn. File
`host-verified.json` ghi kết quả đọc phần cứng riêng bằng lệnh chỉ đọc,
không sửa manifest gốc.

## Các đường thực thi

| Tên trong dữ liệu | Ý nghĩa |
| --- | --- |
| `vpp-vm` | CLI Release, frontend/VM C++ tương thích, tắt cả hai cờ JIT |
| `vpp-aot` | Ứng dụng được compiler V++ dựng native, giữ mô hình giá trị động |
| `vpp-vm-asm` | Cùng source được compiler V++ ghi `.vmb`, chạy trên VM V++/ASM độc lập |
| `vpp-i32` | Ba hàm số nguyên i32 riêng, object do compiler V++ phát; cùng harness và ABI C bốn i32 với C++ |
| `cpp` | Apple Clang 21.0.0, `-O3 -DNDEBUG`, hai translation unit, không LTO |
| `node` | Node.js 20.11.1, V8 và JIT mặc định |
| `java` | OpenJDK 21.0.2, HotSpot/JIT mặc định |
| `python` | CPython 3.14.3, chạy mã inline |
| `ruby` | Ruby 2.6.10, JSON 2.1.0 |

Kernel i32 chỉ có số học, vòng lặp và Fibonacci. Nó không đại diện cho
toàn bộ ngôn ngữ, collection, Unicode, JSON hoặc runtime động.

## Phương pháp

Mỗi ô có **10 tiến trình độc lập**, mỗi tiến trình chạy ba batch warmup rồi
một batch đo. Trước đó chạy preflight cho toàn bộ 49 tổ hợp và hiệu chỉnh
số vòng lặp từng ô, hướng tới 100 ms/batch. Số vòng lặp sau đó được đóng
băng, là bội số của 16; tối thiểu 16, tối đa 100 triệu. Backend chậm có thể
vượt 100 ms ở mức tối thiểu; backend chạm giới hạn có thể chưa đạt 100 ms.
Mỗi vòng lấy mẫu xáo trộn thứ tự các ô bằng seed cố định.
`plan.json` lưu số vòng lặp đã đóng băng và trạng thái bộ xáo trộn trước
khi thu mẫu. Kernel số học native/JIT khởi đầu hiệu chỉnh ở 1.024 vòng,
tránh chạm độ phân giải clock; các ô khác khởi đầu ở 16 vòng.

Thời gian chính lấy từ clock đơn điệu **bên trong tiến trình**, loại trừ
startup, biên dịch, chuẩn bị input và warmup. Vẫn tính vòng lặp harness,
lời gọi hàm, checksum, allocation và GC phát sinh trong batch. Không trừ
thời gian của vòng lặp rỗng. `process_wall_ms` là thời gian runner quan sát,
gồm cả adapter tiến trình/IPC và bốn batch; không dùng nó để xếp hạng
startup của các ngôn ngữ.

Mỗi workload dùng cùng 16 đầu vào `k=17..32`. Kiểm tra từng kết quả bằng
oracle độc lập trong runner và kiểm tra checksum mọi batch. Checksum
truyền bằng chuỗi số nguyên để tránh sai số chuyển đổi JSON số thực; tổng
không vượt miền số nguyên biểu diễn chính xác của f64.

| Workload | Công việc một thao tác |
| --- | --- |
| `arithmetic` | `(k+17)*31 + (k*7+3) - (k+5)*11` |
| `branch_loop` | 256 lần cộng 251 và trừ 65521 khi vượt ngưỡng |
| `fibonacci` | Fibonacci vòng lặp, `n=k%16+20`, không tràn i32 |
| `list_128` | Tạo 128 số `k..k+127`, lọc số chẵn và tính tổng |
| `unicode_replace` | Thay mọi `Việt` bằng `V++`, đếm code point trên source NFC |
| `json_parse` | Parse object có tên tiếng Việt/emoji, số `n`, mảng 16 số; đọc trường và cộng |

Mỗi runtime dùng container và thư viện của nó. Java dùng
`ArrayList<Integer>`, C++ dùng `vector<int>`, các runtime động dùng list/Array.
JSON V++ viết bằng V++; Python, Node và Ruby dùng parser có sẵn. C++ và
Java không có parser JSON tương ứng trong bộ đo này nên để trống ô JSON.
Do đó ô JSON so sánh cả cách triển khai thư viện, không chỉ bộ thực thi
ngôn ngữ. Các thuật toán workload và corpus mới khác bộ đo lịch sử
26/10; không dùng chênh lệch giữa hai bảng làm kết luận regression.

Kết quả chính là trung vị; độ nhiễu dùng MAD/median. Ô vượt 5% được đánh
dấu `*`, không dùng để khẳng định thứ hạng nhỏ. P95 chỉ mô tả 10 mẫu, không
phải ước lượng tail latency sản xuất. Không pin CPU hoặc kiểm soát toàn bộ
tiến trình nền; kết quả chỉ áp dụng cho bộ workload và máy này.

## Kết quả

Đơn vị hai bảng ứng dụng là **µs/thao tác; thấp hơn nhanh hơn**.
**490/490 mẫu chính thức đạt kiểm tra**, đủ 49 ô × 10 tiến trình độc lập.
Cả 49 bản ghi preflight và 102 bản ghi hiệu chỉnh đều đạt oracle/checksum.
Không ô nào có MAD/median vượt 5%; cao nhất là Ruby/vòng lặp **2,27%**.

### Ứng dụng và các ngôn ngữ đối chiếu

| Workload | V++ AOT ứng dụng | C++ `-O3` | Node.js | Java | Python | Ruby |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Số học | 21.56110 | 0.00081 | 0.00112 | 0.00137 | 0.09782 | 0.08174 |
| Vòng lặp có nhánh | 1556.26563 | 0.19866 | 0.15329 | 0.06936 | 4.27098 | 6.51089 |
| Fibonacci vòng lặp | 266.27983 | 0.00908 | 0.01142 | 0.00451 | 0.46393 | 1.07027 |
| Danh sách 128 phần tử | 1856.73437 | 0.24046 | 0.53138 | 0.34491 | 5.22852 | 7.51156 |
| Thay chuỗi Unicode | 9505.50000 | 0.18366 | 1.32997 | 0.18833 | 0.45324 | 1.22038 |
| Phân tích JSON | 24191.53125 | — | 0.41046 | — | 2.01089 | 2.15378 |

### Các đường thực thi V++

| Workload | VM C++ tương thích | AOT V++/ASM | VM V++/ASM |
| --- | ---: | ---: | ---: |
| Số học | 1.21104 | 21.56110 | 31.10089 |
| Vòng lặp có nhánh | 54.31222 | 1556.26563 | 2575.88542 |
| Fibonacci vòng lặp | 9.44246 | 266.27983 | 397.12891 |
| Danh sách 128 phần tử | 74.66797 | 1856.73437 | 2439.96875 |
| Thay chuỗi Unicode | 399.00000 | 9505.50000 | 12752.68750 |
| Phân tích JSON | 1651.25000 | 24191.53125 | 34115.75000 |

AOT ở đây dùng compiler V++ đầy đủ và runtime động hiện tại. Trên cả sáu
bài, nó chậm hơn VM C++ tương thích khoảng **14,65–28,65 lần** và chậm hơn
Python khoảng **220–20.972 lần**, tùy workload. Node, Java, C++ và Ruby cũng
nhanh hơn V++ AOT ở mọi ô đã đo tương ứng. Chưa profile nguyên nhân; dữ liệu
này xác nhận hiện trạng của đường biên dịch/runtime được đo.

### Kernel i32 riêng

| Kernel | C++ `-O3`, ns/op | V++ i32, ns/op | Thời gian V++ / C++ |
| --- | ---: | ---: | ---: |
| Số học | 0.812 | 1.165 | 1.44× |
| Vòng lặp có nhánh | 198.658 | 149.352 | 0.75× |
| Fibonacci vòng lặp | 9.077 | 20.356 | 2.24× |

V++ i32 mất ít thời gian hơn C++ **24,82%** ở vòng lặp có nhánh; số học mất
thêm **43,55%**, Fibonacci mất **2,24 lần** thời gian của C++. Ba phép đo dùng
cùng harness C++, ABI và workload; không suy rộng kết quả kernel i32 thành
hiệu năng của ứng dụng V++ có giá trị động. Các bài rất nhỏ vẫn bao gồm
chi phí gọi hàm và vòng lặp kiểm tra.

### Kiểm chứng và các lượt chẩn đoán

[Kiểm tra độc lập](../benchmark/reports/languages-2026-10-30/audit.txt) xác nhận mọi checksum, oracle,
clock, số vòng lặp cố định, thứ tự xáo trộn và SHA-256. Median, MAD và p95 do
runner V++ tính đã được tính lại độc lập bằng Ruby từ raw samples. JSON
xuất số thực với khoảng 10 chữ số có nghĩa, nên kiểm tra thống kê dùng
sai số tuyệt đối tối đa `median × 1e-8` (tối thiểu `1e-8` ns); checksum
chuỗi số nguyên vẫn kiểm tra chính xác, không dùng dung sai.

Ba lượt phát triển trước đã bị loại **toàn bộ**, không trộn với bảng này:
truyền checksum bằng JSON float gây sai số parse; frontend tương thích
không hỗ trợ truy cập chỉ số trực tiếp trên literal chuỗi; phép nhân i32
khi hiệu chỉnh bị tràn và quay về 16 vòng. Lượt cuối đã sửa các điểm này,
chạy lại từ đầu, không bỏ, thử lại hay thay mẫu nào trong 490 mẫu báo cáo.

- [Số vòng lặp và trạng thái xáo trộn](../benchmark/reports/languages-2026-10-30/plan.json)
- [Bản ghi preflight](../benchmark/reports/languages-2026-10-30/preflight.json)
- [Cấu hình build CLI](../benchmark/reports/languages-2026-10-30/build-config.txt)
- [Bảng đầy đủ chín backend](../benchmark/reports/languages-2026-10-30/table.txt)
- [Median, MAD và số mẫu](../benchmark/reports/languages-2026-10-30/summary.txt)

Rust, Go và .NET chưa cài trên máy, không có kết quả của chúng. Lần này
không đo thời gian biên dịch, startup, I/O, đa luồng hoặc dung lượng RAM.

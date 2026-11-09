# Thời gian chạy integration native

`vpp-integration` dùng compiler V++ tự biên dịch cho 192 golden fixtures và
giữ 22 fixture phụ thuộc VM trong lane C++ tường minh. Lượt CI cũ trước
commit `6e5c524` chủ yếu chạy qua VM C++; thời gian hai lượt không còn đo
cùng một lượng công việc. Mỗi fixture native phải đọc import graph, phân
tích/liên kết, hạ Machine IR, phát mã máy/ASM, assemble/link rồi chạy.

## Điểm nghẽn và bản sửa

- Kiểm tra symbol từng CALL trước đây quét bảng 63 ký tự cho mỗi ký tự
  của symbol, cả khi kiểm tra CFG và khi xuất ASM. Kiểm tra khoảng byte
  ASCII giữ nguyên hợp đồng `[A-Za-z_][A-Za-z0-9_]*` và bỏ vòng lặp lồng.
- Một batch nay giữ một tiến trình compiler V++. Bộ đệm do V++ quản lý
  tái sử dụng thân ASM khi toàn bộ Machine IR và target giống nhau. Key
  bao gồm frame, instructions, immediates, function/string IDs và symbols.
  Chỉ header mang tên hàm được dựng lại; relocation và metadata giữ nguyên.
- Memo chỉ sống trong phiên, giới hạn 64 MiB cho key và text. Không lưu
  executable hoặc kết quả golden. Nguồn, import graph, liên kết và lowering
  vẫn được xử lý lại; chương trình của mỗi fixture chạy trong process riêng.
  `VPP_NATIVE_NO_CACHE=1` tắt memo để đối chiếu.
- `run_tests.sh` chạy tối đa ba worker mặc định: hai lane độc lập và một
  lane có thứ tự. `VPP_NATIVE_TEST_WORKERS=1..4` điều chỉnh ngân sách.
  CTest đặt `PROCESSORS=3` để integration không bị coi là test một CPU.
- Các fixture sự kiện, phân tích cú pháp khai báo FFI, tiện ích định dạng
  mạng và mô phỏng lượng tử chỉ xử lý dữ liệu trong bộ nhớ được xếp vào
  lane độc lập. Bộ lọc keyword vẫn giữ file/network/database và những
  fixture có side effect ở lane tuần tự.
- 13 fixture strict POSIX/FFI native nay dùng một batch compiler với
  `VPP_STRICT_SYSTEM_FFI_POSIX=1`, mỗi fixture vẫn có status/output riêng
  và so với golden. Phép thử VM/JIT strict và legacy opcode giữ nguyên.
- Khi một worker lỗi, runner dừng các lane còn lại và cả cây tiến trình.
  Cleanup HTTP cũng dừng compiler/executable con, tránh server còn sót làm
  lần chạy sau đọc nhầm `/health`. Tiến trình được chọn theo PID con, không
  theo tên command.
- macOS ARM64 có đường assemble/link trực tiếp và cache object bootstrap
  bất biến. `VPP_AOT_DIRECT_LINK_CPP=0` giữ đường CMake để đối chiếu.
  Coverage/sanitizer vẫn dùng CMake với đúng thư viện instrumented.
- Đường CMake linker của Unix (đặc biệt Linux CI) giữ dự án đã configure
  trong cùng tiến trình compiler V++; mỗi fixture vẫn ghi ASM, tạo object
  và executable riêng. Cache invalidated khi thay đổi root, thư viện,
  cấu hình build (gồm cờ coverage/sanitizer), toolchain hoặc runtime.
  Khóa cache còn theo dõi kích thước và mtime các archive/thư viện và
  runtime object để tránh reuse khi thư viện thay đổi giữa hai fixture.
  `VPP_AOT_LINKER_CACHE=0` tắt reuse để kiểm tra parity và bisect.
- Worker nền dùng process group riêng để có thể hủy cả tiến trình con,
  kể cả trong môi trường không cho phép `pgrep` hoặc `ps`.
- CTest đặt `PROCESSORS=2` cho các suite native tốn CPU/khả năng tranh
  tài nguyên (driver, VM ASM, leaf/object parity, dynamic AOT, bootstrap,
  benchmark). Các job `--parallel 2` chỉ chạy một suite như vậy mỗi lúc,
  tránh hàng loạt child timeout 30–600 giây khi native link/bootstrap cùng
  tranh CPU. Tất cả test vẫn được chạy đầy đủ ở lane bình thường.
- Coverage build `VPP_BUILD_NATIVE_CLI=OFF` gắn nhãn `native-toolchain`
  dựa trên `FIXTURES_REQUIRED`/`FIXTURES_SETUP` và loại trừ consumer của
  native bootstrap; không còn chạy compiler-workers khi compiler chưa được
  xây trong build-coverage.

Trong quá trình kiểm tra toàn bộ còn phát hiện lỗi AArch64 có sẵn: peephole
đã bỏ LoadSlot/StoreSlot quanh phép nhân nhưng emitter MUL không nhận nguồn
và đích thay thế. Bản sửa truyền đủ các slot đã chọn, khớp oracle C++.
`import_main`, vòng lặp ba chiều và fixture bốn tham số bắt được lỗi này.

Cache IR/object của compiler i32 trong `gói/biên dịch/bản địa` thuộc đường
biên dịch khác. Thư mục `.vpp-native-cache` đã có dữ liệu không có nghĩa
pipeline ASM đầy đủ trước đây đã tái sử dụng được Machine IR/ASM.

## Phép đo cục bộ ngày 2026-11-09

macOS ARM64, build Release, cùng đường direct link. Đây là số đo cục bộ;
không dùng để suy ra trực tiếp thời gian Linux/Windows trên GitHub Actions.

| Fixture | Trước sửa symbol | Sau sửa symbol |
| --- | ---: | ---: |
| `kiem_tra_trang_thai_oop.vi` | 23,31 s | 14,07 s |
| `kiem_tra_selfhost_i32_artifact.vi` | 37,66 s | 21,82 s |

Trong hồ sơ OOP, tổng phát mã máy giảm từ khoảng 7,91 xuống 3,75 giây,
xuất ASM từ 5,18 xuống 0,77 giây. Frontend/linker vẫn chiếm khoảng 5 giây.
Trong batch OOP → tiện ích OOP → OOP, lần OOP lặp lại có toàn bộ 674 thân
hàm hit memo; thời gian machine/ASM của các thân đó bằng 0 trong profiler.
Đây là kiểm chứng reuse, chưa phải số đo toàn bộ integration trên CI.

Trong bài smoke test CMake fallback trên macOS ARM64, fixture thứ hai dùng
linker cache mất **657 ms**, còn khi tắt cache mất **1195 ms**. Đây là
số đo nhỏ, chỉ minh họa khoản tiết kiệm configure/bootstrap của một fixture;
không thay cho benchmark toàn bộ integration Linux. Regression test
`vpp-native-linker-cache-parity` đã xác nhận cả hai mode xuất hai chương
trình khác nhau với kết quả 11, 22, đúng với expected.

Lượt kiểm chứng đầy đủ trước khi tinh chỉnh lại phân loại fixture và strict
POSIX batch: `vpp-integration` **PASS 380/380** trong **1047,55 giây**
(macOS ARM64). So với lần CI thất bại 4949,16 giây là mức giảm khoảng
4,7 lần, nhưng hai lượt chưa chạy trên cùng loại GitHub runner. Cần đo
trên cùng nền tảng với run tham chiếu `41276324989` để kết luận hiệu quả
toàn CI; không được coi đây là đã khôi phục hoàn toàn thời gian cũ.

Lượt chạy mới nhất sau khi bổ sung linker cache (macOS ARM64):
`vpp-integration` **PASS 373/373, FAIL 0**, trong **986,57 giây**;
bao gồm bước bootstrap, CTest chạy hai test trong **1000,05 giây**.
Lượt này có các bài socket bị bỏ qua do môi trường hạn chế localhost bind,
vì vậy số bài PASS ít hơn lượt 380/380 không có nghĩa là mất bài kiểm thử
trong mã nguồn. Cần chạy lại trong môi trường cho phép bind để xác nhận
đầy đủ socket/HTTP. Các thời gian 1047,55 và 986,57 giây biến động theo
điều kiện chạy, chưa chứng minh riêng mức cải thiện của linker cache.

`vpp-native-leaf-vpp-parity` còn giảm số lần khởi động compiler V++:
16 trường hợp lowering (11 hợp lệ, 5 không hợp lệ) được chạy trong một
batch và 198 phép thử ISA emit trong một batch nữa. Mỗi kết quả vẫn được
đối chiếu với oracle C++ và byte mã máy mong đợi; phần giảm thời gian
đến từ việc loại bỏ khởi động compiler lặp lại, không phải thay đổi oracle.

| Suite kiểm tra riêng trên macOS ARM64 | Sau sửa | Lượt CI/lần thử cũ có tranh chấp |
| --- | ---: | ---: |
| `vpp-native-compiler-driver` | PASS 139,52 s | timeout ở 920,49 s |
| `vpp-native-dynamic-vpp-runtime` | PASS 130,55 s | timeout ở khoảng 1036 s |
| `vpp-benchmark-vpp-tool` | PASS 46,91 s | timeout ở khoảng 948 s |
| `vpp-native-object-parity` | PASS 45,69 s | khoảng 899,14 s |
| `vpp-native-leaf-vpp-parity` | PASS 4,13 s | 2010,17 s (Linux CI) |

Các số trong hai cột đo ở nền tảng/tải đồng thời khác nhau; đây là dấu
hiệu loại bỏ điểm nghẽn và timeout, **không phải** benchmark A/B cùng runner.

## Kiểm chứng và lưu bằng chứng

Sau bản sửa cache metadata và cấu hình processor (macOS ARM64, 2026-11-09),
`cmake --build build --parallel 3` PASS. Một lượt CTest chọn lọc PASS 7/7,
218,89 giây (gồm bootstrap, `vpp-vm-asm` 166,04 giây,
`vpp-selective-imports` 17,73 giây, batch/linker parity và worker scheduler).
Coverage configure probe xác nhận `vpp-compiler-workers-parity` được gắn
`LABELS native-toolchain`. Đây không phải bằng chứng full CTest hoặc GitHub
Actions đã PASS; các suite native dài vẫn cần kiểm chứng riêng.

```sh
cmake --build build --parallel 3
ctest --test-dir build --output-on-failure --parallel 2 \
  -R '^vpp-(native-machine-vpp-parity|native-object-parity|native-machine-lowering-parity|native-batch-parity|native-linker-cache-parity|integration-backend|integration-workers|selective-imports)$'
ctest --test-dir build -R '^vpp-integration$' --output-on-failure
```

`vpp-native-batch-parity` đối chiếu compiler riêng từng file với batch có/
không cache, sửa import giữa các request, lỗi compile/runtime, UTF-8, cwd
và fail-fast. Fixture ASM so sánh toàn bộ text cold/warm/disabled trên
AArch64 Mach-O/ELF và x86-64 ELF/COFF, gồm đổi IDs và xóa memo khi đầy.

`VPP_COMPILER_PROFILE_FILE` ghi thời gian từng phase và bộ đếm memo. Runner
ghi `.milliseconds` cho từng fixture, in 15 fixture chậm nhất và lưu toàn
bộ `timings-*.tsv` tại `VPP_TEST_LOG_DIR` hoặc `build/integration-logs`.
CI Linux/macOS lưu TSV thành artifact cả khi test thành công, giữ 14 ngày.
Lượt lỗi giữ cả output/status/log tại thư mục `run-*`; CI upload thư mục này
cùng `Testing/Temporary/LastTest.log` để phân biệt compiler chậm, test runtime
chậm và chờ HTTP. Xác nhận đầy đủ HTTP/socket cần môi trường cho phép bind.

Để tìm thân hàm tốn thời gian phát mã, `VPP_COMPILER_PROFILE_FILE` còn ghi
`asm.body.function` theo function ID/tên, tách `lower_ms`, `machine_ms` và
`assemble_ms`. Trên `kiem_tra_selfhost_i32_artifact.vi` (macOS ARM64), lượt
profile ghi 449 thân, trong đó có 419 hàm có tên: lowering 5,021 s, phát mã
máy 6,354 s, định dạng ASM 1,300 s; toàn phase `asm.total` là 13,836 s.
Hai phase lớn ngoài ASM là `source.graph` 4,353 s và `source.link` 3,338 s.

Thân tốn nhất là `Bộ Phát Bytecode.biên dịch phát mã IR một`
(ID 261): 799,6 ms tổng, gồm 339,6 ms lowering, 384,5 ms phát mã máy và
75,5 ms định dạng ASM. Tiếp theo là `biên dịch nội tại` (ID 194), 513,5 ms,
trong đó 321,0 ms ở phát mã máy. Đây là thời gian biên dịch các thân hàm
này, không phải thời gian chúng chạy. `machine_ms` là 46% `asm.total`, nên
emitter vẫn là mục tiêu đáng tối ưu; tuy nhiên lượt này chưa so sánh A/B với
phiên bản emitter cũ và chưa chứng minh mức tăng tốc của biểu diễn opcode số.
Đây là số đo có bật profiler, không so sánh trực tiếp wall time với lượt
không profiler. Không bật `VPP_AOT_CPU_PROFILE`: nó đo các hàm AOT của chương
trình đầu ra, không phân rã hàm của compiler, và làm thay đổi đáng kể thời
gian các phase.

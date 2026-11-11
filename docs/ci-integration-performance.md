# Thời gian chạy integration native

`vpp-integration` dùng compiler V++ tự biên dịch cho 191 golden fixtures và
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
  Với đường byte, chỉ header mang tên hàm được dựng lại. Đường ASM trực tiếp
  đưa tên hàm vào key vì thân hàm chứa các nhãn riêng theo tên.
- Memo thân ASM chỉ sống trong phiên, giới hạn 64 MiB cho key và text.
  Khi executable cache chưa trúng, nguồn, import graph, liên kết và lowering
  vẫn được xử lý lại; chương trình của mỗi fixture chạy trong process riêng.
  `VPP_NATIVE_NO_CACHE=1` tắt memo để đối chiếu.
- Mỗi lượt integration chia sẻ một executable cache tạm. Khóa liên tiến trình
  và xuất bản nguyên tử tránh assemble/link lại cùng một ASM trong các worker.
  Source index kiểm tra nội dung nguồn, dependencies thực tế và các đường
  import ưu tiên cao hơn từng vắng mặt; nguồn không đổi có thể bỏ qua cả
  frontend và sinh ASM. Không cache stdout/status: mỗi lượt vẫn chạy chương
  trình trong tiến trình mới với cwd/environment hiện tại.
- `run_tests.sh` chạy bốn worker mặc định; các fixture có trạng thái ngoài
  được giữ thứ tự trong một lane. `VPP_NATIVE_TEST_WORKERS=1..4` điều chỉnh
  ngân sách. CTest đặt `PROCESSORS=4` cho integration.
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
- Đường CMake linker của Unix và Windows giữ dự án đã configure
  trong cùng tiến trình compiler V++; mỗi fixture vẫn ghi ASM, tạo object
  và executable riêng. Cache invalidated khi thay đổi root, thư viện,
  cấu hình build (gồm cờ coverage/sanitizer), toolchain hoặc runtime.
  Khóa cache còn theo dõi kích thước và mtime các archive/thư viện và
  runtime object để tránh reuse khi thư viện thay đổi giữa hai fixture.
  `VPP_AOT_LINKER_CACHE=0` tắt reuse để kiểm tra parity và bisect.
  Windows theo dõi cả archive `.lib`, object chương trình `.obj` và executable
  `bin/Release/vpp-aot-program.exe`; bootstrap object được giữ, còn object và
  executable của fixture trước bị xóa trước khi build fixture tiếp theo.
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

## Điều tra Actions ngày 2026-11-10

Lượt mới nhất [42742446279](https://github.com/winbiru/VXX/actions/runs/42742446279)
chạy commit `dccb014`. Workflow benchmark riêng pass nhưng C/C++ CI thất bại:

| Job | Thời gian integration | Kết quả |
| --- | ---: | --- |
| Linux ARM64 | 1798,90 s | 378 pass, 2 fail |
| Linux x86-64 | 2252,62 s | 378 pass, 2 fail |
| macOS | 1636,17 s | 378 pass, 2 fail |
| Windows | Chưa hoàn tất | Bị hủy khi integration đang chạy |

Cả hai lỗi trên ba nền tảng là cùng fixture lexer, ở interpreter và JIT:
`kiem_tra_bien_dich_tu_vung_vi.expected` thiếu 16 dòng `1` tương ứng với các
assertion đã có trong source. Bổ sung golden; đầu ra interpreter và JIT cục bộ
đều khớp. Đây là lỗi dữ liệu kỳ vọng, không giải thích thời gian compile native.

Đối chiếu cùng job Linux ARM64 với
[run 42446806792](https://github.com/winbiru/VXX/actions/runs/42446806792/job/128627336398)
ở commit `5fb5495`: integration cũ là 254,93 s và dùng `--compiler=cpp`.
Commit `6e5c524` chuyển golden tests sang self-hosted V++; hai lượt có lượng
công việc và số fixture khác nhau, nên tỷ lệ khoảng 7 lần này không phải phép
đo A/B của cùng pipeline.

Bản sửa mới giảm xử lý lặp khi chuẩn hóa và tra import: giữ chuỗi POSIX,
cắt đoạn thay vì nối ký tự, thay dấu phân cách Windows bằng primitive chuỗi,
và lập chỉ mục `(importer, specifier)` cho một lần dựng graph. Chỉ mục được
reset mỗi request; nguồn/import thay đổi giữa hai request vẫn được đọc lại.

Benchmark bốn fixture trên macOS ARM64, cùng batch native và direct linker:

| Fixture | Trước (ms) | Sau (ms) |
| --- | ---: | ---: |
| `kiem_tra_stdlib` | 34556 | 33011 |
| `kiem_tra_package_modules` | 32984 | 31141 |
| `kiem_tra_su_kien` | 34829 | 33180 |
| `kiem_tra_chuoi_nen_tang` | 36201 | 33191 |
| Tổng thời gian fixture | 138570 | 130523 |

Đầu ra và status giống từng byte; tổng giảm 5,81%. Đây là một lượt đo trước/
sau cục bộ, chưa chứng minh độ ổn định thống kê hoặc cải thiện toàn CI. Phép
đo này có trước thay đổi linker Windows và primitive thay dấu phân cách.

Điểm nghẽn Windows còn thấy trực tiếp trong source: cache dự án linker bị
loại bởi `#if !defined(_WIN32)`, khiến mỗi fixture configure CMake và dựng
bootstrap lại. Bản sửa bật reuse cho Windows và mở
`vpp-native-linker-cache-parity` trên mọi nền tảng, có chuẩn hóa CRLF khi
đối chiếu bằng chứng. Cần chạy CI Windows để xác nhận mức giảm thời gian;
kiểm chứng cục bộ chỉ bao phủ compiler macOS và CMake linker trên macOS.
Workflow Windows giữ artifact cả khi thất bại lẫn bị hủy, để không mất log
shard khi CTest chưa kịp in kết quả.

Kiểm chứng cục bộ sau bản sửa trên macOS ARM64:

- Build Release: PASS.
- Bootstrap, batch parity, linker cache parity và native lint: CTest PASS 4/4
  trong 64,83 s. Kiểm tra lại lint sau khi thêm đường dẫn Unicode/UNC: PASS.
- Toàn bộ `vpp-integration`: **PASS 380/380, FAIL 0**, 894,64 s;
  cùng bootstrap là 907,83 s. HTTP/socket đã chạy, không bị bỏ qua do bind.
  TSV: `build/integration-logs/timings-20261110-121812-19089.tsv`.

Đây là kiểm chứng integration cục bộ, không phải full CTest hay một lượt
GitHub Actions mới. Không so sánh trực tiếp 894,64 s với runner macOS CI
để kết luận phần trăm tăng tốc; Windows vẫn cần được xác minh trên CI.

### Cân bằng thời gian các native golden test

`run_tests.sh` dùng `scripts/quality/shard_native_golden.py` để xếp các
fixture theo thời gian tham khảo đo trên macOS ARM64
(`scripts/quality/native_golden_costs.tsv`). Test mới hoặc chưa có số đo
được ước lượng 2 giây; cùng một đường dẫn fixture luôn được xếp chung worker
để tránh ghi cùng tệp output song song. Test có trạng thái bên ngoài vẫn giữ
nguyên thứ tự trong worker 0. Các test thuần dữ liệu được phép chạy trên cả
worker 0 nếu lane tuần tự còn dư thời gian, đồng thời được giữ thứ tự gốc
trong từng worker. Khi không có Python 3, bộ chạy dùng cách chia cũ với lane
tuần tự riêng và round-robin cho hai worker còn lại.

Mô phỏng trên 130 fixture độc lập với timing cục bộ ngày 10/11:

| Cách chia | Worker 1 | Worker 2 | Đường chạy dài nhất |
| --- | ---: | ---: | ---: |
| Chia vòng cũ | 495,82 s | 638,54 s | 638,54 s |
| Chia theo trọng số | 568,34 s | 566,03 s | 568,34 s |

Giảm khoảng 70,2 giây **theo mô phỏng phân chia tải**; chưa cộng thời gian
khởi động, hiệu ứng cache, tranh chấp CPU, và chưa phải số đo thực tế sau sửa.
Unit test của bộ chia được chạy trong job `quality`.

Điều chỉnh tiếp theo dùng số đo của lượt macOS ARM64 đạt 380/380 (12:18
ngày 10/11) để bổ sung chi phí tham khảo cho toàn bộ 191 native fixtures,
bao gồm các ca HTTP/socket. Sau khi rà soát source, 9 fixture chỉ xử lý dữ
liệu, giao thức hoặc quoting trong bộ nhớ được chuyển khỏi danh sách bắt buộc
tuần tự. Các ca đọc/ghi tệp, tạo process/socket và dùng trạng thái ngoài
tiếp tục nằm trong lane tuần tự. Phân bổ mới có 52 fixture ghim thứ tự và
139 fixture linh hoạt:

| Mô phỏng cùng trọng số | Worker 0 | Worker 1 | Worker 2 | Critical path |
| --- | ---: | ---: | ---: | ---: |
| Lane tuần tự riêng + 2 shard | 506,81 s | 592,67 s | 593,12 s | 593,12 s |
| Lane tuần tự được bổ sung việc độc lập | 564,20 s | 564,20 s | 564,19 s | 564,20 s |

Giảm **28,92 giây theo mô phỏng**. Bản ghi timing là thời gian từng fixture
trên một macOS ARM64; phép cộng không tính tranh chấp CPU, các hiệu ứng cache
giữa fixtures hay thời gian khởi động. Chưa chứng minh được thời gian thực tế
sau thay đổi; cần A/B trên cùng runner và đủ HTTP/socket trước khi kết luận
về tốc độ CI.

Hàm `biên dịch đồ thị đích nhập` đã bỏ vòng dựng lại toàn bộ prefix ở mỗi dấu
`/`, nay tìm vị trí dấu phân cách cuối rồi cắt chuỗi một lần. Đây là tối ưu
đường import, không đổi thuật toán chuẩn hóa và báo lỗi.

Kiểm chứng sau chỉnh sửa (cùng máy macOS ARM64, 10/11):

- Unit test sharding: **5/5 PASS**; `bash -n` và `git diff --check`: PASS.
- CTest bootstrap/VM ASM/selective imports/batch/linker parity/worker:
  **8/8 PASS**, 260,84 giây.
- Full `vpp-integration`: **373/373 PASS, 0 FAIL**, 843,67 giây;
  cùng bootstrap trong CTest: **857,69 giây**.
- Log timing: `build/integration-logs/timings-20261110-141408-62161.tsv`.

Lượt này không mở được socket localhost, nên bỏ qua các kiểm tra socket/HTTP
tương ứng. Lượt trước trên cùng máy với số assertion tương ứng mất 911,31
giây; chênh lệch **67,64 giây (7,42%)** theo hai lần chạy thực tế, nhưng
chưa phải A/B kiểm soát tải máy, bộ nhớ đệm và biến động môi trường. Không
được suy ra rằng riêng scheduler hoặc riêng xử lý import đã tạo mức giảm đó.
Windows và toàn bộ GitHub Actions vẫn chưa được xác minh sau chỉnh sửa.

Trên Linux x86-64, `vpp-vm-asm-selfhost` mất 2346,52 s ở run 42742446279,
và `vpp-integration` mất 2252,62 s. Workflow giờ chạy phép kiểm tra selfhost
ASM VM ở job `vm-asm-selfhost` riêng; CTest tự kích hoạt các fixture bootstrap
và `vpp-vm-asm` cần thiết. Job build chính giữ tất cả test còn lại, không bỏ
kiểm thử selfhost. Cách tách này tăng tổng thời gian máy runner do phải build
và bootstrap riêng nhưng giảm thời gian đợi CI tổng thể khi GitHub cấp runner
chạy hai job đồng thời. Chưa có số liệu CI mới để định lượng mức giảm.

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

## Tối ưu API từ điển và tạo Machine IR ngày 2026-11-10

Các wrapper hàm `lấy/đặt/có/xóa/khóa từ điển` gọi primitive V++ trực tiếp,
không khởi tạo `Tiện Ích Từ Điển` mỗi lần. Hai wrapper `biên dịch mã máy trả về`
và `biên dịch mã máy lệnh` cũng tính trực tiếp, giữ cùng phép kiểm tra biên i32,
trường Machine IR và giá trị trả về của method tương ứng. Lớp cũ vẫn giữ
API hướng đối tượng; khi sửa nghiệp vụ cần cập nhật cả hai cách gọi.

Đối chứng A/B của riêng thay đổi từ điển trên `kiem_tra_stdlib.vi`, một máy
macOS ARM64, cùng cấu hình self-host, bật profiler và dùng hai lượt mỗi phía:

| Trường hợp | Lượt 1 (ms) | Lượt 2 (ms) | Trung bình (ms) |
| --- | ---: | ---: | ---: |
| Wrapper cũ tạo instance | 34077 | 33376 | 33726,5 |
| Wrapper trực tiếp | 33415 | 33348 | 33381,5 |

Chênh lệch trung bình khoảng 345 ms (~1,02%). Đầu ra sau khi loại các dòng
`VPP_NATIVE_PROFILE` và mã thoát giống nhau; đây là một mẫu nhỏ, chưa đủ
chứng minh mức cải thiện ổn định trên toàn CI.

Sau khi thêm hai wrapper Machine IR trực tiếp, một lượt `kiem_tra_stdlib.vi`
đạt 33596 ms; `asm.functions` là 16851,46 ms, so với 16970,22 ms của lượt
wrapper từ điển ở trên. Cùng batch, `kiem_tra_package_modules.vi` đạt 31208
ms. Cả hai khớp golden sau khi bỏ dòng profiling. Chưa có A/B nhiều lượt
cho riêng thay đổi Machine IR nên không quy chênh lệch cho tối ưu này.

CTest được chạy với 8 nhóm parity/import/linker/integration hỗ trợ và các
fixture bootstrap phụ thuộc: **11/11 PASS**, tổng thời gian **311,36 s**.
Unit test chia shard **5/5 PASS**; `bash -n`, `git diff --check` và so
golden của hai fixture tự biên dịch đều PASS. Đây là kiểm chứng cục bộ trên
macOS, chưa có số liệu từ GitHub Actions cho revision đang sửa.

Profiler trên `kiem_tra_stdlib.vi` vẫn cho thấy khoảng 14 s dành cho
`source.graph` + `source.link` và 18 s cho `asm.total`, với 1402 cache miss
trên 1470 thân hàm. Mức cải thiện lớn hơn cần hướng đến tái sử dụng AST/module
đã phân tích giữa các fixture có cơ chế invalidation, hoặc giảm số hàm phải
đưa qua emitter; phải bảo toàn import, reflection, initializer và tự biên dịch
thuần V++ trước khi triển khai loại bỏ hàm không dùng.

## Cache cây cú pháp trong phiên native compiler (2026-11-10)

Compiler V++ lưu bản snapshot AST của các module đã phân tích trong cùng một
phiên batch, kiểm tra **toàn bộ nội dung nguồn** ở lần dùng tiếp theo và sao
chép map/list trước khi đưa cho linker (linker có thể sửa AST khi xử lý import).
File thay đổi được phân tích lại; parse thất bại không được ghi vào cache.
Cache giới hạn 128 module, 1 MiB **văn bản nguồn** tổng cộng và 192 KiB mỗi
module. Đây chưa phải giới hạn chính xác cho bộ nhớ AST, vốn có thể lớn hơn.
Các bộ đếm `source.ast.hits`, `source.ast.misses`, `source.ast.bytes` xuất hiện
trong `VPP_COMPILER_PROFILE_FILE`.

Đối chiếu trên cùng macOS ARM64, cùng thứ tự hai fixture, mỗi bản compiler
chạy một phiên batch, có bật profiler; số liệu là một mẫu, chưa phải median:

| Fixture | Trước cache AST (ms) | Có cache AST (ms) |
| --- | ---: | ---: |
| `kiem_tra_su_kien.vi` (cold) | 31582 | 33283 |
| `kiem_tra_chuoi_nen_tang.vi` (warm) | 30765 | 27194 |
| **Tổng** | **62347** | **60477** |

Lượt warm có 93 cache hits, 1 cache miss; thời gian `source.graph` giảm từ
5206,73 ms xuống 1176,29 ms. Tổng hai fixture giảm khoảng **3,0%**, trong khi
lượt cold tăng khoảng **5,4%** do chi phí tạo snapshot. Hai fixture đều trả
về exit 0 và stdout trùng khớp từng byte. Đây chưa phải số đo GitHub Actions
hay bằng chứng tăng tốc trên mọi workload.

`check_native_batch.cmake` bổ sung lượt `before_repeat.vi` để xác nhận cache
AST thực sự có hit, đồng thời kiểm tra invalidation sau khi ghi lại import,
batch có/không cache, lỗi parser/runtime và fail-fast. Bản đầu tiên dùng
chung tên biến `mục` cho snapshot cache và vòng lặp xử lý `chính.vi`, khiến
compile sai; đã tách thành `mục cache` để tránh ghi đè. Driver native cũng
nhận trực tiếp `--batch-run-tests --compiler=vpp` mà không hiểu nhầm flag là
thư mục đầu ra; `--compiler=cpp` bị từ chối khi gọi native trực tiếp.

## Kiểm chứng tiếp theo trên macOS ARM64, 2026-11-10

Sau khi áp dụng cache AST giới hạn, chia tải theo trọng số và các tối ưu
native đã mô tả, lượt `vpp-integration` cục bộ gần nhất **PASS 373/373,
FAIL 0** trong **682,04 giây**; gồm bước bootstrap, CTest mất **695,45
giây**. Timing lưu tại
`build/integration-logs/timings-20261110-160531-2704.tsv`. Một lượt
373/373 trước đó mất 843,67 giây, tương ứng thời gian giảm 161,63 giây
(19,2%) giữa hai lượt chạy. Đây là số quan sát trên cùng loại máy nhưng
**chưa phải A/B kiểm soát tải và cache**, và vẫn bỏ qua một số bài HTTP/socket
do sandbox không cho bind cổng. Lượt 380/380 trước đó là bằng chứng những
bài HTTP/socket này từng được kiểm tra trong môi trường cho phép bind.

Năm CTest chọn lọc liên quan đến bootstrap, linker, batch và worker **PASS
5/5** trong **57,62 giây**. Bộ kiểm tra Python chia shard **PASS 7/7** và
`git diff --check` PASS.

Runner Windows mới thực hiện phân chia native golden thành các phiên compiler
dài hạn, thay cho các batch tuần tự 12 fixture. Trước khi tạo tiến trình,
runner kiểm tra source trùng, tên output trùng (không phân biệt hoa/thường),
fixture lạ, thiếu hoặc được gán lặp trong shard; các trường hợp này kết thúc
sớm với thông báo rõ ràng. Khi worker lỗi hoặc thiếu file timing, runner báo
đúng lỗi và giữ log phục vụ phân tích. **Chưa xác minh runner Windows thực tế**
do máy macOS không có PowerShell; cũng chưa có full CTest hoặc GitHub Actions
trên tất cả nền tảng cho revision đang sửa. Cần một run CI mới để xác nhận
thời gian Windows và so sánh trên cùng GitHub runner.

## Phát ASM trực tiếp cho CLI AOT, 2026-11-10

`build/run/artifact` mặc định in mnemonic thẳng từ Machine IR do
`Bộ Hạ ASM Động` vừa tạo, rồi chuyển cho assembler/linker. Không chạy CFG
validator và byte encoder thêm một lượt trên các thân động nội bộ này,
không dựng danh sách byte và không chuyển byte ngược thành `.byte/.long`.
Các hàm i32 chuyên biệt cùng bộ chọn `fast_i/fast_m/fast_s/ASCII` hiện cũng
phát trực tiếp từ Machine IR, nhận đúng tham số theo ABI và xử lý chia/dư i32.
Các API raw/object và phép đối chiếu object độc lập vẫn giữ byte encoder.
`VPP_AOT_ASM_DIRECT=0` chọn lại đường cũ để bisect;
cờ này nằm trong khóa executable cache để hai chế độ không dùng nhầm artifact.

Bộ in chỉ phát nhãn ở đích nhảy. Nhóm đối số scalar liền nhau được đưa thẳng
vào thanh ghi/stack ABI, không ghi rồi đọc lại eval stack. Chuỗi kiểm tra
trạng thái `Call; BranchZero success; Jump error` dùng trực tiếp thanh ghi
trả về; không ghép qua điểm vào của nhánh khác. ARM64 giữ các frame nhỏ trong
W19..W28; frame lớn và đối số stack theo đúng ABI macOS/Linux/Win64.

Một lượt A/B cục bộ trên macOS ARM64, cùng native compiler, cùng fixture
`kiem_tra_package_modules.vi`, cache executable tắt, bật cả hai profiler:

| Công đoạn | Qua byte encoder | ASM trực tiếp |
| --- | ---: | ---: |
| Frontend + sinh ASM | 32,809 giây | 22,841 giây |
| Riêng sinh ASM (`asm.total`) | 18,384 giây | 8,806 giây |
| Assembler | 0,234 giây | 1,758 giây |
| Linker | 0,048 giây | 0,050 giây |
| Tổng wall time biên dịch | **33,157 giây** | **24,727 giây** |

Tổng giảm **25,4%** dù assembler cần nhiều thời gian hơn để đọc mnemonic.
Hai executable đều exit 0 và stdout trùng golden. Đây là một lượt đo cục bộ,
chưa phải median hoặc số liệu GitHub Actions. Phần `source.graph/source.link`
vẫn còn khoảng 14 giây và chưa được tối ưu thêm bởi thay đổi này.

`vpp-native-real-assembler-smoke` assemble ASM trực tiếp cho ARM64 macOS/Linux
và x86_64 Linux/Windows, rồi chạy C harness trên host để kiểm chứng đối số
vượt thanh ghi, CFG join, nhánh lỗi, frame lớn, hàm i32 có 9 tham số, phép
chia/dư có dấu và lời gọi lồng nhau. Batch parity so đầu ra đường ASM trực
tiếp với đường byte encoder; cache cold/warm được so riêng cho cả hai.

### Mở rộng trực tiếp tới các bộ chọn và hàm chuyên biệt

Lượt đo thêm trên `kiem_tra_native_control_flow.vi` với `--compiler=vpp`,
`VPP_NATIVE_NO_CACHE=1`, cùng binary và host macOS ARM64. Hai chế độ cho
stdout giống nhau và exit 0. Số liệu **một lần mỗi chế độ** (chưa phải median):

| Công đoạn | Byte encoder | ASM trực tiếp toàn AOT |
| --- | ---: | ---: |
| `asm.fast_paths` | 58,514 ms | 16,395 ms |
| `asm.body.machine_ms` | 90,668 ms | 0,236 ms |
| `asm.functions` | 178,032 ms | 83,601 ms |
| `asm.total` | **249,855 ms** | **113,375 ms** |
| Wall time cả chạy và biên dịch | 1,70 s | 1,57 s |

Riêng `asm.total` giảm khoảng **54,6%** trong mẫu này. Frontend vẫn mất
`source.graph` khoảng 47 ms và `source.link` khoảng 29 ms; đây là giới hạn
còn lại ngoài phần sinh mã. Khác biệt toàn bộ wall time nhỏ hơn vì còn
assembler/linker/runtime startup và chịu nhiễu của một lượt đo.

### ARM64 leaf functions: tính trực tiếp trên thanh ghi

Với các hàm i32 không gọi ABI, có tối đa 8 tham số và tối đa 16 slot,
backend ARM64 dùng W0–W8/W11–W17 và không tạo stack frame. W9/W10 giữ
vai trò scratch; `Mod` với trên 9 slot chuyển về frame thông thường vì
cần W11 cho thương số tạm. Trong nhánh leaf, `PushConstant`, `LoadSlot`,
`StoreSlot`, `BranchZero`, `Return`, các phép toán và phép so sánh có thể
đọc/ghi thẳng thanh ghi slot, bỏ các lượt `mov` qua W9/W10. `Div/Mod` vẫn
giữ xử lý biên và chia cho 0 của backend cũ.

Ví dụ Machine IR cộng 8 tham số, giữ đồng thời 8 giá trị eval (16 slot):
ASM ARM64 giảm từ **46 xuống 17 lệnh** (giảm khoảng 63%) mà kết quả chạy
giữ nguyên. `vpp-native-real-assembler-smoke` đã chạy C harness trên macOS
ARM64 cho các trường hợp 16 slot, CFG branch, `StoreSlot`, tất cả phép số
học/bitwise/shift/so sánh, chia dư và fallback. Clang cũng chấp nhận
assembly ARM64 Linux và x86-64 Linux/Windows, nhưng đây không phải xác nhận
runtime trên ba hệ điều hành đó. Bảy suite bootstrap/AOT/leaf/batch/cache
parity đã PASS cục bộ sau tối ưu.

Đo trên macOS ARM64 với `VPP_NATIVE_NO_CACHE=1`, xen kẽ hai chế độ,
cùng compiler và cùng đầu vào; tất cả lượt chạy trả về 0 và stdout giống
nhau. Các số dưới là **median wall time của toàn lượt biên dịch và chạy**:

| Fixture | Số lượt mỗi chế độ | Byte encoder (`ASM_DIRECT=0`) | ASM trực tiếp (`ASM_DIRECT=1`) | Giảm wall time |
| --- | ---: | ---: | ---: | ---: |
| `kiem_tra_native_control_flow.vi` | 5 | 1,323 s | 1,236 s | 6,6% |
| `kiem_tra_package_modules.vi` | 3 | 29,577 s | 22,412 s | 24,2% |

Kết quả này đánh giá **toàn bộ backend ASM trực tiếp** so với byte encoder,
không tách riêng đóng góp của tối ưu leaf. Lượt CI mới trên Linux/Windows
và A/B cùng runner vẫn cần thiết để đánh giá tác động lên thời gian Actions.

### Driver AOT được điều phối bằng V++

Compiler tự chủ dùng `gói/biên dịch/trình điều khiển AOT.vi` để chọn runtime,
ABI, archive, lệnh assembler/linker và đường CMake đối chiếu. V++ chạy các
lệnh bằng thư viện tiến trình và xử lý lỗi khởi chạy/mã thoát. Trong đường
này, C++ cung cấp thư mục tạm, quan sát hệ tệp, cache object và xuất bản
executable nguyên tử; driver C++ cũ giữ vai trò bootstrap/oracle Stage 0.

`VPP_NATIVE_PROFILE=1` in marker `driver-aot-vpp` trước các pha assemble/link.
Việc đọc giá trị từ `CMakeCache.txt` quét byte một lượt, không tách lại toàn
bộ văn bản cho từng dòng. Cache object tính cả nội dung nguồn/include, target,
argv, môi trường toolchain và metadata của công cụ; project CMake được dựng
lại khi chữ ký cấu hình thay đổi.

`vpp-native-vpp-aot-driver` biên dịch driver và compiler thành Stage 2 bằng
V++ rồi dùng Stage 2 tạo/chạy executable độc lập. Test còn kiểm tra kế hoạch
macOS/Linux/Windows, lỗi công cụ, Unicode/space trong argv và bảo toàn đầu ra
cũ khi thất bại. Kiểm tra kế hoạch trên host macOS không thay cho chạy ELF/PE
trên Linux/Windows. Chi tiết: `docs/native-aot-direct-link.md`.

### Source-index FAST-HIT: điều tra ngày 2026-11-11

Khi chạy `kiem_tra_stdlib_tinh_toan.vi` với `VPP_AOT_EXECUTABLE_CACHE_DIR`
và `VPP_AOT_CACHE_TIMING_FILE` trên macOS ARM64, đường FAST-HIT từng tốn
**16,46 giây** mặc dù không gọi assembler/linker. Source-index v3 chứa
352 bản ghi (94 tệp, 258 đường import vắng), dung lượng khoảng 26 KiB.
Đo bên trong `biên dịch AOT đọc cache nguồn` cho thấy **15.757 ms** nằm
trong `tách hằng văn bản`, chỉ **560 ms** nằm trong vòng kiểm tra dependency.

`tách hằng văn bản` gọi `tìm chuỗi chính xác` cho mỗi dấu xuống dòng; hàm
tìm lại dựng chỉ mục UTF-8 cho toàn bộ input ở mỗi lần gọi. Source-index
được chuyển sang bộ tách dòng riêng viết bằng V++: quét danh sách byte UTF-8
một lượt, kết thúc dòng tại byte LF, giữ cả dòng rỗng cuối cùng. Logic xác
thực file fingerprint và negative import probes không đổi; runtime chỉ cung
cấp các primitive chuyển chuỗi/byte và thêm phần tử danh sách.

| Phép đo FAST-HIT | Trước | Sau |
| --- | ---: | ---: |
| Tách source-index | 15.757 ms | 12,5 ms |
| Xác thực toàn source-index | 16.341 ms | 605 ms |
| Thời gian chạy fixture (`elapsed_ms`) | 16.453 ms | 732 ms |
| Wall time toàn lệnh batch | 16,46 giây | 1,36 giây |

Các số liệu lấy từ hai binary bootstrap cùng source trước/sau, cùng fixture,
executable cache và cấu hình direct ASM/link; trace của cả hai lượt là
`FAST-HIT` cho cùng executable. stdout so từng byte là giống nhau.
Đây là **một lần đo mỗi phiên bản**, chưa phải median, và chỉ đo đường
cache đã trúng. Thời gian MISS và tổng integration cần được đo riêng:
với fixture chỉ chạy một lần, thời gian frontend và assembler/linker vẫn còn.

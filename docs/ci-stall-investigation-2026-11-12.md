# Điều tra CI chậm ngày 2026-11-12

Kết luận: phần tốn thời gian chủ yếu là compiler tự chủ dựng lại chương trình
cho từng fixture, đặc biệt là tạo khóa cache và ghi assembly qua các vòng lặp
V++ theo từng byte. Đây không phải thời gian chạy chương trình test. Windows
còn có một lỗi riêng khiến CTest giữ tiến trình con sống đến hạn bốn giờ.

## Bằng chứng từ Actions

### Đối chiếu với commit chạy nhanh ngày 2026-11-01

[Run 41276324989](https://github.com/winbiru/VXX/actions/runs/41276324989)
checkout PR merge `aab1a25` (head `887b861`), còn run 43019877521
checkout merge `9be8a84` (head `702d471`). Đối chiếu thời gian của cùng test
`vpp-integration` trong lượt CTest chính, không lấy lượt sanitizer Linux:

| Nền tảng | Run cũ (giây) | Run chậm (giây) | Tỷ lệ |
| --- | ---: | ---: | ---: |
| Linux x64 | 160,24 | 4068,92 | 25,39x |
| Linux ARM64 | 263,75 | 2276,85 | 8,63x |
| macOS | 229,40 | 3184,18 | 13,88x |
| Windows | 572,92 | 14400 (timeout) | 25,13x đến khi bị dừng |

**Commit thay đổi cơ chế thực thi là `6e5c524` ngày 2026-11-08.**
`run_tests.sh` của merge cũ gọi `--batch-run-tests --compiler=cpp` cho
toàn bộ golden test thông thường: VM dùng chung tiến trình và AST.
Commit này chuyển 189 golden test sang `--compiler=vpp`, yêu cầu fixture
bootstrap compiler native trong CTest và chuyển Windows sang cùng backend.
Đến `702d471`, danh sách `TESTS` tăng từ 210 lên 214 file riêng biệt
(giữ cả 210 file cũ); 192 file thuộc nhóm native và 22 thuộc nhóm VM.
Số file chỉ tăng 1,9%, không thể giải thích mức tăng 8–25 lần.

### Phép đo cô lập sự thay đổi backend

Trên cùng macOS ARM64 và source hiện tại `eee50ba` (đã có tối ưu bên dưới),
chạy cùng ba lượt fixture theo thứ tự, với executable cache rỗng ban đầu.
So sánh chỉ thay backend `cpp`/`vpp` trong batch CLI:

| Fixture | C++/VM | V++ native AOT | Tỷ lệ native/VM |
| --- | ---: | ---: | ---: |
| Package modules (lần đầu) | 2239 ms | 41471 ms | 18,52x |
| Package modules (lặp lại) | 937 ms | 776 ms | 0,83x |
| Stdlib tính toán (lần đầu) | 898 ms | 32969 ms | 36,71x |
| Wall time cả batch | 4,085 s | 75,438 s | 18,47x |

Cả hai fixture có stdout khớp từng byte giữa hai backend, status đều 0.
Lượt lặp có executable cache hit nên không đại diện cho fixture mới.
Phép đo này cô lập chi phí của đường compiler/backend trên cùng source hiện
tại; không phải benchmark binary của merge cũ hoặc phép bisect toàn lịch sử.
Kết hợp với diff `6e5c524` và log Actions, kết quả xác định việc chuyển từ
VM sang compile/link AOT từng entrypoint là nguyên nhân chính của hồi quy
integration giữa hai run. Các pha cụ thể bên dưới giải thích vì sao native
MISS đắt, còn lỗi tiến trình con Windows giải thích riêng timeout bốn giờ.

Dữ liệu phép đo: `build-ci-stall/compare-fast-commit/controlled/comparison.json`.

Đối chiếu [run 43019877521](https://github.com/winbiru/VXX/actions/runs/43019877521),
commit `702d471`, với log người dùng gửi và artifact timing/Windows diagnostics:

| Nền tảng | `vpp-integration` | Kết quả |
| --- | ---: | --- |
| Linux x64 | 4068,92 giây / 67,8 phút | PASS |
| Linux ARM64 | 2276,85 giây / 37,9 phút | PASS |
| macOS | 3184,18 giây / 53,1 phút | PASS |
| Windows | 14400 giây / 4 giờ | Timeout |

Windows mất tổng cộng 15396,44 giây cho CTest; riêng integration chiếm 93,5%.
Artifact Linux x64 có 238 timing: tổng thời gian các fixture là 12474,341 giây
(chạy đồng thời), median 16,2295 giây, 61 fixture vượt một phút. Hai fixture
điển hình là `kiem_tra_package_modules` 249381 ms và
`kiem_tra_stdlib_tinh_toan` 218231 ms. Không được cộng thời gian các worker
để coi đó là wall time.

## Đo compiler trên macOS ARM64

Chạy cùng thứ tự trong một batch: package modules, lặp lại chính fixture đó,
rồi stdlib tính toán. Mỗi phiên bản dùng executable cache rỗng riêng; các object
runtime có thể được reuse. Timing bổ sung giữ executable/source cache hoạt động;
`VPP_NATIVE_PROFILE=1` sẽ vô hiệu hóa executable cache nên không dùng để đo
đường cache thông thường.

| Công việc | Trước | Sau |
| --- | ---: | ---: |
| Package modules, MISS | 61583 ms | 39938 ms |
| Lặp cùng fixture, FAST-HIT | 774 ms | 785 ms |
| Stdlib tính toán, MISS | 52027 ms | 30803 ms |
| Khóa executable cache của package modules | 21674 ms | 14953 ms |
| Ghi assembly của package modules | 15524 ms | 193 ms |
| Assembler của package modules | 1616 ms | 1602 ms |
| Linker của package modules | 42 ms | 38 ms |

Golden output/status của cả hai phiên bản khớp. Tổng wall time batch là
115,196 → 72,366 giây, giảm khoảng 37,2%; riêng hai MISS giảm khoảng 35,1%
và 40,8%. Đây là một lượt đo trước/sau cục bộ, chưa phải median nhiều lượt
hoặc bằng chứng mức giảm tổng CI trên Windows/Linux.

Executable đã được tạo chạy riêng năm lần có median **0,006772 giây**,
đầu ra vẫn khớp golden. Trước sửa, chỉ hai pha tạo khóa cache và ghi assembly
đã chiếm **60,4%** lượt compile-and-run 61,583 giây.

## Nguyên nhân trong mã

1. **Khóa cache băm cả assembly lớn bằng V++.** `AOT mã quan sát` đóng khung
   assembly cùng cấu hình và metadata; `băm sha256` còn giải/kiểm tra UTF-8 toàn
   input trước khi SHA-256 kiểm tra và sao chép danh sách byte một lần nữa.
   CPU sample xác nhận stack đi qua `biên dịch AOT đường cache thực thi` →
   `Bộ Mật Mã.băm sha256` → `mật mã sha256 các byte`. Đây là CPU thực,
   không phải chờ mạng hay linker.
2. **Ghi assembly qua quá nhiều thao tác theo từng byte.** `ghi tệp` chuyển
   cả chuỗi thành boxed byte list; `Bộ Đệm Tệp.ghi các byte` kiểm tra rồi thêm
   từng byte với tra trường đối tượng, sau đó wrapper `tệp thô ghi` kiểm tra
   từng byte thêm lần nữa. Ghi file cục bộ mất 15,5 giây nhưng assembler/linker
   thực chỉ khoảng 1,7 giây.
3. **Fixture kéo theo một đồ thị thư viện lớn.** Package modules chỉ in ba
   dòng nhưng import nhiều package bằng `.*`. Source-index ghi 102 file và
   307 negative import probes; một lượt profiler ghi nhận 1583 thân hàm.
   AST/ASM memo trong batch không bỏ toàn bộ dựng graph, liên kết và tạo khóa
   cho mỗi entrypoint khác nhau. Vì vậy FAST-HIT dưới một giây của cùng source
   không đại diện cho hàng trăm fixture khác nhau.
4. **Cấp phát lớn làm chạy nhiều worker thêm tốn kém.** CPU sample trước sửa
   ghi physical-footprint peak khoảng 3,7 GB ở một worker; bản sau ghi peak RSS
   2798616576 byte. Hai cách đo khác nhau nên không dùng để kết luận tỷ lệ
   giảm RAM. [Runner macOS ARM64 của GitHub có 7 GB RAM](https://docs.github.com/en/actions/reference/runners/github-hosted-runners).
   Bốn worker có thể vượt RAM: đây là suy luận từ phép đo cục bộ và cấu hình
   runner, chưa có số liệu swap của chính CI để khẳng định mức đóng góp.

## Sửa và giới hạn còn lại

- Ghi văn bản theo khối byte giới hạn kích thước; vẫn giữ phần đệm cuối,
  partial I/O, lỗi có cấu trúc, append, Unicode và byte NUL. Input byte API
  vẫn được kiểm tra; primitive OS cũng kiểm tra đệm.
- Cache dùng trực tiếp SHA-256 byte API của V++, giữ thuật toán và định dạng
  khóa. SHA-256 đọc khối đầy từ input, chỉ tạo đuôi padding 64/128 byte;
  không sửa input hoặc sao chép cả thông điệp.
- Mặc định hai native worker trên macOS, bốn trên Linux; vẫn cho override.
- Windows chọn đúng một Python và kiểm tra trước khi tạo HTTP server.
  Dừng cả cây `vpp-cli → vpp-native → HTTP executable`, dùng thời hạn đợi có
  giới hạn và phát hiện worker không hoàn thành fixture trong 900 giây.
  Trước đó `Get-Command ... -CommandType Application` trả cả Python đã cài
  và WindowsApps alias; `.Source` trở thành hai đường dẫn ghép vào một lệnh.
  Cleanup chỉ kill cha để lại server con và handle kế thừa. Đây là cơ chế
  giải thích lỗi script xuất hiện nhưng CTest vẫn đợi đến 14400 giây.
- Tách batch parity, source-cache parity và ASM-cache parity thành ba test,
  mỗi test giữ timeout 300 giây và hai processor slot. Không tăng timeout
  integration; split chỉ giúp định vị lỗi, không phải sửa nguyên nhân CPU.

Khóa cache vẫn tốn khoảng 15 giây và frontend/sinh mã vẫn đáng kể trên fixture
import rộng; thay đổi này chưa loại hết chi phí self-hosting. Cần một lượt CI
mới trên từng OS để xác nhận tổng thời gian, lỗi Windows và áp lực RAM thực tế.

## Kiểm tra sau sửa

- Ba check native batch/source-cache/ASM-cache đều PASS: 19,724 / 18,750 /
  24,877 giây với compiler cuối.
- Bốn golden file I/O PASS ở interpreter và JIT, rồi PASS ở native compiler;
  gồm mixed text/byte, buffer 7 byte cắt giữa UTF-8, NUL, byte 255, ghi lớn,
  append, vị trí, đóng/mở và lỗi có cấu trúc.
- Vector SHA-256/HMAC, các biên padding và kiểm tra input không bị sửa PASS
  ở interpreter/JIT/native. Cache key Unicode/NUL khớp digest tính độc lập.
- AOT driver PASS: kế hoạch macOS/Linux/Windows, OS/file policy, lỗi launch,
  compiler Stage 2 tự chủ, argv Unicode/space và bảo toàn output khi lỗi.
- Ba CTest runtime/file/boundary, bảy Python sharder test và kiểm tra hủy
  worker Unix đều PASS.
- PowerShell regression PASS với hai application tên Python trên PATH,
  bảo toàn exit code và dừng cả cây tiến trình khi timeout. Chạy bằng
  PowerShell portable chính thức trên macOS; cần CI Windows để xác nhận
  hành vi Win32 thực tế.

## Tái hiện phép đo

Bootstrap compiler trước/sau vào hai thư mục riêng bằng
`scripts/quality/prepare_native_compiler.cmake`. Với mỗi compiler, đặt
`VPP_HOME`, `VPP_AOT_ROOT`, `VPP_AOT_EXECUTABLE_CACHE_DIR` trỏ cache rỗng riêng,
`VPP_AOT_CACHE_TIMING_FILE` và `VPP_AOT_EXECUTABLE_CACHE_TRACE`, rồi chạy:

```sh
"$compiler" --batch-run-tests "$results" \
  "$root/scripts/test/kiem_tra_package_modules.vi" \
  "$root/scripts/test/kiem_tra_package_modules.vi" \
  "$root/scripts/test/kiem_tra_stdlib_tinh_toan.vi"
```

Bằng chứng cục bộ nằm trong `build-ci-stall/measured-baseline/`,
`build-ci-stall/measured-sha-streaming/` và `build-ci-stall/evidence/`.

## Tối ưu tiếp theo, xác minh compiler tự host

So với compiler SHA scratch, ABBA trên macOS ARM64 cho cùng ba fixture
(package cold, package warm, math cold) giảm trung bình 65,845 → 51,642 s
(-21,57%) khi profiler tắt; profiler bật giảm 84,471 → 65,910 s (-21,97%).
Output/status giống hệt; cache trace MISS/FAST-HIT/MISS ở chế độ thường.
Package cold 35,904 → 28,637 s, math cold 28,895 → 21,986 s, package warm
khoảng 0,8 s. Khóa SHA và frontend/assembler vẫn chiếm phần lớn cold build.

Các thay đổi giữ lại gồm index slot/StringPool và opcode dày, gom module
cho IR worker, snapshot tối thiểu sau kiểm kiểu, ghép frame cache cân bằng
và SHA byte từ chuỗi VM. Bootstrap, batch all, object parity, worker parity,
Stage1→2→3 convergence và driver/SHA/AST isolation đều PASS. Bằng chứng:
`build-ci-stall/compiler-final-normal-abba/summary.json`,
`build-ci-stall/compiler-final-abba/summary.json`,
`build-ci-stall/compiler-final-checks/summary.json`. Chưa đo CI mới trên
Windows/Linux hoặc RSS đáng tin cậy; không nhân tỷ lệ từ các phép thử riêng.

### SHA và lexer: kết quả bổ sung

- SHA-256 unroll 8 vòng: microbench 126,7115 → 118,196 ms (-6,7%);
  ABBA batch 52,850 → 50,1985 s (-5,02%). Vector padding/Unicode/NUL,
  toàn bộ 256 giá trị byte, HMAC khóa dài và input bất biến đều PASS.
- Lexer bảng phân loại byte: microbench 94,1865 → 72,5365 ms (-22,99%);
  batch 51,1495 → 50,813 s (-0,66%, nhiễu lớn). So sánh toàn bộ 9 trường
  token trên 787817 byte output giống nhau.
- Tái sử dụng loại token đã biết: microbench 73,125 → 56,248 ms (-23,08%);
  batch ABBA 49,827 → 48,414 s (-2,84%). So sánh 791665 byte output
  lexer giống nhau, bao gồm UTF-8 lỗi, vị trí byte/ký tự, từ khóa và số.
  Cả bốn lượt batch output/status giống nhau, trace MISS/FAST-HIT/MISS.

Bằng chứng: `build-ci-stall/sha-round-unroll-abba/summary.json`,
`build-ci-stall/lexer-dense-class-abba/summary.json`,
`build-ci-stall/lexer-classification-abba/summary.json` và các thư mục
experiment tương ứng. Các phép đo độc lập không được nhân tỷ lệ.

### Thử nghiệm parser và kiểm tra hồi quy cuối

Thử dùng một `Bộ Phân Tích Biểu Thức` cho 123 lời gọi từ parser câu lệnh.
Bootstrap C++ thành công nhưng compiler native không phân giải được chuỗi
receiver `mình.bộ biểu thức.biên dịch biểu thức tạo phân tích cú pháp`;
fixture AST dừng với `Native AOT: method not found`. Đã hoàn tác riêng
thử nghiệm parser. Không có số liệu hiệu năng hợp lệ cho thay đổi này.
Cần sửa phân giải receiver theo kiểu trước khi tối ưu theo hướng tái sử dụng
đối tượng. Bản cuối giữ lexer classification và các tối ưu đã đo trước đó.

Kết quả kiểm tra bản lexer classification trên macOS ARM64:

- Parser biểu thức và câu lệnh: golden interpreter/JIT/native đều PASS.
- Worker parity 3,309 s; C++/V++ object parity 19,577 s;
  batch all/cache invalidation 54,893 s; driver/hash 44,875 s: PASS.
- Stage1→2→3 convergence 121,998 s, gồm các golden FFI và native code
  generation: PASS. SHA-256 của stage2 và stage3 giống hệt:
  `cc308ce726640901e5a98703f9e2c2ff6d0865255ede8f01d1dbce8ad73b4eae`.
- Lexer fixture gốc PASS interpreter/JIT. Chạy native nguyên fixture dừng
  tại intrinsic VM-only `bien_dich_phan_tich_vm` của oracle C++ có sẵn từ
  trước; probe native riêng thay duy nhất phép so sánh oracle bằng số token
  kỳ vọng 15, giữ tất cả assertion còn lại, PASS golden (16,126 s cả build).
  Không xóa phép so sánh oracle trong fixture gốc.
- Fixture SHA bổ sung ban đầu bị bootstrap C++ từ chối vùng IR của `chính`.
  Đưa ba ca invalid byte sang helper độc lập, giữ nguyên vector và kiểm tra
  lỗi. Fixture sửa PASS interpreter/JIT/native (native 2,174 s cả build).
  Harness ban đầu exit 1 do ba lần chạy trên; follow-up đã xác minh riêng.

Bằng chứng: `build-ci-stall/classification-final-checks/summary.json`,
`followup-summary.json`, `hash-helper-interpreter.log`, `hash-helper-jit.log`,
`convergence.log` trong cùng thư mục. Không tăng timeout hoặc bỏ test.

Đối chiếu lại GitHub Actions job 124759121578 (01/11/2026): 90/90 PASS,
`vpp-integration` 229,40 s, tổng CTest 2873,79 s. Đây là số liệu lịch sử
của macOS, không so trực tiếp với batch 3 fixture cục bộ. Các cải tiến
chưa được chạy trên CI Windows/Linux mới; chưa xác nhận RSS đỉnh.

Bottleneck còn lại: hai cold cache key vẫn tốn khoảng 7,2 + 6,9 s.
`chuỗi thành byte máy ảo` hiện cấp phát một phần tử boxed cho mỗi byte;
API byte slice có thể giữ byte tùy ý, nhưng dùng chunk cần kiểm tra
biên UTF-8/NUL, padding, digest và chi phí cấp phát/copy. Hướng dài hạn là
buffer byte/word dày, lowering vòng lặp số nguyên có kiểm tra kiểu và
bảng ký hiệu intern/arena; chưa triển khai các cơ chế này.

### Lexer: con trỏ byte UTF-8

Tiếp theo bảng tra 256 byte, loại bỏ mảng chuỗi code point, bảng offset
byte và bảng 128 chuỗi ASCII. Lexer kiểm tra toàn bộ UTF-8 trước khi quét
để giữ thứ tự ưu tiên lỗi, rồi đọc byte trực tiếp, tiến theo độ rộng code
point và chỉ cắt chuỗi tại biên token. Giữ nguyên hợp đồng chín trường.
Danh sách byte nguồn vẫn được cấp phát; token vẫn là dictionary.

- Bootstrap Stage 0 thành công trước khi bổ sung fixture CRLF, escape
  Unicode/LF, dấu sao cuối comment chưa đóng và ưu tiên lỗi UTF-8.
- Bản đầu gặp `Native AOT: index out of range` khi đọc lookahead cuối
  comment. Tách kiểm tra giới hạn khỏi truy cập byte, bootstrap lại và
  đối chiếu 791665 byte output với bản bảng tra trước: giống hệt.
- Golden lexer mới, parser biểu thức/chương trình PASS native; lexer
  mới và oracle lexer C++ PASS interpreter/JIT. Hai launcher mặc định
  `vpp` và `./VPP` đều chạy fixture mới PASS sau cập nhật compiler.
- Microbench ABBA cùng 4500 token, bốn tiến trình, năm mẫu mỗi lượt,
  bỏ mẫu đầu: median 48,8275 -> 34,4275 ms (giảm 29,49%). So sánh trực
  tiếp với bản bảng tra 256 byte; không suy rộng cho toàn compiler.
- Bằng chứng: `build-ci-stall/lexer-cursor-final/microbench.json`,
  `checks.json`, `parity.stdout`, `vpp-native`. Chưa đo RSS hay chạy toàn
  bộ CI đa nền tảng; token compact và interner chưa triển khai.

### Lexer và parser: token gọn, intern ký hiệu

Lexer dùng chung bộ quét byte, cung cấp `biên dịch từ tố hóa gọn` trả về
`token` và pool `ký hiệu`. Record nội bộ là list mười trường theo thứ tự:
loại, giá trị, dòng, cột, vị trí byte, kết thúc byte, cột ký tự, vị trí
ký tự, kết thúc ký tự, ID ký hiệu. Định danh và từ khóa được intern qua
hash index và list tra ngược theo từng lần quét; token khác có ID -1.
Pool độc lập với StringPool phát bytecode. Không chuẩn hóa Unicode.
API lexer cũ tiếp tục trả dictionary chín trường. Parser biểu thức và
chương trình mặc định dùng record gọn, vẫn nhận dictionary từ caller;
phiên tự chủ giữ hành vi nếu caller yêu cầu hoặc sửa token công khai.
Năm helper biểu thức thực hiện trực tiếp thay vì cấp phát parser tạm;
nhánh hợp nhất từ khóa dùng index list trực tiếp. AST vẫn là dictionary.

Bootstrap thành công trước khi tạo fixture `kiem_tra_lexer_token_gon.vi`.
Fixture kiểm tra chín trường, ID lặp, tra ngược, pool độc lập, AST và token
map do caller sửa. Đăng ký fixture native/JIT trong `run_tests.sh`.
Sau bản build cuối, năm golden native, năm interpreter và năm JIT PASS.
Parity lexer cũ với bản cursor: 791665 byte giống hệt.

Microbench native ABBA, cùng nguồn `liên kết.vi` (4500 token, 28 câu lệnh
AST cấp cao nhất), bốn tiến trình, năm mẫu mỗi tiến trình, giữ toàn bộ
mẫu: median lexer công khai 40,8685 → 36,199 ms (-11,43%); lexer + parser
90,002 → 80,058 ms (-11,05%); peak RSS toàn tiến trình qua wait4 trên macOS
32178176 → 30900224 byte (-3,97%). Đây là kết quả kết hợp các thay đổi,
không tách riêng đóng góp interning hoặc compact token. Không cộng/nhân
với các tỷ lệ từ phép đo khác, không suy rộng thành cold build.

Stage 1 → 2 → 3 PASS; stage2 và stage3 giống hệt từng byte, SHA-256:
`ba27f6ac9f09f033d37aaf49ffa605b6d29fbd145ac2b37e88aa13194d2bbd6a`.
Stage3 build và chạy fixture token gọn PASS. Cài binary bootstrap đã kiểm
thử (giữ cách cài các đợt trước) vào `build` và `cmake-build-debug`, cả
`native-compiler` và `native-cli`; SHA-256 chung:
`aeac53e9d7c28e32b5f7b1c89d8a0a56bcc3b8edbace7f955f65e709adb24cd4`.
Backup bốn binary cũ; cả `vpp` và `./VPP` chạy fixture mới PASS, không có
biến môi trường override compiler trong lượt kiểm tra.

Bằng chứng: `build-ci-stall/lexer-compact-final/benchmark.json`,
`checks.json`, `parity.stdout`, `stage2`, `stage3`, `stage3-probe.stdout`,
`default-binaries-before/`. Chưa chạy toàn bộ CI Windows/Linux. Token
vẫn dùng list boxed, loại và giá trị vẫn là chuỗi; parser chưa so sánh
ID trực tiếp. ID từ khóa ghép được cấp lúc hợp nhất, sau khi quét thô.
Bước kế tiếp: loại token số nguyên, span và materialization trì hoãn,
buffer byte không boxed, đo scaling nhiều file và cold cache key.


### Lexer và parser: mã loại token số nguyên

Record gọn có 11 trường: mười trường cũ giữ nguyên, trường 10 là
mã loại (0 không biết, 1 định danh, 2 từ khóa, 3 số nguyên, 4 số thực,
5 chuỗi, 6 toán tử, 7 dấu câu, 8 hết). API công khai vẫn trả map chín
trường; accessor hỗ trợ record mười trường và map cũ. Record 11 trường
dùng mã ở index 10 làm nguồn phân loại, nên caller tự sửa record phải
giữ mã và tên loại đồng nhất. Map thiếu loại trả mã 0; không cam kết
chẩn đoán giống hệt đối với record không hợp lệ. Chuỗi loại còn lưu để
giữ tương thích; token vẫn là list boxed. AST và StringPool không đổi.

Parser dùng mã số để so loại, bảng ưu tiên toán tử tạo một lần mỗi
parser state, cache cursor/list và bảng ưu tiên trong vòng lặp;
giữ fallback cho state cũ. Các giá trị token vẫn so sánh chuỗi.

Bootstrap trước khi mở rộng fixture token gọn. Fixture kiểm tra mã loại,
record cũ, map cũ, AST tương đương, toàn bộ 29 toán tử và tính kết hợp.
15 golden (5 native, 5 interpreter, 5 JIT) PASS; parity lexer 791665
byte giống hệt. Stage 2/3 tự biên dịch với cờ --compiler và đồng nhất
SHA-256 40302c1eebc9e96412330248c6ba7c64c1ca9b341e9d1ae38a2548c57f118f01.
Stage 3 build/chạy fixture PASS. Đã backup và cập nhật bốn binary mặc
định, SHA-256 45af93c7ee512bfd30a34e9509367aba8b660d9c0e45454c269878017a82a60a.
Cả vpp và ./VPP PASS không override compiler.

Benchmark native cùng 4500 token/28 câu lệnh: bản đầu lexer 37,031 →
37,227 ms, lexer + parser 80,461 → 81,2025 ms (regression). Sau cache
lookup: lexer 37,7075 → 38,3300 ms, lexer + parser 81,8345 → 78,8435 ms.
Lặp lại ABBA/BAAB, tám tiến trình, năm mẫu mỗi tiến trình, 20 mẫu mỗi bản:
lexer công khai 35,2825 → 35,6265 ms (+0,97% thời gian), lexer + parser
78,4000 → 73,7910 ms (-5,88% thời gian); peak RSS median toàn tiến trình
qua wait4 30834688 → 31064064 byte (+0,74%). Giữ bản mới vì lợi ích
pipeline parser; không coi đây là cải thiện lexer riêng hoặc bộ nhớ.
Đây là microbench kết hợp nhiều thay đổi, không phải cold build.
Chưa đo toàn bộ CI Linux/Windows.

Bằng chứng: build-ci-stall/lexer-kind-final/benchmark-first.json,
benchmark.json, benchmark-repeat.json, checks.json, parity.stdout,
stage2, stage3, stage3-fixture.stdout và default-binaries-before/.
Bước tiếp theo: mã cố định từ khóa/toán tử/dấu câu, mức ưu tiên trực tiếp
trong token, tối ưu ghép từ khóa và giảm cấp phát boxed.


### Fixed syntax IDs và ghép từ khóa theo thành phần (12/11/2026)

- Token gọn có 12 trường; trường 11 là ID cú pháp ổn định (39 từ khóa, 29 toán tử nhị phân/gán, 4 toán tử khác, 9 dấu câu, mã chấm 300). ID ký hiệu intern theo nguồn và thứ tự StringPool bytecode không thay đổi. Map công khai vẫn 9 trường; record cũ 10/11 trường vẫn được chấp nhận.
- Parser nội bộ dùng ID cú pháp và bit cao của toán tử để tính độ ưu tiên. API trạng thái parser công khai vẫn giữ bảng ưu tiên tùy chỉnh. Đường đọc token gọn kiểm tra kiểu/độ dài một lần; fallback map/record cũ phân nhóm theo ký tự đầu, không tạo lại bảng cú pháp mỗi lượt. Ghép từ khóa dùng so sánh âm tiết, ưu tiên cụm dài; không tạo chuỗi ứng viên. Bảng tiền tố hiện chứa tối đa một cụm hai âm tiết và một cụm ba âm tiết cho mỗi tiền tố theo ngữ pháp hiện hành.
- Bản thử đầu: 35.7145 → 34.7945 ms lexer công khai, 74.6715 → 74.9490 ms lexer + parser; không cài bản này.
- Bản cuối: ABBA/BAAB, 8 process, 5 mẫu/process, 20 mẫu mỗi biến thể, cùng nguồn 4.500 token / 28 câu lệnh AST. Median lexer công khai 36.7290 → 35.0535 ms (giảm 4,56%); lexer + parser 75.3650 → 67.7200 ms (giảm 10,14%); RSS 31.105.024 → 31.031.296 byte (giảm 0,24%). Không cộng dồn tỷ lệ với các lần benchmark trước.
- Biên dịch compiler trước khi mở rộng fixture. Native, interpreter, JIT mỗi chế độ 5/5 PASS; parity lexer cũ 791.665 byte giống hệt. Stage 2 và 3 giống từng byte (SHA-256 2876fe811dc633a046e0395245135bd2c0d680381dcfe50db786e3e08df0308e); Stage 3 biên dịch/chạy fixture PASS.
- Đã cài Stage 1 SHA-256 d3be226755e00d6e5916fb9c627e2aeca926632f0a4aae4e6666c8d73bd1c421 vào bốn vị trí mặc định; launcher global và repo PASS không cần override. Bản trước lưu tại default-binaries-before.
- Bằng chứng: build-ci-stall/lexer-syntax-final/{benchmark-first.json,benchmark-second.json,checks.json}. Giới hạn: macOS ARM64; chưa chạy CI toàn bộ hoặc Linux/Windows, chưa đo thời gian build compiler toàn phần. Token vẫn là list boxed, AST vẫn map/list. Tiếp theo: token đóng gói và lexeme theo span, giảm cấp phát AST và đo cold build.


### Phân loại số theo byte span và phân tích receiver (12/11/2026)

- Lexer phân loại số bằng span trên danh sách byte đã có, không tạo ký tự con; chỉ tra bảng tiền tố ghép từ khóa khi token là định danh. Giữ hành vi dấu chấm, số thực, định danh hỗn hợp, UTF-8 và thứ tự intern.
- Parser nhận diện `gốc.`/`mình.` bằng lát code point, bỏ vòng ghép ký tự đối với định danh thường; mỗi thành viên receiver và postfix được cắt chuỗi một lần. Giữ chẩn đoán thành viên rỗng và tên Unicode nhiều âm tiết.
- Bootstrap thành công trước khi mở rộng fixture. 15/15 golden native/interpreter/JIT PASS; parity lexer 791.665 byte giống hệt; Stage 2/3 tự biên dịch với `--compiler`, SHA-256 chung `0cf7da0bdf12a7e30602e2286701a9ca4e82a8c5f301613a9148218c723148bc`. Stage 3 build/chạy fixture PASS.
- ABBA/BAAB, 20 mẫu mỗi bản, cùng 4.500 token/28 câu lệnh: lexer công khai 34,8725 → 35,6265 ms (+2,16% thời gian); pipeline 68,3840 → 65,2435 ms (-4,59%); peak RSS 31.539.200 → 31.211.520 byte (-1,04%). Chấp nhận đánh đổi vì pipeline là mục tiêu; chưa cô lập nguyên nhân regression lexer, chưa đo cold build. Không cộng tỷ lệ với benchmark trước.
- Đã backup và cài Stage 1 SHA-256 `62410e5f8ac1a5bb7e3e5eda9c7c6f2651627f3aebd8685e1f88882cefa7fd54` vào bốn binary mặc định. Global `vpp` và `./VPP` chạy fixture PASS không có override compiler.
- Bằng chứng: `build-ci-stall/lexer-span-final/{benchmark.json,checks.json,parity.stdout,stage2,stage3,stage3-fixture.stdout,default-binaries-before/}`. Chưa chạy toàn bộ CI hoặc Linux/Windows; token vẫn boxed, chưa trì hoãn lexeme.
- Khảo sát bước kế tiếp: ASM hiện có `vpp_native_bytes_new/get/push_i32/load_i32/store_i32/free` (macOS ARM64 và POSIX x86-64); V++ có API tài nguyên byte trong `gói/biên dịch/bản địa/môi trường thực thi.vi`. Cần kiểm chứng khả năng dùng từ lexer chạy native, interpreter và JIT, vòng đời tài nguyên và giới hạn offset trước khi đổi representation. Không coi API runtime này là token packed đã triển khai.


### Hợp nhất lookup và bảng hậu tố toán tử (12/11/2026)

- Giữ bảng phân loại 256 byte; thêm bảng 128 phần tử cho hậu tố toán tử ASCII, với bit cho dấu bằng, lặp byte và gán dịch bit. Tra một lần thay chuỗi so sánh; giữ thứ tự xử lý chú thích và kiểm tra biên trước khi đọc byte thứ ba.
- Gộp kiểm tra tồn tại và lấy giá trị map trong intern, từ khóa, tiền tố từ khóa ghép; giá trị 0 khác rỗng. Cache bảng từ khóa/dấu một lần trong scanner. Probe map PASS ở native, interpreter, JIT.
- Bootstrap compiler và build benchmark thành công trước khi mở rộng fixture. Bổ sung 13 toán tử đơn ở EOF, 167 cặp không phải mở chú thích, tám trường hợp hậu tố ba byte, chú thích và Unicode; kiểm tra giá trị, loại, span và ID cú pháp bằng oracle chính tả độc lập.
- ABBA/BAAB, 20 mẫu mỗi bản, 4.500 token/28 câu lệnh: lexer công khai 33,6295 → 32,4825 ms (giảm 3.41%); lexer + parser 62,9780 → 61,0955 ms (giảm 2.99%); peak RSS 31.531.008 → 31.105.024 byte (giảm 1.35%). Không cộng dồn với các benchmark trước.
- 15/15 golden PASS (native/interpreter/JIT); parity cũ 791.665 byte giống hệt. Stage 2/3 tự biên dịch với --compiler, cùng SHA-256 `1c756467d32cc649d0ac9a7d1b3ff7f4007bb009736d91115d67264ea4215a0d`; fixture Stage 3 PASS.
- Đã backup và cài Stage 1 `38660c88d5f63b23bdac0220b6735bb248ba0d1ee82ed05ed15a5ab88267520c` vào bốn binary mặc định; global vpp và ./VPP PASS không override.
- Bằng chứng: `build-ci-stall/lexer-lookup-final/{benchmark.json,checks.json,parity.stdout,stage2,stage3,stage3-fixture.stdout,default-binaries-before/}`. Giới hạn: macOS ARM64, microbench, chưa đo cold build hoặc CI toàn bộ Linux/Windows. Token vẫn boxed; bước kế tiếp là giảm cấp phát dữ liệu byte/token hoặc đo cold cache key trước khi thay representation.


### Kiểm tra UTF-8 tại chỗ và bỏ đọc lặp byte đầu (12/11/2026)

- Lexer kiểm tra UTF-8 trước khi quét token, chỉ cần valid/invalid nên không gọi bộ giải mã maximal invalid prefix ở từng code point. Dùng giới hạn byte đầu, độ dài còn lại, continuation và bốn trường hợp E0/ED/F0/F4 để giữ nguyên quy tắc Unicode; bộ giải mã dùng chung không thay đổi.
- Vòng quét định danh tái sử dụng byte đầu đã phân loại và cộng vị trí code point/cột một lần mỗi token; không đọc và tra bảng lặp byte đầu. Giữ nguyên UTF-8, byte span, code point span và thứ tự lỗi.
- Bootstrap compiler thành công trước khi mở rộng fixture. Bổ sung 190 tổ hợp byte biên đối chiếu bộ giải mã chung, năm trường hợp lỗi continuation/truncation, và span định danh ASCII/Unicode hỗn hợp.
- ABBA/BAAB, 20 mẫu mỗi bản, 4.500 token/28 câu lệnh: lexer công khai 33,9210 → 32,8145 ms (giảm 3.26%); lexer + parser 63,7440 → 62,7115 ms (giảm 1.62%); peak RSS 31.170.560 → 31.211.520 byte (tăng 0.13%). Chấp nhận vì thời gian giảm, không tuyên bố cải thiện RAM. Không cộng dồn với các benchmark trước.
- 15/15 golden PASS (native/interpreter/JIT); parity 791.665 byte giống hệt. Stage 2/3 tự biên dịch với --compiler cùng SHA-256 `5f0ea2b16b0ea4622f593458ec211246b197392027d09bf08e1e536b3c022421`; fixture Stage 3 PASS.
- Đã backup và cài Stage 1 `cec6250411c12a45385d5155ecfa46787cb43cfb5b288610fa76d178e5f38582` vào bốn binary mặc định. Global vpp và ./VPP PASS không override.
- Bằng chứng: `build-ci-stall/lexer-utf8-final/{benchmark.json,checks.json,parity.stdout,stage2,stage3,stage3-fixture.stdout,default-binaries-before/}`. Giới hạn: macOS ARM64, microbench, chưa đo cold build/CI toàn bộ. Token vẫn boxed; raw byte-handle ABI chưa được chứng minh là dùng được đồng nhất trong mọi chế độ, nên chưa thay representation.


### Rút gọn điều kiện nóng của lexer (12/11/2026)

- Giữ bảng phân loại 256 byte và bảng hậu tố 128 phần tử. Chỉ kiểm tra byte tiếp theo khi gặp `/`; dùng sentinel để bỏ phép so sánh `/` lặp lại khi phân biệt hai loại chú thích. Sau khi loại bỏ whitespace/quote, phân loại dấu bằng một phép so sánh với 0; trong compact path, kiểm tra mã loại bằng miền giá trị đã biết. Kiểm tra continuation UTF-8 bằng mask `0xC0 == 0x80`, giữ nguyên các giới hạn E0/ED/F0/F4 và guard độ dài.
- Các bản thử khác đều bootstrap thành công nhưng không được cài: canonical sign rows 31,6500 → 32,2370 ms lexer (+1,85%), pipeline 60,8585 → 61,7335 ms (+1,44%); packed numeric sign IDs 31,6700 → 31,7070 ms, pipeline 60,7425 → 60,7120 ms, RSS tăng; phân loại gộp width UTF-8 âm/dương đều làm lexer công khai chậm hơn và chỉ cải thiện pipeline dưới 1%. Đã khôi phục từng thử nghiệm trước khi thực hiện bản cuối. Bằng chứng: `build-ci-stall/lexer-canonical-final/benchmark.json`, `lexer-sign-id-final/benchmark.json`, `lexer-width-final/benchmark{,-negative}.json`.
- Bản cuối bootstrap và build benchmark thành công trước khi chạy fixture. ABBA/BAAB, 20 mẫu mỗi bản, 4.500 token/28 câu lệnh: lexer công khai 32,8080 → 30,2955 ms (giảm 7,66%); lexer + parser 62,4500 → 59,7285 ms (giảm 4,36%); peak RSS 31.014.912 → 31.055.872 byte (tăng 0,13%). Không cộng dồn với benchmark trước.
- 15/15 golden native/interpreter/JIT PASS, parity lexer 791.665 byte giống hệt; Stage 2/3 tự biên dịch với `--compiler` cùng SHA-256 `d06e07ea4667666d89ccf430274632fe5cc084bca4cb398f531859a7b5447de0`; Stage 3 build/chạy fixture PASS. Bộ fixture hiện có bao phủ toàn bộ toán tử, UTF-8 biên và span nên không thêm test trùng lặp.
- Đã backup và cài Stage 1 `ac04b8975ec3b9ccf8c138994d97125403709aa53b71462717a9c1cb21fad133` vào bốn binary mặc định. Global `vpp` và `./VPP` PASS không override.
- Bằng chứng: `build-ci-stall/lexer-branch-final/{benchmark.json,checks.json,focused-checks.json,parity.stdout,stage2,stage3,default-binaries-before/}`. Giới hạn: macOS ARM64, microbenchmark, chưa chạy toàn bộ CI/Linux/Windows hoặc cold build. Token vẫn là list boxed; tiếp theo ưu tiên profile cấp phát token/AST và cold build thay vì bảng lồng nhau.


### Hợp nhất token gọn tại chỗ (12/11/2026)

- Compact lexer dùng hai con trỏ đọc/ghi trên danh sách thô riêng của lần quét; chỉ ghi đè vùng đã đọc và xóa phần dư từ đuôi. Public lexer vẫn trả danh sách riêng để không sửa cache token thô. Giữ span UTF-8, thứ tự intern thô trước cụm ghép và ưu tiên cụm ba thành phần.
- Bootstrap và build benchmark thành công trước khi mở rộng fixture. Kiểm tra cụm đầu/cuối, cụm kề nhau, pool chính xác, cache công khai, nguồn không ghép và nguồn rỗng. 15/15 golden PASS (native/interpreter/JIT); parity 791.665 byte giống hệt. Stage 2/3 cùng SHA-256 `ddb6f9fd69370373a47fccf437d6008538760fd2149d2f38ef3adc0c722ff2c2`; Stage 3 build/chạy fixture PASS.
- ABBA/BAAB, 20 mẫu mỗi bản: lexer công khai 30,2715 → 29,2770 ms; pipeline 58,7045 → 58,2275 ms; lexer gọn 4.500 token 27,5605 → 27,0690 ms, 18.000 token 129,5295 → 127,0430 ms. RSS benchmark gọn 40.820.736 → 41.140.224 byte. Cải thiện thời gian nhỏ, không có bằng chứng giảm RAM; không cộng dồn tỷ lệ.
- Đã backup và cài Stage 1 `bf3c641b61aeb6d2e29f696b6d003d62cfc77cad9590a6c66fe7071214222389` vào bốn binary mặc định; global vpp và ./VPP PASS không override.
- Bằng chứng: `build-ci-stall/lexer-inplace-final/{benchmark.json,benchmark-compact.json,checks.json,focused-checks.json,parity.stdout,stage2,stage3,default-binaries-before/}`. Giới hạn: macOS ARM64, chưa đo cold build hoặc chạy toàn bộ CI/Linux/Windows. Token còn boxed; ưu tiên profile parser/cold build tiếp theo.

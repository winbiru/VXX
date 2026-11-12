# Điều tra CI chậm ngày 2026-11-12

Kết luận: phần tốn thời gian chủ yếu là compiler tự chủ dựng lại chương trình
cho từng fixture, đặc biệt là tạo khóa cache và ghi assembly qua các vòng lặp
V++ theo từng byte. Đây không phải thời gian chạy chương trình test. Windows
còn có một lỗi riêng khiến CTest giữ tiến trình con sống đến hạn bốn giờ.

## Bằng chứng từ Actions

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
  "$root/src/tests/kiem_tra_package_modules.vi" \
  "$root/src/tests/kiem_tra_package_modules.vi" \
  "$root/src/tests/kiem_tra_stdlib_tinh_toan.vi"
```

Bằng chứng cục bộ nằm trong `build-ci-stall/measured-baseline/`,
`build-ci-stall/measured-sha-streaming/` và `build-ci-stall/evidence/`.

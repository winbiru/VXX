# Linux x86-64 freestanding syscall boundary (đang phát triển)

## Kiểm tra e_shstrndx và source bootstrap mới nhất (02/11/2026)

General linker yêu cầu `e_shstrndx` chỉ tới `SHT_STRTAB` có offset,
kích thước, NUL và `sh_name` hợp lệ; `SHN_UNDEF (0)` vẫn được chấp nhận
nếu object không có bảng tên. Canonical compiler ET_REL yêu cầu index 5.
Regression mới đạt **62 partial-link negative**, **31 final-link negative**
và final-link dương tính cho `SHN_UNDEF`.

Lệnh bootstrap đã được kiểm chứng trong Docker Ubuntu 24.04 amd64:

```sh
docker exec -w /work vpp-freestanding-validation \
  bash scripts/quality/check_linux_source_bootstrap.sh \
  /out/native-source-host/verified-source-bootstrap-final-20261102/selfhost/vpp-native-stage2 \
  /out/native-source-host/verified-shstrndx-hardened-final-20261102
```

Output directory trên đã tồn tại: khi chạy lại cần chọn thư mục đích mới.
Gate PASS: `NATIVE_ELF_SELFHOST_CONVERGENCE`,
`SOURCE_BOOTSTRAP_PLATFORM_DIR_ONLY`, `NATIVE_ASSEMBLER_REGRESSIONS`,
`SOURCE_BOOTSTRAP_EXTERNAL_BUILD_TOOLS=NONE`, `SOURCE_BOOTSTRAP_CONVERGENCE`.
Stage 1 và Stage 2 có SHA-256
`60fc6c19578576a96111c75408e4d9300d7b88b0575d0a47b37cda1f652f2be3`.
Host seed/Stage 1/Stage 2 có SHA-256
`bd37d318987bcaf2571398ef0fc78150316980e84a18de76ed28e396e1a88fe3`.
Kết quả áp dụng cho backend i32 Linux x86-64 với compiler seed V++.

Nhánh này xây dựng ELF Linux x86-64 với `_start` riêng và lời gọi syscall
trực tiếp. Object Assembly không sử dụng libc, libstdc++, libc++, libffi,
libcrypto hay CRT. **Runtime V++ động đã chạy ba fixture trong ELF
freestanding trên Ubuntu x86-64. Compiler V++ freestanding đã tự tạo
Stage 27–29, ba executable giống nhau từng byte và không có PT_INTERP,
PT_DYNAMIC hay thư viện động.** Lệnh `build` thông thường của compiler
freestanding mới nhất cũng đã xuất ELF tĩnh không cần libc/CRT; gate
`check_linux_freestanding_selfhosting.sh` kiểm toán trực tiếp output này.
Đường tương thích ghép các object bằng GNU `ld` bên ngoài. **Đường native
hiện có compiler, assembler và linker bằng V++; gate source bootstrap bên
dưới dựng lại cả host object từ nguồn và đã hội tụ.** Các mốc GNU `ld` phía
sau ghi lại đường tương thích và kết quả trước khi có linker/assembler bản địa.

## Tái kiểm chứng ELF ET_REL header và cấu trúc ASM (02/11/2026)

Trên Ubuntu 24.04 x86-64, đã tái hiện partial linker cũ công bố object
khi ELF header khai báo entry point/program headers, flags hoặc ABI version
không hợp lệ. Đã chặn các trường sai trong validator canonical và linker
ELF tổng quát; thêm 10 fixture header hỏng chạy trên cả hai đường partial
và final. Kết quả: **52 ca partial lỗi, 21 ca final-link lỗi**, 32 ca
assembler, multi-stage object linking và executable exit 42 đều PASS.

Gate `check_linux_source_bootstrap.sh` đã PASS tại
`/out/native-source-host/verified-elf-header-hardening-20261102/`.
Compiler Stage 1/2 giống từng byte, SHA-256
`f779edd66ecee67c2dac41d8c762c55d259240b1761db47bf26d40a4a959f041`;
host do ba thế hệ tạo giống nhau, SHA-256
`bd37d318987bcaf2571398ef0fc78150316980e84a18de76ed28e396e1a88fe3`.
Không sử dụng compiler, assembler, linker GNU/LLVM ngoài trong gate.

Để bootstrap được từ seed cũ còn dùng đường dẫn `scripts/ASM/` phẳng,
script tạo symlink cho host mới **chỉ trong thư mục build tạm** sau khi
tự assemble từ văn bản. Symlink được xóa sau self-host; đã kiểm tra riêng
Stage 2 biên dịch và chạy smoke `exit 42`, binary trùng byte khi chỉ còn
`scripts/ASM/linux/`. Repository vẫn dùng các thư mục
`scripts/ASM/{common,linux,macos,windows}` theo nền tảng.

## Toolchain Linux x86-64 i32 tự tái tạo hoàn toàn từ nguồn (02/11/2026)

Compiler có lệnh `assemble <nguồn.s> -o <output.o>`.
Encoder, parser Assembly, symbol/fixup resolution và ELF writer nằm trong
`gói/biên dịch/bản địa/assembler.vi`; compiler nhập trực tiếp module này.
`trình hợp dịch.vi` cũng cung cấp một executable assembler độc lập.

Nguồn bộ máy nằm theo chức năng trong `scripts/ASM/linux/tu_chu_x86_64/`.
`scripts/ASM/linux/vpp_linux_host_x86_64.s` xác định thứ tự ghép các phần.
`prepare_linux_freestanding_host.sh` ghép nguồn văn bản trước khi gọi trình
hợp dịch V++; không cần bộ tiền xử lý C, khai triển macro hay đối tượng có
sẵn. Khi sửa chức năng, sửa tệp `.inc` tương ứng. Các điểm vào `.S` của
đường tương thích vẫn chèn nguồn bằng bộ tiền xử lý của trình hợp dịch.

Gate `scripts/quality/check_linux_source_bootstrap.sh` bắt đầu bằng **một
compiler seed V++ có lệnh assemble**, cùng nguồn `.vi`, `.s` và `.inc`:

1. Tạo thư mục nguồn riêng; từ chối nếu có input `.o`, `.a`, `.so` hoặc `.bin`.
2. Tắt object cache bằng `VPP_NATIVE_NO_CACHE=1`; chặn compiler, assembler,
   linker, `cpp`, `llvm-mc` và `objcopy` bên ngoài qua `PATH`.
3. Ghép các phần nguồn rồi seed tự hợp dịch đối tượng; native ELF gate dùng đúng bộ máy này
   để tạo hai thế hệ compiler, chạy smoke và regression ET_REL.
4. Mỗi compiler mới tự hợp dịch lại host; ba object phải giống từng byte.
5. Compiler cuối chạy regression assembler và kiểm tra công bố output nguyên tử.

```sh
docker exec -w /work vpp-freestanding-validation \
  bash scripts/quality/check_linux_source_bootstrap.sh \
  /out/native-source-host/verified-source-bootstrap-final-20261102/selfhost/vpp-native-stage2 \
  /out/native-source-host/reproduce-source-bootstrap
```

Output directory phải chưa tồn tại. Có thể dùng compiler Stage 2 của gate
đã PASS làm seed cho lần tiếp theo. Gate kiểm chứng cuối đã PASS (exit 0) tại
`/out/native-source-host/verified-source-bootstrap-final-20261102/`:

```text
SOURCE_BOOTSTRAP_INPUT_OBJECTS=NONE
SOURCE_BOOTSTRAP_HOST_FROM_TEXT=PASS
NATIVE_ELF_SELFHOST_EXACT_CMP=0
NATIVE_ELF_REL_REGRESSIONS=PASS
NATIVE_ASSEMBLER_REGRESSIONS=PASS
SOURCE_BOOTSTRAP_HOST_EXACT_CMP=0
SOURCE_BOOTSTRAP_COMPILER_ASSEMBLER_LINKER=VPP
SOURCE_BOOTSTRAP_EXTERNAL_BUILD_TOOLS=NONE
SOURCE_BOOTSTRAP_CONVERGENCE=PASS
```

SHA-256 của cả hai compiler:

```text
933fa8183dcc7079a7168d66ca2fb743539b268c33bb4dc724a5236a893532d3
```

SHA-256 của host do seed và hai compiler tự tạo:

```text
bd37d318987bcaf2571398ef0fc78150316980e84a18de76ed28e396e1a88fe3
```

Regression assembler có **32 ca**: sáu executable trả 42; 23 nguồn lỗi bị
từ chối, bao gồm hằng quá lớn, độ rộng/suffix lệnh, register/scale/section
sai, thiếu nhãn và expression ngoài dialect; ba ca từ chối output cùng path,
hard link hoặc symlink của nguồn. Ca lỗi giữ nguyên nội dung cũ, không để
lại tệp tạm. Bộ regression ELF ET_REL gồm 42 ca âm tính và 11 ca final-link
metadata tương ứng cũng PASS trong cùng gate. Log self-host nằm ở
`verified-source-bootstrap-final-20261102/selfhost.log`; fixture/log assembler
nằm ở thư mục `assembler-regressions/` của artifact.

Mốc đầu trước khi bổ sung toàn bộ ca lỗi assembler vẫn được giữ tại
`/out/native-source-host/verified-source-bootstrap-20261102/`.

Assembler hỗ trợ dialect AT&T cần cho host: GPR 8/16/32/64 bit, các phép
toán SSE2 dùng trong primitive số thực, địa chỉ RIP-relative/SIB, nhãn số
tiến/lùi, branch rel32 cố định, và relocation `R_X86_64_PC32`/`R_X86_64_64`.
Section được hỗ trợ gồm `.text`, `.rodata`, `.data`, `.fini_array` và
`.note.GNU-stack`; các directive dữ liệu là `.long`, `.quad`, `.ascii`,
`.asciz`, `.p2align`, `.globl` và metadata `.type`/`.size`. Đây là assembler
cho target này, không phải parser GAS/C preprocessor tổng quát.

Script chuẩn bị host cũng nhận compiler native, không cần LLVM:

```sh
VPP_LINUX_HOST_COMPILER=/path/to/vpp \
  bash scripts/quality/prepare_linux_freestanding_host.sh
```

Nếu không truyền compiler, script dùng LLVM để bootstrap từ cùng nguồn `.s`.
LLVM không nằm trong đường source bootstrap đã kiểm chứng. Một kiểm tra
đối chiếu trong phát triển xác nhận 2.996 lệnh đã decode và operand ngoài
branch trùng với object LLVM; đích branch và relocation được kiểm chứng qua
executable và hai thế hệ self-host.

**Phạm vi của mốc này:** tự tái tạo compiler + assembler + linker + host
runtime cho backend **i32/Linux x86-64**, từ nguồn với một seed V++ tin cậy và
Linux syscall ABI. Không cần input host object, C/C++, GNU/LLVM assembler,
GNU/LLVM linker hoặc libc trong gate. Đây là self-host của target đã nêu;
không đồng nghĩa mọi backend, frontend đầy đủ hay bootstrap không có seed
đều đã được kiểm chứng.

## Native ELF linker bằng V++ đã hội tụ (kiểm chứng lại 02/11/2026)

`gói/biên dịch/bản địa/trình liên kết elf.vi` nay đọc các object ELF64
ET_REL, bố trí segment, giải symbol và xử lý `R_X86_64_64`, `PC32`, `PLT32`,
rồi ghi ELF ET_EXEC trực tiếp. Tệp tạm nhận quyền `0700` bằng syscall
`fchmod` trên descriptor đang mở trước khi đóng và đổi tên nguyên tử.
Các segment có quyền `R`, `RX`, `R`, `RW`; `PT_GNU_STACK` là `RW`.

Đã chạy trên Ubuntu 24.04 amd64 trong container `vpp-freestanding-validation`:

```text
NATIVE_ELF_SMOKE_EXIT=42
NATIVE_ELF_RUNTIME_SMOKE_EXIT=42
NATIVE_ELF_SELFHOST_EXACT_CMP=0
NATIVE_ELF_REL_REGRESSIONS=PASS
NATIVE_ELF_EXTERNAL_BUILD_TOOLS=NONE
NATIVE_ELF_SELFHOST_CONVERGENCE=PASS
```

Hai compiler liên tiếp từ lần kiểm chứng ngày 02/11/2026 có cùng SHA-256:

```text
6effde437d165ed92cbd24469d396becaf6ddd7fe0b1bd10550fb8c267b27e91
```

Hash `6c64703f7c541e11e6ae0764fa7c47af1a04df105083f16edf7ee111fdb68c66`
thuộc mốc đã PASS trước khi bổ sung kiểm tra canonical section-header.
Mốc kiểm chứng ngày 01/11/2026 có SHA-256
`ee1b2a7dec6ec10352ae847fbf71c4ee621e641e62e15838ecb0db90e41137d8`;
đây là một bản build cũ, không phải hash hiện tại.

Trong gate, `cc`, `c++`, `gcc`, `g++`, `clang`, `clang++`, `ld`, `ld.lld`,
`lld`, `as` bị chặn qua `PATH`; không có lần gọi nào được ghi nhận. Các ELF
không có `PT_INTERP`, `PT_DYNAMIC`, `DT_NEEDED` hoặc segment đồng thời ghi
và thực thi. Chương trình kiểm tra chuỗi, gọi hàm và vòng lặp trả 42. Lệnh
`object` cho nguồn đơn cũng đã chạy khi `ld` bị chặn và chỉ định nghĩa `main`.

Harness riêng đã chạy một archive hợp lệ và 12 archive lỗi: dữ liệu cắt
cụt, global trùng, offset 64 bit ngoài giới hạn, code ghi được, alignment
sai, bảng symbol/entry RELA sai, relocation không hỗ trợ/ngoài section,
addend ngoài giới hạn và `main` chưa được giải quyết. Các ca lỗi giữ nguyên
output cũ và không để lại tệp tạm.

Tái lập trong container đang có compiler seed hỗ trợ primitive 78:

```sh
docker exec -w /work -e VPP_HOME=/work vpp-freestanding-validation \
  bash scripts/quality/check_native_elf_linker.sh \
  /out/native-elf-linker/vpp-seed /out/native-elf-linker/recheck
```

Ngày 02/11/2026, gate đã biên dịch và chạy hai harness V++ cho đường
`ET_REL -> ET_REL -> ET_EXEC` trong Docker Ubuntu 24.04 x86-64. Harness
Python viết byte ELF trực tiếp, không gọi assembler hay linker bên ngoài.
Final-link fixture dùng host ET_REL tối giản `_start -> main -> exit(60)`
thay cho host production vì host production nhập thêm nhiều symbol runtime
không nằm trong fixture `main/foo`. Host tối giản chỉ nhập `main` và chứa
`R_X86_64_PLT32` addend `-4`.

Partial linker đã PASS với `main.o + foo.o -> merged.o -> ET_EXEC` (exit 42),
giải ký hiệu theo thứ tự đảo, bỏ qua runtime object đầu, và ba mô-đun qua
hai vòng liên kết. `stage1.o + foo.o -> stage2.o` giống **từng byte** với
liên kết trực tiếp ba object. Object còn tham chiếu `foo` khi đưa vào final
linker bị từ chối mà không ghi đè output cũ. Object không có relocation
cũng liên kết thành executable chạy được (exit 42).

Regression partial linker hiện có **42 trường hợp âm tính**, bao gồm duplicate
strong, relocation lỗi, object cắt cụt, vượt số object, section chồng lấn,
tên symbol thiếu NUL, section-header vượt tệp, 64-bit offset/size/value
có high word, symbol index và addend sai, cùng string table không hợp lệ.
Trong mốc mới, validator canonical từ chối `sh_addr != 0` ở sáu section
khác NULL (kể cả `.note.GNU-stack` rỗng) và cả high word của `sh_addr`.
NULL section phải hoàn toàn bằng 0; các metadata section phải có chính xác
`sh_flags`, `sh_link`, `sh_info`, `sh_addralign`, `sh_entsize` theo writer V++.
Các trường `sh_addr` cũng được kiểm tra trong ELF reader tổng quát của final
linker; final-link regression từ chối **11 fixture lỗi** tương ứng với
`sh_addr`, NULL section, string-table link/info và high word của RELA entsize.
Tất cả ca âm tính đều giữ nguyên output/sentinel và không để lại tệp tạm.
Vùng đệm byte của V++
dành thêm một byte NUL ở sau độ dài logic; vì vậy vòng lặp zero-fill
`i <= dài` trong hai writer hiện phù hợp với quy ước cấp phát này.

Full gate mới nhất đã hoàn tất (exit 0) tại
`/out/native-elf-linker/verified-headers-20261102/`, với regression trong
`/out/native-elf-linker/verified-headers-20261102/elf-partial-regressions/`.
Đợt kiểm chứng trước nằm ở `/out/native-elf-linker/verified-20261102/`.

Log đã kiểm chứng trên máy này:
`build/native-dynamic-vpp-ubuntu-docker/native-elf-linker/gate.log` và
`input-gate.log`, `final-verification.log` trong cùng thư mục. Gate input dùng
`check_native_elf_linker_inputs.py <harness> <runtime-object.o> <output-dir>`;
object fixture được chuẩn bị riêng, ngoài gate không công cụ ngoài.
Kiểm tra bổ sung đã biên dịch lại Stage 0 C++ trên macOS ARM64 và dùng nó
để tạo executable từ nguồn V++ kèm runtime mới; chương trình trả 42.
Đây là kiểm tra tương thích bootstrap, không phải xác nhận freestanding
trên macOS.

**Phạm vi của mốc native ELF trước khi có assembler:** cần host syscall
`.o` chuẩn bị trước. Mốc source bootstrap ở trên đã thay bước assembler ngoài
bằng V++. Native linker được chọn bằng
`VPP_NATIVE_ELF_LINK=1`; đường GNU `ld` tương thích vẫn được giữ. `runtime-object`
và object nhiều mô-đun vẫn dùng `ld -r`. Linker giới hạn 32 object, 256 section
được nạp, 10.000 symbol, 2.048 global phân biệt và ELF đầu ra tối đa 1 MiB.
Địa chỉ/addend hỗ trợ theo ABI arena i32 thấp; TLS, shared objects và các
relocation khác bị từ chối. ELF đầu ra hiện không có section/symbol table.
Mốc này lúc đó chưa tự chủ từ nguồn vì host ASM còn cần assembler ngoài;
mốc source bootstrap ở trên đã xử lý phụ thuộc đó. Bootstrap compiler cũ
cũng cần bước chuyển tiếp để nhận
primitive `fchmod`; hai vòng self-host đã đo bắt đầu từ seed mới có primitive này.

## GNU ld trực tiếp từ lệnh `build` và hội tụ Stage 3–4 (01/11/2026)

Compiler Linux freestanding nay phát hiện host ASM object đã chuẩn bị sẵn ở
`scripts/ASM/linux/vpp_aot_primitives_linux_x86_64.o` và dùng **GNU `ld` trực tiếp**
để tạo ELF ET_EXEC. Không gọi `cc`/`clang` trong bước liên kết đó. Có thể
tạo object ELF x86-64 này từ macOS ARM64 hoặc Linux bằng:

```sh
bash scripts/quality/prepare_linux_freestanding_host.sh
```

Object được `.gitignore` bỏ qua; chạy lại lệnh sau mỗi lần sửa ASM/includes.
Khi chưa có object, `build` vẫn dùng đường `cc` tương thích hiện có. Có thể
đặt `VPP_SELFHOST_REQUIRE_LD_ONLY_BUILD=1` để gate từ chối trường hợp thiếu
object và chủ động chặn mọi lần gọi `cc` trong đường `build`.

Trên Ubuntu 24.04 `linux/amd64` trong Docker, lệnh `build` với `cc` bị chặn
đã tự biên dịch compiler thêm hai vòng liên tiếp. **Stage 3 và Stage 4 trùng
từng byte**, SHA-256:

```text
4dcd8d7c726dea34d89b22d0fb47e018dfc8449f7f024116cfdacd560c245354
STANDARD_BUILD_STAGES_EXACT_CMP=PASS
```

Kiểm toán cho thấy ELF64 x86-64 tĩnh, không có `PT_INTERP`, `PT_DYNAMIC`,
`DT_NEEDED` hoặc symbol chưa liên kết. Trong cùng lần kiểm thử, hai compiler
bootstrap qua `runtime-object` và GNU `ld` trực tiếp cũng hội tụ với SHA-256
`2bd02bf0149342df1d99baa35cbec922276bc2152e6b3ad31913616f07ecd09f`.
Khi so sánh hai đường biên dịch này, bytes thuộc các segment được tải vào
bộ nhớ giống nhau; chỉ thứ tự một số symbol ELF khác nhau. Muốn tái lập gate
theo đường `build` với cấm `cc`:

```sh
VPP_SELFHOST_REQUIRE_LD_ONLY_BUILD=1 bash \
  /src/scripts/quality/check_linux_freestanding_selfhosting.sh --verify \
  /out/host-libc-removal/freestanding-ld-auto-test/vpp-stage3-standard-build \
  /out/host-libc-removal/freestanding-ld-auto-test/vpp-stage4-standard-build \
  /out/host-libc-removal/freestanding-ld-auto-verified \
  /src/scripts/ASM/linux/vpp_aot_primitives_linux_x86_64.o
```

Đây là self-hosting của compiler/runtime không phụ thuộc C/C++ khi chạy,
nhưng **chưa phải self-hosting 100%**: `clang` vẫn được dùng để tạo host ASM
object từ nguồn, và GNU `ld` vẫn được dùng để xử lý ELF/relocation. Việc
triển khai linker ET_EXEC bằng V++ và thay assembler ngoài vẫn là bước tiếp theo.

## Compiler freestanding tự biên dịch: Stage 27–29 (01/11/2026)

Trên Docker Ubuntu 24.04 `linux/amd64`, compiler freestanding được tạo từ
object V++ chế độ `runtime-object` và host ASM biên dịch với
`VPP_LINUX_HOST_NO_CRT=1`, `VPP_VM_F64_FREESTANDING=1`. Compiler chạy
`--version` và biên dịch chương trình mẫu thành công. Stage 27 tạo Stage 28,
Stage 28 tạo Stage 29. Cả ba file có SHA-256:

```text
7b17244800a70e12b0a630d70fdb444d91f0155bd9dc0d2a30bcefcf1fe4bd8f
FREESTANDING_SELFHOST_EXACT_CMP=0
FREESTANDING_SELFHOST_SMOKE_EXIT=42
FREESTANDING_STATIC_SMOKE_EXIT=42
FREESTANDING_SELFHOST_CONVERGENCE=PASS
```

Kiểm toán từng ELF xác nhận ET_EXEC x86-64, stack không thực thi, không
có `PT_INTERP`, `PT_DYNAMIC`, `DT_NEEDED` hoặc symbol chưa được giải quyết.
Smoke `runtime-object` được liên kết thành ELF static, chạy trực tiếp và trả
mã thoát 42. Script tái lập (các đối số là đường dẫn trong Ubuntu container):

```sh
bash /src/scripts/quality/check_linux_freestanding_selfhosting.sh --build \
  /out/host-libc-removal/vpp-freestanding-stage27 \
  /out/host-libc-removal/host-freestanding.o \
  /out/host-libc-removal/freestanding-convergence
```

Để kiểm tra lại hai stage sẵn có, dùng `--verify <stage1> <stage2>
<output-dir> [host-asm.o]`. Đối số host ASM tuỳ chọn cho phép chạy thêm
smoke ELF freestanding. **Lưu ý:** `object` cố ý bỏ các hàm runtime; dùng
`runtime-object` khi xây compiler hoặc chương trình freestanding kèm host ASM.
Quy trình bootstrap trước đây gọi `clang`/`ld` để liên kết object. Đường
`runtime-object` hiện dùng `ld` trực tiếp; lệnh `build` cũng dùng `ld` nếu
host object đã chuẩn bị, nếu không vẫn gọi `cc` để tương thích.
Lệnh `build` sinh ELF không phụ thuộc libc, nhưng vẫn cần linker ngoài.
Muốn đạt self-hosting 100%, cần liên kết ELF
ET_EXEC và xử lý relocation bằng V++ mà không phụ thuộc các lệnh này.

Các thành phần:

- `scripts/ASM/linux/vpp_linux_start_x86_64.S`: nhận `argc`, `argv`, `envp` từ
  initial stack; kết thúc bằng `exit_group`.
- `scripts/ASM/linux/vpp_linux_syscalls_x86_64.S`: adapter System V AMD64 sang
  Linux syscall ABI, trả giá trị thô `-errno` khi lỗi.
- `scripts/ASM/linux/vpp_linux_runtime_core_x86_64.S`: arena 68.162.560 byte,
  thao tác byte/i32/binding, read/write/open/close, stat/lstat/fstat, file
  path/rename/unlink/mkdir, cwd/executable path, biến môi trường/HOME,
  `mkstemp` bằng `getrandom` + `openat(O_EXCL)`, tạo tiến trình bằng
  `fork`/`execve` với tra cứu `PATH` và `waitpid` bằng `wait4`.
- `scripts/ASM/linux/vpp_linux_directory_x86_64.S`: sử dụng lại primitive
  thư mục và danh tính hệ điều hành, gọi trực tiếp `getpid`, `getuid`,
  `openat`, `getdents64`, `close`, `mkdirat`, `unlinkat`. Đường dẫn có giới
  hạn 4096 byte và phải có NUL trong arena; handle được thu hồi khi đóng.
- `scripts/ASM/linux/vpp_linux_sha256_x86_64.S`: SHA-256 32 bit không phụ thuộc
  OpenSSL, xuất API băm từng phần `begin/update/end` trên arena V++.
- `scripts/ASM/linux/vpp_linux_process_x86_64.S`: subprocess qua `fork`,
  `pipe2`, `dup2`, `execve`, `poll`, `wait4` và `kill`.
  Vùng con trỏ `argv/envp` được kiểm tra trước khi thực thi; cleanup
  process group vẫn cần thử nghiệm race/PID reuse trên Linux thực.
- `scripts/ASM/linux/vpp_linux_socket_x86_64.S`: socket TCP/UDP,
  connect/listen/accept/send/recv, timeout, port cục bộ, dùng syscall
  Linux trực tiếp. Giữ nguyên API `vpp_native_os_socket` với
  control block 128 byte, trả `-errno` và lưu errno dương ở offset +28.
  Riêng control offset không hợp lệ chỉ trả `-EINVAL`.
- Tái sử dụng `scripts/ASM/common/posix/vpp_vm_system_primitives.inc` cho đồng hồ hệ thống,
  đồng hồ CPU luồng, ngủ tối đa 999 ms/lần và entropy bằng `getrandom`.
  Nhánh Linux của các primitive này gọi syscall trực tiếp, không gọi libc.

Các hàm `vpp_native_*` chuyển kết quả syscall lỗi thành `-1` và lưu errno
cho `vpp_native_os_errno()` theo ABI V++ hiện hành. Trạng thái errno hiện
là **toàn cục, chỉ đúng cho chạy đơn luồng**. Đường dẫn từ arena được kiểm
NUL trong tối đa 4096 byte trước khi chuyển cho kernel. Vùng nhớ được cấp
phát `PROT_READ|PROT_WRITE`, không có quyền thực thi.

Để biên dịch, liên kết và chạy kiểm thử trên **Linux x86-64** với clang hoặc
`cc`, GNU binutils và Bash:

```sh
bash scripts/quality/check_linux_freestanding.sh
```

Trên máy macOS ARM64 có Docker Desktop hỗ trợ container `linux/amd64`, chạy
probe trong Ubuntu 24.04 bằng:

```sh
bash scripts/quality/check_linux_freestanding_docker.sh
```

Script gắn mã nguồn vào container ở chế độ chỉ đọc, cài `clang` và `binutils`
trong container, và lưu object/ELF tại `build/native-freestanding-ubuntu-docker`.
Container cần truy cập Docker daemon, kết nối mạng để cài gói và hỗ trợ chạy
x86-64 qua lớp mô phỏng khi host là ARM64.

Nếu Docker Desktop trên Apple Silicon báo `rosetta error: bss_size overflow`
sau khi kiểm tra ELF tĩnh thành công, đây là lỗi của Rosetta khi khởi chạy
ELF x86-64, chưa phải kết quả chạy probe V++. Một số phiên bản Docker Desktop
có tuỳ chọn `Use Rosetta for x86_64/amd64 emulation on Apple Silicon` trong
Settings > General: có thể tắt để dùng QEMU. Nếu không có tuỳ chọn này và
Ubuntu amd64 đang chạy được bình thường, không cần thay đổi Settings.
File `build/native-freestanding-ubuntu-docker/docker-run.log` lưu kết quả
Docker trực tiếp và script tự in hướng dẫn này nếu gặp lỗi Rosetta.

Kiểm thử dựng executable với `-nostdlib -static -no-pie`, kiểm tra
`PT_INTERP`, `DT_NEEDED`, symbol chưa được định nghĩa và chạy probe.
Probe kiểm tra syscall, `errno`, arena, `argv/envp`, file/đường dẫn, tạo
tệp tạm an toàn và dọn tệp. Probe cũng tạo tiến trình con qua bảng argv
64-bit do V++ dựng, kiểm tra `PATH` cùng mã thoát 127 khi `execve` thất bại.
Probe socket mới kiểm tra TCP/UDP loopback với cổng tạm, send/recv,
SO_RCVTIMEO/SO_SNDTIMEO, thời hạn accept và tham số không hợp lệ.
Đồng thời kiểm tra `clock_gettime`, `nanosleep`, `getrandom` và tham số sai.
Tiến trình con không thể thực thi được công cụ sẽ trả trạng thái lỗi qua
`waitpid`; dịch vụ compiler-host trong V++ kiểm tra mã thoát này.
Nếu cấu hình CMake với `BUILD_TESTS=ON`, bài
kiểm thử xuất hiện trong CTest trên máy Linux x86-64 dưới tên
`vpp-linux-freestanding-syscalls`.

Trên macOS ARM64 có thể chỉ *cross-assemble* các object bằng
`clang -target x86_64-unknown-linux-gnu -c`. Lệnh tái lập kiểm tra:

```sh
bash scripts/quality/check_linux_freestanding_cross.sh
```

Lệnh này xác minh các object ELF (bao gồm directory, process và socket)
và kiểm tra tham chiếu symbol
được định nghĩa trong tập object. CTest `vpp-linux-freestanding-cross-asm`
được đăng ký khi Clang có sẵn trên macOS ARM64 hoặc Linux x86-64. Kết quả
này **chưa chứng minh** ELF chạy được trên Linux hoặc không có dependency ở
executable cuối cùng.

`spawn_clang` hiện kiểm tra toàn bộ bảng `argv` (tối đa 4096 con trỏ) trước
khi `fork`, bao gồm phạm vi arena và ký tự kết thúc NUL của từng chuỗi.
`stat`, `lstat`, `fstat` dùng bộ đệm kernel riêng, sau đó ánh xạ `st_dev`,
`st_mode` và `st_ino` vào header tương thích V++ tại offset +0, +4, +8.
`waitpid` tự gọi lại `wait4` khi nhận `EINTR`. Probe Linux bao gồm các ca
kiểm tra con trỏ `argv` không hợp lệ, chuỗi thiếu NUL và bảng thiếu NULL.
Tham số arena không hợp lệ trả `-1` và `vpp_native_os_errno() = EINVAL`;
`mmap` lỗi giữ lại errno thực.

`scripts/build-vpp-aot-runtime.sh` hiện cho phép chạy trên Linux x86-64
để tạo `runtime.o` từ `runtime AOT.vi` bằng trình biên dịch i32 V++, sau đó
kiểm tra ELF64 relocatable qua `readelf`. Bước compiler bootstrap vẫn sử dụng
công cụ liên kết ngoài. Runtime động đã liên kết vào ELF freestanding và
kiểm chứng chạy trên Ubuntu 24.04 linux/amd64 trong Docker.

Để kiểm chứng **runtime V++ động thực sự** trên Ubuntu x86-64, sau khi probe
freestanding đã đạt, chạy bài tích hợp riêng:

```sh
bash scripts/quality/check_linux_dynamic_vpp_docker.sh
```

Bài kiểm tra xây Stage 0 C++ (chỉ dùng lúc bootstrap), Stage 1 compiler viết
bằng V++, `runtime.o` từ `runtime syscall.vi`, sau đó dùng đường
`--emit-native` biên dịch ba fixture `hello`, `vm_asm.vi` và
`native_dynamic_vpp.vi` thành ELF tĩnh. Nó kiểm tra `PT_INTERP`,
`PT_DYNAMIC`, `DT_NEEDED`, symbol chưa định nghĩa và so khớp output của
các fixture bao gồm closure, đối tượng, ngoại lệ và áp lực GC. Build tạm và
log ở `build/native-dynamic-vpp-ubuntu-docker/` (các file `stage0-*.log`,
`stage1-build.log`, `runtime-build.log`, `*.build.log`, `*.audit.log`,
`*.stdout`, `*.stderr`, `docker-run.log`). Trong lượt kiểm chứng 01/11/2026,
**cả ba fixture `hello`, `vm`, `dynamic` đều PASS**, với kiểm toán không có
`PT_INTERP`, `PT_DYNAMIC`, `DT_NEEDED`, unresolved symbols hay stack thực thi.
Chỉ riêng probe syscall thành công không chứng minh đã self-host hay loại bỏ
mọi phụ thuộc ở compiler bootstrap.

Compiler viết bằng V++ tiếp tục tự biên dịch đến Stage 7; **Stage 6 và Stage 7
khớp SHA-256 và giống từng byte** sau khi linker loại bỏ `STT_FILE` tên object
tạm. Gate kiểm tra lưu ở `scripts/quality/check_linux_selfhosting_convergence.sh`.
Để kiểm tra lại các artifact Docker đã có mà không tốn thời gian dựng lại:

```sh
docker exec vpp-freestanding-validation bash \
  /work/scripts/quality/check_linux_selfhosting_convergence.sh --verify \
  /out/convergence-after-elf-symbol-fix
```

Muốn dựng lại các Stage 5–7 từ Stage 4 đã có, dùng `--build` cùng đường dẫn
đầu ra (tuỳ chọn đối số thứ ba là đường tới compiler Stage 4). Việc dựng lại
trên QEMU có thể chậm. Kết quả hội tụ không chứng minh compiler độc lập:
compiler Stage 9 còn `libc.so.6`, `ld-linux-x86-64.so.2` và gọi external
`cc`; OpenSSL/libcrypto đã được thay bằng SHA-256 Assembly.

Các bước tiếp theo trước khi thay hoàn toàn runtime POSIX/C++: mở rộng
compiler-host freestanding, thay các primitive libc bằng syscall,
thay external linker `cc`, rồi kiểm chứng lại hội tụ và dependency audit.
Đồng thời cần thử process/socket trên Linux x86-64 không qua lớp mô phỏng.

# Cài đặt V++ (bản V++/ASM tự chủ)

Bạn có thể [cài VDK từ gói phát hành](#cài-đặt-vdk-và-chọn-phiên-bản-sdk) hoặc tự build bộ công cụ từ mã nguồn.

## Build từ mã nguồn

Bản hiện tại hỗ trợ bootstrap compiler đầy đủ trên **macOS ARM64 (Apple Silicon)**. CMake 3.20+, công cụ Xcode Command Line Tools (`xcode-select --install`) và shell Bash là các điều kiện build. Clang dùng để dịch Assembly của nền tảng và ở bước bootstrap seed cũ; không biên dịch C/C++. Compiler sinh ra dùng đường ARM64/Mach-O trực tiếp.

```sh
cmake -S . -B build-pure-vpp
cmake --build build-pure-vpp --parallel
ctest --test-dir build-pure-vpp --output-on-failure
./VPP phiên bản
./VPP build hello.vi -o hello
./hello
./VPP jit hello.vi
```

Cài đặt tại một prefix tùy chọn:

```sh
cmake --install build-pure-vpp --prefix "$HOME/.local"
"$HOME/.local/bin/vpp" phiên bản
"$HOME/.local/bin/vpp" jit hello.vi
```

### JIT native (thử nghiệm)

Target `vpp-jit` được build cùng compiler. Có thể build riêng bằng
`cmake --build build-pure-vpp --target vpp-jit`. Sau khi cài đặt, dùng
`vpp jit hello.vi` hoặc chạy trực tiếp `vpp-jit hello.vi`.
JIT sinh mã máy trong bộ nhớ, không cần assembler/linker hoặc tệp thực thi
cho từng chương trình. Khi gọi chương trình không có đối số, mặc định thử
backend i32 và tự chọn backend động nếu backend i32 không biên dịch được.
Dùng `vpp jit --dynamic "$PWD/hello.vi"` để chọn trực tiếp kiểu động,
số thực, chuỗi, bộ sưu tập, lớp, ngoại lệ và import module. Chế độ gọi hàm
`--function` và `--raw-program` giữ ABI i32. Khi gọi `vpp-jit` trực tiếp
với import thư viện chuẩn, đặt `VPP_HOME` trỏ tới thư mục cài đặt `share/vpp`. `VPP_JIT_PROFILE=1` in thống
kê hàm nóng ra stderr; ngưỡng hiện tại là 32 lần gọi i32 phù hợp.
`VPP_JIT_TIER=0` tắt tầng thích ứng để so sánh baseline. Ảnh dynamic
giới hạn 1 MiB, nên ưu tiên import module hẹp. macOS ARM64 đã được kiểm thử
thực thi dynamic và tiered; Linux và Windows x86-64 còn cần xác nhận thực thi
trên nền tảng đích.

Hoặc dùng `bash scripts/unix/install-vpp.sh "$HOME/.local"`. Lệnh này không sửa shell profile. Thêm `$HOME/.local/bin` vào `PATH` nếu cần.

Hai seed được kiểm tra SHA-256, chữ ký và thư viện động trước khi build. Đọc [bootstrap/README.md](../bootstrap/README.md) để thay seed. Linux/Windows sử dụng quy trình bootstrap riêng tại tài liệu đó; các gói VDK được phát hành sau khi qua kiểm thử native trên nền tảng đích. Các bản phát hành cũ và tài liệu lịch sử không mô tả cách build của nhánh hiện tại.


## Cài đặt VDK và chọn phiên bản SDK

Chọn gói cho hệ điều hành và kiến trúc của bạn, rồi tải cả gói và tệp `.sha256`
tương ứng. [GitHub Releases](https://github.com/winbiru/VXX/releases)

Dùng số phiên bản trong tên gói thay cho `1.0.0`. Đặt biến `archive` theo đúng
tên tệp đã tải, kể cả chữ hoa hoặc chữ thường.

| Nền tảng | Tên gói |
| --- | --- |
| macOS ARM64 (Apple Silicon) | `vdk-<version>-macos-arm64.tar.gz` |
| Linux x86-64 | `vdk-<version>-linux-x64.tar.gz` |
| Windows x86-64 | `vdk-<version>-windows-x64.zip` |

Chạy các lệnh bên dưới trong thư mục tải xuống. Mỗi phiên bản VDK dùng một thư mục riêng.

### macOS ARM64 và Linux x86-64

Kiểm tra SHA-256 bằng lệnh tương ứng với hệ điều hành.

```sh
version=1.0.0
# macOS ARM64
archive="vdk-$version-macos-arm64.tar.gz"
shasum -a 256 -c "$archive.sha256"

# Linux x86-64: dùng hai dòng sau thay hai dòng macOS ở trên.
# archive="vdk-$version-linux-x64.tar.gz"
# sha256sum -c "$archive.sha256"
```

Khi checksum báo `OK`, giải nén gói và lưu `PATH` cùng cấu hình UTF-8 vào profile của shell.

```sh
sdk="$HOME/.vpp/vdk/$version"
mkdir -p "$HOME/.vpp/vdk" && mkdir "$sdk" &&
  tar -xzf "$archive" -C "$sdk" &&
  export PATH="$sdk/bin:$PATH" &&
  vdk cai-hien-thi &&
  (
    sdk_bin='$HOME/.vpp/vdk/'"$version"'/bin'
    path_line=$(printf 'case ":$PATH:" in *":%s:"*) ;; *) export PATH="%s:$PATH" ;; esac' \
      "$sdk_bin" "$sdk_bin")
    case "${SHELL##*/}" in
      zsh) set -- "${ZDOTDIR:-$HOME}/.zshrc" ;;
      bash)
        login_profile="$HOME/.profile"
        if [ -f "$HOME/.bash_profile" ]; then
          login_profile="$HOME/.bash_profile"
        elif [ -f "$HOME/.bash_login" ]; then
          login_profile="$HOME/.bash_login"
        fi
        set -- "$HOME/.bashrc" "$login_profile" ;;
      *) set -- "$HOME/.profile" ;;
    esac
    for profile in "$@"; do
      if ! grep -Fxq "$path_line" "$profile" 2>/dev/null; then
        printf '\n%s\n' "$path_line" >> "$profile" || exit 1
      fi
    done
  ) &&
  vdk_locale=$(bash "$sdk/bin/vdk-display.sh" locale) &&
  export LANG="$vdk_locale" LC_CTYPE="$vdk_locale" &&
  if [ -n "${LC_ALL:-}" ] && [ "$(locale charmap 2>/dev/null)" != UTF-8 ]; then
    unset LC_ALL
  fi &&
  vdk check-display &&
  vdk phiên bản && vdk chẩn đoán
```

### Windows x86-64 (PowerShell)

```powershell
$version = "1.0.0"
$archive = "vdk-$version-windows-x64.zip"
$expected = ((Get-Content "$archive.sha256" -ErrorAction Stop) -split '\s+')[0]
if ((Get-FileHash $archive -Algorithm SHA256 -ErrorAction Stop).Hash -ne $expected) {
    throw "SHA-256 không khớp"
}
$sdk = Join-Path $env:LOCALAPPDATA "VPP\vdk\$version"
if (Test-Path $sdk) { throw "Phiên bản VDK này đã được cài đặt" }
Expand-Archive -Path $archive -DestinationPath $sdk -ErrorAction Stop
$bin = Join-Path $sdk "bin"
$env:Path = "$bin;$env:Path"

$utf8 = [System.Text.UTF8Encoding]::new($false)
[Console]::InputEncoding = $utf8
[Console]::OutputEncoding = $utf8
$global:OutputEncoding = $utf8
vdk cai-hien-thi
if ($LASTEXITCODE -ne 0) { throw "VDK chưa lưu được cấu hình UTF-8" }

$userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
if (($userPath -split ';') -notcontains $bin) {
    $newPath = if ($userPath) { "$bin;$userPath" } else { $bin }
    [Environment]::SetEnvironmentVariable('Path', $newPath, 'User')
}
vdk check-display
vdk phiên bản
vdk chẩn đoán
```

Sau khi cài, mở terminal mới và chạy `vpp phiên bản` rồi `vdk phiên bản`.
Nếu đã cài VDK theo hướng dẫn cũ, thêm đường dẫn `bin` của SDK đang dùng vào
`PATH` trong profile shell, hoặc vào biến `Path` của tài khoản Windows.

### Lệnh VDK và khóa phiên bản

Trên Linux, launcher `vpp` cần `clang` trong `PATH`. Trên Linux và Windows,
chế độ AOT cần Clang/LLD tương thích. Có thể dùng
MSYS2 CLANG64 trên Windows. Một số lệnh VDK thường dùng gồm các lệnh sau.

```sh
vdk trợ giúp
vdk phiên bản
vdk chẩn đoán
vdk thư viện danh sách
vdk thư viện danh sách --json
vdk cài hiển thị
vdk kiểm tra hiển thị
vdk biên dịch hello.vi --đầu-ra hello
vdk chạy hello.vi
vdk chạy tức thời hello.vi
vdk kiểm tra hello.vi
vdk biên-dịch-máy-ảo hello.vi --đầu-ra hello.vmb
vdk gỡ hiển thị
```

VDK chứa compiler, runtime AOT, VM, JIT, thư viện chuẩn, tài liệu, ví dụ,
template dự án và metadata tô màu editor. `vdk-release.json` mô tả layout
SDK; `vdk.json` tại dự án chỉ định phiên bản cần dùng.

```json
{"version": "1.0.0"}
```

VDK tìm pin gần nhất trong các thư mục cha của thư mục làm việc.
Cài nhiều phiên bản bằng cách giải nén mỗi SDK vào
`~/.vpp/vdk/<version>` trên POSIX hoặc
`%LOCALAPPDATA%/VPP/vdk/<version>` trên Windows. Thứ tự chọn:

1. `--thư-mục-vdk <SDK-root>` ở đầu lệnh; phải khớp pin nếu có.
2. Khi có pin, thư mục phiên bản trong kho SDK của người dùng.
3. `VDK_HOME`, sau đó `VPP_HOME` (SDK root hoặc `share/vpp`).
4. SDK chứa launcher đang chạy.

Ứng viên có manifest sai phiên bản sẽ bị bỏ qua; pin sai JSON, thiếu version
hoặc không có SDK khớp sẽ báo lỗi. Ví dụ:

```sh
vdk --thư-mục-vdk "/opt/vdk/1.0.0" phiên bản
vdk --thư-mục-vdk "/opt/vdk/1.0.0" biên dịch hello.vi --đầu-ra hello
```

`doctor` kiểm tra tệp thực thi, thư mục thư viện, runtime và sự đồng nhất
phiên bản trong metadata. Compiler native hiện chỉ in tên compiler khi dùng
`--version`, vì vậy metadata SDK là nguồn phiên bản dùng cho pin.
Các lệnh ngoài nhóm quản lý được chuyển tới compiler/JIT của SDK đã chọn;
phạm vi lệnh phụ thuộc compiler native đi kèm. Các thành phần C/C++ và
toolchain bên ngoài của VDK cũ không được đưa lại vào SDK mới.


### Cấu hình hiển thị tiếng Việt UTF-8

Các lệnh cài đặt chạy `vdk cai-hien-thi` để lưu cấu hình UTF-8 cho tài khoản hiện tại.
Có thể chạy lại lệnh này bằng tên `vdk cài hiển thị` hoặc `vdk install-display`.
Trên Windows, VDK dùng code page 65001 và cấu hình profile PowerShell 5.1/7
với UTF-8 BOM; trên Linux/macOS chọn locale UTF-8 đã cài, ưu tiên tiếng Việt,
sau đó C.UTF-8 hoặc locale UTF-8 khác. Không cần sudo hay thay đổi toàn hệ thống.
Mở terminal mới sau khi cài hoặc gỡ. `vdk kiểm tra hiển thị` in mẫu tiếng Việt
và trạng thái profile; font terminal vẫn phải hỗ trợ các ký tự tiếng Việt.
`vdk gỡ hiển thị` chỉ loại bỏ khối cấu hình VDK quản lý.

Các lệnh tiếng Anh cũ (`version`, `doctor`, `library list`, `build`, `run`,
`jit`, `lint`, `vm-build`, `--vdk-home`) vẫn tương thích.


### Gỡ cài đặt V++/VDK

Chạy `vpp gỡ cài đặt` hoặc `vdk gỡ cài đặt` (alias `uninstall`).
Bản đóng gói và bản cài bằng CMake có danh sách tệp SHA-256 tại
`vpp-install-manifest.tsv`. Chỉ tệp đã cài và còn nguyên nội dung được xóa;
tệp đã sửa, dự án riêng và SDK khác được giữ nguyên. Bản cài không có danh
sách này sẽ từ chối gỡ. Nếu cần, chạy `vdk gỡ hiển thị` trước và tự bỏ
đường dẫn SDK tương ứng khỏi PATH.

# Cài đặt và cập nhật V++

## Release artifact

Source hiện tại mang phiên bản V++/VDK `1.0.0`. Release workflow tạo hai nhóm artifact.
Ba runtime bundle giữ tên ổn định để installer và VS Code extension có thể dùng URL
`releases/latest/download`:

- `vpp-linux-x64.tar.gz`
- `vpp-macos.tar.gz`
- `vpp-windows-x64.zip`

Ngoài ra CPack tạo VDK đầy đủ theo mẫu
`vdk-1.0.0-<platform>-<arch>.tar.gz` trên Unix/macOS và
`vdk-1.0.0-windows-<arch>.zip` trên Windows. VDK chứa `vdk`, compiler/VM,
static libraries, public headers, `gói/`, catalog, tài liệu, templates và examples.

Artifact chứa executable, `gói/`, `templates/`, `examples`, installer và uninstaller của nền tảng.
Installer có thể chạy lại trên cùng thư mục cài đặt để cập nhật. Các thư mục do V++ quản lý
(`gói/`, `templates/`, `examples/`) được thay toàn bộ sau khi bundle mới đã được staging, vì vậy
file đã bị xóa khỏi release mới không còn sót lại sau update.

## Linux và macOS

### Linux x64

```bash
curl -L https://github.com/winbiru/VXX/releases/latest/download/vpp-linux-x64.tar.gz -o vpp-linux-x64.tar.gz
tar -xzf vpp-linux-x64.tar.gz
./install-vpp.sh
source "$HOME/.bashrc" 2>/dev/null || source "$HOME/.bash_profile"
vpp phiên bản
```

### macOS

```bash
curl -L https://github.com/winbiru/VXX/releases/latest/download/vpp-macos.tar.gz -o vpp-macos.tar.gz
tar -xzf vpp-macos.tar.gz
./install-vpp.sh
source "$HOME/.zshrc"
vpp phiên bản
```

Sau khi giải nén, installer Unix có thể chạy trực tiếp bằng:

```bash
./install-vpp.sh
```

Mặc định V++ được đặt trong `$HOME/.local/bin`. Có thể chọn prefix khác:

```bash
VPP_INSTALL_DIR="$HOME/.local/share/vpp" ./install-vpp.sh
```

Installer cấu hình `PATH` và `VPP_HOME` trong profile shell. CI hoặc hệ thống quản lý môi trường
có thể đặt `VPP_SKIP_PROFILE=1` để chỉ cài file mà không sửa profile.
Installer tự kiểm tra executable trước/sau khi cài và dùng block profile có marker để cài lại
không nhân đôi `PATH` hoặc `VPP_HOME`.

Để cập nhật, tải artifact release mới, giải nén và chạy lại cùng installer với cùng
`VPP_INSTALL_DIR`.

Gỡ trên Linux/macOS:

```bash
vpp gỡ cài đặt
```

Bộ gỡ chỉ xóa các thành phần do V++ quản lý và dọn block profile của V++; file/project khác
trong thư mục cài đặt được giữ lại.

## Windows

Tải và giải nén runtime bundle mới nhất:

```powershell
Invoke-WebRequest -Uri "https://github.com/winbiru/VXX/releases/latest/download/vpp-windows-x64.zip" -OutFile "vpp-windows-x64.zip"
Expand-Archive -Path "vpp-windows-x64.zip" -DestinationPath ".\vpp-bin" -Force
```

Sau đó chạy PowerShell installer:

```powershell
$previousPolicy = Get-ExecutionPolicy -Scope Process
try {
    Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
    & .\vpp-bin\install-vpp.ps1
} finally {
    Set-ExecutionPolicy -Scope Process -ExecutionPolicy $previousPolicy -Force
}
vpp phiên bản
```

Hoặc dùng wrapper:

```cmd
vpp-bin\install-vpp.cmd
```

Mặc định V++ được cài tại `%LOCALAPPDATA%\Programs\VPP`. Có thể đổi prefix bằng
`-InstallDir`. `-NoPathUpdate` dành cho CI hoặc môi trường tự quản lý `PATH`/`VPP_HOME`.
Chạy lại installer trên cùng `InstallDir` để cập nhật.

## Gỡ trên Windows

Với bản mới có hỗ trợ lệnh gỡ:

```powershell
vpp gỡ cài đặt
```

Hoặc gọi trực tiếp bộ gỡ của bản cài mặc định:

```powershell
& "$env:LOCALAPPDATA\Programs\VPP\vpp-uninstall.cmd"
```

Nếu cài ở thư mục khác, chạy `vpp-uninstall.cmd` trong thư mục đó.
Bộ gỡ xóa executable, `gói`, `templates`, `examples` và các script gỡ;
đồng thời dọn PATH/VPP_HOME của bản cài. Sao lưu thay đổi trong các thư mục này trước khi gỡ.
Đóng và mở lại toàn bộ terminal/VS Code sau khi gỡ.

Bản cũ có thể chỉ in trợ giúp khi gọi `vpp gỡ cài đặt`. Nếu không có bộ gỡ
trong thư mục cài, dùng `uninstall-vpp.ps1` từ một bundle mới đầy đủ để gỡ bản cũ:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\vpp-bin\uninstall-vpp.ps1 -InstallDir "$env:LOCALAPPDATA\Programs\VPP"
```

Chỉ chạy lệnh trên khi file script thực sự tồn tại. Giải nén bundle mới vào thư mục
trống để tránh trộn file cũ/mới. Thay đổi source không tự cập nhật ZIP đã phát hành;
cài lại cùng ZIP cũ không bổ sung lệnh gỡ hoặc bản sửa UTF-8.

## Đánh giá Homebrew và winget

**Homebrew:** contract 1.0 trong source đã được freeze. Formula có thể cài runtime bundle hoặc
VDK macOS vào `libexec`, symlink `vpp`/`vdk` ra `bin`, và đặt `VPP_HOME` theo layout của formula.
Khi phát hành formula, khóa URL/version/checksum vào release asset cụ thể thay vì URL `latest`.

**winget:** nên chờ Windows có artifact cài đặt ký số ổn định (ưu tiên MSI/MSIX hoặc portable
package có metadata/version/hash cố định). ZIP + PowerShell installer hiện đủ cho release trực
tiếp, nhưng chưa phải format tốt để gửi manifest vào winget community repository.

**Linux package manager:** tarball + installer là contract portable 1.0. Debian/RPM có thể thêm
sau khi layout/versioning đã freeze.

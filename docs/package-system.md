# Package system V++ 0.9

Tài liệu này mô tả contract package hiện được triển khai trong compiler và CLI.
Local path package, dependency graph transitive, deterministic lockfile, cache theo
fingerprint và restore/install offline từ lockfile đã chạy end-to-end. Git/registry
transport và registry publish vẫn thuộc roadmap 0.9.

## Project layout

Một project V++ có thể dùng layout tối thiểu sau:

```text
project/
├── vpp.json
├── vpp.lock          # sinh bởi `vpp khóa`, có thể chưa tồn tại
└── gói/
    └── ten-goi/
        └── main.vi
```

`gói/` là tên thư mục chuẩn. Resolver vẫn nhận `goi/` và `packages/` để tương thích
project cũ. Package entry mặc định là `main.vi`.

## `vpp.json` schema 1

Manifest canonical:

```json
{
  "schema": 1,
  "name": "ung-dung",
  "version": "1.0.0",
  "dependencies": [
    {
      "name": "thu-vien",
      "version": "^1.2.0",
      "source": "path",
      "location": "../thu-vien"
    }
  ]
}
```

`name` bắt buộc khác rỗng. `version` phải là Semantic Versioning 2.0 đầy đủ
`major.minor.patch`. Dependency name không được trùng. `source` hiện có ba giá trị
được dành sẵn trong contract: `path`, `git`, `registry`. CLI 0.9 hiện resolve/cài trực
tiếp được `path`; Git/registry đã có schema nhưng solver từ chối rõ ràng cho tới khi
transport tương ứng được triển khai.

Reader vẫn nhận manifest cũ:

```json
{"name":"legacy","version":"0.1.0","gói":["mạng"]}
```

và nâng mỗi phần tử thành path dependency trong bộ nhớ. Khi ghi lại, CLI luôn dùng
schema 1 canonical.

## Version và range

Runtime package dùng cùng implementation SemVer. Các range hiện hỗ trợ:

```text
*
1.2.3
^1.2.3
~1.2.3
>=1.2.0 <2.0.0
```

Các comparator cách nhau bằng khoảng trắng được AND với nhau. OR range (`||`) chưa
thuộc contract 0.9 hiện tại và bị từ chối rõ ràng.

## Resolver

`PackageResolver` sở hữu toàn bộ policy biến import target thành source path. Compiler
registry chỉ nhận kết quả đã resolve và biên dịch source đó.

Thứ tự chính:

1. file/path thật trong project, tìm từ `CompilationContext.importResolutionBase` và đi lên;
2. compatibility redirect của layout package cũ;
3. bare package dưới `gói/`, `goi/`, `packages/`;
4. alias chuẩn như `vpp_core` → `lõi`;
5. package dưới `VPP_HOME` của bản cài đặt.

File project thật luôn được ưu tiên trước fallback package để không đổi nghĩa source cũ.

## `vpp.lock`

`vpp khóa` hoặc `vpp gói khóa` khóa trạng thái package vendored hiện tại:

```json
{
  "schema": 1,
  "packages": [
    {
      "name": "thu-vien",
      "version": "1.2.3",
      "source": "path",
      "location": "../thu-vien",
      "resolved": "gói/thu-vien",
      "fingerprint": "fnv1a64:..."
    }
  ]
}
```

Entry được sắp theo tên nên cùng trạng thái tạo cùng bytes. `fingerprint` được tính từ
relative path + bytes của toàn bộ regular file trong package tree, dùng để phát hiện
thay đổi và kiểm tra tính lặp lại. Đây là fingerprint reproducibility, không phải chữ ký
mật mã hoặc security integrity hash.

Khi package có `vpp.json`, `vpp khóa` dùng exact version của package đã cài và kiểm tra
nó có thỏa range trong manifest project hay không. Xung đột version làm lệnh thất bại.

`vpp cài đặt <nguồn>`, `vpp cập nhật`/`vpp đồng bộ` và `vpp khóa` đều giữ lockfile
đồng bộ với graph vừa materialize. Trước khi ghi lock, CLI đối chiếu version trong
package đã cài với exact version solver đã chọn để tránh tạo lockfile mang version mới
nhưng fingerprint của bytes cũ.

## Dependency graph

Solver duyệt toàn graph local path package theo thứ tự dependency-first. Cùng một
dependency được deduplicate nếu các range tương thích. Range xung đột bị từ chối với
tên dependency rõ ràng; cycle bị từ chối với đường đi xác định như `a -> b -> a`.

Package được materialize qua staging directory. `vpp.lock`, `.vpp/` và cây `gói/`
đã cài của source project không bị vendored vào artifact package con.

## Cache và offline

Project dùng cache content-addressed tại:

```text
.vpp/cache/fnv1a64-<digest>/
```

Sau install/sync hoặc khi khóa graph, bytes đã cài được snapshot vào cache bằng đúng
fingerprint ghi trong `vpp.lock`. Cache entry chỉ được dùng nếu fingerprint tính lại
vẫn khớp; entry bị sửa ngoài ý muốn không được coi là hợp lệ.

`vpp phục hồi` đọc `vpp.lock`, ưu tiên cache trước và chỉ quay về local path source khi
cache chưa có. Vì vậy một source path có thể bị xóa mà project vẫn phục hồi được chính
xác bytes đã khóa nếu snapshot còn trong `.vpp/cache`.

`vpp phục hồi --offline` (hoặc `--ngoại-tuyến`) là cache-only: thiếu bất kỳ fingerprint
nào thì lệnh thất bại thay vì truy cập source. `vpp cài đặt --offline` dùng cùng đường
restore này; `vpp cài đặt` không truyền source cũng cài lại graph từ lockfile.

`vpp xóa <tên>` resolve trạng thái manifest sau khi xóa trước khi commit, rồi ghi lại
lockfile cho graph còn lại để lock cũ không thể phục hồi nhầm package vừa bị xóa.

Khi chạy một file `.vi`, CLI đi lên từ thư mục source để tìm `vpp.lock` gần nhất. Nếu
project đã có lock, toàn bộ package được khóa phải tồn tại và fingerprint trên đĩa phải
khớp trước khi compiler chạy. Package bị sửa tay hoặc bị thiếu làm run/dump compile thất
bại với hướng dẫn `vpp phục hồi` hoặc `vpp khóa`; vì vậy một project đã khóa không thể âm
thầm chạy trên dependency bytes khác với lockfile.

## CLI dependency flow

Các lệnh local path hiện có:

```text
vpp cài đặt <nguồn> [tên]
vpp cập nhật          # alias workflow của đồng bộ
vpp đồng bộ
vpp khóa
vpp phục hồi [--offline]
vpp cài đặt --offline
vpp xóa <tên>
vpp danh sách
```

Các dạng `vpp gói ...` tương ứng dùng cùng implementation.

## Phần còn lại của Package 0.9

- Git source fetch và registry source fetch/publish;
- registry metadata/version selection và update policy cho remote package;
- cryptographic integrity khi registry contract được chốt.

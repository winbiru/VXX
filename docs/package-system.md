# Package system V++ 0.9

Tài liệu này mô tả contract package hiện được triển khai trong compiler và CLI.
Local path, Git và filesystem registry package, dependency graph transitive,
deterministic lockfile, cache theo fingerprint và restore/install offline từ lockfile
đã chạy end-to-end.

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
trong contract: `path`, `git`, `registry`; cả ba đều đi qua source materializer riêng,
không nằm trong solver graph. Git dependency có thêm `ref`; nếu manifest cũ bỏ trường này, reader dùng
`HEAD`:

```json
{
  "name": "thu-vien-git",
  "version": "^1.2.0",
  "source": "git",
  "location": "https://example.org/thu-vien.git",
  "ref": "stable"
}
```

Registry dependency dùng `name + version range` để chọn bản SemVer cao nhất phù hợp.
`location` là registry root; nếu để rỗng thì source nằm trong một registry artifact sẽ
kế thừa registry root bao quanh, còn project gốc dùng biến `VPP_REGISTRY`:

```json
{
  "name": "thu-vien-registry",
  "version": "^1.2.0",
  "source": "registry",
  "location": "/srv/vpp-registry"
}
```

## Registry v1

Registry 0.9 là filesystem-backed store, phù hợp local, shared disk hoặc mounted volume:

```text
registry/
├── vpp-registry.json
└── thu-vien/
    ├── 1.2.0/
    │   ├── vpp.json
    │   └── main.vi
    └── 1.4.0/
        ├── vpp.json
        └── main.vi
```

`vpp gói phát hành <registry-root>` xuất artifact hiện tại vào
`<registry>/<name>/<version>/`. Phiên bản đã phát hành là bất biến: phát hành lại cùng bytes
là idempotent, nhưng cùng `name@version` với bytes khác bị từ chối. `vpp.json` nằm trong
artifact là metadata tối thiểu cho name/version/dependencies; root có marker schema
`vpp-registry.json`.

Install hỗ trợ cả registry root tường minh và default root qua môi trường:

```text
vpp gói cài đặt registry+/srv/vpp-registry#thu-vien@^1.2.0
VPP_REGISTRY=/srv/vpp-registry vpp gói cài đặt registry:thu-vien@^1.2.0
```

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

1. file/path thật trong project, tìm từ thư mục của importer và đi lên hết các ancestor;
2. compatibility redirect của layout package cũ, chỉ sau khi không có local file thật;
3. bare package dưới `gói/`, `goi/`, `packages/`, ưu tiên ancestor gần importer;
4. alias chuẩn như `vpp_core` → `lõi` trong cùng bước package;
5. fallback dưới `VPP_HOME` của bản cài đặt.

Các phase trên không xen kẽ theo từng ancestor. Vì vậy một local file ở ancestor xa vẫn thắng
package trùng tên nằm gần hơn. Nested import bắt đầu resolve từ thư mục chứa chính module
importer; semantic module graph và codegen dùng cùng policy này. Khi base được truyền tường minh,
process current working directory không thay đổi kết quả. Contract được freeze bởi ADR 0003 và
collision matrix test `vpp-import-precedence-hardening`.

Compatibility note cho 1.0 RC: nếu source trước đây vô tình dựa vào việc package gần hơn thắng
local file ở ancestor xa, target sẽ đổi về local file theo contract đã chốt. Project muốn chọn
package rõ ràng nên tránh collision tên hoặc dùng target package/path không mơ hồ.

## `vpp.lock`

`vpp gói khóa` khóa trạng thái package vendored hiện tại:

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

Git entry lưu thêm `revision` là exact commit đã resolve từ `ref`. `ref` có thể là
branch/tag/commit trong `vpp.json`, nhưng `vpp.lock` luôn pin commit bất biến để restore
không phụ thuộc branch/tag có di chuyển về sau hay không.

Khi package có `vpp.json`, `vpp gói khóa` dùng exact version của package đã cài và kiểm tra
nó có thỏa range trong manifest project hay không. Xung đột version làm lệnh thất bại.

`vpp gói cài đặt <nguồn>`, `vpp gói cập nhật`/`vpp gói đồng bộ` và `vpp gói khóa` đều giữ lockfile
đồng bộ với graph vừa materialize. Trước khi ghi lock, CLI đối chiếu version và artifact
bytes đã cài với source vừa resolve. Vì vậy Git ref di chuyển nhưng package vendored còn
cũ làm `vpp gói khóa` thất bại và yêu cầu `vpp gói cập nhật`/`vpp gói đồng bộ`, thay vì tạo lockfile
ghép revision mới với fingerprint cũ.

## Dependency graph

Solver duyệt graph theo thứ tự dependency-first và nhận source materializer riêng, nên
logic graph/version không tự gọi Git hay đọc registry. Path, Git và registry có thể xuất hiện trong cùng graph;
cùng một dependency được deduplicate nếu các range tương thích. Range xung đột bị từ
chối với tên dependency rõ ràng; cycle bị từ chối với đường đi xác định như `a -> b -> a`.

Package được materialize qua staging directory. `.git/`, `vpp.lock`, `.vpp/` và cây
`gói/` đã cài của source project không bị vendored vào artifact package con.

## Cache và offline

Project dùng cache content-addressed tại:

```text
.vpp/cache/fnv1a64-<digest>/
```

Sau install/sync hoặc khi khóa graph, bytes đã cài được snapshot vào cache bằng đúng
fingerprint ghi trong `vpp.lock`. Cache entry chỉ được dùng nếu fingerprint tính lại
vẫn khớp; entry bị sửa ngoài ý muốn không được coi là hợp lệ.

`vpp gói phục hồi` đọc `vpp.lock` và ưu tiên cache trước. Khi cache chưa có, path package
quay về source path; Git package clone source rồi checkout exact `revision`; registry package
resolve exact version đã khóa từ registry root rồi kiểm fingerprint. Vì vậy branch/tag có
thể đã di chuyển hoặc registry có thêm version mới mà restore vẫn lấy đúng dependency đã khóa.

`vpp gói phục hồi --ngoại-tuyến` là chế độ chỉ dùng cache: thiếu bất kỳ fingerprint
nào thì lệnh thất bại thay vì truy cập source. `--offline` vẫn là alias tương thích.
`vpp gói cài đặt --ngoại-tuyến` dùng cùng đường restore này; `vpp gói cài đặt` không truyền
source cũng cài lại graph từ lockfile.

`vpp gói xóa <tên>` resolve trạng thái manifest sau khi xóa trước khi commit, rồi ghi lại
lockfile cho graph còn lại để lock cũ không thể phục hồi nhầm package vừa bị xóa.

Khi chạy một file `.vi`, CLI đi lên từ thư mục source để tìm `vpp.lock` gần nhất. Nếu
project đã có lock, toàn bộ package được khóa phải tồn tại và fingerprint trên đĩa phải
khớp trước khi compiler chạy. Package bị sửa tay hoặc bị thiếu làm run/dump compile thất
bại với hướng dẫn `vpp gói phục hồi` hoặc `vpp gói khóa`; vì vậy một project đã khóa không thể âm
thầm chạy trên dependency bytes khác với lockfile.

## CLI dependency flow

Các lệnh path/Git/registry hiện có:

```text
vpp gói cài đặt <nguồn> [tên]
vpp gói cài đặt git+<repository>[#<ref>] [tên]
vpp gói cài đặt registry:<tên>[@<range>]
vpp gói cài đặt registry+<root>#<tên>[@<range>]
vpp gói phát hành <registry-root>
vpp gói cập nhật          # cùng workflow với đồng bộ
vpp gói đồng bộ
vpp gói khóa
vpp gói phục hồi [--ngoại-tuyến]
vpp gói cài đặt --ngoại-tuyến
vpp gói xóa <tên>
vpp gói danh sách
```

Các tên lệnh tiếng Anh tương ứng vẫn được giữ làm alias tương thích cho script cũ.

## Giới hạn sau Package 0.9

- Registry 0.9 là filesystem-backed; hosted HTTP registry/auth/API server chưa thuộc contract này.
- `fingerprint` phục vụ reproducibility, chưa phải cryptographic signature/integrity proof.
- Publish signing, trust policy và registry authentication cần được chốt trước public hosted registry.

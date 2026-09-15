# Package system V++ 0.9

Tài liệu này mô tả contract package hiện được triển khai trong compiler và CLI.
Các phần cache, tải Git/registry và restore hoàn toàn từ lockfile vẫn thuộc roadmap 0.9.

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
được dành sẵn trong contract: `path`, `git`, `registry`. CLI 0.9 hiện cài trực tiếp
được `path`; Git/registry fetch và cache vẫn chưa triển khai.

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

## Phần còn lại của Package 0.9

- dependency solver cho graph transitive và conflict giữa nhiều requester;
- restore/install từ `vpp.lock`;
- cache/offline mode;
- Git source fetch và registry source fetch/publish;
- cryptographic integrity khi registry contract được chốt.

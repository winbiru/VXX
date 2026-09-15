# Quản lý kho API — project V++ thực tế

Đây là application V++ chạy độc lập, không phải regression fixture. Mục tiêu của project là
ép compiler/runtime đi qua một luồng ứng dụng dài: nhiều module, package dependency, object
model, interface, JSON, HTTP server, file I/O và exception nghiệp vụ.

## Kiến trúc

```text
quan-ly-kho-api/
├── vpp.json
├── vpp.lock                    # tạo bởi package manager
├── gói/ma-don-hang/            # dependency được vendor bởi CLI
├── data/
│   ├── san-pham.json
│   └── don-hang.json
└── src/
    ├── main.vi                 # HTTP server thật, port 8088
    ├── demo.vi                 # luồng end-to-end không block
    ├── app/bootstrap.vi        # composition root / dependency injection
    ├── domain/
    │   ├── san_pham.vi
    │   └── don_hang.vi
    ├── repository/
    │   ├── hop_dong.vi         # interface repository
    │   ├── san_pham_repository.vi
    │   └── don_hang_repository.vi
    ├── service/
    │   ├── kho_service.vi
    │   ├── don_hang_service.vi
    │   └── bao_cao_service.vi
    └── http/
        ├── controller.vi
        ├── router.vi
        └── server.vi
```

Dependency `ma-don-hang` nằm ở `../ma-don-hang`. Project dùng dependency này để sinh mã sản
phẩm/đơn hàng, vì vậy import `ma-don-hang` phải đi qua package resolver thay vì import file
trực tiếp.

## Cài dependency

Từ thư mục `examples/quan-ly-kho-api`:

```bash
../../build-sanitize-local/bin/vpp-cli gói cài đặt ../ma-don-hang
```

Lệnh này cập nhật manifest, vendor package vào `gói/ma-don-hang`, tạo `vpp.lock` và cache của
project.

## Chạy luồng end-to-end

```bash
../../build-sanitize-local/bin/vpp-cli chạy src/demo.vi
```

Demo thực hiện tuần tự:

1. Ghi seed JSON xuống file.
2. Đọc JSON thành list/map runtime và hydrate thành object `SảnPhẩm`.
3. Nhập kho.
4. Tạo đơn có SKU trùng nhau để kiểm tra bước gom số lượng bằng map.
5. Xuất kho và persist cả sản phẩm lẫn đơn hàng.
6. Cố tạo đơn vượt tồn kho và bắt exception nghiệp vụ.
7. Lập báo cáo doanh thu/tồn kho.
8. Hủy đơn và hoàn kho.
9. Đọc lại hai file JSON đã persist.

## Chạy HTTP API

```bash
../../build-sanitize-local/bin/vpp-cli chạy src/main.vi
```

Server lắng nghe `127.0.0.1:8088` qua native HTTP runtime.

Các endpoint:

| Method | Path | Ý nghĩa |
| --- | --- | --- |
| GET | `/health` | health check |
| GET | `/san-pham` | danh sách sản phẩm |
| POST | `/san-pham` | tạo sản phẩm |
| POST | `/san-pham/nhap-kho` | nhập thêm tồn kho |
| GET | `/san-pham/thong-ke` | thống kê kho |
| GET | `/don-hang` | danh sách đơn |
| POST | `/don-hang` | tạo đơn và trừ tồn |
| POST | `/don-hang/huy` | hủy đơn và hoàn tồn |
| GET | `/bao-cao` | tổng hợp doanh thu/tồn kho |

Ví dụ tạo đơn:

```bash
curl -X POST http://127.0.0.1:8088/don-hang \
  -H 'Content-Type: application/json' \
  -d '{"khachHang":"Minh Anh","sanPham":[{"maSanPham":"SP-1","soLuong":2}]}'
```

## Những phần V++ được stress

- import graph nhiều tầng và relative import;
- package resolver + manifest + lockfile + vendoring;
- class, constructor, method dispatch, instance fields;
- interface contract cho repository và HTTP controller;
- list/map mutation, nested collection và object nằm trong collection;
- JSON parse/serialize qua native hook;
- file/path/directory I/O qua native hook;
- HTTP server/request/response qua native hook;
- exception `ném` / `thử` / `bắt lỗi` qua nhiều tầng service;
- module initialization và export visibility;
- các call chain dài từ HTTP → service → repository → file → JSON → domain.

## Xác minh HTTP thật

Từ thư mục repo, sau khi build `build-sanitize-local/bin/vpp-cli`:

```bash
python3 examples/quan-ly-kho-api/scripts/verify_http.py
```

Script chạy bản sao ứng dụng trong thư mục tạm, kiểm tra 94 request qua TCP
localhost: toàn bộ endpoint, JSON có Unicode/dấu ngoặc kép, lỗi nghiệp vụ,
25 vòng đặt–hủy và dữ liệu sau khi khởi động lại. Cổng 8088 phải đang trống.
Server được dừng khi hoàn tất; script in thư mục chứa dữ liệu kiểm chứng.

# Quản lý kho API — project V++ thực tế

> Milestone: Warehouse Showcase 1.0

Đây là application V++ chạy độc lập, không phải regression fixture. Mục tiêu của project là
ép compiler/runtime đi qua một luồng ứng dụng dài: nhiều module, package dependency, object
model, interface, JSON, HTTP server, file I/O và exception nghiệp vụ.

## Yêu cầu — V++0.9

Project này được cập nhật để chạy trực tiếp bằng bản release **V++0.9**. Release được phát hành
ngày 15/09/2026 và hiện cung cấp binary dựng sẵn cho Linux x64 và macOS.

### macOS

```bash
curl -L https://github.com/winbiru/VXX/releases/download/V%2B%2B0.9/vpp-macos.tar.gz -o vpp-macos.tar.gz
tar -xzf vpp-macos.tar.gz
./install-vpp.sh
source ~/.zshrc
vpp giúp đỡ
```

### Linux x64

```bash
curl -L https://github.com/winbiru/VXX/releases/download/V%2B%2B0.9/vpp-linux-x64.tar.gz -o vpp-linux-x64.tar.gz
tar -xzf vpp-linux-x64.tar.gz
./install-vpp.sh
vpp giúp đỡ
```

Installer mặc định cài `vpp`, thư viện chuẩn `gói/`, `templates/` và toàn bộ `examples/` vào
`~/.local/bin`, đồng thời thiết lập `VPP_HOME`. Release V++0.9 hiện chưa có asset Windows.

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
    ├── feature_check.vi        # feature gate không mở HTTP server
    ├── transaction_check.vi    # kiểm tra rollback khi persist đơn thất bại
    ├── app/bootstrap.vi        # composition root / dependency injection
    ├── diagnostics/
    │   ├── feature_math.vi
    │   ├── feature_model.vi
    │   └── language_features.vi
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
vpp gói cài đặt ../ma-don-hang
```

Lệnh này cập nhật manifest, vendor package vào `gói/ma-don-hang`, tạo `vpp.lock` và cache của
project.

## Chạy luồng end-to-end

```bash
vpp chạy src/demo.vi
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
vpp chạy src/main.vi
```

Server mặc định lắng nghe `127.0.0.1:8088` qua native HTTP runtime. Có thể đổi port mà không
sửa source bằng biến môi trường:

```bash
VPP_WAREHOUSE_PORT=18088 vpp chạy src/main.vi
```

Các endpoint:

| Method | Path | Ý nghĩa |
| --- | --- | --- |
| GET | `/health` | health check |
| GET | `/features` | chạy feature gate và trả báo cáo coverage |
| GET | `/san-pham` | danh sách sản phẩm |
| POST | `/san-pham` | tạo sản phẩm |
| PUT | `/san-pham` | cập nhật tên/giá/tồn kho theo `id` trong body |
| DELETE | `/san-pham` | loại sản phẩm theo `id` trong body |
| POST | `/san-pham/chi-tiet` | lấy chi tiết sản phẩm theo `id` |
| POST | `/san-pham/tim-kiem` | tìm theo id/tên + phân trang |
| POST | `/san-pham/nhap-kho` | nhập thêm tồn kho |
| GET | `/san-pham/thong-ke` | thống kê kho |
| GET | `/don-hang` | danh sách đơn |
| POST | `/don-hang` | tạo đơn và trừ tồn |
| POST | `/don-hang/chi-tiet` | lấy chi tiết đơn theo `id` |
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
- compensation transaction: nếu lưu đơn thất bại sau khi đã trừ kho, service khôi phục snapshot tồn kho;
- module initialization và export visibility;
- các call chain dài từ HTTP → service → repository → file → JSON → domain.

Ngoài luồng nghiệp vụ trên, `src/diagnostics/language_features.vi` còn kiểm tra các feature
không tự nhiên xuất hiện trong CRUD kho: truthiness/equality, precedence và compound operator,
`chọn/ca`, `bỏ qua`, đệ quy, tham số mặc định, lambda/higher-order function, closure capture,
module alias/namespace, set/tuple, collection lồng nhau, inheritance + `gốc`, visibility,
interface inheritance, Unicode, conversion, math, config/time và JSON roundtrip.

Chạy riêng feature gate:

```bash
vpp chạy src/feature_check.vi
```

Khi tất cả contract đều còn hoạt động, lệnh in một JSON với toàn bộ nhóm feature bằng `1`.
`src/main.vi` cũng chạy gate này trước khi mở port 8088, nên regression ngôn ngữ sẽ làm ứng
dụng fail-fast thay vì khởi động với behavior sai.

Có thể chạy cùng gate dưới các mode runtime:

```bash
VPP_ENABLE_JIT=1 vpp chạy src/feature_check.vi
VPP_ENABLE_GC=1 VPP_GC_INTERVAL=1 vpp chạy src/feature_check.vi
```

Kiểm tra riêng contract rollback của luồng đặt hàng:

```bash
vpp chạy src/transaction_check.vi
```

Test dùng một `KhoĐơnHàng` cố ý phát sinh lỗi ở bước persist và xác nhận `tonKho`, `daBan`,
`phienBan` của sản phẩm được khôi phục về snapshot trước transaction.

Các giới hạn vẫn không thể chứng minh đầy đủ bằng một application `.vi`: malformed bytecode
hoặc invariant VM nội bộ, negative compile-time cases, LSP/formatter/package CLI/installer,
và các đặc tính phi chức năng như hiệu năng JIT hay thời điểm GC chạy.

## Xác minh HTTP thật

Sau khi cài V++0.9 và đứng tại thư mục `examples/quan-ly-kho-api`:

```bash
python3 scripts/verify_http.py
```

Script chạy bản sao ứng dụng trong thư mục tạm, tự chọn một localhost port đang trống (hoặc dùng
`VPP_WAREHOUSE_PORT` nếu được đặt), kiểm tra transaction rollback, feature gate,
CRUD/search sản phẩm, request health đồng thời và toàn bộ endpoint qua TCP localhost. Sau đó script
chạy 25 vòng đặt–hủy và xác minh dữ liệu vẫn đúng sau khi server khởi động lại.

Nếu môi trường sandbox cấm bind localhost socket, script kết thúc với mã `77` và thông báo `SKIP`
để phân biệt giới hạn hạ tầng với lỗi của application/runtime.

Script ưu tiên executable theo thứ tự `VPP_EXEC`, build local của repository, binary `vpp` nằm
cạnh thư mục `examples` trong release, rồi mới tới `vpp` trong `PATH`. Vì vậy cùng một script
có thể dùng cho cả release V++0.9 lẫn quá trình phát triển compiler tại local.
Server được dừng khi hoàn tất; script in thư mục chứa dữ liệu kiểm chứng.

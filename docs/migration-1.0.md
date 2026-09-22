# Migration lên V++ 1.0

Tài liệu này tập trung vào những contract đã được chốt trước 1.0 và các alias tương thích còn
được giữ cho source/script cũ.

## CLI

Tên canonical dùng tiếng Việt:

| Trước đây / alias | 1.0 canonical |
| --- | --- |
| `new` | `khởi tạo` |
| `run` | `chạy` |
| `test` | `kiểm thử` |
| `build` | `dựng` |
| package commands tiếng Anh | namespace `gói` + tên lệnh tiếng Việt |

Alias cũ vẫn chạy trong 1.0 để giảm gãy script, nhưng tài liệu và project mới nên dùng tên
canonical.

## Import

Target import được viết trực tiếp:

```vi
nhập lõi;
nhập nhập xuất;
nhập gói/mạng/rest;
nhập feature_math như toán;
```

Mỗi câu `nhập` chỉ nhận một target. Module cần re-export dependency phải dùng `công khai nhập`.
Import thường không tự chuyển tiếp symbol cho module phía ngoài.

Precedence 1.0 cho bare/extensionless import là local file trước package trên toàn bộ chuỗi
ancestor được phép tìm kiếm; vì vậy local file ở ancestor xa hơn vẫn thắng package trùng tên
ở ancestor gần hơn. Resolution dùng thư mục của importer/context, không dùng process cwd.
Compatibility redirect/alias chỉ chạy sau local lookup; `VPP_HOME` là fallback cuối.

## Object model

Receiver 1.0 là `mình`; gọi superclass qua `gốc`:

```vi
lớp Con kế thừa Cha {
    hàm công khai môTả() {
        trả về gốc.môTả() + " / con";
    };
}
```

Constructor dùng `hàm khởi tạo(...)`. Constructor lớp cha không tự chạy; gọi tường minh
`gốc.khởi tạo(...)` khi cần. Cú pháp kế thừa canonical là `kế thừa`; alias cũ chỉ nên dùng cho
source cần tương thích.

Interface dùng `giao diện` và class dùng `triển khai`. Interface là contract compile-time trong
1.0, chưa có runtime interface introspection.

## Dynamic typing và call boundary

V++ 1.0 chốt dynamic typing. Parameter bắt buộc bị thiếu hoặc đối số dư không còn được runtime
âm thầm bù giá trị. Direct call có thể bị chặn ở compile time; dynamic/indirect call được kiểm
tra khi runtime resolve callable thật.

## Closure

Closure capture binding bằng tham chiếu tới shared cell. Nếu code cũ giả định lambda giữ một
snapshot giá trị tại thời điểm tạo, hãy tạo binding riêng trước khi tạo closure.

## Module visibility

Top-level public/default declaration thuộc export surface. Symbol `riêng tư`/`bảo vệ` không thể
được truy cập qua import. Local import cycle giờ bị từ chối có diagnostic đường đi thay vì bị
bỏ qua ngầm.

## Package và lockfile

Project mới nên dùng `vpp.json` schema 1 và commit `vpp.lock` khi cần reproducibility. Lockfile
pin exact version/revision + fingerprint. Nếu dependency đã cài bị sửa tay, CLI từ chối run/build
cho tới khi restore hoặc cập nhật lock có chủ ý.

Registry 0.9/1.0 hiện là filesystem registry. Hosted registry/auth/signing không nằm trong
contract phát hành này.

## String và Unicode

Length/reverse/index/slice dùng code point UTF-8. Các API text tiếng Việt dùng NFC/case mapping
theo phạm vi 1.0. Code cũ dựa vào byte offset cho chuỗi UTF-8 cần chuyển sang semantics code point.

## Exception và runtime error

`bắt lỗi` bắt giá trị do `ném`. VM fault là lỗi fatal của lần `run()` hiện tại và không bị
`bắt lỗi` nuốt. Các native/runtime boundary đã chặt hơn, vì vậy input từng bị cắt/ngầm chấp nhận
như số thực có phần lẻ ở API integer có thể bị từ chối rõ ràng trong 1.0.

## HTTP JSON helper

Các helper HTTP 1.0 đọc JSON theo parser JSON thật và chỉ lấy exact top-level key có giá trị
chuỗi. Hành vi cũ dựa trên regex có thể từng match nhầm key lồng nhau hoặc đoạn văn bản giống
JSON nằm trong string; các false match đó không còn được xem là compatibility behavior.

## Checklist migration

1. Chạy formatter/linter trên source.
2. Chuyển command/script sang tên CLI canonical khi thuận tiện.
3. Kiểm tra import/re-export và visibility module.
4. Kiểm tra constructor, `mình`/`gốc`, interface/override.
5. Sinh/cập nhật `vpp.lock` và restore dependency sạch.
6. Chạy `vpp dựng` và `vpp kiểm thử` toàn project.
7. Kiểm tra lại code xử lý Unicode, integer boundary và runtime exception.

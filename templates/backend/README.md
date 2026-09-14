# V++ backend OOP

## Chạy

```bash
vpp application.vi
```

Server đọc cổng từ `application.properties` (mặc định: `8080`). Sửa giá trị
`port` trước khi chạy nếu cổng đó đang được dùng.

Với cấu hình mặc định, server lắng nghe tại `http://127.0.0.1:8080`.

```bash
curl http://127.0.0.1:8080/health
```

Endpoint `/health` trả JSON boolean `true`; các path khác trả `404` với JSON `null`.

## Cấu trúc

- `application.vi`: composition root dựng object graph; `ApiApplication` nhận server qua constructor.
- `config.vi`: object đọc cấu hình.
- `controller.vi`: interface `TrìnhXửLýHttp` và controller.
- `router.vi`: router nhận controller bằng constructor injection.
- `server.vi`: HTTP server adapter nhận router bằng constructor injection.

Luồng chính: `main -> MáyChủApi -> BộĐịnhTuyến -> Controller`; sau khi ghép xong,
`MáyChủApi` được inject vào `ApiApplication` để quản lý vòng đời.

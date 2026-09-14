# V++ HTTP API OOP example

## Chạy

Từ thư mục gốc của repository:

```bash
cd examples/api_project
./start_api_project.sh
```

Hoặc, khi `vpp` đã có trong `PATH`:

```bash
vpp examples/api_project/application.vi
```

Server đọc cổng từ `application.properties`; mặc định là `8080`. Thay đổi
`port=8080` trong file đó trước khi chạy để dùng cổng khác.

## Kiến trúc OOP

`main` là composition root: chỉ tạo object và nối dependency. Request được xử lý bằng
các instance chuyên trách, còn `ApiApplication` chỉ sở hữu vòng đời server:

```text
CấuHìnhApi
     │
     └──> MáyChủApi ──> BộĐịnhTuyến ──> HealthController
             │              │
             │              └──────────> NotFoundController
             │
             └──> ApiApplication
```

- `config.vi`: bao đóng nguồn cấu hình.
- `controller.vi`: interface và các HTTP controller.
- `router.vi`: định tuyến request, nhận controller qua constructor.
- `server.vi`: adapter vòng đời HTTP server, nhận router qua constructor.
- `application.vi`: composition root dựng object graph; `ApiApplication` nhận server qua constructor.

Ví dụ hiện có một endpoint nghiệp vụ; route không tồn tại đi qua controller 404 riêng.

## Health

```bash
curl -s http://localhost:8080/health
```

Endpoint trả JSON `true`. Các path khác trả `404` với JSON `null`.

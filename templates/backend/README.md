# V++ backend

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

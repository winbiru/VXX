# V++ HTTP API example

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

Ví dụ chỉ có một endpoint.

## Health

```bash
curl -s http://localhost:8080/health
```

Endpoint trả JSON `true`. Các path khác trả `404` với JSON `null`.

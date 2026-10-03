# Ứng dụng V++

Project được tạo bằng `vpp khởi tạo ứng dụng <tên>`.

## Chạy

```bash
vpp chạy src/chính.vi
```

## Dựng và kiểm thử

```bash
vpp dựng src/chính.vi
vpp kiểm thử tests
vpp --soát-lỗi src
vpp --định-dạng src --kiểm-tra
```

`vpp.json` là manifest schema 1. Thư mục `gói/` được dùng cho dependency đã cài;
`.vpp/` chứa cache/state cục bộ và không nên commit.

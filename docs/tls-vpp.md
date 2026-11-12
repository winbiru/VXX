# Mật mã TLS bằng V++

## Phạm vi đã triển khai

`gói/bảo mật/khóa TLS.vi` và `gói/bảo mật/bản ghi TLS.vi` dùng byte/list,
SHA-256 và phép toán V++ để xử lý các khối dưới đây. Không gọi libssl,
OpenSSL, SecureTransport, C++ VM hoặc libffi trong đường tính toán này.

| Thành phần | Trạng thái |
| --- | --- |
| HMAC byte, HKDF-Extract/Expand SHA-256 | Đã triển khai; vector RFC 5869 PASS |
| TLS 1.3 Expand-Label và Derive-Secret | Đã triển khai; key/IV được đối chiếu bằng Python hmac/hashlib |
| ChaCha20 và Poly1305 | Vector RFC 8439 PASS; biên Poly1305 đối chiếu oracle số nguyên lớn Python |
| AEAD ChaCha20-Poly1305 | Mã hóa/giải mã, RFC vector và từ chối tag bị thay đổi PASS |
| Codec TLS 1.3 record | Header/AAD, content type/padding, sequence/nonce từng chiều và trạng thái lỗi đã triển khai |
| X25519/ECDHE, ClientHello/ServerHello, handshake transcript/Finished | Chưa triển khai thành TLS client |
| X.509, chữ ký chuỗi CA, thời hạn, SAN/hostname, SNI | Chưa triển khai provider xác thực |
| Opcode 206–208 của standalone VM | Tiếp tục fail-closed; chưa dùng các primitive mới để mở kết nối |

Đối tượng `Bản Ghi TLS VPP(secret, hướng)` nhận traffic secret SHA-256
32 byte; `hướng=1` chỉ gửi, `hướng=0` chỉ nhận. Đối tượng sinh key/IV cho
TLS_CHACHA20_POLY1305_SHA256, dùng sequence 64 bit cho nonce, giới hạn
plaintext 16.384 byte và ciphertext 16.640 byte. Tag phải hợp lệ trước
khi tính/trả plaintext. Lỗi giải mã/định dạng làm state nhận không dùng
tiếp được. Codec không tự tạo hoặc xác thực traffic secret.

Các primitive đang là thành phần triển khai/kiểm thử, chưa có kiểm toán
side-channel hoặc chứng minh thời gian chạy không phụ thuộc khóa. Không
được nối vào public TLS API trước khi hoàn thành và kiểm chứng handshake,
trao đổi khóa, chữ ký/chứng chỉ, trust store, hostname và cleanup.

## Kiểm chứng

`scripts/quality/check_vm_tls_crypto.cmake` biên dịch fixture
`src/tests/fixtures/vm_tls_crypto.vi` rồi chạy trên VM V++/ASM với PATH rỗng.
Test nằm trong `vpp-vm-asm`, kiểm tra vector ChaCha20, Poly1305, HKDF và
AEAD; 16 vị trí tag bị sửa; key/nonce/byte/độ dài sai; record với padding;
hai record liên tiếp; header/AAD mẫu đối chiếu oracle; state đã hỏng và
đường gửi/nhận sai. Các trường hợp Poly1305 toàn byte 255 ở độ dài
0/1/15/16/17/31/32/33/64 bắt lỗi carry/reduction tại biên block.

Kết quả nằm trong lượt macOS 4/4 PASS và Ubuntu linux/amd64 Docker 5/5 PASS
ngày 28/10/2026. Đây là kiểm chứng primitive/record, **chưa phải handshake
TLS end-to-end hoặc chứng chỉ hợp lệ/sai CA/sai hostname/hết hạn**.

Tham chiếu thuật toán: [RFC 5869](https://www.rfc-editor.org/rfc/rfc5869),
[RFC 8439](https://www.rfc-editor.org/rfc/rfc8439),
[RFC 8446](https://www.rfc-editor.org/rfc/rfc8446).

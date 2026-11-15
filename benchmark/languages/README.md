# Benchmark ứng dụng V++ AOT

Chạy bộ đo native sau khi dựng bộ biên dịch V++:

```sh
make benchmark
# Hoặc dùng bộ biên dịch đã dựng:
VPP_EXEC="$PWD/build-pure-vpp/bin/vpp" bash benchmark/languages/run.sh
```

Mặc định bộ đo native chạy 21 workload, 9 mẫu mỗi workload, mục tiêu 60 ms
mỗi batch. Có thể điều chỉnh bằng `VPP_BENCH_RUNS` và
`VPP_BENCH_TARGET_MS`. Kết quả lưu vào một thư mục mới dưới
`benchmark/reports/` (hoặc đường dẫn truyền vào run.sh).
Bộ đo so sánh đa ngôn ngữ vẫn mặc định 6 bài cũ;
`--extended` thêm các bài mới cho V++ AOT được biên dịch từ workloads.vi.
Các runtime khác chỉ chạy những bài có trong manifest của chúng.

| Nhóm | Workload | Đơn vị của một lượt |
| --- | --- | --- |
| Số nguyên | arithmetic, branch_loop, fibonacci | Một phép tính, 256 nhánh lặp, hoặc một Fibonacci |
| Số nguyên | gcd_64, primes_256 | 64 UCLN, hoặc đếm số nguyên tố đến 256+k |
| Số thực | float_256 | 256 bước truy hồi và cộng số thực |
| Hàm | recursion | Fibonacci đệ quy với n=10..14 |
| Ma trận | matrix_4 | Tạo hai ma trận 4×4, nhân và tổng có trọng số |
| Danh sách | list_128, sort_64 | Tạo/lọc/tổng 128 phần tử, hoặc tạo/sắp xếp 64 phần tử |
| Tìm kiếm | binary_search_128 | Một tìm kiếm nhị phân trong danh sách chuẩn bị trước |
| Từ điển | map_64 | 64 lần chèn và 64 lần tra cứu, khóa chuẩn bị trước |
| Cấp phát | objects_32 | Tạo 32 từ điển chứa danh sách và duyệt |
| Văn bản | unicode_replace, concat_64, split_32, unicode_normalize | Thay Unicode, nối 64 đoạn, tách 32 số, hoặc chuẩn hóa tiếng Việt |
| JSON | json_parse, json_encode, json_nested | Phân tích bài gốc, tạo JSON mảng, hoặc phân tích 8 bản ghi lồng nhau |
| Ngoại lệ | exceptions_16 | 16 lần try/catch, trong đó 4 lần ném và bắt lỗi |

Thời gian là trung vị của **một lượt workload**, không phải từng phép toán
trong workload. Các đầu vào thay đổi theo 16 giá trị seed 17..32.
Chuẩn bị fixture, 16 kiểm tra đầu vào, 3 lượt làm nóng, biên dịch và khởi
động nằm ngoài batch đo. Vòng lặp bộ đo, checksum, xử lý bộ nhớ và GC còn
lại sau tối ưu nằm trong thời gian đo. Các bài concat/normalize/encode
còn kiểm tra toàn bộ chuỗi tạo ra trong lượt đo.

Python tính kết quả tham chiếu độc lập cho 16 đầu vào và checksum toàn batch.
Báo cáo lưu cả mẫu thô, số lượt, hash executable, trung vị và MAD.
Nếu checksum hoặc kiểm tra thời gian không đúng, bộ đo kết thúc với lỗi.
Không suy rộng một workload thành tốc độ chung của V++.

Bộ đo này chưa đo I/O đĩa, mạng, đa luồng, RSS hay thời gian tự biên dịch.
Không coi bài tạo object là phép đo riêng chi phí GC hay OOP dispatch.

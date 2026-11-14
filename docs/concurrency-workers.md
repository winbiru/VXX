# Worker V++ trên Native AOT

Worker chạy một hàm V++ trên **OS thread riêng**, với `Program`, call frames,
runtime heap và tài nguyên file riêng. Chức năng này dùng được khi biên dịch
bằng compiler V++ tự biên dịch (`--compiler=vpp`).

```vpp
nhập gói.lõi.đồng thời.*;

hàm chính() {
    dữ_liệu = [2, 3];
    mã = chạy luồng(hàm(x) {
        x[0] = x[0] * 10;
        trả về x;
    }, [dữ_liệu]);

    kết_quả = chờ luồng(mã, 1000);
    in kết_quả[0];  // 20
    in dữ_liệu[0];  // 2: dữ liệu gốc không bị thay đổi
};
```

Các API trong `gói/lõi/đồng thời.vi`:

| API | Ý nghĩa |
| --- | --- |
| `chạy luồng(hàm, [đối_số])` | Tạo worker và trả mã nguyên dương. Chấp nhận hàm, closure hoặc tên hàm hợp lệ. |
| `chờ luồng(mã, timeout = rỗng)` | Chờ kết quả. `rỗng` tương đương `-1` (vô hạn), `0` chỉ thăm dò. Trả về `rỗng` khi hết thời gian, ném lỗi khi worker lỗi hoặc bị hủy. |
| `hủy luồng(mã)` | Yêu cầu dừng hợp tác. Trả `đúng` khi yêu cầu được chấp nhận; worker dừng ở safepoint, `luồng máy ảo nghỉ` hoặc khi đang chờ worker khác. |
| `trạng thái luồng(mã)` | Trả `đang chạy`, `hoàn tất`, `đã hủy` hoặc `lỗi`. |
| `Luồng Công Việc` | Đối tượng quản lý vòng đời với `chờ`, `trạng thái`, `hủy`, `đóng`. `đóng` hủy và chờ worker rồi đánh dấu đã đóng. |

Đối số, closure captures, globals và kết quả được **deep copy** giữa các
worker; các chu trình và các tham chiếu dùng chung bên trong một graph được
bảo toàn. Worker con có thể tạo và chờ worker khác. Runtime phát hiện tự chờ
và chu trình chờ để báo lỗi thay vì deadlock.

Hủy là **cooperative cancellation**, không dùng lệnh cưỡng bức kết thúc OS
thread. Mã native hoặc lệnh I/O chặn lâu có thể trì hoãn việc hủy; nếu cần
dừng nhanh, nên bố trí các điểm kiểm tra hoặc thời gian chờ hữu hạn. Khi
`Program` gốc kết thúc, runtime yêu cầu hủy rồi join tất cả worker còn sống.

Chạy regression riêng:

```sh
cmake --build build --target vpp-cli --parallel 2
ctest --test-dir build -R '^vpp-native-aot-workers$' --output-on-failure
```

CTest sử dụng fixture `native-cli-compiler` để biên dịch và chạy
`scripts/test/kiem_tra_native_worker.vi` bằng `--compiler=vpp`.

## Worker trong bộ biên dịch V++

Với đồ thị có nhiều mô-đun, compiler self-hosted dùng worker V++ để hạ
AST đã chuẩn hóa kiểu thành IR. Mỗi worker sở hữu bản sao dữ liệu riêng;
sổ ký hiệu, function ID, StringPool và phát bytecode/liên kết vẫn xử lý
tuần tự để bảo toàn bytecode khi dùng import lồng nhau.

`VPP_COMPILER_WORKERS` mặc định là `2`; nhận số nguyên `0..8`, trong đó
`0` và `1` giữ cách hạ IR tuần tự. Mô-đun đơn luôn đi đường tuần tự.
Khi worker báo lỗi, compiler hủy và chờ những worker còn lại trước khi
trả lỗi. Đối với đồ thị lớn, cân nhắc RAM vì worker sao chép dữ liệu AST
và runtime; tăng worker không đảm bảo cải thiện thời gian biên dịch.

```sh
VPP_COMPILER_WORKERS=2 build/bin/vpp-cli build --compiler=vpp main.vi -o main.vbc
ctest --test-dir build -R '^vpp-compiler-workers-parity$' --output-on-failure
```

Bài kiểm thử parity so sánh artifact khi dùng `0`, `1`, `2`, `4`, `8` worker,
bao gồm cả trường hợp chỉ có một import, sau đó chạy bytecode sinh từ
compiler nhiều worker. Giá trị cấu hình sai hoặc ngoài `0..8` phải báo lỗi.

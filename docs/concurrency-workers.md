# Worker V++ trên Native AOT và standalone VM

Hợp đồng worker V++ yêu cầu chạy một hàm trên **OS thread riêng**, với
`Program`, call frames và runtime heap riêng. Runtime thuần V++/ASM triển
khai opcode 139–143 qua machine ABI 86, dùng pthread trên macOS ARM64/x86_64,
`clone`/`futex` trên Linux x86_64 và `_beginthreadex` trên Windows x64. Cả Native AOT và standalone VM có đường
khởi chạy worker riêng, không cần runtime C/C++. macOS ARM64 đã chạy kiểm
thử thực tế; macOS x86_64 đã chạy machine regression qua Rosetta, Linux
x86_64 đã chạy machine regression freestanding, regression V++ worker trên
VM và Native AOT trong container. Compiler chạy trong VM Linux cũng tạo
bytecode giống từng byte ở 0/1/2/4/8 worker. Cần xác nhận thêm trên runner
GitHub Actions; Windows x64 hiện mới cross-assemble.
Linux ARM64 chưa có OS worker; yêu cầu tạo worker trên nền tảng này báo lỗi.
Seed bootstrap Windows i32 cũ dùng ABI giới hạn và chỉ biên dịch tuần tự;
biên dịch boundary với `VPP_WINDOWS_I32_BOOTSTRAP` giữ nguyên catalog import
và không có vùng BSS worker. Các boundary native, raw JIT và VM đầy đủ
của compiler hiện tại vẫn hỗ trợ worker thật.

Linux x86_64 tiếp tục liên kết tĩnh, không cần libc/pthread: worker dùng
stack riêng 64 MiB có guard page, `clone` chia sẻ không gian địa chỉ,
`CLONE_CHILD_CLEARTID` và `futex` để join. Arena, worker identity và errno
nằm trong context GS riêng; FS không bị thay đổi. Callback raw Linux chỉ
được gọi V++/ASM/syscall, vì thread không được khởi tạo bởi libc. Root có
GS do host khác quản lý sẽ bị từ chối khi bật worker.

macOS x86_64 dùng pthread TLS và stack 64 MiB. Hợp đồng machine worker đã
được triển khai độc lập với compiler; pipeline tự biên dịch đầy đủ trên
macOS vẫn yêu cầu ARM64 theo cấu hình build hiện tại.

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

Kết quả được giữ dưới dạng snapshot bất biến: mỗi lần `chờ luồng` giải mã
một bản sao riêng. Sau callback V++ kết thúc, runtime thu gọn arena thành
gói kết quả (hoặc giải phóng toàn bộ nếu không có gói); thread đã kết thúc
được join khi quan sát trạng thái cuối hoặc khi root đóng registry. Callback
machine ABI thô vẫn giữ arena theo hợp đồng của nó. Registry hiện có tối đa
4096 handle trong một vòng đời root, không tái sử dụng handle; các gói kết
quả được giữ đến khi root đóng. Cancellation không đảm bảo ngắt I/O đang chặn.

Chạy regression trên runtime thuần hiện tại để kiểm tra mức hỗ trợ thực tế:

```sh
python3 scripts/quality/check_worker_machine.py
cmake --build build --parallel 2
ctest --test-dir build -R '^vpp-(worker-machine|compiler-workers)$' --output-on-failure
```

Bài kiểm thử `scripts/test/kiem_tra_native_worker.vi` tiếp tục yêu cầu worker
thật và sẽ báo lỗi nếu runtime chưa hỗ trợ.

## Worker trong bộ biên dịch V++

Với đồ thị có nhiều mô-đun, compiler self-hosted dùng worker V++ để hạ
AST đã chuẩn hóa kiểu thành IR. Mỗi worker sở hữu bản sao dữ liệu riêng;
sổ ký hiệu, function ID, StringPool và phát bytecode/liên kết vẫn xử lý
tuần tự để bảo toàn bytecode khi dùng import lồng nhau.

`VPP_COMPILER_WORKERS` mặc định là `0`; nhận số nguyên `0..8`, trong đó
`0` và `1` giữ cách hạ IR tuần tự. Với `2..8`, compiler sử dụng OS worker
trên host hỗ trợ, kể cả đồ thị chỉ có mô-đun gốc (một job). Compiler không tự
chuyển yêu cầu song song tường minh thành chạy đồng bộ trên host không hỗ trợ.
API linker trực tiếp cũng kiểm tra tag số nguyên và miền `0..8` trước khi
thay đổi đồ thị; `đúng`/`sai` có cùng tag với `1`/`0` theo runtime V++.
Khi worker báo lỗi, compiler hủy và chờ những worker còn lại trước khi
trả lỗi. Đối với đồ thị lớn, cân nhắc RAM vì worker sao chép dữ liệu AST
và runtime; tăng worker không đảm bảo cải thiện thời gian biên dịch.

Gate `scripts/quality/check_compiler_workers.cmake` kiểm tra cả compiler
native và compiler thực thi trong standalone VM, so sánh bytecode khi dùng
`0`, `1`, `2`, `4`, `8` worker, kiểm tra đồ thị một import/một mô-đun,
số job thực sự đã gửi, số job chờ tối đa, cấu hình sai và chạy bytecode
sinh ra. Gate cũng so sánh từng byte của executable Native AOT ở cả năm
cấu hình worker và chạy từng executable để kiểm tra kết quả.

Trong phép đo cục bộ trên macOS ARM64 với đồ thị 13 mô-đun nhỏ, bản
compiler trước khi bổ sung kiểm tra tham số linker mất khoảng 20–21 giây
khi chạy tuần tự và 24 giây khi dùng 2, 4 hoặc 8 worker. Chi phí snapshot,
khởi tạo worker và đồng bộ lớn hơn lợi ích hạ IR song song trên workload
này. Chỉ bước hạ IR chạy song song; mặc định vẫn là tuần tự cho tới khi
có benchmark đại diện cho dự án lớn chứng minh lợi ích. Phép kiểm tra
mở rộng trên chính mã nguồn compiler VM (39 mô-đun) cũng cho bytecode
giống từng byte khi compiler native hoặc compiler trong standalone VM dùng
0, 2, 4 và 8 worker; các cấu hình song song đều gửi 10 job và đạt mức đồng
thời yêu cầu. Hai thế hệ tự biên dịch Native AOT tuần tự tiếp theo cũng
tạo executable giống từng byte với compiler đầu vào trên macOS ARM64.

Job `posix-worker-machine` trong `.github/workflows/native-vpp.yml` chạy
machine regression trực tiếp trên `ubuntu-24.04` và `macos-15-intel`.
Linux dùng executable ASM freestanding để kiểm tra thread thật mà không
đưa callback Python/libc vào thread raw. VM Linux được build bằng
`scripts/linux/build-vpp-vm.sh`; chỉ boundary của VM tham chiếu callback
`vm_worker_entry`. Job `linux-full` chạy thêm gate compiler worker sau
khi đóng gói VM hiện tại.

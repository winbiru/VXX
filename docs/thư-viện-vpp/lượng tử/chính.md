# Lập trình lượng tử

Spec ID: `VPP-QUANTUM-001`, phiên bản `0.1.0`, trạng thái `IMPLEMENTED`.
API thuần V++ và cầu nối Aer đã được kiểm tra cục bộ; adapter QPU được kiểm tra
bằng service giả, chưa xác minh trên phần cứng thật.

Gói `vi.quantum` được import bằng `nhập lượng tử;`, phụ thuộc collection và
PRNG của V++. Gói tùy chọn `vi.quantum.qiskit` dùng tiến trình và JSON.
Không tự import qua gói `chuẩn`.

## Mục tiêu và phạm vi

Viết mạch lượng tử bằng V++, mô phỏng mạch nhỏ và xuất OpenQASM 3.0 để dùng
với hệ sinh thái Qiskit. Phiên bản này hỗ trợ cổng rời rạc, phép đo theo cơ sở Z
và reset. Cổng quay có tham số, mô hình nhiễu, điều kiện cổ điển giữa mạch,
QIR, pulse và bảo đảm tăng tốc lượng tử nằm ngoài phạm vi.

## Mô hình và invariant

- Mạch là danh sách chỉ dẫn, không phải các biến chứa qubit vật lý độc lập.
- Thanh ghi có `n` qubit và `b` bit cổ điển, đều trong miền `1..1024`.
- Mỗi lần chạy bắt đầu ở `|0...0>`; bit chưa được ghi có giá trị 0 trong bộ mô
  phỏng V++ và Aer. Chuỗi kết quả theo thứ tự `c[b-1]...c[0]`, qubit 0 là bit
  thấp nhất trong chỉ số vector trạng thái.
- `các lệnh()` trả bản sao sâu của chỉ dẫn. `trạng thái()` trả dữ liệu độc lập
  ở mỗi lần gọi. Các trường lưu trữ của instance là chi tiết nội bộ.
- Một lần đo làm sụp đổ đúng qubit được đo và chuẩn hóa trạng thái còn lại.
  Reset tương đương đo qubit, sau đó áp dụng X nếu kết quả là 1; kết quả đo
  nội bộ không ghi đè bit cổ điển.
- Đo cùng bit nhiều lần giữ lần ghi cuối. Mỗi shot chuẩn bị lại mạch.
- Không sửa cùng một mạch đồng thời từ nhiều luồng. Không có tài nguyên native
  phải đóng trên các đối tượng mạch/bộ mô phỏng.

## Yêu cầu

| ID | Contract |
| --- | --- |
| FR-01 | Mạch MUST hỗ trợ H, X, Y, Z, S, T, CX, measure, reset và từ chối chỉ số sai trước khi thêm chỉ dẫn. |
| FR-02 | Bộ mô phỏng MUST tính biên độ phức, pha và giao thoa, xác suất Born, phép đo giữa mạch và reset. |
| FR-03 | Kết quả MUST dùng cùng thứ tự bit và ánh xạ qubit/bit như Qiskit; hạt giống giống nhau MUST tái lập kết quả V++. |
| FR-04 | Bộ xuất MUST tạo OpenQASM 3.0 giữ thứ tự cổng, ánh xạ phép đo và reset. |
| FR-05 | Cầu nối MUST mô phỏng bằng Aer; gửi QPU MUST chỉ xảy ra khi gọi `gửi` với backend tường minh. |
| FR-06 | API QPU MUST trả job ID khi gửi, và chỉ đọc kết quả khi trạng thái DONE. |
| NFR-01 | Vector trạng thái MUST bị chặn theo giới hạn qubit trước khi cấp phát. |
| NFR-02 | Cầu nối MUST dùng argv, không ghép shell; MUST không chuyển token qua argv. |
| NFR-03 | Import gói MUST không gửi job, tải SDK hay truy cập tài khoản. |
| NFR-04 | Đo cuối mạch SHOULD tái sử dụng vector trạng thái và dùng CDF để tránh mô phỏng lại từng shot. |

## API contract

```vi
Mạch Lượng Tử(số qubit, số bit = rỗng);
mạch.số qubit();
mạch.số bit();
mạch.h(q); mạch.x(q); mạch.y(q); mạch.z(q); mạch.s(q); mạch.t(q);
mạch.cx(điều khiển, đích);
mạch.đo(q, bit);
mạch.đo tất cả();
mạch.reset(q);
mạch.các lệnh();

Bộ Mô Phỏng Lượng Tử(giới hạn qubit = 12);
máy.trạng thái(mạch);
máy.chạy(mạch, số lần = 1024, hạt giống = 1);

xuất openqasm(mạch);
```

Số bit `rỗng` mặc định bằng số qubit. Kích thước, chỉ số, số lần và hạt giống
phải là số nguyên V++ (không ép kiểu từ chuỗi hoặc số thực). Chỉ số bắt đầu
từ 0; hai qubit của CX phải khác nhau. `đo tất cả` yêu cầu `b >= n`, thêm
`đo(q, q)` cho mọi qubit; kiểm tra trước khi thêm bất kỳ chỉ dẫn nào. Các
phương thức thêm cổng/đo/reset trả chính mạch để nối lời gọi.

`các lệnh` trả list các bộ ba `[tên, qubit, đối số]`; đối số là qubit đích
của CX, bit đích của measure, hoặc -1 cho cổng đơn/reset. Dạng snapshot này
chỉ đọc theo contract; thay đổi snapshot không làm thay đổi mạch.

Giới hạn mô phỏng mặc định 12, cấu hình trong `1..16`; số lần chạy `1..1000000`,
hạt giống trong miền int32. `trạng thái` yêu cầu mạch không chứa measure/reset,
trả map `{"thực": [...], "ảo": [...], "xác suất": [...]}` dài `2^n`.
`chạy` yêu cầu ít nhất một phép đo tường minh, trả
`{"đếm": {"00": 507, "11": 517}, "số lần": 1024, "backend": "vpp-statevector"}`.
Chỉ có khóa kết quả đã xuất hiện. Cùng seed chỉ bảo đảm tái lập trong cùng
backend/phiên bản, không yêu cầu histogram V++ bằng histogram Aer.

Vector trạng thái dùng bộ nhớ O(2^n), mỗi cổng đơn/CX tốn O(2^n). Với đo cuối,
vector được tính một lần rồi lấy mẫu bằng tìm kiếm nhị phân CDF; với đo giữa
mạch/reset, mỗi shot chạy lại mạch. Giới hạn 12 là giới hạn tài nguyên mặc định,
không phải cam kết tốc độ. Đây là mô phỏng CPU, không tạo tăng tốc lượng tử.

`xuất openqasm` trả chuỗi có newline cuối, thanh ghi `q`, `c` và
`include "stdgates.inc"`. Không tự thêm phép đo. Qiskit và bộ mô phỏng V++
khởi tạo thanh ghi về 0; OpenQASM 3.0 tổng quát không bảo đảm giá trị ban đầu
của qubit/bit. Với bộ thực thi khác, cần bảo đảm trạng thái đầu tương ứng và
không đọc bit chưa được ghi. Qiskit importer hiện chưa nhận khởi tạo bit cổ điển
trong khai báo nên bộ xuất không thêm biểu thức khởi tạo đó.

Các hàm `lượng tử yêu cầu nguyên` và `lượng tử lũy thừa hai` trong `nền.vi`
là trợ giúp triển khai, không phải API dựng mạch.

## Cầu nối Qiskit

Từ thư mục gốc repository, tạo môi trường Python 3.10 trở lên:

```bash
python3 -m venv .venv-quantum
.venv-quantum/bin/python -m pip install -r 'gói/lượng tử/phụ thuộc qiskit.txt'
```

Windows dùng `.venv-quantum\Scripts\python.exe`. Muốn dùng QPU, cài thêm
`phụ thuộc ibm.txt` và cấu hình tài khoản QiskitRuntimeService theo tài liệu IBM.
Adapter dùng tài khoản đã lưu của SDK, không nhận token trong lời gọi V++.

```vi
nhập lượng tử;
nhập gói/lượng tử/qiskit;

sdk = Cầu Nối Qiskit("/đường/tuyệt/đối/python", "/sdk/gói/lượng tử/cầu nối qiskit.py");
kết quả = sdk.mô phỏng(mạch, 1024, 42);
```

`Cầu Nối Qiskit(python, tệp cầu nối)` yêu cầu hai chuỗi không rỗng. Dùng đường
dẫn tuyệt đối để độc lập thư mục làm việc; SDK đã cài chứa cầu nối tại
`$VPP_HOME/gói/lượng tử/cầu nối qiskit.py`.

| API | Input / output / side effect |
| --- | --- |
| `mô phỏng(mạch, số lần=1024, hạt giống=1)` | Số lần `1..1000000`, seed `0..2147483647`; mạch cần phép đo. Chạy Aer cục bộ, trả `đếm`, `số lần`, `backend=qiskit-aer`. |
| `gửi(mạch, backend, số lần=1024)` | Backend là chuỗi không rỗng, số lần như trên, mạch cần phép đo. Transpile theo thiết bị rồi tạo **một** job QPU; trả `job_id`, `backend`, `số lần`. Có thể sử dụng quota/chi phí của tài khoản. |
| `kết quả(mã job)` | ID không rỗng. Đọc trạng thái; trả `job_id`, `status`, `ready`. DONE thêm `đếm`, `số lần`; các trạng thái khác không chờ hoàn tất. Dừng polling khi ERROR/CANCELLED. |

Cầu nối nhận một đối số JSON tối đa 24000 ký tự, giao thức 1, stdout chỉ chứa
một JSON `protocol/ok/result` hoặc `protocol/ok/error`; diagnostic SDK đi vào
stderr. Mỗi lời gọi có deadline tiến trình 120000 ms. Timeout chỉ dừng tiến
trình cục bộ, không hủy job QPU; nếu mất phản hồi sau khi submit, cần kiểm tra
danh sách job trong tài khoản trước khi gửi lại. Không tự retry hoặc tự cancel.

Dependency ranges: Qiskit `>=2.1,<3`, Aer `>=0.17,<1`, IBM Runtime `>=0.40,<1`.
Đã kiểm tra trên macOS ARM64 với Qiskit 2.6.0, Aer 0.18.0, IBM Runtime 0.50.0; Linux/Windows
chưa được chạy trong phiên triển khai này. Cài SDK là bước chủ động của người dùng.

## Error model

Lỗi API là exception chuỗi V++, có prefix ổn định `lượng tử:` hoặc `qiskit:`.
Miền lỗi gồm: kiểu/phạm vi sai, chỉ số sai, CX trùng qubit, thiếu bit cho đo tất
cả, vượt giới hạn mô phỏng, trạng thái có phép đo/reset, thiếu phép đo, thiếu
SDK, Python thất bại, timeout, phản hồi JSON/giao thức sai và lỗi backend.
Không coi phần message chi tiết từ SDK là mã lỗi ổn định. Không tự thay bằng
backend khác khi gặp lỗi. Lỗi validation không thêm chỉ dẫn hoặc gửi job.

## Acceptance và kiểm thử

| AC | FR/NFR | Kiểm chứng |
| --- | --- | --- |
| AC-01 | FR-01/02 | `kiem_tra_goi_luong_tu.vi`: H², Y², pha S/T/Z, Bell/GHZ, CX hai chiều; sai số 1e-9. |
| AC-02 | FR-02/03 | Cùng test: 1024 shots Bell, seed tái lập, thứ tự/ánh xạ/ghi đè bit, collapse và reset qubit vướng víu. |
| AC-03 | FR-01, NFR-01 | Validation trước mutation; 15 đường lỗi input/tài nguyên và kiểm tra snapshot độc lập. |
| AC-04 | FR-04 | `kiem_tra_luong_tu_openqasm.vi` và `check_quantum.py`: parser Qiskit nạp được mạch xuất; vector trạng thái trùng. |
| AC-05 | FR-02/04 | `check_quantum.py`: 12 mạch ngẫu nhiên 1–4 qubit đối chiếu biên độ phức với Qiskit, sai số 1e-9. |
| AC-06 | FR-05/06 | Cầu nối V++ → Aer → V++; mock service xác nhận một submit và polling không gọi result khi chưa DONE. Không gửi job thật trong test. |
| AC-07 | NFR-02/03 | Protocol từ chối request sai, thiếu SDK trả lỗi rõ, SDK stdout không làm hỏng JSON; import không tạo service. |

```bash
./build/bin/vpp-cli chạy src/tests/kiem_tra_goi_luong_tu.vi
./build/bin/vpp-cli chạy src/tests/kiem_tra_luong_tu_openqasm.vi
.venv-quantum/bin/python scripts/quality/check_quantum.py
ctest --test-dir build -R vpp-quantum-bridge --output-on-failure
./build/bin/vpp-cli chạy examples/luong-tu/bell.vi
./build/bin/vpp-cli chạy examples/luong-tu/ghz.vi
```

`check_quantum.py` bỏ qua phần đối chiếu SDK nếu chưa cài dependency tùy chọn;
kiểm tra protocol vẫn chạy. Đặt `VPP_EXEC` để chọn CLI khác. Ba regression V++
được đăng ký trong `run_tests.sh`, cũng được harness Windows đọc từ danh sách đó.
`kiem_tra_luong_tu_qiskit.vi` kiểm tra validation mà không khởi chạy Python.

Chạy ví dụ Aer từ root, với đường dẫn Python tuyệt đối:

```bash
VPP_QUANTUM_PYTHON="$PWD/.venv-quantum/bin/python" \
  ./build/bin/vpp-cli chạy examples/luong-tu/qiskit.vi
```

## Nguồn đối chiếu và tương thích

| Concept | Contract V++ | Kiểm thử |
| --- | --- | --- |
| Qiskit QuantumCircuit / gates | `Mạch Lượng Tử`, FR-01 | AC-01/05 |
| Statevector / Born measurement | `Bộ Mô Phỏng Lượng Tử`, FR-02/03 | AC-01/02/05 |
| OpenQASM 3 / QASM importer | `xuất openqasm`, FR-04 | AC-04 |
| Aer / SamplerV2 / RuntimeJobV2 | `Cầu Nối Qiskit`, FR-05/06 | AC-06 |

Nguồn chính: [OpenQASM 3.0](https://openqasm.com/versions/3.0/index.html),
[Qiskit OpenQASM API](https://quantum.cloud.ibm.com/docs/en/api/qiskit/qasm3),
[AerSimulator](https://qiskit.github.io/qiskit-aer/stubs/qiskit_aer.AerSimulator.html),
[SamplerV2](https://quantum.cloud.ibm.com/docs/en/api/qiskit-ibm-runtime/sampler-v2).
Không có API Java/JDK tương ứng. Gói đang experimental ở phiên bản 0.1.0;
thêm cổng là thay đổi tương thích, đổi thứ tự bit/giao thức/chữ ký hiện có là
thay đổi breaking cần tăng phiên bản và cập nhật acceptance.

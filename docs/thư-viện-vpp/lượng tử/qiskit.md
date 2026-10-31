# Cầu Nối Qiskit

Import `gói/lượng tử/qiskit` rồi dùng `Cầu Nối Qiskit()` để mô phỏng
trực tiếp bằng V++, trả backend `vpp-statevector`.
Hai đối số đường dẫn chọn transport SDK ngoài tùy chọn. Backend cục bộ
chưa gửi/poll QPU thật; không phụ thuộc cầu nối Python để mô phỏng.
Xem [VPP-QUANTUM-001](chính.md#cầu-nối-qiskit) cho API, giao thức và timeout.
Triển khai adapter: `gói/lượng tử/qiskit.vi`.

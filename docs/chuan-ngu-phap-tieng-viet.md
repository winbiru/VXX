# Chuẩn ngữ pháp tiếng Việt trong V++

Phiên bản quy ước: 1.0.

Tài liệu quy định cách sử dụng tiếng Việt khi đặt tên và viết mã V++, xây dựng
thư viện, công cụ VPP/VDK, tài liệu và thông báo. Áp dụng cho mã mới và các thay
đổi được rà soát. Đây là quy ước của dự án; cú pháp hợp lệ và hành vi thực thi
tuân theo [tham chiếu ngôn ngữ](language-reference.md),
[ngữ nghĩa](semantics.md), mã nguồn và kiểm thử của compiler.
[BNF](grammar.bnf) là tài liệu cú pháp bổ trợ, không thay thế quy ước này.

## 1. Thành phần câu và từ loại

Thành phần câu mô tả vai trò của một từ hoặc cụm từ trong câu; từ loại mô tả
đặc điểm ngữ pháp của từ. Không đồng nhất chủ ngữ với danh từ hay vị ngữ với động từ.

| Thành phần câu | Chức năng | Ví dụ |
| --- | --- | --- |
| Chủ ngữ | Đối tượng được nói đến | bộ biên dịch |
| Vị ngữ | Hành động, trạng thái hoặc đặc điểm của chủ ngữ | đọc mã nguồn từ tệp |
| Bổ ngữ | Bổ sung nghĩa cho động từ hoặc tính từ | mã nguồn |
| Trạng ngữ | Bổ sung thời gian, nơi chốn, cách thức, nguyên nhân... | trước khi biên dịch |
| Định ngữ | Bổ sung đặc điểm cho danh từ | nguồn (trong tệp nguồn) |

| Từ loại | Vai trò thường gặp | Ví dụ |
| --- | --- | --- |
| Danh từ | Gọi tên sự vật, khái niệm | tệp, chuỗi, bộ nhớ, tiến trình |
| Động từ | Diễn đạt hành động hoặc trạng thái | đọc, ghi, chạy, tồn tại |
| Tính từ | Diễn đạt đặc điểm, tính chất | rỗng, hợp lệ, ổn định |
| Đại từ | Thay thế hoặc chỉ đối tượng | mình, nó |
| Phụ từ | Bổ sung ý nghĩa thời gian, phủ định, mức độ... | đã, đang, chưa, không |
| Quan hệ từ | Liên kết và biểu thị quan hệ | của, từ, với, đến |
| Số từ | Chỉ số lượng hoặc thứ tự | một, hai, thứ nhất |

Ví dụ: **Bộ biên dịch đọc mã nguồn từ tệp.**

- Chủ ngữ: bộ biên dịch (cụm danh từ).
- Vị ngữ: đọc mã nguồn từ tệp (cụm động từ).
- Động từ trung tâm: đọc; bổ ngữ chỉ đối tượng: mã nguồn.
- Cụm từ chỉ nguồn: từ tệp.

Cụm danh từ có thể làm chủ ngữ hoặc bổ ngữ. Vị ngữ có thể là cụm động từ,
cụm tính từ hoặc kết cấu khác. Trong câu “Danh sách rỗng”, tính từ “rỗng”
có thể làm vị ngữ mà không cần “là”. Câu mệnh lệnh như “Đọc tệp” có thể
lược chủ ngữ khi người thực hiện đã rõ.

## 2. Trật tự từ

Ưu tiên cách diễn đạt tự nhiên của tiếng Việt:

- Câu thông thường: chủ ngữ + vị ngữ.
- Cụm hành động: động từ + đối tượng + thành phần bổ sung.
- Cụm danh từ: danh từ trung tâm + thành phần bổ nghĩa, ví dụ tệp nguồn,
  mã lỗi, đường dẫn đầu ra, bộ nhớ đệm. Giữ nguyên các thuật ngữ ghép đã
  được sử dụng ổn định; không đảo từ một cách máy móc.
- Điều kiện hoặc thời gian có thể đứng trước hành động: “Khi có dữ liệu, đọc tệp”.
- Dùng từ chỉ quan hệ khi cần phân biệt nghĩa: “đọc từ tệp”, “ghi vào tệp”,
  “chuyển từ chuỗi sang số”.

Không dịch từng từ theo trật tự tiếng Anh. Tên cần ngắn, đúng nghĩa và đủ rõ
trong phạm vi sử dụng.

## 3. Quy tắc đặt tên trong mã V++

| Thành phần | Cấu trúc ưu tiên | Ví dụ |
| --- | --- | --- |
| Biến, tham số, thuộc tính | Danh từ hoặc cụm danh từ | `mã nguồn`, `số lần thử`, `đường dẫn` |
| Hằng số | Cụm danh từ; ghi rõ đơn vị khi cần | `thời gian chờ mili giây` |
| Biến boolean | Trạng thái hoặc mệnh đề ngắn | `đã đóng`, `đang chạy`, `có lỗi`, `hợp lệ` |
| Hàm hành động | Động từ + đối tượng | `đọc tệp`, `tính tổng`, `nối chuỗi` |
| Hàm truy vấn | Danh từ hoặc hành động truy vấn | `thư mục hiện tại`, `lấy độ dài` |
| Hàm trả boolean | Vị ngữ hoặc mệnh đề điều kiện | `là tệp`, `có dữ liệu`, `đường dẫn tồn tại` |
| Lớp | Cụm danh từ, viết hoa đầu mỗi tiếng | `Bộ Biên Dịch`, `Danh Sách`, `Hệ Tệp` |
| Giao diện | Vai trò, khả năng hoặc hợp đồng | `Đóng Được`, `Vòng Đời Ứng Dụng` |
| Gói và mô-đun | Danh từ hoặc lĩnh vực chức năng | `bộ sưu tập`, `nhập xuất`, `chuỗi` |

Tên biến ngắn như `i`, `j`, `x` được dùng khi ý nghĩa rõ trong vòng lặp hoặc
công thức. Không dùng tên chung chung như `dữ liệu 1` nếu có tên cụ thể hơn.

### Phương thức và chủ thể hành động

Trong lời gọi phương thức, đối tượng nhận thường tương ứng với chủ ngữ;
tên phương thức và đối số diễn đạt vị ngữ. Ví dụ `bộ đếm.tăng(2)`,
`tệp.đóng()`. Tránh lặp lại chủ thể trong `tệp.đóng tệp()`.

Hàm toàn cục hoặc hàm nội bộ có thể dùng tiền tố lĩnh vực khi cần phân biệt
API, như `vdk chọn`, `danh sách trộn ổn định` hoặc `tệp chuẩn hóa đường dẫn`.
Giữ quy ước của mô-đun đang sửa nếu thay đổi sẽ phá tương thích.

Tên phải phản ánh hành vi: phân biệt thêm/thay thế, sao chép/sửa tại chỗ,
hủy/đóng, truy vấn/kiểm tra có phát sinh lỗi. Hàm trả boolean không nên đặt
tên như một hành động gây thay đổi nếu nó chỉ đọc trạng thái.

### Chính tả và Unicode

- Lưu văn bản bằng UTF-8; chuẩn hóa tên mới về Unicode NFC khi soạn mã.
  Không giả định compiler tự coi mọi dạng Unicode tương đương là một tên.
- Viết đầy đủ dấu tiếng Việt; dùng một dấu cách thông thường giữa các tiếng
  trong tên V++ mới khi cú pháp hỗ trợ. Hàm, biến và mô-đun ưu tiên chữ thường;
  lớp theo kiểu viết hoa đầu mỗi tiếng. Giữ cách viết chính thức của chữ viết tắt.
- Không dùng dấu gạch nối trong định danh nguồn (có thể được hiểu là phép trừ).
  Dấu gạch nối dành cho tên tùy chọn CLI khi phù hợp.
- Giữ các tên cũ dùng dấu gạch dưới, camelCase, tên ASCII hoặc alias nếu chúng
  thuộc hợp đồng tương thích; tránh từ khóa dành riêng và tên gây nhập nhằng.
- Giữ các định danh giao thức, ABI, cấu hình và định dạng máy như HTTP, TLS,
  JSON, UTF-8, SHA-256, VPP_HOME; không dịch tùy tiện tên khóa dữ liệu.

## 4. Cú pháp thực thi và ví dụ

Sử dụng từ khóa đã được triển khai: `hàm`, `trả về`, `nếu`, `hoặc nếu`,
`hoặc`, `khi`, `lặp`, `chọn`, `ca`, `mặc định`, `nhập`, `in`, `mình`,
`đúng`, `sai`, `rỗng`. Dùng `=` để gán, `==` để so sánh, `||` cho
phép hoặc logic; `hoặc` trong cấu trúc điều kiện là nhánh còn lại.
Lời gọi độc lập có tên bắt đầu bằng `khi` có thể cần `gọi` để phân biệt vòng lặp.
Không tự thêm từ khóa như “thì”, “hãy” hay bỏ dấu phân cách vì câu đọc tự nhiên.

```vi
lớp Bộ Đếm {
    hàm công khai khởi tạo() {
        mình.giá trị = 0;
    };
    hàm công khai tăng(số bước) {
        mình.giá trị += số bước;
    };
    hàm công khai lấy giá trị() {
        trả về mình.giá trị;
    };
};

hàm tính tổng(số thứ nhất, số thứ hai) {
    trả về số thứ nhất + số thứ hai;
};

hàm chính() {
    bộ đếm = Bộ Đếm();
    bộ đếm.tăng(tính tổng(2, 3));
    kết quả = bộ đếm.lấy giá trị();
    nếu (kết quả == 5) {
        in kết quả;
    };
};
```

Kết quả mong đợi: `5`. Chủ thể là `bộ đếm`; hành động là `tăng`; đối số
là giá trị do `tính tổng` trả về. Tên `kết quả` chỉ dữ liệu.

## 5. Gói, công cụ và lệnh dòng lệnh

Tên thư mục và tệp nguồn ưu tiên lĩnh vực hoặc đối tượng, ví dụ
`gói/bộ sưu tập/danh sách.vi`. Điểm vào chuẩn là `chính.vi`.
ID gói dạng máy như `vi.base`, khóa manifest và đường dẫn import đã công bố
phải tuân theo [hệ thống gói](package-system.md) và hợp đồng hiện tại.

Lệnh CLI ưu tiên động từ + đối tượng; truy vấn có thể dùng cụm danh từ.
Nhóm lệnh có thể đặt lĩnh vực trước lệnh con. Ví dụ thực tế của VDK:

```text
vdk trợ giúp
vdk phiên bản
vdk chẩn đoán
vdk thư viện danh sách
vdk thư viện danh sách --json
vdk cài hiển thị
vdk kiểm tra hiển thị
vdk gỡ hiển thị
vdk biên dịch hello.vi --đầu-ra hello
vdk chạy hello.vi
vdk chạy tức thời hello.vi
vdk kiểm tra hello.vi
vdk biên-dịch-máy-ảo hello.vi --đầu-ra hello.vmb
```

Giữ alias tiếng Anh và lệnh khởi tạo ASCII `vdk cai-hien-thi` để tương thích
và hỗ trợ môi trường chưa gõ được dấu. Tùy chọn nhiều tiếng dùng dấu gạch nối,
như `--đầu-ra` và `--thư-mục-vdk`. Không tự dịch giá trị đối số hoặc đường dẫn.
Xem [CLI](cli.md) để biết lệnh được hỗ trợ.

## 6. Thuật ngữ kỹ thuật và thông báo

| Thuật ngữ gốc | Cách dùng tiếng Việt |
| --- | --- |
| compiler / interpreter | bộ biên dịch / bộ thông dịch |
| runtime / virtual machine | môi trường thực thi / máy ảo |
| bytecode | mã byte |
| file / directory / path | tệp / thư mục / đường dẫn |
| class / object / method | lớp / đối tượng / phương thức |
| parameter / argument | tham số / đối số |
| return value / exit code | giá trị trả về / mã thoát |
| thread / process | luồng / tiến trình |
| worker | worker (đơn vị thực thi theo hợp đồng V++; không đồng nhất với luồng hay tiến trình) |
| cancellation / timeout | yêu cầu hủy / hết thời gian chờ |

Dùng thuật ngữ nhất quán theo ngữ cảnh; phân biệt khái niệm kỹ thuật có hành vi
khác nhau. Giữ nguyên tên chuẩn, mã lỗi và khóa dữ liệu dành cho máy.

Thông báo dành cho người đọc nên nêu tình trạng, đối tượng và cách khắc phục
khi có thể. Ví dụ: “Không tìm thấy tệp nguồn: hello.vi” hoặc
“Đã cài cấu hình UTF-8. Mở terminal mới để áp dụng.”

## 7. Rà soát và tương thích

Khi thêm tên hoặc sửa tài liệu:

- [ ] Xác định rõ chủ thể, hành động, đối tượng, trạng thái và từ loại.
- [ ] Kiểm tra trật tự từ, chính tả, Unicode và sự nhất quán thuật ngữ.
- [ ] Bảo đảm tên biểu thị đúng hành vi và không xung đột từ khóa.
- [ ] Kiểm tra cú pháp bằng compiler và chạy ví dụ thực thi.
- [ ] Giữ tương thích API/CLI, tên gói, ABI và định dạng dữ liệu.

Khi cần đổi tên API hiện có, lập ánh xạ tên cũ sang tên mới, giữ alias hoặc
có kế hoạch chuyển đổi và kiểm thử tương thích. Không đổi tên hàng loạt
bằng thay thế văn bản thiếu phân tích định danh.

Tài liệu này là quy ước rà soát cho dự án, chưa phải bộ kiểm tra ngữ pháp
tự động và không mở rộng cú pháp compiler.

## Khai báo hằng

`hằng` khai báo một ràng buộc chỉ được khởi tạo một lần, có thể ở cấp mô-đun hoặc trong hàm:

```vpp
hằng KEY_ROWS = "rows";
hằng SQL_SELECT = "SELECT";
hàm chính() {
    hằng giới hạn = 10;
    in SQL_SELECT + " " + KEY_ROWS;
    in giới hạn;
};
```

Phải cung cấp biểu thức khởi tạo. Các phép gán lại (`=`, `+=`, `-=`, …)
và cập nhật (`++`, `--`) vào tên hằng là lỗi biên dịch. Hằng giữ nguyên
ràng buộc tới giá trị; các phần tử của danh sách hoặc từ điển vẫn có thể
được thay đổi theo quy tắc của kiểu dữ liệu. Tên biến có thể gồm nhiều từ.
Ở đầu câu lệnh, `hằng tên` là khai báo hằng; tránh dùng tên biến bắt đầu
bằng cụm này trong phép gán thông thường. Phạm vi ràng buộc của hằng theo
quy tắc biến cấp hàm/mô-đun hiện hành.

Hằng cấp mô-đun có thể đặt trong thư mục `hằng/` của dự án hoặc trong
`gói/hằng/` của thư viện chuẩn, sau đó nhập theo tên, toàn bộ nhóm hoặc bí danh:

```vpp
nhập gói.hằng.cơ sở dữ liệu.JDBC_MYSQL;
nhập gói.hằng.lượng tử.* như Q;
nhập gói.hằng.cơ sở dữ liệu.CỔNG_MYSQL như CỔNG;

hàm chính() {
    in JDBC_MYSQL;
    in Q.SỐ_BƯỚC_NEWTON;
    in CỔNG;
};
```

Đọc trực tiếp tên hằng đã nhập, không thêm dấu ngoặc gọi hàm. Không được
gán lại hằng đã nhập. Hằng được khởi tạo trong mô-đun định nghĩa và được
dùng chung giữa các mô-đun nhập nó. Tệp `gói/hằng/chính.vi` tái xuất
các nhóm hằng chuẩn; có thể dùng `nhập hằng.CỔNG_POSTGRESQL;` để nhập từ gói này.

Nhóm hằng chuẩn gồm cơ sở dữ liệu, lượng tử, HTTP và các mã lỗi theo
thư viện. Dùng hằng trong biểu thức, đối số và truy cập khóa từ điển.
Hằng có giá trị literal cũng có thể dùng làm khóa khi khởi tạo từ điển
hoặc làm giá trị tham số mặc định:

```vpp
nhập gói.hằng.http.MIME_JSON;
hằng KEY_ROWS = "rows";
hằng GIỚI_HẠN = 100;

hàm phản hồi(kiểu = MIME_JSON, giới hạn = GIỚI_HẠN) {
    trả về {KEY_ROWS: giới hạn, "kiểu": kiểu};
};
```

Khóa hằng phải là chuỗi. Tham số mặc định hỗ trợ các literal vô hướng
và số nguyên âm theo định dạng hiện hành. Hằng khởi tạo bằng lời gọi hàm
hoặc biểu thức tính toán vẫn dùng được trong biểu thức thông thường;
ở hai vị trí cần giá trị tĩnh này, compiler báo lỗi nếu không có literal.

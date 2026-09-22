# Tham chiếu ngôn ngữ V++ 1.0

Tài liệu này mô tả bề mặt ngôn ngữ V++ 1.0 đang được compiler/runtime triển khai.
Quy tắc chi tiết về scope, equality, truthiness, closure, module và lỗi runtime nằm trong
`docs/semantics.md` và `docs/runtime-errors.md`.

## Chương trình tối thiểu

```vi
hàm main() {
    in "Xin chào V++";
};
```

V++ dùng dấu `;` để kết thúc phần lớn statement. Identifier hỗ trợ Unicode, vì vậy tên tiếng
Việt có dấu có thể dùng trực tiếp.

## Giá trị và toán tử

Các giá trị nền tảng gồm số nguyên, số thực, chuỗi, boolean `đúng`/`sai`, `rỗng`, list, map,
tuple, set, function/closure, class và instance.

```vi
số = 42;
tỷLệ = 3.5;
tên = "V++";
cóHiệuLực = đúng;
khôngCó = rỗng;
danhSách = [1, 2, 3];
dữLiệu = {"tên": "V++", "phiên bản": 1};
```

Toán tử chính: `+`, `-`, `*`, `/`, `%`, `==`, `!=`, `<`, `>`, `<=`, `>=`, `&&`, `||`, `!`,
`=`, `+=`, `-=`, `*=`, `/=`, `%=`, `++`, `--`.

`+` cộng số khi hai toán hạng là số; nếu có toán hạng phi số, runtime nối biểu diễn chuỗi.
Chi tiết promotion, equality, ordering và truthiness được khóa trong `docs/semantics.md`.

## Điều kiện và vòng lặp

```vi
nếu (điểm >= 8) {
    in "giỏi";
} nếu không {
    in "chưa đạt mức giỏi";
};

tổng = 0;
lặp(i = 0; i < 10; i++) {
    nếu (i == 5) {
        bỏ qua;
    };
    tổng += i;
};
```

`thoát` rời vòng lặp hoặc nhánh `chọn`; `bỏ qua` chuyển sang lượt lặp tiếp theo.

```vi
chọn (mã) {
    ca 200: {
        in "thành công";
    }
    ca 404: {
        in "không tìm thấy";
    }
    mặc định: {
        in "khác";
    }
}
```

## Hàm, tham số mặc định và lambda

```vi
hàm cộng(a, b = 10) {
    trả về a + b;
};

nhânĐôi = hàm(x) {
    trả về x * 2;
};

in cộng(5);
in nhânĐôi(6);
```

V++ dùng dynamic typing. Compiler kiểm tra số đối số khi đích gọi được biết chắc; các lời gọi
động được kiểm tra lại ở runtime. Closure capture binding lexical bằng ô nhớ dùng chung, vì
vậy mutation sau khi tạo closure vẫn quan sát được từ closure.

## Module và import

```vi
nhập lõi;
nhập feature_math như toán;
nhập gói/mạng/rest;
```

Mỗi câu `nhập` nhận một target module/package. Import tương đối được phân giải từ thư mục của
module đang import. Alias tạo namespace, ví dụ `toán.tổng(2, 3)`.

Khai báo top-level mặc định/công khai nằm trong bề mặt export; `riêng tư` và `bảo vệ` không
được export. Dùng `công khai nhập ...` để re-export:

```vi
công khai nhập toan như toán;
```

Import cycle local bị từ chối ở compile time và diagnostic chứa chuỗi module gây chu trình.

## Lớp, kế thừa và interface

```vi
giao diện CóTên {
    hàm tên();
}

lớp Nền {
    hàm bảo vệ mãNộiBộ() {
        trả về "SP";
    };
}

lớp công khai SảnPhẩm kế thừa Nền triển khai CóTên {
    hàm khởi tạo(tên) {
        mình.tênSảnPhẩm = tên;
    };

    hàm công khai tên() {
        trả về mình.tênSảnPhẩm;
    };

    hàm công khai mã() {
        trả về gốc.mãNộiBộ();
    };
}

sảnPhẩm = SảnPhẩm("Cà phê");
in sảnPhẩm.tên();
```

`mình` là receiver hiện tại. `gốc` bắt đầu lookup từ superclass và có thể dùng để gọi method
hoặc constructor lớp cha. Class chỉ kế thừa một class; một class có thể `triển khai` nhiều
interface, còn interface có thể kế thừa nhiều interface. Interface là contract compile-time
trong 1.0.

Method hỗ trợ `công khai`, `bảo vệ`, `riêng tư`. Field hiện là thuộc tính động trên instance.
Constructor dùng tên `khởi tạo` và có thể có tham số mặc định.

## Exception của ngôn ngữ

```vi
thử {
    ném "dữ liệu không hợp lệ";
} bắt lỗi (e) {
    in e;
};
```

`ném` có thể mang bất kỳ giá trị runtime nào. `bắt lỗi` bắt exception do chương trình ném;
lỗi VM fatal như chia cho 0 hoặc bytecode/state không hợp lệ tuân theo contract riêng trong
`docs/runtime-errors.md`.

## Collection và Unicode

List/map hỗ trợ literal, index và mutation. Stdlib cung cấp helper cho set/tuple, map, text,
JSON, file, path, config, time, HTTP và các nhóm tiện ích khác.

Chuỗi nền tảng xử lý length/reverse/index/slice theo code point UTF-8. Các API text 1.0 có
case/normalization dành cho tiếng Việt và NFC theo phạm vi được mô tả trong roadmap; malformed
UTF-8 được kiểm tra tại các native boundary quan trọng.

## Tài liệu liên quan

- `docs/semantics.md`: scope, truthiness, equality, coercion, module, closure và finalizer policy.
- `docs/runtime-errors.md`: exception, VM fault, call boundary, module initialization và stack trace.
- `docs/package-system.md`: manifest, lockfile, SemVer, cache/offline, Git và registry.
- `docs/testing.md`: `vpp kiểm thử` và package `kiểm thử`.
- `docs/cli.md`: contract CLI 1.0.
- `docs/grammar.bnf`: mô tả BNF tham khảo; language reference này là tài liệu người dùng ưu tiên.

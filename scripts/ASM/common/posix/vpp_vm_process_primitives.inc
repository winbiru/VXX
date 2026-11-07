// ABI tiến trình VM nhận độ lệch vùng nhớ. Lệnh: 0 tạo, 1 thăm dò, 2 đọc, 3 đợi, 4 đóng. Khối điều
// khiển 1024 byte: pid@0, stdout@4, stderr@8, độ lệch argv@12, con trỏ argv@16, argc@20, thời
// hạn@28, đệm@32, số byte@36, kênh@40, errno@48. Môi trường riêng của con: độ lệch@24, số mục@44,
// đệm envp@56; vùng tạm từ +64. Đối tượng posix_spawn chỉ tồn tại trong ASM.
#if defined(__APPLE__)
#define VM_PROC_SYM(x) _##x
#define VM_PROC_LIB(x) _##x
#else
#define VM_PROC_SYM(x) x
#if defined(__x86_64__)
#define VM_PROC_LIB(x) x@PLT
#else
#define VM_PROC_LIB(x) x
#endif
#endif
.text
#if defined(__aarch64__) || defined(__arm64__)
.macro VAO_TIEN_TRINH
    stp x19, x20, [sp, #-64]!
    stp x21, x22, [sp, #16]
    stp x23, x30, [sp, #32]
    mov w19, w0
    mov w22, w1
    bl VM_PROC_SYM(vpp_native_memory_base)
    mov x21, x0
    cbz x21, Ltien_trinh_khong_hop_le_bat
// Kiểm tra giới hạn cũng ngăn khối điều khiển 1024 byte bị tràn.
    cmp w22, #0
    b.lt Ltien_trinh_khong_hop_le_bat
    mov w10, #0x52c0
    movk w10, #0x3fe, lsl #16 // 67000000 - 1024 = 0x03fe 52c0
    cmp w22, w10
    b.hi Ltien_trinh_khong_hop_le_bat
    tst w22, #15
    b.ne Ltien_trinh_khong_hop_le_bat
    add x20, x21, w22, uxtw
.endm
.macro TRA_TIEN_TRINH
    ldp x23, x30, [sp, #32]
    ldp x21, x22, [sp, #16]
    ldp x19, x20, [sp], #64
    ret
.endm
.macro GOI_TIEN_TRINH ten
    bl VM_PROC_LIB(\ten)
.endm

.globl VM_PROC_SYM(vpp_native_os_process)
VM_PROC_SYM(vpp_native_os_process):
    VAO_TIEN_TRINH
    cmp w19, #4
    b.hi Ltien_trinh_khong_hop_le_bat
    cbz w19, Ltien_trinh_tao_bat
    cmp w19, #1
    b.eq Ltien_trinh_tham_do_bat
    cmp w19, #2
    b.eq Ltien_trinh_doc_bat
    cmp w19, #3
    b.eq Ltien_trinh_doi_bat
    b Ltien_trinh_don_dep_bat

Ltien_trinh_tao_bat:
    ldr w23, [x20, #20]
    cmp w23, #1
    b.lt Ltien_trinh_khong_hop_le_bat
    cmp w23, #128
    b.gt Ltien_trinh_khong_hop_le_bat
    ldr w10, [x20, #12] // Bảng độ lệch argv, mỗi mục i32
    ldr w11, [x20, #16] // Bảng con trỏ argv, mỗi mục u64
    cmp w10, #0
    b.lt Ltien_trinh_khong_hop_le_bat
    cmp w11, #0
    b.lt Ltien_trinh_khong_hop_le_bat
    tst w10, #3
    b.ne Ltien_trinh_khong_hop_le_bat
    tst w11, #7
    b.ne Ltien_trinh_khong_hop_le_bat
    mov w12, #0x54c0
    movk w12, #0x3fe, lsl #16 // 67000000 - 512
    cmp w10, w12
    b.hi Ltien_trinh_khong_hop_le_bat
    mov w12, #0x52b8
    movk w12, #0x3fe, lsl #16 // 67000000 - 1032
    cmp w11, w12
    b.hi Ltien_trinh_khong_hop_le_bat
    add x22, x21, w10, uxtw
    add x23, x21, w11, uxtw
    mov w9, #0
Ltien_trinh_argv_bat:
    ldr w10, [x20, #20]
    cmp w9, w10
    b.ge Ltien_trinh_argv_xong_bat
    ldr w11, [x22, w9, uxtw #2]
    mov w12, #0x10
    movk w12, #0x80, lsl #16 // 8388624, kích thước dữ liệu heap tối thiểu
    cmp w11, w12
    b.lo Ltien_trinh_khong_hop_le_bat
    mov w12, #0x46c0
    movk w12, #0x3fe, lsl #16 // 67000000 - 4096
    cmp w11, w12
    b.hi Ltien_trinh_khong_hop_le_bat
    add x13, x21, w11, uxtw
    str x13, [x23, w9, uxtw #3]
// Mỗi chuỗi argv phải kết thúc trong 4096 byte được cấp. Duyệt từng byte để giữ nguyên các thanh ghi
// vòng lặp.
    mov w14, #0
Ltien_trinh_doi_so_nul_bat:
    ldrb w15, [x13, w14, uxtw]
    cbz w15, Ltien_trinh_doi_so_hop_le_bat
    add w14, w14, #1
    cmp w14, #4096
    b.lo Ltien_trinh_doi_so_nul_bat
    b Ltien_trinh_khong_hop_le_bat
Ltien_trinh_doi_so_hop_le_bat:
    add w9, w9, #1
    b Ltien_trinh_argv_bat
Ltien_trinh_argv_xong_bat:
    str xzr, [x23, w9, uxtw #3]
    str wzr, [x20, #48]
    mov w10, #-1
    stp w10, w10, [x20, #64]
    stp w10, w10, [x20, #72]
    str wzr, [x20, #88] // Đã khởi tạo thao tác tệp
    str wzr, [x20, #92] // Đã khởi tạo thuộc tính tiến trình
    add x0, x20, #64
    GOI_TIEN_TRINH pipe
    cbnz w0, Ltien_trinh_errno_bat
    add x0, x20, #72
    GOI_TIEN_TRINH pipe
    cbnz w0, Ltien_trinh_errno_bat
// Dời mô tả 0/1/2 trước addclose để không đóng nhầm stdout/stderr của con.
    mov w22, #0
Ltien_trinh_doi_mo_ta_bat:
    cmp w22, #4
    b.ge Ltien_trinh_dong_khi_exec_khoi_tao_bat
    add x23, x20, #64
    ldr w0, [x23, w22, uxtw #2]
    cmp w0, #3
    b.ge Ltien_trinh_doi_mo_ta_tiep_bat
#if defined(__APPLE__)
    mov w1, #67              // F_DUPFD_CLOEXEC
#else
    mov w1, #1030
#endif
    mov w2, #3
    GOI_TIEN_TRINH fcntl
    cmp w0, #0
    b.lt Ltien_trinh_errno_bat
    mov w23, w0
    add x9, x20, #64
    ldr w0, [x9, w22, uxtw #2]
    GOI_TIEN_TRINH close
    add x9, x20, #64
    str w23, [x9, w22, uxtw #2]
Ltien_trinh_doi_mo_ta_tiep_bat:
    add w22, w22, #1
    b Ltien_trinh_doi_mo_ta_bat
Ltien_trinh_dong_khi_exec_khoi_tao_bat:
    mov w22, #0
Ltien_trinh_dong_khi_exec_bat:
    add x9, x20, #64
    ldr w0, [x9, w22, uxtw #2]
    mov w1, #2               // F_SETFD
    mov w2, #1               // FD_CLOEXEC
    GOI_TIEN_TRINH fcntl
    cmp w0, #0
    b.lt Ltien_trinh_errno_bat
    add w22, w22, #1
    cmp w22, #4
    b.lt Ltien_trinh_dong_khi_exec_bat
    add x0, x20, #192
    GOI_TIEN_TRINH posix_spawn_file_actions_init
    cbnz w0, Ltien_trinh_loi_ket_qua_bat
    mov w9, #1
    str w9, [x20, #88]
    add x0, x20, #192
    ldr w1, [x20, #68]
    mov w2, #1
    GOI_TIEN_TRINH posix_spawn_file_actions_adddup2
    cbnz w0, Ltien_trinh_loi_ket_qua_bat
    add x0, x20, #192
    ldr w1, [x20, #76]
    mov w2, #2
    GOI_TIEN_TRINH posix_spawn_file_actions_adddup2
    cbnz w0, Ltien_trinh_loi_ket_qua_bat
    mov w22, #0
Ltien_trinh_them_dong_bat:
    add x0, x20, #192
    add x9, x20, #64
    ldr w1, [x9, w22, uxtw #2]
    GOI_TIEN_TRINH posix_spawn_file_actions_addclose
    cbnz w0, Ltien_trinh_loi_ket_qua_bat
    add w22, w22, #1
    cmp w22, #4
    b.lt Ltien_trinh_them_dong_bat
    add x0, x20, #320
    GOI_TIEN_TRINH posix_spawnattr_init
    cbnz w0, Ltien_trinh_loi_ket_qua_bat
    mov w9, #1
    str w9, [x20, #92]
    add x0, x20, #320
    mov w1, #2               // POSIX_SPAWN_SETPGROUP
    GOI_TIEN_TRINH posix_spawnattr_setflags
    cbnz w0, Ltien_trinh_loi_ket_qua_bat
    add x0, x20, #320
    mov w1, #0
    GOI_TIEN_TRINH posix_spawnattr_setpgroup
    cbnz w0, Ltien_trinh_loi_ket_qua_bat
// Ghép môi trường cho tiến trình con, giữ nguyên môi trường cha. V++ kiểm tra KEY=VALUE và quản lý
// độ lệch; ASM thay các khóa trùng trong môi trường kế thừa.
#if defined(__APPLE__)
    GOI_TIEN_TRINH _NSGetEnviron
    ldr x5, [x0]
#else
    adrp x9, :got:environ
    ldr x9, [x9, :got_lo12:environ]
    ldr x5, [x9]
#endif
    ldr w9, [x20, #44]       // Số mục môi trường ghi đè của con
    cbz w9, Ltien_trinh_moi_truong_san_sang_bat
    cmp w9, #128
    b.hi Ltien_trinh_sai_moi_truong_bat
    ldr w10, [x20, #24]     // Độ lệch i32 của các mục ghi đè
    ldr w11, [x20, #56]     // Bảng con trỏ envp u64 đã ghép
    tst w10, #3
    b.ne Ltien_trinh_sai_moi_truong_bat
    tst w11, #7
    b.ne Ltien_trinh_sai_moi_truong_bat
    mov w12, #0x54c0
    movk w12, #0x3fe, lsl #16
    cmp w10, w12
    b.hi Ltien_trinh_sai_moi_truong_bat
// Dùng giới hạn cấp phát an toàn 33824 byte.
    mov w12, #0xd2a0       // 67000000 - 33824 = 0x03fd d2a0
    movk w12, #0x3fd, lsl #16
    cmp w11, w12
    b.hi Ltien_trinh_sai_moi_truong_bat
    add x22, x21, w10, uxtw
    add x23, x21, w11, uxtw
    mov w16, #0             // Chỉ số mục kế thừa
    mov w17, #0             // Chỉ số kết quả đã ghép
Ltien_trinh_moi_truong_ke_thua_bat:
    cmp w16, #4096
    b.hs Ltien_trinh_sai_moi_truong_bat
    ldr x9, [x5, w16, uxtw #3]
    cbz x9, Ltien_trinh_moi_truong_them_cuoi_bat
    mov w10, #0             // Chỉ số mục ghi đè
Ltien_trinh_moi_truong_kiem_tra_ghi_de_bat:
    ldr w11, [x20, #44]
    cmp w10, w11
    b.ge Ltien_trinh_moi_truong_giu_bat
    ldr w12, [x22, w10, uxtw #2]
    mov w13, #0x10
    movk w13, #0x80, lsl #16
    cmp w12, w13
    b.lo Ltien_trinh_sai_moi_truong_bat
    mov w13, #0x46c0
    movk w13, #0x3fe, lsl #16
    cmp w12, w13
    b.hi Ltien_trinh_sai_moi_truong_bat
    add x11, x21, w12, uxtw
    mov w12, #0
Ltien_trinh_moi_truong_so_sanh_bat:
    ldrb w13, [x11, w12, uxtw]
    ldrb w14, [x9, w12, uxtw]
    cmp w13, #61            // '=' ngăn tên biến với giá trị
    b.eq Ltien_trinh_moi_truong_khoa_cuoi_bat
    cmp w13, w14
    b.ne Ltien_trinh_moi_truong_tiep_ghi_de_bat
    add w12, w12, #1
    cmp w12, #4096
    b.lo Ltien_trinh_moi_truong_so_sanh_bat
    b Ltien_trinh_sai_moi_truong_bat
Ltien_trinh_moi_truong_khoa_cuoi_bat:
    cmp w14, #61
    b.eq Ltien_trinh_moi_truong_bo_qua_bat
Ltien_trinh_moi_truong_tiep_ghi_de_bat:
    add w10, w10, #1
    b Ltien_trinh_moi_truong_kiem_tra_ghi_de_bat
Ltien_trinh_moi_truong_giu_bat:
    str x9, [x23, w17, uxtw #3]
    add w17, w17, #1
Ltien_trinh_moi_truong_bo_qua_bat:
    add w16, w16, #1
    b Ltien_trinh_moi_truong_ke_thua_bat
Ltien_trinh_moi_truong_them_cuoi_bat:
    mov w10, #0
Ltien_trinh_moi_truong_them_ghi_de_bat:
    ldr w11, [x20, #44]
    cmp w10, w11
    b.ge Ltien_trinh_moi_truong_cham_dut_bat
    ldr w12, [x22, w10, uxtw #2]
    mov w13, #0x10
    movk w13, #0x80, lsl #16
    cmp w12, w13
    b.lo Ltien_trinh_sai_moi_truong_bat
    mov w13, #0x46c0
    movk w13, #0x3fe, lsl #16
    cmp w12, w13
    b.hi Ltien_trinh_sai_moi_truong_bat
    add x12, x21, w12, uxtw
    str x12, [x23, w17, uxtw #3]
    add w17, w17, #1
    add w10, w10, #1
    b Ltien_trinh_moi_truong_them_ghi_de_bat
Ltien_trinh_moi_truong_cham_dut_bat:
    str xzr, [x23, w17, uxtw #3]
    mov x5, x23
Ltien_trinh_moi_truong_san_sang_bat:
    add x0, x20, #0
    ldr w9, [x20, #16]
    add x4, x21, w9, uxtw
    ldr x1, [x4]
    add x2, x20, #192
    add x3, x20, #320
    GOI_TIEN_TRINH posix_spawnp
    cbnz w0, Ltien_trinh_loi_ket_qua_bat
    mov w9, #1
    str w9, [x20, #96]       // Đã tạo tiến trình thành công
    b Ltien_trinh_hoan_tat_bat
Ltien_trinh_sai_moi_truong_bat:
    mov w0, #22
    b Ltien_trinh_loi_ket_qua_bat
Ltien_trinh_errno_bat:
#if defined(__APPLE__)
    GOI_TIEN_TRINH __error
#else
    GOI_TIEN_TRINH __errno_location
#endif
    ldr w0, [x0]
Ltien_trinh_loi_ket_qua_bat:
    str w0, [x20, #48]
    str wzr, [x20, #96]
Ltien_trinh_hoan_tat_bat:
    ldr w9, [x20, #92]
    cbz w9, 1f
    add x0, x20, #320
    GOI_TIEN_TRINH posix_spawnattr_destroy
1:
    ldr w9, [x20, #88]
    cbz w9, 2f
    add x0, x20, #192
    GOI_TIEN_TRINH posix_spawn_file_actions_destroy
2:
    ldr w0, [x20, #68]
    GOI_TIEN_TRINH close
    ldr w0, [x20, #76]
    GOI_TIEN_TRINH close
    mov w0, #-1
    str w0, [x20, #68]
    str w0, [x20, #76]
    ldr w9, [x20, #96]
    cbz w9, Ltien_trinh_tao_that_bai_bat
    ldr w9, [x20, #64]
    str w9, [x20, #4]
    ldr w9, [x20, #72]
    str w9, [x20, #8]
    mov w0, #1
    TRA_TIEN_TRINH
Ltien_trinh_tao_that_bai_bat:
    ldr w0, [x20, #64]
    cmp w0, #0
    b.lt 3f
    GOI_TIEN_TRINH close
3:
    ldr w0, [x20, #72]
    cmp w0, #0
    b.lt 4f
    GOI_TIEN_TRINH close
4:
    mov w0, #0
    TRA_TIEN_TRINH

Ltien_trinh_tham_do_bat:
    mov w22, #0
    ldr w9, [x20, #4]
    cmp w9, #0
    b.lt 1f
    str w9, [x20, #64]
    mov w10, #1
    strh w10, [x20, #68]
    strh wzr, [x20, #70]
    add w22, w22, #1
1:
    ldr w9, [x20, #8]
    cmp w9, #0
    b.lt 2f
    add x10, x20, #64
    add x10, x10, w22, uxtw #3
    str w9, [x10]
    mov w9, #1
    strh w9, [x10, #4]
    strh wzr, [x10, #6]
    add w22, w22, #1
2:
    cbz w22, Ltien_trinh_tham_do_ca_hai_het_tep_bat
    add x0, x20, #64
    mov w1, w22
    ldr w2, [x20, #28]
    mov w9, #5000
    cmp w2, w9
    b.hi Ltien_trinh_khong_hop_le_bat
    GOI_TIEN_TRINH poll
    cmp w0, #0
    b.lt Ltien_trinh_nhap_xuat_errno_bat
    mov w9, #0
    mov w10, #0
    ldr w11, [x20, #4]
    cmp w11, #0
    b.lt 3f
    ldrh w12, [x20, #70]
    mov w13, #0x39
    tst w12, w13          // POLLIN|POLLERR|POLLHUP|POLLNVAL
    cset w13, ne
    orr w9, w9, w13
    add w10, w10, #1
3:
    ldr w11, [x20, #8]
    cmp w11, #0
    b.lt 4f
    add x12, x20, #64
    add x12, x12, w10, uxtw #3
    ldrh w12, [x12, #6]
    mov w13, #0x39
    tst w12, w13
    cset w13, ne
    orr w9, w9, w13, lsl #1
4:
    mov w0, w9
    TRA_TIEN_TRINH
Ltien_trinh_tham_do_ca_hai_het_tep_bat:
    mov w0, #4
    TRA_TIEN_TRINH

Ltien_trinh_doc_bat:
    ldr w22, [x20, #40]
    cmp w22, #1
    b.eq 1f
    cmp w22, #2
    b.ne Ltien_trinh_khong_hop_le_bat
1:
    ldr w23, [x20, #36]
    cmp w23, #1
    b.lt Ltien_trinh_khong_hop_le_bat
    cmp w23, #4096
    b.gt Ltien_trinh_khong_hop_le_bat
    ldr w9, [x20, #32]
    cmp w9, #0
    b.lt Ltien_trinh_khong_hop_le_bat
    mov w10, #0x56c0
    movk w10, #0x3fe, lsl #16
    sub w10, w10, w23
    cmp w9, w10
    b.hi Ltien_trinh_khong_hop_le_bat
    add x23, x21, w9, uxtw
    add x22, x20, w22, uxtw #2
    ldr w0, [x22]
    cmp w0, #0
    b.lt Ltien_trinh_doc_het_tep_bat
Ltien_trinh_doc_thu_lai_bat:
    mov x1, x23
    ldr w2, [x20, #36]
    GOI_TIEN_TRINH read
    cmp x0, #0
    b.gt Ltien_trinh_tra_ve_bat
    b.eq Ltien_trinh_doc_dong_bat
#if defined(__APPLE__)
    GOI_TIEN_TRINH __error
#else
    GOI_TIEN_TRINH __errno_location
#endif
    ldr w9, [x0]
    cmp w9, #4
    b.eq Ltien_trinh_doc_thu_lai_bat
    neg w0, w9
    TRA_TIEN_TRINH
Ltien_trinh_doc_dong_bat:
    ldr w0, [x22]
    GOI_TIEN_TRINH close
    mov w9, #-1
    str w9, [x22]
Ltien_trinh_doc_het_tep_bat:
    mov w0, #0
    TRA_TIEN_TRINH

Ltien_trinh_doi_bat:
    ldr w22, [x20]
    cmp w22, #0
    b.le Ltien_trinh_khong_hop_le_bat
    ldr w9, [x20, #4]
    cmp w9, #0
    b.ge Ltien_trinh_khong_hop_le_bat
    ldr w9, [x20, #8]
    cmp w9, #0
    b.ge Ltien_trinh_khong_hop_le_bat
Ltien_trinh_doi_thu_lai_bat:
    mov w0, w22
    add x1, x20, #100
    mov w2, #0
    GOI_TIEN_TRINH waitpid
    cmp w0, #0
    b.gt Ltien_trinh_doi_thoat_bat
#if defined(__APPLE__)
    GOI_TIEN_TRINH __error
#else
    GOI_TIEN_TRINH __errno_location
#endif
    ldr w9, [x0]
    cmp w9, #4
    b.eq Ltien_trinh_doi_thu_lai_bat
    neg w0, w9
    TRA_TIEN_TRINH
Ltien_trinh_doi_thoat_bat:
    str wzr, [x20]           // Tắt dọn nhóm tiến trình ở lần sau
    ldr w9, [x20, #100]
    and w10, w9, #127
    cbz w10, Ltien_trinh_doi_binh_thuong_bat
    cmp w10, #127
    b.eq Ltien_trinh_khong_hop_le_bat
    add w0, w10, #128
    TRA_TIEN_TRINH
Ltien_trinh_doi_binh_thuong_bat:
    lsr w0, w9, #8
    and w0, w0, #255
    TRA_TIEN_TRINH

Ltien_trinh_don_dep_bat:
    ldr w0, [x20, #4]
    cmp w0, #0
    b.lt 1f
    GOI_TIEN_TRINH close
1:
    ldr w0, [x20, #8]
    cmp w0, #0
    b.lt 2f
    GOI_TIEN_TRINH close
2:
    mov w9, #-1
    str w9, [x20, #4]
    str w9, [x20, #8]
    ldr w22, [x20]
    str wzr, [x20]
    cmp w22, #0
    b.le Ltien_trinh_don_dep_xong_bat
    neg w0, w22
    mov w1, #9
    GOI_TIEN_TRINH kill
3:
    mov w0, w22
    add x1, x20, #100
    mov w2, #0
    GOI_TIEN_TRINH waitpid
    cmp w0, #0
    b.ge Ltien_trinh_don_dep_xong_bat
#if defined(__APPLE__)
    GOI_TIEN_TRINH __error
#else
    GOI_TIEN_TRINH __errno_location
#endif
    ldr w9, [x0]
    cmp w9, #4
    b.eq 3b
Ltien_trinh_don_dep_xong_bat:
    mov w0, #0
    TRA_TIEN_TRINH

Ltien_trinh_nhap_xuat_errno_bat:
#if defined(__APPLE__)
    GOI_TIEN_TRINH __error
#else
    GOI_TIEN_TRINH __errno_location
#endif
    ldr w0, [x0]
// EINTR trong poll được coi là đánh thức; V++ kiểm tra thời hạn đơn điệu trước lần thăm dò tiếp
// theo.
    cmp w0, #4
    b.eq Ltien_trinh_tham_do_bi_ngat_bat
    neg w0, w0
Ltien_trinh_tra_ve_bat:
    TRA_TIEN_TRINH
Ltien_trinh_tham_do_bi_ngat_bat:
    mov w0, #0
    TRA_TIEN_TRINH
Ltien_trinh_khong_hop_le_bat:
    mov w0, #-22
    TRA_TIEN_TRINH

#else
#if defined(__x86_64__)
#if defined(__linux__)
// Bộ biên dịch tự chủ dùng chung mã tiến trình độc lập. Nhánh này kiểm tra theo vùng nhớ khởi động
// 67 MB; bộ thực thi độc lập có vùng nhớ lớn hơn.
#define PR_ARENA 67000000
#if !defined(VPP_PROCESS_HOST_INITIAL_ENVP)
#define VPP_PROCESS_HOST_ENVIRON 1
#endif
#include "../../linux/vpp_linux_process_x86_64.S"
#if !defined(VPP_PROCESS_HOST_INITIAL_ENVP)
#undef VPP_PROCESS_HOST_ENVIRON
#endif
#undef PR_ARENA
#else
#include "vpp_vm_process_x86_64.inc"
#endif
#else
#error Kiến trúc tiến trình VM độc lập chưa được hỗ trợ
#endif
#endif

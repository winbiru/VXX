// Đọc định danh tệp và kiểm tra quyền truy cập.

// struct stat Darwin ARM64: st_dev@0, st_mode@4, st_ino 64 bit@8. Hai vùng tạm 208 byte tách biệt
// dành cho so sánh định danh tệp nguồn/đích. Chỉ cho phép ghi vào hai vùng đó; kiểm tra đường đủ
// PATH_MAX byte kể cả NUL.
.macro STAT_LIEN_KET ten, dich
    .globl _\ten
_\ten:
    VAO_BO_NHO
    KIEM_TRA_BIEN 4096
    cmp w1, #3376
    b.eq 1f
    cmp w1, #3584
    b.ne Llien_ket_stat_loi
1:  add x1, x9, w1, uxtw
    add x0, x9, w0, uxtw
    bl _\dich
    TRA_BO_NHO
.endm
STAT_LIEN_KET vpp_native_os_stat, stat
STAT_LIEN_KET vpp_native_os_lstat, lstat
Llien_ket_stat_loi:
    mov w0, #-1
    TRA_BO_NHO

.globl _vpp_native_os_access_exec
.globl _vpp_native_os_fchmod
_vpp_native_os_fchmod:
    b _fchmod
_vpp_native_os_access_exec:
    VAO_BO_NHO
    KIEM_TRA_BIEN 4096
    add x0, x9, w0, uxtw
    mov w1, #1                 // X_OK; V++ quyết định chính sách truy cập
    bl _access
    TRA_BO_NHO


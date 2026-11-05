#if defined(__linux__) && defined(__x86_64__)
#include "../linux/vpp_aot_vpp_runtime_x86_64.inc"
#else
// ASM đổi con trỏ, đọc siêu dữ liệu và gọi máy gián tiếp. Giá trị động, khung gọi, bao đóng, ngoại
// lệ và GC được viết bằng V++.
#if !defined(__APPLE__) || (!defined(__aarch64__) && !defined(__arm64__))
#error ABI AOT động V++ hiện yêu cầu macOS ARM64
#endif
.text
.p2align 2
.macro AOT_VPP ten, so_doi_so
    .globl _vpp_native_dynamic_\ten
_vpp_native_dynamic_\ten:
    sub sp, sp, #48
    str x30, [sp, #32]
    str w0, [sp]
    .if \so_doi_so > 1
    str w1, [sp, #4]
    .endif
    .if \so_doi_so > 2
    str w2, [sp, #8]
    .endif
    .if \so_doi_so > 3
    str w3, [sp, #12]
    .endif
    .if \so_doi_so > 4
    str w4, [sp, #16]
    .endif
    .if \so_doi_so > 5
    str w5, [sp, #20]
    .endif
    .if \so_doi_so > 6
    str w6, [sp, #24]
    .endif
    .if \so_doi_so > 7
    str w7, [sp, #28]
    .endif
    bl _vpp_fn_aot_\ten
    ldr x30, [sp, #32]
    add sp, sp, #48
    ret
.endm

// Vòng gập i32 có thể tránh toàn bộ khung gọi V++ khi các ô cục bộ đã nằm
// trong vùng nội tuyến/cache của frame. Trường hợp phức tạp vẫn đi qua hàm
// V++ để giữ nguyên ngữ nghĩa scope, closure và GC.
.macro AOT_VPP_GAP_I32 ten, so_doi_so
    .globl _vpp_native_dynamic_\ten
_vpp_native_dynamic_\ten:
    sub sp, sp, #176
    str w0, [sp]
    str w1, [sp, #4]
    str w2, [sp, #8]
    str w3, [sp, #12]
    str w4, [sp, #16]
    str w5, [sp, #20]
    str w6, [sp, #24]
    str w7, [sp, #28]
    stp x19, x20, [sp, #80]
    stp x21, x22, [sp, #96]
    stp x23, x24, [sp, #112]
    stp x25, x26, [sp, #128]
    stp x27, x28, [sp, #144]
    str x30, [sp, #160]
    str d8, [sp, #168]

    mov w20, w0
    bl _vpp_native_memory_base
    cbz x0, Laot_gap_khung_du_phong
    mov x19, x0
    add x21, x19, w20, uxtw

    // Chỉ dùng cache cục bộ. Frame toàn cục/constructor cần tra scope đầy đủ.
    ldr w9, [x21, #36]
    cbnz w9, Laot_gap_khung_du_phong
    ldr w9, [x21, #8]
    cbz w9, Laot_gap_khung_du_phong
    add x10, x19, w9, uxtw
    ldr w10, [x10, #4]
    cbnz w10, Laot_gap_khung_du_phong

    ldr w9, [sp, #28]
    ubfx w10, w9, #16, #16
    and w11, w9, #0xffff
    cbz w10, Laot_gap_khung_du_phong
    cbz w11, Laot_gap_khung_du_phong
    str w10, [sp, #32]
    str w11, [sp, #36]

    ldr w0, [sp, #4]
    bl Laot_tim_o_cuc_bo
    cbz w0, Laot_gap_khung_du_phong
    str w0, [sp, #40]
    ldr w0, [sp, #8]
    bl Laot_tim_o_cuc_bo
    cbz w0, Laot_gap_khung_du_phong
    str w0, [sp, #44]
    ldr w0, [sp, #12]
    bl Laot_tim_o_cuc_bo
    cbz w0, Laot_gap_khung_du_phong
    str w0, [sp, #48]
    ldr w0, [sp, #16]
    bl Laot_tim_o_cuc_bo
    cbz w0, Laot_gap_khung_du_phong
    str w0, [sp, #52]
    ldr w0, [sp, #24]
    bl Laot_tim_o_cuc_bo
    cbz w0, Laot_gap_khung_du_phong
    str w0, [sp, #56]

    ldr w9, [sp, #40]
    ldr w10, [sp, #44]
    ldr w11, [sp, #48]
    ldr w12, [sp, #52]
    ldr w13, [sp, #56]
    cmp w9, w10
    b.eq Laot_gap_khung_du_phong
    cmp w9, w11
    b.eq Laot_gap_khung_du_phong
    cmp w9, w12
    b.eq Laot_gap_khung_du_phong
    cmp w9, w13
    b.eq Laot_gap_khung_du_phong
    cmp w11, w10
    b.eq Laot_gap_khung_du_phong
    cmp w11, w12
    b.eq Laot_gap_khung_du_phong
    cmp w11, w13
    b.eq Laot_gap_khung_du_phong

    ldr w0, [sp, #40]
    bl Laot_doc_i32_nhanh
    cbz w1, Laot_gap_khung_du_phong
    mov w25, w0
    ldr w0, [sp, #44]
    bl Laot_doc_i32_nhanh
    cbz w1, Laot_gap_khung_du_phong
    mov w24, w0
    ldr w0, [sp, #56]
    bl Laot_doc_i32_nhanh
    cbz w1, Laot_gap_khung_du_phong
    mov w26, w0

    movz w9, #0xca00
    movk w9, #0x3b9a, lsl #16
    cmp w25, w9
    b.gt Laot_gap_khung_du_phong
    cmp w24, w9
    b.gt Laot_gap_khung_du_phong
    neg w10, w9
    cmp w25, w10
    b.lt Laot_gap_khung_du_phong
    cmp w24, w10
    b.lt Laot_gap_khung_du_phong
    subs w24, w24, w25
    b.le Laot_gap_khung_rong
    movz w9, #0xe100
    movk w9, #0x05f5, lsl #16
    cmp w24, w9
    b.gt Laot_gap_khung_du_phong

    // Giải callable trực tiếp: i32 thô/đóng hộp hoặc closure không capture.
    ldr w9, [sp, #52]
    add x10, x19, w9, uxtw
    ldr w11, [x10]
    ldr w12, [x10, #4]
    cmp w11, #12
    b.eq Laot_gap_khung_co_callee
    cbz w12, Laot_gap_khung_du_phong
    add x10, x19, w12, uxtw
    ldr w11, [x10]
    cmp w11, #1
    b.eq Laot_gap_khung_callee_hop
    cmp w11, #7
    b.ne Laot_gap_khung_du_phong
    ldr w11, [x10, #8]
    cbz w11, Laot_gap_khung_closure_rong
    add x14, x19, w11, uxtw
    ldr w14, [x14, #4]
    cbnz w14, Laot_gap_khung_du_phong
Laot_gap_khung_closure_rong:
    ldr w11, [x10, #12]
    cbnz w11, Laot_gap_khung_du_phong
    ldr w12, [x10, #4]
    b Laot_gap_khung_co_callee
Laot_gap_khung_callee_hop:
    ldr w12, [x10, #4]
Laot_gap_khung_co_callee:
    tbnz w12, #31, Laot_gap_khung_du_phong
    adrp x9, _vpp_aot_spec@PAGE
    add x9, x9, _vpp_aot_spec@PAGEOFF
    ldr x10, [x9, #80]
    cmp x12, x10
    b.hs Laot_gap_khung_du_phong
    ldr x10, [x9, #88]
    add x10, x10, x12, lsl #4
    ldr x22, [x10]
    cbz x22, Laot_gap_khung_du_phong
    ldr w11, [x10, #8]
    cmp w11, #2
    b.ne Laot_gap_khung_du_phong
    ldr w11, [x10, #12]
    tbnz w11, #31, Laot_gap_khung_du_phong
    str w11, [sp, #68]

    mov w27, #0
    tbz w11, #1, Laot_gap_khung_du_lieu_xong
    ldr w0, [sp, #20]
    bl Laot_tim_o_cuc_bo
    cbz w0, Laot_gap_khung_du_phong
    str w0, [sp, #64]
    ldr w9, [sp, #40]
    cmp w0, w9
    b.eq Laot_gap_khung_du_phong
    ldr w9, [sp, #48]
    cmp w0, w9
    b.eq Laot_gap_khung_du_phong
    bl Laot_doc_i32_nhanh
    cbz w1, Laot_gap_khung_du_phong
    mov w27, w0
Laot_gap_khung_du_lieu_xong:

    ldr w9, [sp, #48]
    add x10, x19, w9, uxtw
    ldr w9, [x10, #4]
    cbz w9, Laot_gap_khung_du_phong
    add x10, x19, w9, uxtw
    ldr w11, [x10]
    cmp w11, #2
    b.ne Laot_gap_khung_du_phong
    str w9, [sp, #60]
    ldr d8, [x10, #8]

Laot_gap_khung_batch:
    cbz w24, Laot_gap_khung_xong
    mov w28, w24
    cmp w28, #4096
    b.ls Laot_gap_khung_batch_nho
    mov w28, #4096
Laot_gap_khung_batch_nho:
    mov x0, x22
    mov w1, w25
    mov w2, w28
    mov w3, w26
    mov w4, w27
    ldr w5, [sp, #32]
    ldr w6, [sp, #36]
    bl Laot_gap_i32_tho
    mov w23, w0
    add w25, w25, w28
    sub w24, w24, w28

Laot_gap_khung_luu:
    scvtf d0, w23
    fadd d8, d8, d0
    ldr w9, [sp, #60]
    add x10, x19, w9, uxtw
    str d8, [x10, #8]

    // Ô heap vẫn phải đóng hộp để closure/GC không đọc i32 như con trỏ.
    ldr w9, [sp, #40]
    sub sp, sp, #16
    str w20, [sp]
    str w9, [sp, #4]
    str w25, [sp, #8]
    bl _vpp_fn_aot_ghi_i32
    add sp, sp, #16
    cbnz w0, Laot_gap_khung_loi

    movz w9, #0x1000
    movk w9, #0x0040, lsl #16
    add x10, x19, w9, uxtw
    ldr w11, [x10, #4]
    cbnz w11, Laot_gap_khung_loi
    ldr w11, [x10, #36]
    movz w9, #0x0000
    movk w9, #0x0010, lsl #16
    cmp w11, w9
    b.lo Laot_gap_khung_batch
    mov w0, w20
    bl _vpp_native_dynamic_safepoint
    cbnz w0, Laot_gap_khung_loi
    b Laot_gap_khung_batch

Laot_gap_khung_rong:
    mov w0, #0
    b Laot_gap_khung_tra
Laot_gap_khung_xong:
    mov w0, #0
    b Laot_gap_khung_tra
Laot_gap_khung_loi:
    mov w0, #2
    b Laot_gap_khung_tra
Laot_gap_khung_du_phong:
    bl _vpp_fn_aot_\ten
Laot_gap_khung_tra:
    ldr d8, [sp, #168]
    ldr x30, [sp, #160]
    ldp x27, x28, [sp, #144]
    ldp x25, x26, [sp, #128]
    ldp x23, x24, [sp, #112]
    ldp x21, x22, [sp, #96]
    ldp x19, x20, [sp, #80]
    add sp, sp, #176
    ret
.endm

#include "vpp_aot_vpp_exports.inc"

// Tra một slot cục bộ theo đúng đường nhanh của vm_cell: bốn ô nội tuyến
// rồi bốn mục cache. Không tìm thấy thì trả 0 để caller dùng đường đầy đủ.
Laot_tim_o_cuc_bo:
    tbnz w0, #31, Laot_tim_o_cuc_bo_sai
    add w9, w0, #1
    ldr w10, [x21, #60]
    cmp w10, w9
    b.eq Laot_tim_o_cuc_bo_mot
    ldr w10, [x21, #172]
    cmp w10, w9
    b.eq Laot_tim_o_cuc_bo_hai
    ldr w10, [x21, #160]
    cmp w10, w9
    b.eq Laot_tim_o_cuc_bo_ba
    ldr w10, [x21, #176]
    cmp w10, w9
    b.eq Laot_tim_o_cuc_bo_bon
    ldr w10, [x21, #40]
    cbz w10, 1f
    ldr w11, [x21, #128]
    cmp w11, w0
    b.eq Laot_tim_o_cuc_bo_cache
1:  ldr w10, [x21, #48]
    cbz w10, 2f
    ldr w11, [x21, #132]
    cmp w11, w0
    b.eq Laot_tim_o_cuc_bo_cache
2:  ldr w10, [x21, #52]
    cbz w10, 3f
    ldr w11, [x21, #136]
    cmp w11, w0
    b.eq Laot_tim_o_cuc_bo_cache
3:  ldr w10, [x21, #56]
    cbz w10, Laot_tim_o_cuc_bo_sai
    ldr w11, [x21, #140]
    cmp w11, w0
    b.ne Laot_tim_o_cuc_bo_sai
Laot_tim_o_cuc_bo_cache:
    mov w0, w10
    ret
Laot_tim_o_cuc_bo_mot:
    add w0, w20, #144
    ret
Laot_tim_o_cuc_bo_hai:
    add w0, w20, #152
    ret
Laot_tim_o_cuc_bo_ba:
    add w0, w20, #164
    ret
Laot_tim_o_cuc_bo_bon:
    add w0, w20, #180
    ret
Laot_tim_o_cuc_bo_sai:
    mov w0, #0
    ret

// Đọc i32 từ ô nội tuyến kiểu 12 hoặc từ giá trị i32 đã đóng hộp.
// w1=1 báo thành công, w1=0 yêu cầu deopt về runtime V++.
Laot_doc_i32_nhanh:
    cbz w0, Laot_doc_i32_nhanh_sai
    add x9, x19, w0, uxtw
    ldr w10, [x9]
    ldr w11, [x9, #4]
    cmp w10, #12
    b.eq Laot_doc_i32_nhanh_tho
    cbz w11, Laot_doc_i32_nhanh_sai
    add x9, x19, w11, uxtw
    ldr w10, [x9]
    cmp w10, #1
    b.ne Laot_doc_i32_nhanh_sai
    ldr w0, [x9, #4]
    mov w1, #1
    ret
Laot_doc_i32_nhanh_tho:
    mov w0, w11
    mov w1, #1
    ret
Laot_doc_i32_nhanh_sai:
    mov w1, #0
    ret

// info(trường, chỉ số, byte) chỉ trả dữ liệu; giữ con trỏ máy đủ độ rộng bên trong ASM.
.globl _vpp_native_aot_info
_vpp_native_aot_info:
    adrp x9, _vpp_aot_spec@PAGE
    add x9, x9, _vpp_aot_spec@PAGEOFF
    cmp w0, #4
    b.hi Laot_thong_tin_bang
    cmp w0, #3
    b.eq Laot_thong_tin_ngoai
    cmp w0, #4
    b.eq Laot_thong_tin_bien_dich
    lsl x10, x0, #4
    ldr x0, [x9, x10]
    ret
Laot_thong_tin_ngoai:
    ldr w0, [x9, #64]
    ret
Laot_thong_tin_bien_dich:
    ldr w0, [x9, #56]
    ret
Laot_thong_tin_bang:
    tbnz w1, #31, Laot_thong_tin_sai
    cmp w0, #11
    b.hi Laot_thong_tin_ham
    cmp w0, #10
    b.lo Laot_thong_tin_sai
    ldr x10, [x9]
    cmp x1, x10
    b.hs Laot_thong_tin_sai
    ldr x10, [x9, #8]
    add x10, x10, x1, lsl #4
    ldr x11, [x10, #8]
    cmp w0, #10
    b.eq Laot_thong_tin_do_dai
    tbnz w2, #31, Laot_thong_tin_sai
    cmp x2, x11
    b.hs Laot_thong_tin_sai
    ldr x10, [x10]
    ldrb w0, [x10, x2]
    ret
Laot_thong_tin_do_dai:
    mov w0, w11
    ret
Laot_thong_tin_ham:
    cmp w0, #30
    b.hs Laot_thong_tin_ham_i32
    sub w0, w0, #20
    cmp w0, #2
    b.hi Laot_thong_tin_sai
    ldr x10, [x9, #16]
    cmp x1, x10
    b.hs Laot_thong_tin_sai
    ldr x10, [x9, #24]
    mov x11, #24
    madd x10, x1, x11, x10
    ldr w0, [x10, x0, lsl #2]
    ret
Laot_thong_tin_ham_i32:
    sub w0, w0, #30
    cmp w0, #1
    b.hi Laot_thong_tin_sai
    ldr x10, [x9, #80]
    cmp x1, x10
    b.hs Laot_thong_tin_sai
    ldr x10, [x9, #88]
    add x10, x10, x1, lsl #4
    add x10, x10, #8
    ldr w0, [x10, x0, lsl #2]
    ret
Laot_thong_tin_sai:
    mov w0, #-1
    ret

// invoke(loại, chỉ số, khung) gọi hàm máy bằng địa chỉ đầy đủ. ABI i32 của V++ chỉ truyền độ lệch
// khung đã quản lý.
.globl _vpp_native_aot_invoke
_vpp_native_aot_invoke:
    adrp x9, _vpp_aot_spec@PAGE
    add x9, x9, _vpp_aot_spec@PAGEOFF
    cmp w0, #2
    b.eq Laot_goi_ham_muc
    tbnz w1, #31, Laot_thong_tin_sai
    cmp w0, #1
    b.eq Laot_goi_ham_khoi_tao
    cbnz w0, Laot_thong_tin_sai
    ldr x10, [x9, #16]
    cmp x1, x10
    b.hs Laot_thong_tin_sai
    ldr x10, [x9, #24]
    mov x11, #24
    madd x10, x1, x11, x10
    ldr x10, [x10, #16]
    b Laot_goi_ham_goi
Laot_goi_ham_khoi_tao:
    ldr x10, [x9, #32]
    cmp x1, x10
    b.hs Laot_thong_tin_sai
    ldr x10, [x9, #40]
    ldr x10, [x10, x1, lsl #3]
    b Laot_goi_ham_goi
Laot_goi_ham_muc:
    ldr x10, [x9, #48]
Laot_goi_ham_goi:
    mov w0, w2
    br x10

// invoke_i32(chỉ_số, đối_số_0, đối_số_1) giữ con trỏ hàm 64-bit hoàn toàn
// trong ASM và chỉ trao đổi i32 với runtime V++.
.globl _vpp_native_aot_invoke_i32
_vpp_native_aot_invoke_i32:
    tbnz w0, #31, Laot_goi_i32_sai
    adrp x9, _vpp_aot_spec@PAGE
    add x9, x9, _vpp_aot_spec@PAGEOFF
    ldr x10, [x9, #80]
    cmp x0, x10
    b.hs Laot_goi_i32_sai
    ldr x10, [x9, #88]
    add x10, x10, x0, lsl #4
    ldr x10, [x10]
    cbz x10, Laot_goi_i32_sai
    mov w0, w1
    mov w1, w2
    br x10
Laot_goi_i32_sai:
    mov w0, #0
    ret

// Chạy một batch i32 với ABI máy thuần. Hai phép modulo là đường nóng nhất
// của benchmark: lũy thừa hai dùng mask, số dương khác dùng nghịch đảo 2^32/d
// tính một lần cho cả batch. Số âm giữ sdiv để đúng quy tắc phần dư có dấu.
// x0=hàm, w1=first, w2=count, w3=seed, w4=data, w5=cycle, w6=divisor.
Laot_gap_i32_tho:
    stp x19, x20, [sp, #-96]!
    stp x21, x22, [sp, #16]
    stp x23, x24, [sp, #32]
    stp x25, x26, [sp, #48]
    stp x27, x28, [sp, #64]
    str x30, [sp, #80]
    mov x19, x0
    mov w20, w1
    mov w21, w2
    mov w22, w3
    mov w23, w4
    mov w24, w5
    mov w25, w6
    mov w28, #0

    sub w9, w24, #1
    tst w24, w9
    mov w10, #-1
    csel w10, w9, w10, eq
    str w10, [sp, #88]
    sub w9, w25, #1
    tst w25, w9
    mov w10, #-1
    csel w10, w9, w10, eq
    str w10, [sp, #92]

    mov x9, #1
    lsl x9, x9, #32
    udiv x26, x9, x24
    udiv x27, x9, x25

Laot_gap_i32_tho_vong:
    cbz w21, Laot_gap_i32_tho_xong
    mov w9, w20
    tbnz w9, #31, Laot_gap_i32_tho_cycle_am
    ldr w10, [sp, #88]
    tbnz w10, #31, Laot_gap_i32_tho_cycle_nghich_dao
    and w10, w9, w10
    b Laot_gap_i32_tho_cycle_xong
Laot_gap_i32_tho_cycle_nghich_dao:
    uxtw x10, w9
    mul x10, x10, x26
    lsr x10, x10, #32
    msub w11, w10, w24, w9
    cmp w11, w24
    sub w12, w11, w24
    csel w10, w12, w11, hs
    b Laot_gap_i32_tho_cycle_xong
Laot_gap_i32_tho_cycle_am:
    sdiv w10, w9, w24
    msub w10, w10, w24, w9
Laot_gap_i32_tho_cycle_xong:
    add w0, w22, w10
    mov w1, w23
    blr x19

    mov w9, w0
    tbnz w9, #31, Laot_gap_i32_tho_chia_am
    ldr w10, [sp, #92]
    tbnz w10, #31, Laot_gap_i32_tho_chia_nghich_dao
    and w10, w9, w10
    b Laot_gap_i32_tho_chia_xong
Laot_gap_i32_tho_chia_nghich_dao:
    uxtw x10, w9
    mul x10, x10, x27
    lsr x10, x10, #32
    msub w11, w10, w25, w9
    cmp w11, w25
    sub w12, w11, w25
    csel w10, w12, w11, hs
    b Laot_gap_i32_tho_chia_xong
Laot_gap_i32_tho_chia_am:
    sdiv w10, w9, w25
    msub w10, w10, w25, w9
Laot_gap_i32_tho_chia_xong:
    add w28, w28, w10
    add w20, w20, #1
    sub w21, w21, #1
    b Laot_gap_i32_tho_vong

Laot_gap_i32_tho_xong:
    mov w0, w28
    ldr x30, [sp, #80]
    ldp x27, x28, [sp, #64]
    ldp x25, x26, [sp, #48]
    ldp x23, x24, [sp, #32]
    ldp x21, x22, [sp, #16]
    ldp x19, x20, [sp], #96
    ret

// fold_i32(chỉ_số, cấu_hình) thực hiện một batch tối đa 4096 lượt hoàn toàn
// trong thanh ghi máy. Cấu hình gồm first,count,seed,data,cycle,divisor (i32).
.globl _vpp_native_aot_fold_i32
_vpp_native_aot_fold_i32:
    stp x19, x20, [sp, #-96]!
    stp x21, x22, [sp, #16]
    stp x23, x24, [sp, #32]
    stp x25, x26, [sp, #48]
    stp x27, x28, [sp, #64]
    str x30, [sp, #80]
    tbnz w0, #31, Laot_gap_i32_sai
    mov w28, w1
    adrp x9, _vpp_aot_spec@PAGE
    add x9, x9, _vpp_aot_spec@PAGEOFF
    ldr x10, [x9, #80]
    cmp x0, x10
    b.hs Laot_gap_i32_sai
    ldr x10, [x9, #88]
    add x10, x10, x0, lsl #4
    ldr x19, [x10]
    cbz x19, Laot_gap_i32_sai
    bl _vpp_native_memory_base
    cbz x0, Laot_gap_i32_sai
    add x9, x0, w28, uxtw
    ldr w20, [x9]
    ldr w21, [x9, #4]
    ldr w22, [x9, #8]
    ldr w23, [x9, #12]
    ldr w24, [x9, #16]
    ldr w25, [x9, #20]
    cmp w21, #0
    b.le Laot_gap_i32_sai
    cmp w21, #4096
    b.hi Laot_gap_i32_sai
    cmp w24, #0
    b.le Laot_gap_i32_sai
    cmp w25, #0
    b.le Laot_gap_i32_sai
    mov x0, x19
    mov w1, w20
    mov w2, w21
    mov w3, w22
    mov w4, w23
    mov w5, w24
    mov w6, w25
    bl Laot_gap_i32_tho
    b Laot_gap_i32_tra
Laot_gap_i32_sai:
    mov w0, #0
Laot_gap_i32_tra:
    ldr x30, [sp, #80]
    ldp x27, x28, [sp, #64]
    ldp x25, x26, [sp, #48]
    ldp x23, x24, [sp, #32]
    ldp x21, x22, [sp, #16]
    ldp x19, x20, [sp], #96
    ret

#endif

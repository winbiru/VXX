// SHA-256 thuần lệnh CPU cho ABI máy i32 V++ độc lập trên Linux. Ngữ cảnh tại độ lệch 3904: trạng
// thái[8]@0, số byte@32, số byte chờ@40, khối chờ[64]@48; vừa ô SHA256_CTX 112 byte.
#if !defined(__linux__) || !defined(__x86_64__)
#error Bộ băm SHA-256 chỉ hỗ trợ Linux x86-64
#endif

.text
.p2align 4
.globl vpp_native_sha_begin
.type vpp_native_sha_begin, @function
vpp_native_sha_begin:
    pushq %rbp
    movq %rsp, %rbp
    call vpp_native_memory_base
    testq %rax, %rax
    jz .Lsha_bat_dau_sai
    movq .Lsha_ban_dau(%rip), %rdx
    movq %rdx, 3904(%rax)
    movq .Lsha_ban_dau+8(%rip), %rdx
    movq %rdx, 3912(%rax)
    movq .Lsha_ban_dau+16(%rip), %rdx
    movq %rdx, 3920(%rax)
    movq .Lsha_ban_dau+24(%rip), %rdx
    movq %rdx, 3928(%rax)
    movq $0, 3936(%rax)        // Số byte đã xử lý
    movl $0, 3944(%rax)        // Số byte đang chờ
    xorl %eax, %eax
    leave
    ret
.Lsha_bat_dau_sai:
    movl $-1, %eax
    leave
    ret
.size vpp_native_sha_begin, .-vpp_native_sha_begin

.globl vpp_native_sha_update
.type vpp_native_sha_update, @function
vpp_native_sha_update:
    pushq %rbp
    movq %rsp, %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    subq $16, %rsp
    movl %edi, -40(%rbp)
    movl %esi, -44(%rbp)
    call vpp_native_memory_base
    testq %rax, %rax
    jz .Lsha_cap_nhat_sai
    movl -40(%rbp), %edx
    cmpl $135271424, %edx
    ja .Lsha_cap_nhat_sai
    movl -44(%rbp), %r13d
    testl %r13d, %r13d
    js .Lsha_cap_nhat_sai
    movq %rdx, %rcx
    addq %r13, %rcx
    cmpq $135271424, %rcx
    ja .Lsha_cap_nhat_sai
    leaq (%rax,%rdx), %rbx
    leaq 3904(%rax), %r12
    movl 40(%r12), %r14d
    cmpl $63, %r14d
    ja .Lsha_cap_nhat_sai
    addq %r13, 32(%r12)
.Lsha_cap_nhat_tiep:
    testq %r13, %r13
    jz .Lsha_cap_nhat_hop_le
    movb (%rbx), %al
    movb %al, 48(%r12,%r14)
    incq %rbx
    incl %r14d
    decq %r13
    cmpl $64, %r14d
    jne .Lsha_cap_nhat_tiep
    movq %r12, %rdi
    leaq 48(%r12), %rsi
    call .Lsha_nen
    xorl %r14d, %r14d
    jmp .Lsha_cap_nhat_tiep
.Lsha_cap_nhat_hop_le:
    movl %r14d, 40(%r12)
    xorl %eax, %eax
    jmp .Lsha_cap_nhat_ve
.Lsha_cap_nhat_sai:
    movl $-1, %eax
.Lsha_cap_nhat_ve:
    addq $16, %rsp
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    leave
    ret
.size vpp_native_sha_update, .-vpp_native_sha_update

.globl vpp_native_sha_end
.type vpp_native_sha_end, @function
vpp_native_sha_end:
    pushq %rbp
    movq %rsp, %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    subq $8, %rsp
    movl %edi, %r13d
    call vpp_native_memory_base
    testq %rax, %rax
    jz .Lsha_cuoi_sai
    cmpl $68162528, %r13d // Đích cần 32 byte
    ja .Lsha_cuoi_sai
    leaq 3904(%rax), %r12
    leaq (%rax,%r13), %rbx
    movl 40(%r12), %ecx
    cmpl $63, %ecx
    ja .Lsha_cuoi_sai
    movb $0x80, 48(%r12,%rcx)
    incl %ecx
    cmpl $56, %ecx
    jbe .Lsha_cuoi_cuoi_cung
.Lsha_cuoi_dau_dem_cho_du:
    cmpl $64, %ecx
    jae .Lsha_cuoi_dau_xong
    movb $0, 48(%r12,%rcx)
    incl %ecx
    jmp .Lsha_cuoi_dau_dem_cho_du
.Lsha_cuoi_dau_xong:
    movq %r12, %rdi
    leaq 48(%r12), %rsi
    call .Lsha_nen
    xorl %ecx, %ecx
.Lsha_cuoi_cuoi_cung:
    cmpl $56, %ecx
    jae .Lsha_cuoi_do_dai
    movb $0, 48(%r12,%rcx)
    incl %ecx
    jmp .Lsha_cuoi_cuoi_cung
.Lsha_cuoi_do_dai:
    movq 32(%r12), %rax
    shlq $3, %rax
    bswapq %rax
    movq %rax, 104(%r12)
    movq %r12, %rdi
    leaq 48(%r12), %rsi
    call .Lsha_nen
    xorl %ecx, %ecx
.Lsha_cuoi_ma_bam:
    movl (%r12,%rcx), %eax
    bswapl %eax
    movl %eax, (%rbx,%rcx)
    addq $4, %rcx
    cmpl $32, %ecx
    jb .Lsha_cuoi_ma_bam
    movl $0, 40(%r12)
    xorl %eax, %eax
    jmp .Lsha_cuoi_ve
.Lsha_cuoi_sai:
    movl $-1, %eax
.Lsha_cuoi_ve:
    addq $8, %rsp
    popq %r13
    popq %r12
    popq %rbx
    leave
    ret
.size vpp_native_sha_end, .-vpp_native_sha_end

// Hàm nén dùng trạng thái và khối 64 byte; w[64] cùng con trỏ riêng nằm trên ngăn xếp. Các thanh ghi
// được bảo toàn giữ tám từ trạng thái qua từng vòng.
.Lsha_nen:
    pushq %rbp
    movq %rsp, %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    subq $296, %rsp            // lịch 256 byte, con trỏ và phần căn chỉnh
    movq %rdi, 256(%rsp)
    leaq .Lsha_vong_hang_so(%rip), %rax
    movq %rax, 264(%rsp)
    xorl %ecx, %ecx
.Lsha_nap:
    movl (%rsi,%rcx,4), %eax
    bswapl %eax
    movl %eax, (%rsp,%rcx,4)
    incl %ecx
    cmpl $16, %ecx
    jb .Lsha_nap
.Lsha_lich:
    cmpl $64, %ecx
    jae .Lsha_vong_chuan_bi
// sigma0(w[i-15]) = ror7 ^ ror18 ^ shr3.
    movl -60(%rsp,%rcx,4), %edx
    movl %edx, %r10d
    rorl $7, %r10d
    movl %edx, %r11d
    rorl $18, %r11d
    xorl %r11d, %r10d
    shrl $3, %edx
    xorl %edx, %r10d
// sigma1(w[i-2]) = ror17 ^ ror19 ^ shr10.
    movl -8(%rsp,%rcx,4), %edx
    movl %edx, %r11d
    rorl $17, %r11d
    movl %edx, %eax
    rorl $19, %eax
    xorl %eax, %r11d
    shrl $10, %edx
    xorl %edx, %r11d
    addl %r11d, %r10d
    addl -64(%rsp,%rcx,4), %r10d
    addl -28(%rsp,%rcx,4), %r10d
    movl %r10d, (%rsp,%rcx,4)
    incl %ecx
    jmp .Lsha_lich
.Lsha_vong_chuan_bi:
    movq 256(%rsp), %rdx
    movl (%rdx), %eax
    movl 4(%rdx), %ebx
    movl 8(%rdx), %r12d
    movl 12(%rdx), %r13d
    movl 16(%rdx), %r14d
    movl 20(%rdx), %r15d
    movl 24(%rdx), %r8d
    movl 28(%rdx), %r9d
    xorl %ecx, %ecx
.Lsha_vong:
// Giá trị tạm 1 = h + BigSigma1(e) + Ch(e,f,g) + K[i] + W[i].
    movl %r14d, %r10d
    rorl $6, %r10d
    movl %r14d, %edx
    rorl $11, %edx
    xorl %edx, %r10d
    movl %r14d, %edx
    rorl $25, %edx
    xorl %edx, %r10d
    movl %r15d, %edx
    xorl %r8d, %edx
    andl %r14d, %edx
    xorl %r8d, %edx
    addl %edx, %r10d
    addl %r9d, %r10d
    movq 264(%rsp), %rsi
    addl (%rsi,%rcx,4), %r10d
    addl (%rsp,%rcx,4), %r10d
// Giá trị tạm 2 = BigSigma0(a) + Maj(a,b,c).
    movl %eax, %r11d
    rorl $2, %r11d
    movl %eax, %edx
    rorl $13, %edx
    xorl %edx, %r11d
    movl %eax, %edx
    rorl $22, %edx
    xorl %edx, %r11d
    movl %eax, %edx
    xorl %ebx, %edx
    andl %r12d, %edx
// Hàm đa số = (a & b) ^ ((a ^ b) & c).
    movl %eax, %esi
    andl %ebx, %esi
    xorl %esi, %edx
    addl %edx, %r11d
    movl %r8d, %r9d
    movl %r15d, %r8d
    movl %r14d, %r15d
    leal (%r13,%r10), %r14d
    movl %r12d, %r13d
    movl %ebx, %r12d
    movl %eax, %ebx
    leal (%r10,%r11), %eax
    incl %ecx
    cmpl $64, %ecx
    jb .Lsha_vong
    movq 256(%rsp), %rdx
    addl %eax, (%rdx)
    addl %ebx, 4(%rdx)
    addl %r12d, 8(%rdx)
    addl %r13d, 12(%rdx)
    addl %r14d, 16(%rdx)
    addl %r15d, 20(%rdx)
    addl %r8d, 24(%rdx)
    addl %r9d, 28(%rdx)
    addq $296, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    leave
    ret

.section .rodata
.p2align 3
.Lsha_ban_dau:
    .long 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a
    .long 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
.Lsha_vong_hang_so:
    .long 0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5
    .long 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5
    .long 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3
    .long 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174
    .long 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc
    .long 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da
    .long 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7
    .long 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967
    .long 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13
    .long 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85
    .long 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3
    .long 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070
    .long 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5
    .long 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3
    .long 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208
    .long 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
.section .note.GNU-stack,"",@progbits

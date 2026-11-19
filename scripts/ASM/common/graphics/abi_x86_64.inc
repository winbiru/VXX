// Quy ước nội bộ SysV, chuyển đối số nguyên sang ABI Windows tại điểm gọi.
.intel_syntax noprefix
.macro GOI dich
#ifdef _WIN32
    sub rsp, 48
    mov [rsp+32], r8
    mov [rsp+40], r9
    mov r9, rcx
    mov r8, rdx
    mov rdx, rsi
    mov rcx, rdi
    call \dich
    add rsp, 48
#else
    mov r11, rax
    xor eax, eax
    .ifc \dich,rax
        call r11
    .else
        call \dich
    .endif
#endif
.endm
.macro GOI_SAU_FLOAT dich
#ifdef _WIN32
    sub rsp, 32
    call \dich
    add rsp, 32
#else
    call \dich
#endif
.endm
.macro DIA_CHI reg, nhan
    lea \reg, [rip+\nhan]
.endm
// main giữ các thanh ghi không biến đổi theo cả hai ABI.
.macro VAO
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    push r13
    push r14
    push r15
    push rdi
    push rsi
    sub rsp, 8
#ifdef _WIN32
    mov edi, ecx
    mov rsi, rdx
#endif
.endm
.macro RA
    add rsp, 8
    pop rsi
    pop rdi
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret
.endm

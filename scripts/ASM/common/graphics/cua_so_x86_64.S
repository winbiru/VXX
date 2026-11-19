// SDL2: cửa sổ ảnh BMP, sự kiện JSON và đọc lại điểm ảnh OpenGL.
// SDL2 là thư viện nền tảng bên ngoài; dữ liệu ảnh được tạo ở V++.
#include "abi_x86_64.inc"
.macro DOC_SO
    mov r15, rdi
    DIA_CHI rsi, cuoi_so
    mov edx, 10
    GOI strtol
#ifdef _WIN32
    movsxd rax, eax // long là 32-bit trên Windows (LLP64).
#endif
    mov rdx, [rip+cuoi_so]
    cmp rdx, r15
    je Lloi
    cmp byte ptr [rdx], 0
    jne Lloi
    cmp rax, 16384
    ja Lloi
.endm
.text
#ifdef _WIN32
.globl wmain
wmain:
#else
.globl main
main:
#endif
    VAO
    mov r12d, edi
    mov r13, rsi
#ifdef _WIN32
    // CRT wmain giữ nguyên đường dẫn và tiêu đề Unicode.
    cmp r12d, 7
    ja Lloi
    xor ebx, ebx
Ldoi_argv:
    cmp ebx, r12d
    jge Ldoi_xong
    mov rdi, [r13+rbx*8]
    GOI wcslen
    lea rcx, [rax*2+2]
    mov rdx, [r13+rbx*8]
    DIA_CHI rdi, Lutf8
    DIA_CHI rsi, Lutf16
    GOI SDL_iconv_string
    test rax, rax
    jz Lloi
    DIA_CHI rdx, argv_utf8
    mov [rdx+rbx*8], rax
    inc ebx
    jmp Ldoi_argv
Ldoi_xong:
    DIA_CHI r13, argv_utf8
#endif
    cmp r12d, 2
    jne Llenh
    mov rdi, [r13+8]
    DIA_CHI rsi, Lcheck
    GOI strcmp
    test eax, eax
    jnz Lloi
    DIA_CHI rdi, Lversion
    GOI puts
    xor eax, eax
    jmp Lket_thuc
Llenh:
    cmp r12d, 6
    jne Lopengl
    mov rdi, [r13+8]
    DIA_CHI rsi, Lwindow
    GOI strcmp
    test eax, eax
    jz Lwindow_args
    mov rdi, [r13+8]
    DIA_CHI rsi, Lwindowcheck
    GOI strcmp
    test eax, eax
    jz Lwindow_check_args
    mov rdi, [r13+8]
    DIA_CHI rsi, Leventcheck
    GOI strcmp
    test eax, eax
    jnz Lloi
    mov byte ptr [rip+kiem_tra], 2
    jmp Lwindow_args
Lwindow_check_args:
    mov byte ptr [rip+kiem_tra], 1
Lwindow_args:
    mov rdi, [r13+32]
    DOC_SO
    cmp eax, 1
    jl Lloi
    cmp eax, 16384
    jg Lloi
    mov r14d, eax
    mov rdi, [r13+40]
    DOC_SO
    cmp eax, 1
    jl Lloi
    cmp eax, 16384
    jg Lloi
    mov r15d, eax
    GOI SDL_SetMainReady
    mov edi, 32
    GOI SDL_Init
    test eax, eax
    jnz Lloi_sdl
    mov rdi, [r13+24]
    mov esi, 0x2fff0000
    mov edx, esi
    mov ecx, r14d
    mov r8d, r15d
    mov r9d, 0x24 // shown + resizable
    GOI SDL_CreateWindow
    test rax, rax
    jz Lloi_sdl
    mov [rip+cua], rax
    mov rdi, rax
    mov esi, -1
    xor edx, edx
    GOI SDL_CreateRenderer
    test rax, rax
    jz Lloi_sdl
    mov [rip+ve], rax
    mov rdi, [r13+16]
    DIA_CHI rsi, Lrb
    GOI SDL_RWFromFile
    test rax, rax
    jz Lloi_sdl
    mov rdi, rax
    mov esi, 1
    GOI SDL_LoadBMP_RW
    test rax, rax
    jz Lloi_sdl
    mov rbx, rax
    mov rdi, [rip+ve]
    mov rsi, rbx
    GOI SDL_CreateTextureFromSurface
    mov [rip+anh], rax
    mov rdi, rbx
    GOI SDL_FreeSurface
    cmp qword ptr [rip+anh], 0
    je Lloi_sdl
Lve_lai:
    mov rdi, [rip+ve]
    GOI SDL_RenderClear
    test eax, eax
    jnz Lloi_sdl
    mov rdi, [rip+ve]
    mov rsi, [rip+anh]
    xor edx, edx
    xor ecx, ecx
    GOI SDL_RenderCopy
    test eax, eax
    jnz Lloi_sdl
    mov rdi, [rip+ve]
    GOI SDL_RenderPresent
    cmp byte ptr [rip+kiem_tra], 1
    je Lthanh_cong
    cmp byte ptr [rip+kiem_tra], 2
    jne Ldoi
    // Dùng SDL_PushEvent để kiểm thử chính vòng nhận sự kiện.
    mov byte ptr [rip+kiem_tra], 0
    DIA_CHI rdi, phim_thu
    GOI SDL_PushEvent
    cmp eax, 1
    jne Lloi_sdl
    DIA_CHI rdi, chuot_thu
    GOI SDL_PushEvent
    cmp eax, 1
    jne Lloi_sdl
    DIA_CHI rdi, thoat_thu
    GOI SDL_PushEvent
    cmp eax, 1
    jne Lloi_sdl
Ldoi:
    DIA_CHI rdi, su_kien
    GOI SDL_WaitEvent
    test eax, eax
    jz Lloi_sdl
    mov eax, [rip+su_kien]
    cmp eax, 0x100
    je Lthanh_cong
    cmp eax, 0x200
    jne Lphim
    cmp byte ptr [rip+su_kien+12], 14
    je Lthanh_cong
    jmp Lve_lai
Lphim:
    cmp eax, 0x300
    je Lphim_xuat
    cmp eax, 0x301
    jne Lchuot
Lphim_xuat:
    DIA_CHI rdx, Ltrue
    cmp eax, 0x300
    je Lphim_in
    DIA_CHI rdx, Lfalse
Lphim_in:
    mov esi, [rip+su_kien+20]
    mov ecx, esi
    // format dùng cùng keycode hai lần.
    mov r8, rdx
    mov edx, esi
    mov rcx, r8
    DIA_CHI rdi, Lkey
    GOI printf
    jmp Lday
Lchuot:
    cmp eax, 0x401
    je Lchuot_xuat
    cmp eax, 0x402
    jne Ldoi
Lchuot_xuat:
    DIA_CHI r8, Ltrue
    cmp eax, 0x401
    je Lchuot_in
    DIA_CHI r8, Lfalse
Lchuot_in:
    mov esi, [rip+su_kien+20]
    mov edx, [rip+su_kien+24]
    movzx ecx, byte ptr [rip+su_kien+16]
    cmp ecx, 1
    jb Ldoi
    cmp ecx, 5
    ja Ldoi
    dec ecx
    DIA_CHI rdi, Lmouse
    GOI printf
Lday:
    xor edi, edi
    GOI fflush
    jmp Ldoi

// OpenGL: cửa sổ ẩn, clear và glReadPixels từ framebuffer thực.
Lopengl:
    cmp r12d, 5
    jne Lloi
    mov rdi, [r13+8]
    DIA_CHI rsi, Lgl
    GOI strcmp
    test eax, eax
    jnz Lloi
    xor ebx, ebx
Ldoc_mau:
    mov rdi, [r13+rbx*8+16]
    DOC_SO
    cmp eax, 255
    ja Lloi
    DIA_CHI rcx, mau
    mov [rcx+rbx*4], eax
    inc ebx
    cmp ebx, 3
    jl Ldoc_mau
    GOI SDL_SetMainReady
    mov edi, 32
    GOI SDL_Init
    test eax, eax
    jnz Lloi_sdl
    xor ebx, ebx
Lgl_thuoc_tinh:
    mov edi, ebx
    mov esi, 8
    GOI SDL_GL_SetAttribute
    test eax, eax
    jnz Lloi_sdl
    inc ebx
    cmp ebx, 4
    jl Lgl_thuoc_tinh
    DIA_CHI rdi, Lversion
    xor esi, esi
    xor edx, edx
    mov ecx, 32
    mov r8d, 32
    mov r9d, 10
    GOI SDL_CreateWindow
    test rax, rax
    jz Lloi_sdl
    mov [rip+cua], rax
    mov rdi, rax
    GOI SDL_GL_CreateContext
    test rax, rax
    jz Lloi_sdl
    mov [rip+ngu_canh], rax
    DIA_CHI rdi, Lclearcolor
    GOI SDL_GL_GetProcAddress
    test rax, rax
    jz Lloi_sdl
    mov rbx, rax
    cvtsi2ss xmm0, dword ptr [rip+mau]
    cvtsi2ss xmm1, dword ptr [rip+mau+4]
    cvtsi2ss xmm2, dword ptr [rip+mau+8]
    divss xmm0, [rip+f255]
    divss xmm1, [rip+f255]
    divss xmm2, [rip+f255]
    movss xmm3, [rip+f1]
    GOI_SAU_FLOAT rbx
    DIA_CHI rdi, Lclear
    GOI SDL_GL_GetProcAddress
    test rax, rax
    jz Lloi_sdl
    mov edi, 0x4000
    GOI rax
    DIA_CHI rdi, Lread
    GOI SDL_GL_GetProcAddress
    test rax, rax
    jz Lloi_sdl
#ifdef _WIN32
    sub rsp, 64
    xor ecx, ecx
    xor edx, edx
    mov r8d, 1
    mov r9d, 1
    mov qword ptr [rsp+32], 0x1908
    mov qword ptr [rsp+40], 0x1401
    DIA_CHI r10, diem
    mov [rsp+48], r10
    call rax
    add rsp, 64
#else
    sub rsp, 16
    DIA_CHI r10, diem
    mov [rsp], r10
    xor edi, edi
    xor esi, esi
    mov edx, 1
    mov ecx, 1
    mov r8d, 0x1908
    mov r9d, 0x1401
    call rax
    add rsp, 16
#endif
    DIA_CHI rdi, Lgeterror
    GOI SDL_GL_GetProcAddress
    test rax, rax
    jz Lloi_sdl
    GOI rax
    test eax, eax
    jnz Lloi_sdl
    movzx esi, byte ptr [rip+diem]
    movzx edx, byte ptr [rip+diem+1]
    movzx ecx, byte ptr [rip+diem+2]
    movzx r8d, byte ptr [rip+diem+3]
    DIA_CHI rdi, Lpixel
    GOI printf
Lthanh_cong:
    xor r12d, r12d
    jmp Ldon
Lloi_sdl:
    GOI SDL_GetError
    mov rsi, rax
    DIA_CHI rdi, Lerror
    GOI SDL_Log
    mov r12d, 1
Ldon:
    mov rdi, [rip+anh]
    test rdi, rdi
    jz Ldon_ve
    GOI SDL_DestroyTexture
Ldon_ve:
    mov rdi, [rip+ve]
    test rdi, rdi
    jz Ldon_gl
    GOI SDL_DestroyRenderer
Ldon_gl:
    mov rdi, [rip+ngu_canh]
    test rdi, rdi
    jz Ldon_cua
    GOI SDL_GL_DeleteContext
Ldon_cua:
    mov rdi, [rip+cua]
    test rdi, rdi
    jz Ldon_sdl
    GOI SDL_DestroyWindow
Ldon_sdl:
    GOI SDL_Quit
    mov eax, r12d
    jmp Lket_thuc
Lloi:
    mov eax, 2
Lket_thuc:
#ifdef _WIN32
    mov r12d, eax
    xor ebx, ebx
Ldon_argv:
    DIA_CHI rdx, argv_utf8
    mov rdi, [rdx+rbx*8]
    test rdi, rdi
    jz Ltiep_argv
    GOI SDL_free
Ltiep_argv:
    inc ebx
    cmp ebx, 7
    jl Ldon_argv
    mov eax, r12d
#endif
    RA

.data
.p2align 2
f255: .float 255.0
f1: .float 1.0
Lcheck: .asciz "--check"
Lwindow: .asciz "--window"
Lwindowcheck: .asciz "--window-check"
Leventcheck: .asciz "--window-events-check"
Lgl: .asciz "--opengl"
Lversion: .asciz "vpp-graphics-sdl 1"
Lrb: .asciz "rb"
Lutf8: .asciz "UTF-8"
Lutf16: .asciz "UTF-16LE"
Ltrue: .asciz "true"
Lfalse: .asciz "false"
Lkey: .asciz "{\"loại\":\"bàn phím\",\"phím\":\"%d\",\"mã phím\":%d,\"đang nhấn\":%s}\n"
Lmouse: .asciz "{\"loại\":\"chuột\",\"x\":%d,\"y\":%d,\"nút\":%d,\"đang nhấn\":%s}\n"
Lpixel: .asciz "{\"đỏ\":%u,\"lục\":%u,\"lam\":%u,\"alpha\":%u}\n"
Lerror: .asciz "SDL: %s\n"
Lclearcolor: .asciz "glClearColor"
Lclear: .asciz "glClear"
Lread: .asciz "glReadPixels"
Lgeterror: .asciz "glGetError"
.p2align 3
phim_thu: .long 0x300,0,0,1,4,97; .zero 32
chuot_thu: .long 0x401,0,0,0; .byte 1,1,1,0; .long 4,5; .zero 28
thoat_thu: .long 0x100; .zero 52
.bss
.p2align 3
cua: .zero 8
ve: .zero 8
anh: .zero 8
ngu_canh: .zero 8
su_kien: .zero 64
mau: .zero 12
diem: .zero 4
argv_utf8: .zero 56
cuoi_so: .zero 8
kiem_tra: .zero 1
#ifndef _WIN32
.section .note.GNU-stack,"",@progbits
#endif

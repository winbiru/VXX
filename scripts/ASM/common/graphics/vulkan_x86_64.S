// Vulkan 1.0: xóa ảnh RGBA8, sao chép ảnh vào bộ đệm và đồng bộ
// transfer -> host để đọc RGBA từ bộ nhớ HOST_VISIBLE | HOST_COHERENT.
// Các bố cục LP64/LLP64 được kiểm tra bằng Vulkan SDK ở CMake.
#include "abi_x86_64.inc"
.macro RAO_ANH nguon, dich, rao_anh
#ifdef _WIN32
    sub rsp, 80
    mov rcx, [rip+command]
    mov edx, \nguon
    mov r8d, \dich
    xor r9d, r9d
    mov qword ptr [rsp+32], 0
    mov qword ptr [rsp+40], 0
    mov qword ptr [rsp+48], 0
    mov qword ptr [rsp+56], 0
    mov qword ptr [rsp+64], 1
    DIA_CHI rax, \rao_anh
    mov [rsp+72], rax
    call vkCmdPipelineBarrier
    add rsp, 80
#else
    sub rsp, 32
    mov rdi, [rip+command]
    mov esi, \nguon
    mov edx, \dich
    xor ecx, ecx
    xor r8d, r8d
    xor r9d, r9d
    mov qword ptr [rsp], 0
    mov qword ptr [rsp+8], 0
    mov qword ptr [rsp+16], 1
    DIA_CHI rax, \rao_anh
    mov [rsp+24], rax
    call vkCmdPipelineBarrier
    add rsp, 32
#endif
.endm
.text
.globl main
main:
    VAO
    mov r13, rsi
    cmp edi, 5
    jne Ltham_so
    mov rdi, [r13+8]
    DIA_CHI rsi, Llenh
    GOI strcmp
    test eax, eax
    jnz Ltham_so
    xor ebx, ebx
Ldoc:
    mov rdi, [r13+rbx*8+16]
    mov r15, rdi
    DIA_CHI rsi, cuoi
    mov edx, 10
    GOI strtol
#ifdef _WIN32
    movsxd rax, eax // long là 32-bit trên Windows (LLP64).
#endif
    mov rdx, [rip+cuoi]
    cmp rdx, r15
    je Ltham_so
    cmp byte ptr [rdx], 0
    jne Ltham_so
    cmp rax, 255
    ja Ltham_so
    cvtsi2ss xmm0, eax
    divss xmm0, [rip+f255]
    DIA_CHI rdx, mau_xoa
    movss [rdx+rbx*4], xmm0
    inc ebx
    cmp ebx, 3
    jl Ldoc
    mov r12d, 1
    DIA_CHI rdi, tao_instance
    xor esi, esi
    DIA_CHI rdx, instance
    GOI vkCreateInstance
    test eax, eax
    jnz Lloi
    mov r12d, 2
    mov rdi, [rip+instance]
    DIA_CHI rsi, so_thiet_bi
    xor edx, edx
    GOI vkEnumeratePhysicalDevices
    test eax, eax
    jnz Lloi
    cmp dword ptr [rip+so_thiet_bi], 0
    je Lloi
    cmp dword ptr [rip+so_thiet_bi], 32
    jbe Lliet_ke
    mov dword ptr [rip+so_thiet_bi], 32
Lliet_ke:
    mov rdi, [rip+instance]
    DIA_CHI rsi, so_thiet_bi
    DIA_CHI rdx, thiet_bi
    GOI vkEnumeratePhysicalDevices
    cmp eax, 5 // VK_INCOMPLETE
    je Lchon
    test eax, eax
    jnz Lloi
Lchon:
    xor ebx, ebx
Lthu_thiet_bi:
    cmp ebx, [rip+so_thiet_bi]
    jae Lloi
    DIA_CHI rax, thiet_bi
    mov r13, [rax+rbx*8]
    mov dword ptr [rip+so_hang], 64
    mov rdi, r13
    DIA_CHI rsi, so_hang
    DIA_CHI rdx, hang
    GOI vkGetPhysicalDeviceQueueFamilyProperties
    xor r14d, r14d
Lthu_hang:
    cmp r14d, [rip+so_hang]
    jae Ltiep_thiet_bi
    lea eax, [r14+r14*2]
    DIA_CHI rdx, hang
    lea rdx, [rdx+rax*8]
    cmp dword ptr [rdx+4], 0
    je Ltiep_hang
    test dword ptr [rdx], 3 // graphics hoặc compute cho vkCmdClearColorImage
    jnz Ltao_thiet_bi
Ltiep_hang:
    inc r14d
    jmp Lthu_hang
Ltiep_thiet_bi:
    inc ebx
    jmp Lthu_thiet_bi
Ltao_thiet_bi:
    mov r12d, 3
    mov [rip+tao_hang+20], r14d
    mov [rip+tao_pool+20], r14d
    DIA_CHI rax, uu_tien
    mov [rip+tao_hang+32], rax
    DIA_CHI rax, tao_hang
    mov [rip+tao_device+24], rax
    mov rdi, r13
    DIA_CHI rsi, tao_device
    xor edx, edx
    DIA_CHI rcx, device
    GOI vkCreateDevice
    test eax, eax
    jnz Lloi
    mov rdi, [rip+device]
    mov esi, r14d
    xor edx, edx
    DIA_CHI rcx, queue
    GOI vkGetDeviceQueue
    mov r12d, 4
    mov rdi, [rip+device]
    DIA_CHI rsi, tao_buffer
    xor edx, edx
    DIA_CHI rcx, buffer
    GOI vkCreateBuffer
    test eax, eax
    jnz Lloi
    mov rdi, [rip+device]
    mov rsi, [rip+buffer]
    DIA_CHI rdx, yeu_cau
    GOI vkGetBufferMemoryRequirements
    mov rdi, r13
    DIA_CHI rsi, bo_nho_vat_ly
    GOI vkGetPhysicalDeviceMemoryProperties
    xor ebx, ebx
Lchon_nho:
    cmp ebx, [rip+bo_nho_vat_ly]
    jae Lloi
    bt dword ptr [rip+yeu_cau+16], ebx
    jnc Ltiep_nho
    DIA_CHI rax, bo_nho_vat_ly
    mov eax, [rax+rbx*8+4]
    and eax, 6
    cmp eax, 6
    je Lcap_nho
Ltiep_nho:
    inc ebx
    jmp Lchon_nho
Lcap_nho:
    mov r12d, 5
    mov rax, [rip+yeu_cau]
    mov [rip+cap_nho+16], rax
    mov [rip+cap_nho+24], ebx
    mov rdi, [rip+device]
    DIA_CHI rsi, cap_nho
    xor edx, edx
    DIA_CHI rcx, memory
    GOI vkAllocateMemory
    test eax, eax
    jnz Lloi
    mov rdi, [rip+device]
    mov rsi, [rip+buffer]
    mov rdx, [rip+memory]
    xor ecx, ecx
    GOI vkBindBufferMemory
    test eax, eax
    jnz Lloi
    mov r12d, 6
    mov rdi, [rip+device]
    DIA_CHI rsi, tao_anh
    xor edx, edx
    DIA_CHI rcx, image
    GOI vkCreateImage
    test eax, eax
    jnz Lloi
    mov rdi, [rip+device]
    mov rsi, [rip+image]
    DIA_CHI rdx, yeu_cau
    GOI vkGetImageMemoryRequirements
    xor ebx, ebx
Lchon_nho_anh:
    cmp ebx, [rip+bo_nho_vat_ly]
    jae Lloi
    bt dword ptr [rip+yeu_cau+16], ebx
    jc Lcap_nho_anh
    inc ebx
    jmp Lchon_nho_anh
Lcap_nho_anh:
    mov rax, [rip+yeu_cau]
    mov [rip+cap_nho+16], rax
    mov [rip+cap_nho+24], ebx
    mov rdi, [rip+device]
    DIA_CHI rsi, cap_nho
    xor edx, edx
    DIA_CHI rcx, image_memory
    GOI vkAllocateMemory
    test eax, eax
    jnz Lloi
    mov rdi, [rip+device]
    mov rsi, [rip+image]
    mov rdx, [rip+image_memory]
    xor ecx, ecx
    GOI vkBindImageMemory
    test eax, eax
    jnz Lloi
    mov rax, [rip+image]
    mov [rip+rao_anh_ghi+40], rax
    mov [rip+rao_anh_doc+40], rax
    mov r12d, 7
    mov rdi, [rip+device]
    DIA_CHI rsi, tao_pool
    xor edx, edx
    DIA_CHI rcx, pool
    GOI vkCreateCommandPool
    test eax, eax
    jnz Lloi
    mov rax, [rip+pool]
    mov [rip+cap_lenh+16], rax
    mov rdi, [rip+device]
    DIA_CHI rsi, cap_lenh
    DIA_CHI rdx, command
    GOI vkAllocateCommandBuffers
    test eax, eax
    jnz Lloi
    mov rdi, [rip+command]
    DIA_CHI rsi, bat_dau
    GOI vkBeginCommandBuffer
    test eax, eax
    jnz Lloi
    // UNDEFINED -> TRANSFER_DST_OPTIMAL; xóa màu bằng GPU.
    RAO_ANH 1, 0x1000, rao_anh_ghi
    mov rdi, [rip+command]
    mov rsi, [rip+image]
    mov edx, 7
    DIA_CHI rcx, mau_xoa
    mov r8d, 1
    DIA_CHI r9, pham_vi
    GOI vkCmdClearColorImage
    // TRANSFER_DST_OPTIMAL -> TRANSFER_SRC_OPTIMAL; chép ảnh vào buffer.
    RAO_ANH 0x1000, 0x1000, rao_anh_doc
    mov rdi, [rip+command]
    mov rsi, [rip+image]
    mov edx, 6
    mov rcx, [rip+buffer]
    mov r8d, 1
    DIA_CHI r9, vung_chep
    GOI vkCmdCopyImageToBuffer
    // 10 đối số: barrier bộ nhớ, không có buffer/image barrier riêng.
#ifdef _WIN32
    sub rsp, 80
    mov rcx, [rip+command]
    mov edx, 0x1000 // TRANSFER
    mov r8d, 0x4000 // HOST
    xor r9d, r9d
    mov qword ptr [rsp+32], 1
    DIA_CHI rax, rao
    mov [rsp+40], rax
    mov qword ptr [rsp+48], 0
    mov qword ptr [rsp+56], 0
    mov qword ptr [rsp+64], 0
    mov qword ptr [rsp+72], 0
    call vkCmdPipelineBarrier
    add rsp, 80
#else
    sub rsp, 32
    mov rdi, [rip+command]
    mov esi, 0x1000
    mov edx, 0x4000
    xor ecx, ecx
    mov r8d, 1
    DIA_CHI r9, rao
    mov qword ptr [rsp], 0
    mov qword ptr [rsp+8], 0
    mov qword ptr [rsp+16], 0
    mov qword ptr [rsp+24], 0
    call vkCmdPipelineBarrier
    add rsp, 32
#endif
    mov rdi, [rip+command]
    GOI vkEndCommandBuffer
    test eax, eax
    jnz Lloi
    mov r12d, 8
    DIA_CHI rax, command
    mov [rip+gui+48], rax
    mov rdi, [rip+queue]
    mov esi, 1
    DIA_CHI rdx, gui
    xor ecx, ecx
    GOI vkQueueSubmit
    test eax, eax
    jnz Lloi
    mov rdi, [rip+queue]
    GOI vkQueueWaitIdle
    test eax, eax
    jnz Lloi
    mov r12d, 9
    mov rdi, [rip+device]
    mov rsi, [rip+memory]
    xor edx, edx
    mov ecx, 4
    xor r8d, r8d
    DIA_CHI r9, dia_chi
    GOI vkMapMemory
    test eax, eax
    jnz Lloi
    mov rax, [rip+dia_chi]
    mov r14d, [rax]
    mov rdi, [rip+device]
    mov rsi, [rip+memory]
    GOI vkUnmapMemory
    mov esi, r14d
    and esi, 255
    mov edx, r14d
    shr edx, 8
    and edx, 255
    mov ecx, r14d
    shr ecx, 16
    and ecx, 255
    mov r8d, r14d
    shr r8d, 24
    DIA_CHI rdi, Lpixel
    GOI printf
    xor r12d, r12d
    jmp Ldon
Lloi:
    // Mã thoát chỉ rõ bước lỗi, stdout dành cho JSON.
Ldon:
    cmp qword ptr [rip+device], 0
    je Ldon_instance
    mov rdi, [rip+device]
    GOI vkDeviceWaitIdle
    mov rsi, [rip+pool]
    test rsi, rsi
    jz Ldon_anh
    mov rdi, [rip+device]
    xor edx, edx
    GOI vkDestroyCommandPool
Ldon_anh:
    mov rsi, [rip+image]
    test rsi, rsi
    jz Ldon_nho_anh
    mov rdi, [rip+device]
    xor edx, edx
    GOI vkDestroyImage
Ldon_nho_anh:
    mov rsi, [rip+image_memory]
    test rsi, rsi
    jz Ldon_buffer
    mov rdi, [rip+device]
    xor edx, edx
    GOI vkFreeMemory
Ldon_buffer:
    mov rsi, [rip+buffer]
    test rsi, rsi
    jz Ldon_nho
    mov rdi, [rip+device]
    xor edx, edx
    GOI vkDestroyBuffer
Ldon_nho:
    mov rsi, [rip+memory]
    test rsi, rsi
    jz Ldon_device
    mov rdi, [rip+device]
    xor edx, edx
    GOI vkFreeMemory
Ldon_device:
    mov rdi, [rip+device]
    xor esi, esi
    GOI vkDestroyDevice
Ldon_instance:
    mov rdi, [rip+instance]
    test rdi, rdi
    jz Ltra
    xor esi, esi
    GOI vkDestroyInstance
Ltra:
    mov eax, r12d
    RA
Ltham_so:
    mov eax, 10
    RA

.data
Llenh: .asciz "--vulkan"
Lpixel: .asciz "{\"đỏ\":%u,\"lục\":%u,\"lam\":%u,\"alpha\":%u}\n"
.p2align 3
uu_tien: .float 1.0
f255: .float 255.0
mau_xoa: .float 0,0,0,1
pham_vi: .long 1,0,1,0,1
.p2align 3
vung_chep: .quad 0; .long 0,0,1,0,0,1,0,0,0,1,1,1

.p2align 3
// sType, pNext và các trường còn lại theo Vulkan 1.0.
tao_instance: .long 1,0; .zero 56
tao_hang: .long 2,0; .quad 0; .long 0,0,1,0; .quad 0
tao_device: .long 3,0; .quad 0; .long 0,1; .zero 48
tao_buffer: .long 12,0; .quad 0; .quad 0,4; .long 2,0,0,0; .quad 0
tao_anh: .long 14,0; .quad 0; .long 0,1,37,1,1,1,1,1,1,0,3,0,0,0; .quad 0; .long 0,0
rao_anh_ghi: .long 45,0; .quad 0; .long 0,0x1000,0,7,-1,-1; .quad 0; .long 1,0,1,0,1,0
rao_anh_doc: .long 45,0; .quad 0; .long 0x1000,0x800,7,6,-1,-1; .quad 0; .long 1,0,1,0,1,0
cap_nho: .long 5,0; .quad 0,0; .long 0,0
tao_pool: .long 39,0; .quad 0; .long 0,0
cap_lenh: .long 40,0; .quad 0,0; .long 0,1
bat_dau: .long 42,0; .quad 0; .long 1,0; .quad 0
rao: .long 46,0; .quad 0; .long 0x1000,0x2000
gui: .long 4,0; .quad 0; .long 0,0; .quad 0,0; .long 1,0; .quad 0,0,0
.bss
.p2align 3
cuoi: .zero 8
instance: .zero 8
device: .zero 8
queue: .zero 8
buffer: .zero 8
image: .zero 8
image_memory: .zero 8
memory: .zero 8
pool: .zero 8
command: .zero 8
dia_chi: .zero 8
so_thiet_bi: .zero 4
so_hang: .zero 4
.p2align 3
thiet_bi: .zero 256
hang: .zero 1536
yeu_cau: .zero 24
bo_nho_vat_ly: .zero 528
#ifndef _WIN32
.section .note.GNU-stack,"",@progbits
#endif

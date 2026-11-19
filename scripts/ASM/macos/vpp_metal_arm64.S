// Metal: xóa texture RGBA8 và đọc điểm ảnh sau khi GPU hoàn tất.
.text
.p2align 2
.macro DIA_CHI reg, sym
    adrp \reg, \sym@PAGE
    add \reg, \reg, \sym@PAGEOFF
.endm
// Các selector cố định do linker Objective-C cung cấp.
.macro GOI sel
    bl "_objc_msgSend$\sel"
.endm
.globl _main
_main:
    stp x29, x30, [sp, #-144]!
    mov x29, sp
    stp x19, x20, [sp, #16]
    stp x21, x22, [sp, #32]
    stp x23, x24, [sp, #48]
    stp x25, x26, [sp, #64]
    stp x27, x28, [sp, #80]
    mov w19, w0
    mov x20, x1
    mov x21, #0 // autorelease pool
    mov x22, #0 // device
    mov x23, #0 // queue
    mov x24, #0 // texture
    mov w28, #2
    cmp w19, #5
    b.ne Lxong
    ldr x0, [x20, #8]
    DIA_CHI x1, Lmetal
    bl _strcmp
    cbnz w0, Lxong
    mov x25, #0
Ldoc_mau:
    add x8, x20, #16
    ldr x0, [x8, x25, lsl #3]
    add x1, sp, #112
    mov x26, x0
    mov w2, #10
    bl _strtol
    ldr x8, [sp, #112]
    cmp x8, x26
    b.eq Lxong
    ldrb w8, [x8]
    cbnz w8, Lxong
    cmp x0, #255
    b.hi Lxong
    add x8, sp, #96
    str w0, [x8, x25, lsl #2]
    add x25, x25, #1
    cmp x25, #3
    b.lt Ldoc_mau
    bl _objc_autoreleasePoolPush
    mov x21, x0
    mov w28, #1
    mov w19, #1
    bl _MTLCreateSystemDefaultDevice
    cbz x0, Lloi
    mov x22, x0
    mov w19, #2
    GOI newCommandQueue
    cbz x0, Lloi
    mov x23, x0
    mov w19, #3
    DIA_CHI x0, Ltexture
    bl _objc_getClass
    mov w2, #70 // MTLPixelFormatRGBA8Unorm
    mov w3, #1
    mov w4, #1
    mov w5, #0
    GOI texture2DDescriptorWithPixelFormat:width:height:mipmapped:
    cbz x0, Lloi
    mov x25, x0
    mov x2, #0 // MTLStorageModeShared
    GOI setStorageMode:
    mov x0, x25
    mov x2, #4 // MTLTextureUsageRenderTarget
    GOI setUsage:
    mov w19, #4
    mov x0, x22
    mov x2, x25
    GOI newTextureWithDescriptor:
    cbz x0, Lloi
    mov x24, x0
    mov w19, #5
    DIA_CHI x0, Lpass
    bl _objc_getClass
    GOI renderPassDescriptor
    cbz x0, Lloi
    mov x25, x0
    GOI colorAttachments
    mov x2, #0
    GOI objectAtIndexedSubscript:
    cbz x0, Lloi
    mov x26, x0
    mov x2, x24
    GOI setTexture:
    mov x0, x26
    mov x2, #2 // MTLLoadActionClear
    GOI setLoadAction:
    mov x0, x26
    mov x2, #1 // MTLStoreActionStore
    GOI setStoreAction:
    ldp w8, w9, [sp, #96]
    ldr w10, [sp, #104]
    ucvtf d0, w8
    ucvtf d1, w9
    ucvtf d2, w10
    mov w8, #255
    ucvtf d4, w8
    fdiv d0, d0, d4
    fdiv d1, d1, d4
    fdiv d2, d2, d4
    fmov d3, #1.0
    mov x0, x26
    GOI setClearColor:
    mov w19, #6
    mov x0, x23
    GOI commandBuffer
    cbz x0, Lloi
    mov x27, x0
    mov w19, #7
    mov x2, x25
    GOI renderCommandEncoderWithDescriptor:
    cbz x0, Lloi
    GOI endEncoding
    mov x0, x27
    GOI commit
    mov x0, x27
    GOI waitUntilCompleted
    mov w19, #8
    mov x0, x27
    GOI status
    cmp x0, #4
    b.ne Lloi
    // MTLRegion gồm 6 NSUInteger, truyền gián tiếp theo ABI ARM64.
    sub sp, sp, #64
    stp xzr, xzr, [sp]
    mov x8, #1
    stp xzr, x8, [sp, #16]
    stp x8, x8, [sp, #32]
    mov x0, x24
    add x2, sp, #48
    mov x3, #4
    mov x4, sp
    mov x5, #0
    GOI getBytes:bytesPerRow:fromRegion:mipmapLevel:
    ldr w8, [sp, #48]
    add sp, sp, #64
    // Darwin ARM64 truyền đối số variadic trên stack.
    and x9, x8, #255
    ubfx x10, x8, #8, #8
    ubfx x11, x8, #16, #8
    ubfx x12, x8, #24, #8
    sub sp, sp, #32
    stp x9, x10, [sp]
    stp x11, x12, [sp, #16]
    DIA_CHI x0, Lpixel
    bl _printf
    add sp, sp, #32
    mov w28, #0
    b Lxong
Lloi:
    DIA_CHI x0, Lerror
    adrp x8, ___stderrp@GOTPAGE
    ldr x8, [x8, ___stderrp@GOTPAGEOFF]
    ldr x1, [x8]
    sub sp, sp, #16
    str x19, [sp]
    mov x0, x1
    DIA_CHI x1, Lerror
    bl _fprintf
    add sp, sp, #16
Lxong:
    mov x0, x24
    GOI release
    mov x0, x23
    GOI release
    mov x0, x22
    GOI release
    cbz x21, Ltra
    mov x0, x21
    bl _objc_autoreleasePoolPop
Ltra:
    mov w0, w28
    ldp x27, x28, [sp, #80]
    ldp x25, x26, [sp, #64]
    ldp x23, x24, [sp, #48]
    ldp x21, x22, [sp, #32]
    ldp x19, x20, [sp, #16]
    ldp x29, x30, [sp], #144
    ret
.cstring
Lmetal: .asciz "--metal"
Ltexture: .asciz "MTLTextureDescriptor"
Lpass: .asciz "MTLRenderPassDescriptor"
Lpixel: .asciz "{\"đỏ\":%u,\"lục\":%u,\"lam\":%u,\"alpha\":%u}\n"
Lerror: .asciz "Metal: không thể thực hiện lệnh GPU (bước %d)\n"

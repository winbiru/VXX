// Cửa sổ Cocoa chứa WKWebView. Giao diện và nội dung thuộc V++; ASM chỉ nối ABI.
// argv: đường HTML tuyệt đối, tiêu đề UTF-8, rộng, cao. stdout: JSON từng dòng.
.text
.p2align 2
.macro DIA_CHI reg, sym
    adrp \reg, \sym@PAGE
    add \reg, \reg, \sym@PAGEOFF
.endm
.macro LOP sym
    DIA_CHI x0, \sym
    bl _objc_getClass
.endm
.macro CHON sym
    DIA_CHI x0, \sym
    bl _sel_registerName
    mov x1, x0
.endm

// Phương thức delegate: đóng cửa sổ kết thúc tiến trình.
Ldong:
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    LOP Lnsapp
    mov x8, x0
    stp x8, xzr, [sp, #-16]!
    CHON Lshared
    ldp x0, xzr, [sp], #16
    bl _objc_msgSend
    stp x0, xzr, [sp, #-16]!
    CHON Lterminate
    ldp x0, xzr, [sp], #16
    mov x2, #0
    bl _objc_msgSend
    ldp x29, x30, [sp], #16
    ret

// WKScriptMessageHandler: chuyển body (NSDictionary) thành JSON UTF-8.
// Không giữ con trỏ từ JavaScript ngoài phạm vi callback.
Lsu_kien:
    stp x29, x30, [sp, #-48]!
    mov x29, sp
    stp x19, x20, [sp, #16]
    stp x21, x22, [sp, #32]
    mov x19, x3
    CHON Lbody
    mov x0, x19
    bl _objc_msgSend
    mov x19, x0
    LOP Ljson
    mov x20, x0
    CHON Lvalidjson
    mov x0, x20
    mov x2, x19
    bl _objc_msgSend
    cbz w0, Lsu_kien_xong
    CHON Ljsondata
    mov x0, x20
    mov x2, x19
    mov x3, #0
    mov x4, #0
    bl _objc_msgSend
    cbz x0, Lsu_kien_xong
    mov x19, x0
    LOP Lstring
    mov x20, x0
    CHON Lalloc
    mov x0, x20
    bl _objc_msgSend
    mov x20, x0
    CHON Linitdata
    mov x0, x20
    mov x2, x19
    mov x3, #4 // NSUTF8StringEncoding
    bl _objc_msgSend
    cbz x0, Lsu_kien_xong
    mov x20, x0
    CHON Lutf8
    mov x0, x20
    bl _objc_msgSend
    cbz x0, Lsu_kien_giai_phong
    bl _puts
    mov x0, #0
    bl _fflush
Lsu_kien_giai_phong:
    CHON Lrelease
    mov x0, x20
    bl _objc_msgSend
Lsu_kien_xong:
    ldp x21, x22, [sp, #32]
    ldp x19, x20, [sp, #16]
    ldp x29, x30, [sp], #48
    ret

.globl _main
.p2align 2
_main:
    stp x29, x30, [sp, #-112]!
    mov x29, sp
    stp x19, x20, [sp, #16]
    stp x21, x22, [sp, #32]
    stp x23, x24, [sp, #48]
    stp x25, x26, [sp, #64]
    stp x27, x28, [sp, #80]
    mov x19, x1
    cmp w0, #2
    b.ne Ltham_so
    ldr x0, [x19, #8]
    DIA_CHI x1, Lcheck
    bl _strcmp
    cbnz w0, Lloi
    DIA_CHI x0, Lversion
    bl _puts
    mov w0, #0
    b Lxong
Ltham_so:
    cmp w0, #5
    b.ne Lloi
    ldr x0, [x19, #24]
    bl _atoi
    cmp w0, #1
    b.lt Lloi
    cmp w0, #16384
    b.gt Lloi
    mov w27, w0
    ldr x0, [x19, #32]
    bl _atoi
    cmp w0, #1
    b.lt Lloi
    cmp w0, #16384
    b.gt Lloi
    mov w28, w0
    bl _objc_autoreleasePoolPush
    str x0, [sp, #96]
    LOP Lnsapp
    mov x20, x0
    CHON Lshared
    mov x0, x20
    bl _objc_msgSend
    mov x20, x0
    CHON Lpolicy
    mov x0, x20
    mov x2, #0
    bl _objc_msgSend

    // Đăng ký lớp delegate cho cả NSWindow và WKScriptMessageHandler.
    LOP Lobject
    DIA_CHI x1, Ldelegate_name
    mov x2, #0
    bl _objc_allocateClassPair
    cbz x0, Lloi_pool
    mov x21, x0
    CHON Lclose
    mov x0, x21
    DIA_CHI x2, Ldong
    DIA_CHI x3, Lclose_type
    bl _class_addMethod
    cbz w0, Lloi_pool
    CHON Lmessage
    mov x0, x21
    DIA_CHI x2, Lsu_kien
    DIA_CHI x3, Lmessage_type
    bl _class_addMethod
    cbz w0, Lloi_pool
    mov x0, x21
    bl _objc_registerClassPair
    CHON Lnew
    mov x0, x21
    bl _objc_msgSend
    mov x21, x0

    // Cửa sổ Cocoa, NSRect truyền qua d0..d3 (AAPCS64 HFA).
    LOP Lwindow
    mov x22, x0
    CHON Lalloc
    mov x0, x22
    bl _objc_msgSend
    mov x22, x0
    CHON Linitwindow
    mov x0, x22
    mov x2, #15 // titled, closable, miniaturizable, resizable
    mov x3, #2  // NSBackingStoreBuffered
    mov x4, #0
    fmov d0, xzr
    fmov d1, xzr
    scvtf d2, w27
    scvtf d3, w28
    bl _objc_msgSend
    cbz x0, Lloi_pool
    mov x22, x0
    CHON Lreleased
    mov x0, x22
    mov x2, #0
    bl _objc_msgSend
    CHON Lsetdelegate
    mov x0, x22
    mov x2, x21
    bl _objc_msgSend

    LOP Lstring
    mov x23, x0
    CHON Lfromutf8
    mov x0, x23
    ldr x2, [x19, #16]
    bl _objc_msgSend
    mov x24, x0
    CHON Ltitle
    mov x0, x22
    mov x2, x24
    bl _objc_msgSend

    // WKWebViewConfiguration và kênh sự kiện JS -> Cocoa.
    LOP Lconfig
    mov x24, x0
    CHON Lnew
    mov x0, x24
    bl _objc_msgSend
    mov x24, x0
    // Kho dữ liệu tạm cho mỗi cửa sổ, không ghi dữ liệu trình duyệt vào hồ sơ người dùng.
    LOP Ldatastore
    mov x25, x0
    CHON Lnonpersistent
    mov x0, x25
    bl _objc_msgSend
    mov x25, x0
    CHON Lsetdatastore
    mov x0, x24
    mov x2, x25
    bl _objc_msgSend
    CHON Lcontroller
    mov x0, x24
    bl _objc_msgSend
    mov x25, x0
    CHON Lfromutf8
    mov x0, x23
    DIA_CHI x2, Lchannel
    bl _objc_msgSend
    mov x26, x0
    CHON Laddhandler
    mov x0, x25
    mov x2, x21
    mov x3, x26
    bl _objc_msgSend

    LOP Lwebview
    mov x25, x0
    CHON Lalloc
    mov x0, x25
    bl _objc_msgSend
    mov x25, x0
    CHON Linitview
    mov x0, x25
    mov x2, x24
    fmov d0, xzr
    fmov d1, xzr
    scvtf d2, w27
    scvtf d3, w28
    bl _objc_msgSend
    cbz x0, Lloi_pool
    mov x25, x0
    CHON Lcontent
    mov x0, x22
    mov x2, x25
    bl _objc_msgSend

    // Chỉ cấp quyền đọc tệp HTML được truyền qua argv.
    CHON Lfromutf8
    mov x0, x23
    ldr x2, [x19, #8]
    bl _objc_msgSend
    cbz x0, Lloi_pool
    mov x26, x0
    LOP Lurl
    mov x23, x0
    CHON Lfileurl
    mov x0, x23
    mov x2, x26
    bl _objc_msgSend
    cbz x0, Lloi_pool
    mov x26, x0
    CHON Lload
    mov x0, x25
    mov x2, x26
    mov x3, x26
    bl _objc_msgSend

    CHON Lcenter
    mov x0, x22
    bl _objc_msgSend
    CHON Lshow
    mov x0, x22
    mov x2, #0
    bl _objc_msgSend
    CHON Lactivate
    mov x0, x20
    mov x2, #1
    bl _objc_msgSend
    CHON Lrun
    mov x0, x20
    bl _objc_msgSend
    ldr x0, [sp, #96]
    bl _objc_autoreleasePoolPop
    mov w0, #0
    b Lxong
Lloi_pool:
    ldr x0, [sp, #96]
    bl _objc_autoreleasePoolPop
Lloi:
    mov w0, #2
Lxong:
    ldp x27, x28, [sp, #80]
    ldp x25, x26, [sp, #64]
    ldp x23, x24, [sp, #48]
    ldp x21, x22, [sp, #32]
    ldp x19, x20, [sp, #16]
    ldp x29, x30, [sp], #112
    ret

.section __TEXT,__cstring,cstring_literals
Lcheck: .asciz "--check"
Lversion: .asciz "vpp-graphics-macos 1"
Lnsapp: .asciz "NSApplication"
Lobject: .asciz "NSObject"
Lwindow: .asciz "NSWindow"
Lstring: .asciz "NSString"
Ljson: .asciz "NSJSONSerialization"
Lurl: .asciz "NSURL"
Ldatastore: .asciz "WKWebsiteDataStore"
Lnonpersistent: .asciz "nonPersistentDataStore"
Lsetdatastore: .asciz "setWebsiteDataStore:"
Lconfig: .asciz "WKWebViewConfiguration"
Lwebview: .asciz "WKWebView"
Ldelegate_name: .asciz "VPPGraphicsDelegate"
Lchannel: .asciz "vpp"
Lshared: .asciz "sharedApplication"
Lpolicy: .asciz "setActivationPolicy:"
Lclose: .asciz "windowWillClose:"
Lclose_type: .asciz "v@:@"
Lmessage: .asciz "userContentController:didReceiveScriptMessage:"
Lmessage_type: .asciz "v@:@@"
Lbody: .asciz "body"
Lvalidjson: .asciz "isValidJSONObject:"
Ljsondata: .asciz "dataWithJSONObject:options:error:"
Linitdata: .asciz "initWithData:encoding:"
Lutf8: .asciz "UTF8String"
Lrelease: .asciz "release"
Lalloc: .asciz "alloc"
Lnew: .asciz "new"
Linitwindow: .asciz "initWithContentRect:styleMask:backing:defer:"
Lreleased: .asciz "setReleasedWhenClosed:"
Lsetdelegate: .asciz "setDelegate:"
Lfromutf8: .asciz "stringWithUTF8String:"
Ltitle: .asciz "setTitle:"
Lcontroller: .asciz "userContentController"
Laddhandler: .asciz "addScriptMessageHandler:name:"
Linitview: .asciz "initWithFrame:configuration:"
Lcontent: .asciz "setContentView:"
Lfileurl: .asciz "fileURLWithPath:"
Lload: .asciz "loadFileURL:allowingReadAccessToURL:"
Lcenter: .asciz "center"
Lshow: .asciz "makeKeyAndOrderFront:"
Lactivate: .asciz "activateIgnoringOtherApps:"
Lrun: .asciz "run"
Lterminate: .asciz "terminate:"
.subsections_via_symbols

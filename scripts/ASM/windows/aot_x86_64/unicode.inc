// Chuyển đường dẫn giữa UTF-8 và UTF-16.

// Đường V++ là UTF-8; API CRT dùng bảng mã ANSI nên phải đổi sang UTF-16 để giữ đúng tên tiếng Việt.
// rcx: đường UTF-8 kết thúc NUL; rdx: đích 4096 WCHAR. Trả số WCHAR kể cả NUL, hoặc 0 nếu lỗi.
Lduong_utf8_sang_utf16:
    subq $56, %rsp
    movq %rdx, 32(%rsp)    // lpWideCharStr, đối số 5
    movl $4096, 40(%rsp)  // cchWideChar, đối số 6
    movq %rcx, %r8       // lpMultiByteStr
    movl $65001, %ecx   // CP_UTF8
    movl $8, %edx       // MB_ERR_INVALID_CHARS
    movl $-1, %r9d      // Đầu vào kết thúc bằng NUL
    call MultiByteToWideChar
    addq $56, %rsp
    ret
// rcx: đường UTF-16 kết thúc NUL; rdx: đích UTF-8 dài 4096 byte. Trả số byte kể cả NUL, hoặc 0 nếu
// lỗi.
Lduong_utf16_sang_utf8:
    subq $72, %rsp
    movq %rdx, 32(%rsp)    // lpMultiByteStr, đối số 5
    movl $4096, 40(%rsp)  // cbMultiByte, đối số 6
    movq $0, 48(%rsp)     // lpDefaultChar, đối số 7
    movq $0, 56(%rsp)     // lpUsedDefaultChar, đối số 8
    movq %rcx, %r8       // lpWideCharStr
    movl $65001, %ecx    // CP_UTF8
    xorl %edx, %edx
    movl $-1, %r9d
    call WideCharToMultiByte
    addq $72, %rsp
    ret


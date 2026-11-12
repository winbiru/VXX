// Trích dẫn đối số UTF-16 theo quy tắc dòng lệnh Windows.

// rcx giữ đối số UTF-16 đã kiểm tra, tối đa 4095 đơn vị mã; rdx là đệm 16392 byte. _wspawnvp ghép
// nguyên các mục argv nên phải trích dẫn theo quy tắc Windows: gấp đôi số dấu gạch chéo ngược trước
// nháy đóng, thêm một dấu khi trước nháy trong nội dung; vị trí khác giữ nguyên.
Ltao_tien_trinh_trich_utf16:
    movw $34, (%rdx)
    addq $2, %rdx
Ltao_tien_trinh_trich_tiep:
    movzwl (%rcx), %eax
    testw %ax, %ax
    je Ltao_tien_trinh_trich_ket_thuc
    cmpw $92, %ax
    je Ltao_tien_trinh_trich_gach_cheo
    cmpw $34, %ax
    jne Ltao_tien_trinh_trich_thuong
    movw $92, (%rdx)
    addq $2, %rdx
Ltao_tien_trinh_trich_thuong:
    movw %ax, (%rdx)
    addq $2, %rdx
    addq $2, %rcx
    jmp Ltao_tien_trinh_trich_tiep
Ltao_tien_trinh_trich_gach_cheo:
    xorl %r9d, %r9d
Ltao_tien_trinh_trich_dem:
    cmpw $92, (%rcx)
    jne Ltao_tien_trinh_trich_da_dem
    incq %r9
    addq $2, %rcx
    jmp Ltao_tien_trinh_trich_dem
Ltao_tien_trinh_trich_da_dem:
    xorl %r8d, %r8d          // Ký tự thường tiếp theo
    cmpw $34, (%rcx)
    jne 1f
    leaq 1(%r9,%r9), %r9    // 2*n+1 dấu gạch chéo ngược trước nháy trong nội dung
    movl $1, %r8d
    jmp Ltao_tien_trinh_trich_ghi
1:  cmpw $0, (%rcx)
    jne Ltao_tien_trinh_trich_ghi
    addq %r9, %r9           // 2*n dấu gạch chéo ngược trước nháy đóng
    movl $2, %r8d
Ltao_tien_trinh_trich_ghi:
    testq %r9, %r9
    je Ltao_tien_trinh_trich_sau_gach_cheo
    movw $92, (%rdx)
    addq $2, %rdx
    decq %r9
    jmp Ltao_tien_trinh_trich_ghi
Ltao_tien_trinh_trich_sau_gach_cheo:
    cmpl $1, %r8d
    jne 1f
    movw $34, (%rdx)
    addq $2, %rdx
    addq $2, %rcx
    jmp Ltao_tien_trinh_trich_tiep
1:  cmpl $2, %r8d
    je Ltao_tien_trinh_trich_ket_thuc
    jmp Ltao_tien_trinh_trich_tiep
Ltao_tien_trinh_trich_ket_thuc:
    movw $34, (%rdx)
    movw $0, 2(%rdx)
    ret

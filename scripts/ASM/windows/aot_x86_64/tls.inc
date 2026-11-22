// Schannel TLS client on an existing Winsock TCP handle.
// Native pointers live only in ctl+120 and this private session allocation.
// ctl+144 is set to 1 only after SSL chain and hostname policy succeeds.
.equ TLS_CRED, 0
.equ TLS_CTX, 16
.equ TLS_SOCKET, 32
.equ TLS_FLAGS, 40
.equ TLS_PENDING, 44
.equ TLS_CIPHER, 48
.equ TLS_PLAIN, 56
.equ TLS_PLAIN_POS, 64
.equ TLS_PLAIN_LEN, 68
.equ TLS_HEADER, 72
.equ TLS_TRAILER, 76
.equ TLS_MAX_MESSAGE, 80
// SecPkgContext_StreamSizes writes five DWORDs at 72..91, including
// cbBlockSize at 88. Keep the absolute deadline outside that structure.
.equ TLS_BLOCK_SIZE, 88
.equ TLS_DEADLINE, 96
.equ TLS_IN_BUFS, 112
.equ TLS_IN_DESC, 176
.equ TLS_CONN_INFO, 192
.equ TLS_OUT_BUFS, 1056
.equ TLS_OUT_DESC, 1088
.equ TLS_SCHANNEL_CRED, 224
.equ TLS_EXPIRY, 304
.equ TLS_ATTR, 312
.equ TLS_CERT, 320
.equ TLS_CHAIN, 328
.equ TLS_CHAIN_PARA, 336
.equ TLS_SSL_EXTRA, 440
.equ TLS_POLICY_PARA, 472
.equ TLS_POLICY_STATUS, 496
.equ TLS_WIDE_NAME, 544
.equ TLS_WRITE_BUF, 1104
.equ TLS_SESSION_BYTES, 1120
.equ TLS_BUFFER_BYTES, 65536

.if TLS_HEADER + 20 > TLS_DEADLINE
    .error "Schannel stream sizes overlap the handshake deadline"
.endif
.if TLS_DEADLINE + 8 > TLS_IN_BUFS
    .error "Schannel handshake deadline overlaps input SecBuffers"
.endif
.if TLS_CONN_INFO + 28 > TLS_SCHANNEL_CRED
    .error "Schannel connection info overlaps credential data"
.endif
.if TLS_SCHANNEL_CRED + 80 > TLS_EXPIRY
    .error "Schannel credential structure overlaps expiry timestamp"
.endif
.if TLS_CHAIN_PARA + 96 > TLS_SSL_EXTRA
    .error "Schannel certificate chain parameters overlap SSL policy"
.endif
.if TLS_POLICY_STATUS + 24 > TLS_WIDE_NAME
    .error "Schannel SSL policy status overlaps hostname storage"
.endif
.if TLS_WIDE_NAME + 508 > TLS_OUT_BUFS
    .error "Schannel hostname storage overlaps output SecBuffers"
.endif
.if TLS_WRITE_BUF + 8 > TLS_SESSION_BYTES
    .error "Schannel session allocation is undersized"
.endif

.text
.Lwin_tls_socket:
    pushq %rbp
    movq %rsp, %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    pushq %rsi
    pushq %rdi
    subq $152, %rsp
    movl $0, 28(%r15)
    cmpl $14, %r12d
    je .Lwin_tls_release
    cmpl $11, %r12d
    jb .Lwin_tls_invalid
    cmpl $13, %r12d
    ja .Lwin_tls_invalid
    // Check guest buffer offset without overflowing the 32-bit arena.
    movl 20(%r15), %edx
    testl %edx, %edx
    jle .Lwin_tls_invalid
    cmpl $65536, %edx
    ja .Lwin_tls_invalid
    movl 16(%r15), %eax
    cmpl $VPP_ARENA_SIZE, %eax
    ja .Lwin_tls_invalid
    movl $VPP_ARENA_SIZE, %ecx
    subl %edx, %ecx
    cmpl %ecx, %eax
    ja .Lwin_tls_invalid
    leaq (%rbx,%rax), %rdi
    cmpl $11, %r12d
    je .Lwin_tls_upgrade
    movq 120(%r15), %r13
    testq %r13, %r13
    jz .Lwin_tls_invalid
    cmpl $1, 144(%r15)
    jne .Lwin_tls_invalid
    testl $4, TLS_FLAGS(%r13)
    jz .Lwin_tls_invalid
    movq TLS_SOCKET(%r13), %r14
    cmpl $12, %r12d
    je .Lwin_tls_write
    jmp .Lwin_tls_read

.Lwin_tls_upgrade:
    cmpl $253, %edx
    ja .Lwin_tls_invalid
    cmpq $0, 120(%r15)
    jne .Lwin_tls_invalid
    cmpl $0, 144(%r15)
    jne .Lwin_tls_invalid
    call .Lwin_socket_tim
    cmpq $-1, %rax
    je .Lwin_tls_invalid
    movq %rax, %r14
    movl $1, %ecx
    movl $TLS_SESSION_BYTES, %edx
    call calloc
    testq %rax, %rax
    jz .Lwin_tls_error
    movq %rax, %r13
    movq %r13, 120(%r15)
    movq %r14, TLS_SOCKET(%r13)
    movl $TLS_BUFFER_BYTES, %ecx
    call malloc
    movq %rax, TLS_CIPHER(%r13)
    testq %rax, %rax
    jz .Lwin_tls_error
    movl $TLS_BUFFER_BYTES, %ecx
    call malloc
    movq %rax, TLS_PLAIN(%r13)
    testq %rax, %rax
    jz .Lwin_tls_error
    // Keep pending decrypted bytes intact across an interleaved TLS send.
    movl $TLS_BUFFER_BYTES, %ecx
    call malloc
    movq %rax, TLS_WRITE_BUF(%r13)
    testq %rax, %rax
    jz .Lwin_tls_error
    // ASCII DNS/IP identity from the VM becomes NUL-terminated UTF-16.
    // Reject any direct machine call with invalid characters.
    movl 20(%r15), %ecx
    xorl %eax, %eax
.Lwin_tls_hostname_loop:
    movzbl (%rdi,%rax), %edx
    cmpl $33, %edx
    jb .Lwin_tls_invalid_session
    cmpl $126, %edx
    ja .Lwin_tls_invalid_session
    movw %dx, TLS_WIDE_NAME(%r13,%rax,2)
    incl %eax
    cmpl %ecx, %eax
    jb .Lwin_tls_hostname_loop
    movw $0, TLS_WIDE_NAME(%r13,%rax,2)

    // SCHANNEL_CRED v4. Automatic trust, no client credentials, strong crypto.
    movl $4, TLS_SCHANNEL_CRED(%r13)
    movl $0x00400030, TLS_SCHANNEL_CRED+72(%r13)
    xorl %ecx, %ecx
    leaq .Lwin_tls_schannel_name(%rip), %rdx
    movl $2, %r8d
    xorl %r9d, %r9d
    leaq TLS_SCHANNEL_CRED(%r13), %rax
    movq %rax, 32(%rsp)
    movq $0, 40(%rsp)
    movq $0, 48(%rsp)
    leaq TLS_CRED(%r13), %rax
    movq %rax, 56(%rsp)
    leaq TLS_EXPIRY(%r13), %rax
    movq %rax, 64(%rsp)
    call AcquireCredentialsHandleW
    testl %eax, %eax
    js .Lwin_tls_error
    orl $1, TLS_FLAGS(%r13)
    call GetTickCount64
    movl 12(%r15), %edx
    testl %edx, %edx
    jle .Lwin_tls_error
    cmpl $300000, %edx
    ja .Lwin_tls_error
    addq %rdx, %rax
    movq %rax, TLS_DEADLINE(%r13)
    // First InitializeSecurityContext has no old context or input tokens.
    movl $0, TLS_PENDING(%r13)
    jmp .Lwin_tls_first_step

.Lwin_tls_step:
    // Bound each blocking Winsock receive by the remaining handshake time.
    // A socket-wide SO_RCVTIMEO otherwise restarts at every TLS record.
    call .Lwin_tls_limit_timeout
    testl %eax, %eax
    jnz .Lwin_tls_error
    // Receive more handshake ciphertext as needed.
    movl TLS_PENDING(%r13), %eax
    cmpl $TLS_BUFFER_BYTES, %eax
    jae .Lwin_tls_error
    movq TLS_CIPHER(%r13), %rdx
    addq %rax, %rdx
    movl $TLS_BUFFER_BYTES, %r8d
    subl %eax, %r8d
    movq %r14, %rcx
    xorl %r9d, %r9d // recv(socket, buffer, remaining, flags=0)
    call recv
    testl %eax, %eax
    jle .Lwin_tls_error
    addl %eax, TLS_PENDING(%r13)
.Lwin_tls_retry_step:
    // Extra handshake bytes can keep ISC in a retry loop without another
    // recv(). Enforce the same absolute deadline on that path too.
    call GetTickCount64
    cmpq TLS_DEADLINE(%r13), %rax
    jae .Lwin_tls_error
    movl TLS_PENDING(%r13), %eax
    movl %eax, TLS_IN_BUFS(%r13)
    movl $2, TLS_IN_BUFS+4(%r13)  // SECBUFFER_TOKEN
    movq TLS_CIPHER(%r13), %rax
    movq %rax, TLS_IN_BUFS+8(%r13)
    movl $0, TLS_IN_BUFS+16(%r13)
    movl $0, TLS_IN_BUFS+20(%r13)
    movq $0, TLS_IN_BUFS+24(%r13)
    movl $0, TLS_IN_DESC(%r13)
    movl $2, TLS_IN_DESC+4(%r13)
    leaq TLS_IN_BUFS(%r13), %rax
    movq %rax, TLS_IN_DESC+8(%r13)
    leaq TLS_CTX(%r13), %rdx
    leaq TLS_IN_DESC(%r13), %rax
    movq %rax, 48(%rsp)
    jmp .Lwin_tls_call_isc
.Lwin_tls_first_step:
    xorl %edx, %edx
    movq $0, 48(%rsp)
.Lwin_tls_call_isc:
    // Separate input and output descriptors because SSPI rewrites buffer types.
    movl $0, TLS_OUT_BUFS(%r13)
    movl $2, TLS_OUT_BUFS+4(%r13)
    movq $0, TLS_OUT_BUFS+8(%r13)
    movl $0, TLS_OUT_BUFS+16(%r13)
    movl $17, TLS_OUT_BUFS+20(%r13) // SECBUFFER_ALERT
    movq $0, TLS_OUT_BUFS+24(%r13)
    movl $0, TLS_OUT_DESC(%r13)
    movl $2, TLS_OUT_DESC+4(%r13)
    leaq TLS_OUT_BUFS(%r13), %rax
    movq %rax, TLS_OUT_DESC+8(%r13)
    leaq TLS_CRED(%r13), %rcx
    leaq TLS_WIDE_NAME(%r13), %r8
    movl $0x811c, %r9d  // STREAM, CONFIDENTIALITY, replay/sequence, ALLOCATE
    movq $0, 32(%rsp)
    movq $0, 40(%rsp) // Schannel requires TargetDataRep=0.
    movq $0, 56(%rsp)
    leaq TLS_CTX(%r13), %rax
    movq %rax, 64(%rsp)
    leaq TLS_OUT_DESC(%r13), %rax
    movq %rax, 72(%rsp)
    leaq TLS_ATTR(%r13), %rax
    movq %rax, 80(%rsp)
    leaq TLS_EXPIRY(%r13), %rax
    movq %rax, 88(%rsp)
    call InitializeSecurityContextW
    movl %eax, -72(%rbp)
    // SEC_I_CONTINUE_NEEDED = 0x90312; SEC_E_INCOMPLETE_MESSAGE = 0x80090318.
    cmpl $0x80090318, %eax
    jne .Lwin_tls_check_status
    // Do not overwrite an SSPI-owned token on the next retry.
    cmpq $0, TLS_OUT_BUFS+8(%r13)
    jne .Lwin_tls_error
    cmpq $0, TLS_OUT_BUFS+24(%r13)
    jne .Lwin_tls_error
    jmp .Lwin_tls_step
.Lwin_tls_check_status:
    cmpl $0x90312, %eax
    je .Lwin_tls_isc_valid
    testl %eax, %eax
    jnz .Lwin_tls_error
.Lwin_tls_isc_valid:
    orl $2, TLS_FLAGS(%r13)
    // Preserve encrypted tail bytes returned as SECBUFFER_EXTRA.
    movl $0, -76(%rbp)
    cmpl $0, TLS_PENDING(%r13)
    jz .Lwin_tls_send_handshake
    cmpl $5, TLS_IN_BUFS+20(%r13)
    jne .Lwin_tls_no_extra
    movl TLS_IN_BUFS+16(%r13), %edx
    testl %edx, %edx
    jz .Lwin_tls_no_extra
    cmpl TLS_PENDING(%r13), %edx
    ja .Lwin_tls_error
    movl %edx, -76(%rbp)
    // For InitializeSecurityContext, SECBUFFER_EXTRA describes the tail
    // of the input token. Its pvBuffer can be NULL on Schannel.
    movq TLS_CIPHER(%r13), %rcx
    movl TLS_PENDING(%r13), %eax
    subl -76(%rbp), %eax
    leaq (%rcx,%rax), %rdx
    movl -76(%rbp), %r8d
    call memmove
.Lwin_tls_no_extra:
    movl -76(%rbp), %eax
    movl %eax, TLS_PENDING(%r13)
.Lwin_tls_send_handshake:
    call .Lwin_tls_send_token
    testl %eax, %eax
    jnz .Lwin_tls_error
    cmpl $0, -72(%rbp)
    je .Lwin_tls_handshake_complete
    // Process coalesced handshake records without waiting for more bytes.
    cmpl $0, TLS_PENDING(%r13)
    jne .Lwin_tls_retry_step
    jmp .Lwin_tls_step
.Lwin_tls_handshake_complete:
    // Restore the caller's socket timeout for established TLS traffic.
    call .Lwin_socket_dat_timeout
    testl %eax, %eax
    jnz .Lwin_tls_error
    movl TLS_ATTR(%r13), %eax
    andl $0x8010, %eax // Require stream encryption and confidentiality.
    cmpl $0x8010, %eax
    jne .Lwin_tls_error
    leaq TLS_CTX(%r13), %rcx
    movl $0x5a, %edx // SECPKG_ATTR_CONNECTION_INFO
    leaq TLS_CONN_INFO(%r13), %r8
    call QueryContextAttributesW
    testl %eax, %eax
    jnz .Lwin_tls_error
    movl TLS_CONN_INFO(%r13), %eax
    cmpl $0x800, %eax // SP_PROT_TLS1_2_CLIENT
    je .Lwin_tls_protocol_ok
    cmpl $0x2000, %eax // SP_PROT_TLS1_3_CLIENT
    jne .Lwin_tls_error
.Lwin_tls_protocol_ok:
    cmpl $128, TLS_CONN_INFO+8(%r13) // Minimum cipher strength.
    jb .Lwin_tls_error
    // Explicitly evaluate the peer's chain AND the requested DNS/IP name.
    leaq TLS_CTX(%r13), %rcx
    movl $0x53, %edx  // SECPKG_ATTR_REMOTE_CERT_CONTEXT
    leaq TLS_CERT(%r13), %r8
    call QueryContextAttributesW
    testl %eax, %eax
    jnz .Lwin_tls_error
    cmpq $0, TLS_CERT(%r13)
    je .Lwin_tls_error
    // CERT_CHAIN_PARA is 96 bytes on Win64. Its full layout must be
    // initialized and kept disjoint from SSL policy parameters.
    movl $96, TLS_CHAIN_PARA(%r13)
    // RequestedUsage (USAGE_MATCH_TYPE_AND): serverAuth only.
    movl $1, TLS_CHAIN_PARA+16(%r13)
    leaq .Lwin_tls_server_eku(%rip), %rax
    movq %rax, -88(%rbp)
    leaq -88(%rbp), %rax
    movq %rax, TLS_CHAIN_PARA+24(%r13)
    // Bound online revocation retrievals by the remaining TLS deadline.
    call GetTickCount64
    movq TLS_DEADLINE(%r13), %rdx
    cmpq %rdx, %rax
    jae .Lwin_tls_error
    subq %rax, %rdx
    cmpl $10000, %edx
    jbe 1f
    movl $10000, %edx
1:
    movl %edx, TLS_CHAIN_PARA+56(%r13) // dwUrlRetrievalTimeout
    xorl %ecx, %ecx
    movq TLS_CERT(%r13), %rdx
    xorl %r8d, %r8d
    movq 32(%rdx), %r9 // CERT_CONTEXT.hCertStore contains the peer chain.
    leaq TLS_CHAIN_PARA(%r13), %rax
    movq %rax, 32(%rsp)
    // Revocation excluding root, cumulative URL retrieval timeout. Disable
    // AIA downloads, whose separate issuer retrievals can exceed this budget.
    // Intermediates must be sent by the peer or already in the OS stores.
    movq $0x48002000, 40(%rsp)
    movq $0, 48(%rsp)
    leaq TLS_CHAIN(%r13), %rax
    movq %rax, 56(%rsp)
    call CertGetCertificateChain
    testl %eax, %eax
    jz .Lwin_tls_error
    movq TLS_CHAIN(%r13), %rax
    testq %rax, %rax
    je .Lwin_tls_error
    // Check the chain engine's trust result as well as the SSL hostname policy.
    cmpl $0, 4(%rax) // CERT_CHAIN_CONTEXT.TrustStatus.dwErrorStatus
    jne .Lwin_tls_error
    movl $24, TLS_SSL_EXTRA(%r13)
    movl $2, TLS_SSL_EXTRA+4(%r13) // AUTHTYPE_SERVER
    leaq TLS_WIDE_NAME(%r13), %rax
    movq %rax, TLS_SSL_EXTRA+16(%r13)
    movl $16, TLS_POLICY_PARA(%r13)
    leaq TLS_SSL_EXTRA(%r13), %rax
    movq %rax, TLS_POLICY_PARA+8(%r13)
    movl $24, TLS_POLICY_STATUS(%r13)
    movl $4, %ecx  // CERT_CHAIN_POLICY_SSL
    movq TLS_CHAIN(%r13), %rdx
    leaq TLS_POLICY_PARA(%r13), %r8
    leaq TLS_POLICY_STATUS(%r13), %r9
    call CertVerifyCertificateChainPolicy
    testl %eax, %eax
    jz .Lwin_tls_error
    cmpl $0, TLS_POLICY_STATUS+4(%r13)
    jne .Lwin_tls_error
    // Certificate and revocation checks also count against the upgrade deadline.
    call GetTickCount64
    cmpq TLS_DEADLINE(%r13), %rax
    jae .Lwin_tls_error
    leaq TLS_CTX(%r13), %rcx
    movl $4, %edx  // SECPKG_ATTR_STREAM_SIZES
    leaq TLS_HEADER(%r13), %r8
    call QueryContextAttributesW
    testl %eax, %eax
    jnz .Lwin_tls_error
    cmpl $256, TLS_HEADER(%r13)
    ja .Lwin_tls_error
    cmpl $256, TLS_TRAILER(%r13)
    ja .Lwin_tls_error
    cmpl $16384, TLS_MAX_MESSAGE(%r13)
    ja .Lwin_tls_error
    cmpl $1, TLS_MAX_MESSAGE(%r13)
    jb .Lwin_tls_error
    // Release temporary certificate handles before exposing TLS to the VM.
    call .Lwin_tls_free_certs
    orl $4, TLS_FLAGS(%r13)
    movl $1, 144(%r15)
    xorl %eax, %eax
    jmp .Lwin_tls_return

.Lwin_tls_write:
    // Copy at most one Schannel stream record and report partial plaintext I/O.
    movl 20(%r15), %esi
    cmpl TLS_MAX_MESSAGE(%r13), %esi
    jbe 1f
    movl TLS_MAX_MESSAGE(%r13), %esi
1:
    movl TLS_HEADER(%r13), %eax
    addl %esi, %eax
    addl TLS_TRAILER(%r13), %eax
    cmpl $TLS_BUFFER_BYTES, %eax
    ja .Lwin_tls_error
    movq TLS_WRITE_BUF(%r13), %rcx
    movl TLS_HEADER(%r13), %eax
    addq %rax, %rcx
    movq %rdi, %rdx
    movl %esi, %r8d
    call memcpy
    movl TLS_HEADER(%r13), %eax
    movl %eax, TLS_IN_BUFS(%r13)
    movl $7, TLS_IN_BUFS+4(%r13)  // STREAM_HEADER
    movq TLS_WRITE_BUF(%r13), %rax
    movq %rax, TLS_IN_BUFS+8(%r13)
    movl %esi, TLS_IN_BUFS+16(%r13)
    movl $1, TLS_IN_BUFS+20(%r13) // DATA
    movl TLS_HEADER(%r13), %eax
    addq TLS_WRITE_BUF(%r13), %rax
    movq %rax, TLS_IN_BUFS+24(%r13)
    movl TLS_TRAILER(%r13), %eax
    movl %eax, TLS_IN_BUFS+32(%r13)
    movl $6, TLS_IN_BUFS+36(%r13) // STREAM_TRAILER
    movq TLS_IN_BUFS+24(%r13), %rax
    addq %rsi, %rax
    movq %rax, TLS_IN_BUFS+40(%r13)
    movl $0, TLS_IN_BUFS+48(%r13)
    movl $0, TLS_IN_BUFS+52(%r13)
    movq $0, TLS_IN_BUFS+56(%r13)
    movl $0, TLS_IN_DESC(%r13)
    movl $4, TLS_IN_DESC+4(%r13)
    leaq TLS_IN_BUFS(%r13), %rax
    movq %rax, TLS_IN_DESC+8(%r13)
    leaq TLS_CTX(%r13), %rcx
    xorl %edx, %edx
    leaq TLS_IN_DESC(%r13), %r8
    xorl %r9d, %r9d
    call EncryptMessage
    testl %eax, %eax
    jnz .Lwin_tls_error
    movl TLS_IN_BUFS(%r13), %r8d
    addl TLS_IN_BUFS+16(%r13), %r8d
    jc .Lwin_tls_error
    addl TLS_IN_BUFS+32(%r13), %r8d
    jc .Lwin_tls_error
    cmpl $TLS_BUFFER_BYTES, %r8d
    ja .Lwin_tls_error
    // EncryptMessage may return smaller stream header/trailer lengths.
    // Send each returned SecBuffer at its own address, without assuming
    // the three buffers remain packed contiguously.
    movq %r14, %rcx
    movq TLS_IN_BUFS+8(%r13), %rdx
    movl TLS_IN_BUFS(%r13), %r8d
    call .Lwin_tls_send_all
    testl %eax, %eax
    jnz .Lwin_tls_error
    movq %r14, %rcx
    movq TLS_IN_BUFS+24(%r13), %rdx
    movl TLS_IN_BUFS+16(%r13), %r8d
    call .Lwin_tls_send_all
    testl %eax, %eax
    jnz .Lwin_tls_error
    movq %r14, %rcx
    movq TLS_IN_BUFS+40(%r13), %rdx
    movl TLS_IN_BUFS+32(%r13), %r8d
    call .Lwin_tls_send_all
    testl %eax, %eax
    jnz .Lwin_tls_error
    movl %esi, %eax
    jmp .Lwin_tls_return

.Lwin_tls_read:
    movl TLS_PLAIN_LEN(%r13), %eax
    testl %eax, %eax
    jnz .Lwin_tls_copy_plain
.Lwin_tls_decrypt:
    cmpl $0, TLS_PENDING(%r13)
    je .Lwin_tls_recv
    movl TLS_PENDING(%r13), %eax
    movl %eax, TLS_IN_BUFS(%r13)
    movl $1, TLS_IN_BUFS+4(%r13) // DATA ciphertext
    movq TLS_CIPHER(%r13), %rax
    movq %rax, TLS_IN_BUFS+8(%r13)
    movl $0, TLS_IN_BUFS+16(%r13)
    movl $0, TLS_IN_BUFS+20(%r13)
    movq $0, TLS_IN_BUFS+24(%r13)
    movl $0, TLS_IN_BUFS+32(%r13)
    movl $0, TLS_IN_BUFS+36(%r13)
    movq $0, TLS_IN_BUFS+40(%r13)
    movl $0, TLS_IN_BUFS+48(%r13)
    movl $0, TLS_IN_BUFS+52(%r13)
    movq $0, TLS_IN_BUFS+56(%r13)
    movl $0, TLS_IN_DESC(%r13)
    movl $4, TLS_IN_DESC+4(%r13)
    leaq TLS_IN_BUFS(%r13), %rax
    movq %rax, TLS_IN_DESC+8(%r13)
    leaq TLS_CTX(%r13), %rcx
    leaq TLS_IN_DESC(%r13), %rdx
    xorl %r8d, %r8d
    xorl %r9d, %r9d
    call DecryptMessage
    cmpl $0x80090318, %eax // INCOMPLETE_MESSAGE
    je .Lwin_tls_recv
    cmpl $0x90317, %eax // CONTEXT_EXPIRED / close_notify
    je .Lwin_tls_eof
    testl %eax, %eax
    jnz .Lwin_tls_error
    // DecryptMessage can place DATA and EXTRA in any SecBuffer slot.
    movl $0, -72(%rbp) // plaintext length
    movl $0, -76(%rbp) // extra length
    movq $0, -88(%rbp) // extra pointer, copied after plaintext
    xorl %esi, %esi
.Lwin_tls_scan_buffers:
    leaq TLS_IN_BUFS(%r13), %rdi
    leaq (%rdi,%rsi), %rdi
    cmpl $1, 4(%rdi)
    jne .Lwin_tls_check_extra
    movl (%rdi), %r8d
    cmpl $TLS_BUFFER_BYTES, %r8d
    ja .Lwin_tls_error
    testl %r8d, %r8d
    jz .Lwin_tls_check_extra
    // Schannel decrypts in place. Reject an invalid DATA slice before copy.
    movq 8(%rdi), %rdx
    movq TLS_CIPHER(%r13), %rax
    cmpq %rax, %rdx
    jb .Lwin_tls_error
    movq %rdx, %rcx
    subq %rax, %rcx
    movl TLS_PENDING(%r13), %eax
    cmpq %rax, %rcx
    ja .Lwin_tls_error
    subq %rcx, %rax
    cmpq %rax, %r8
    ja .Lwin_tls_error
    movl -72(%rbp), %eax
    addl %r8d, %eax
    jc .Lwin_tls_error
    cmpl $TLS_BUFFER_BYTES, %eax
    ja .Lwin_tls_error
    movl %eax, -72(%rbp)
    movq TLS_PLAIN(%r13), %rcx
    movl -72(%rbp), %eax
    subl %r8d, %eax
    addq %rax, %rcx
    call memcpy
.Lwin_tls_check_extra:
    leaq TLS_IN_BUFS(%r13), %rdi
    leaq (%rdi,%rsi), %rdi
    cmpl $5, 4(%rdi)
    jne .Lwin_tls_scan_next
    movl (%rdi), %r8d
    cmpl TLS_PENDING(%r13), %r8d
    ja .Lwin_tls_error
    movl %r8d, -76(%rbp)
    movq 8(%rdi), %rax
    movq %rax, -88(%rbp)
.Lwin_tls_scan_next:
    addq $16, %rsi
    cmpq $64, %rsi
    jb .Lwin_tls_scan_buffers
    movl -76(%rbp), %r8d
    testl %r8d, %r8d
    jz .Lwin_tls_no_extra_record
    movq TLS_CIPHER(%r13), %rcx
    movq -88(%rbp), %rdx
    testq %rdx, %rdx
    jnz .Lwin_tls_check_extra_record
    // Some providers report the length of a trailing EXTRA buffer without
    // setting pvBuffer. The unused ciphertext is the final cbBuffer bytes.
    movl TLS_PENDING(%r13), %eax
    subl -76(%rbp), %eax
    leaq (%rcx,%rax), %rdx
.Lwin_tls_check_extra_record:
    // An EXTRA pointer must refer to the original ciphertext allocation.
    cmpq %rcx, %rdx
    jb .Lwin_tls_error
    movq %rdx, %rax
    subq %rcx, %rax
    movl TLS_PENDING(%r13), %r9d
    subl -76(%rbp), %r9d
    cmpq %r9, %rax
    ja .Lwin_tls_error
    call memmove
.Lwin_tls_no_extra_record:
    movl -76(%rbp), %eax
    movl %eax, TLS_PENDING(%r13)
    movl -72(%rbp), %eax
    movl %eax, TLS_PLAIN_LEN(%r13)
    movl $0, TLS_PLAIN_POS(%r13)
    testl %eax, %eax
    jz .Lwin_tls_decrypt
.Lwin_tls_copy_plain:
    movl TLS_PLAIN_LEN(%r13), %r8d
    cmpl 20(%r15), %r8d
    jbe 1f
    movl 20(%r15), %r8d
1:
    movl %r8d, -72(%rbp)
    movq %rdi, %rcx // recover guest destination from control offset
    movl 16(%r15), %eax
    leaq (%rbx,%rax), %rcx
    movq TLS_PLAIN(%r13), %rdx
    movl TLS_PLAIN_POS(%r13), %eax
    addq %rax, %rdx
    call memcpy
    movl -72(%rbp), %eax
    addl %eax, TLS_PLAIN_POS(%r13)
    subl %eax, TLS_PLAIN_LEN(%r13)
    jmp .Lwin_tls_return
.Lwin_tls_recv:
    movl TLS_PENDING(%r13), %eax
    cmpl $TLS_BUFFER_BYTES, %eax
    jae .Lwin_tls_error
    movq TLS_CIPHER(%r13), %rdx
    addq %rax, %rdx
    movl $TLS_BUFFER_BYTES, %r8d
    subl %eax, %r8d
    movq %r14, %rcx
    xorl %r9d, %r9d // Winsock recv takes flags in r9, not on the stack.
    call recv
    testl %eax, %eax
    jle .Lwin_tls_error // Unexpected EOF without authenticated close_notify.
    addl %eax, TLS_PENDING(%r13)
    jmp .Lwin_tls_decrypt
.Lwin_tls_eof:
    xorl %eax, %eax
    jmp .Lwin_tls_return

.Lwin_tls_release:
    call .Lwin_tls_dispose
    xorl %eax, %eax
    jmp .Lwin_tls_return
.Lwin_tls_invalid_session:
.Lwin_tls_error:
    // A failed SSPI leg may still have initialized a context handle.
    movq 120(%r15), %r13
    testq %r13, %r13
    jz .Lwin_tls_error_dispose
    movq TLS_CTX(%r13), %rax
    orq TLS_CTX+8(%r13), %rax
    jz .Lwin_tls_error_dispose
    orl $2, TLS_FLAGS(%r13)
.Lwin_tls_error_dispose:
    call .Lwin_tls_dispose
    movl $5, 28(%r15)
    movl $-5, %eax
    jmp .Lwin_tls_return
.Lwin_tls_invalid:
    movl $22, 28(%r15)
    movl $-22, %eax
.Lwin_tls_return:
    leaq -56(%rbp), %rsp
    popq %rdi
    popq %rsi
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret

// Send each SSPI output token, then release its SSPI-owned buffer on every path.
.Lwin_tls_send_token:
    pushq %rbp
    movq %rsp, %rbp
    subq $48, %rsp
    movl $0, -4(%rbp)
    movq TLS_OUT_BUFS+8(%r13), %rdx
    testq %rdx, %rdx
    jnz .Lwin_tls_token_available
    cmpl $0, TLS_OUT_BUFS(%r13)
    je .Lwin_tls_token_done
    movl $-1, -4(%rbp)
    jmp .Lwin_tls_token_done
.Lwin_tls_token_available:
    movl TLS_OUT_BUFS(%r13), %r8d
    testl %r8d, %r8d
    jz .Lwin_tls_token_free
    movq %r14, %rcx
    call .Lwin_tls_send_all
    movl %eax, -4(%rbp)
.Lwin_tls_token_free:
    movq TLS_OUT_BUFS+8(%r13), %rcx
    call FreeContextBuffer
    movq $0, TLS_OUT_BUFS+8(%r13)
.Lwin_tls_token_done:
    movq TLS_OUT_BUFS+24(%r13), %rcx
    testq %rcx, %rcx
    jz .Lwin_tls_token_ret
    call FreeContextBuffer
    movq $0, TLS_OUT_BUFS+24(%r13)
.Lwin_tls_token_ret:
    movl -4(%rbp), %eax
    leave
    ret

// rcx = Winsock socket, rdx = buffer, r8d = byte count.
.Lwin_tls_send_all:
    pushq %rbp
    movq %rsp, %rbp
    pushq %rbx
    pushq %rsi
    pushq %rdi
    subq $40, %rsp
    movq %rcx, %rbx
    movq %rdx, %rsi
    movl %r8d, %edi
.Lwin_tls_send_next:
    testl %edi, %edi
    jz .Lwin_tls_send_ok
    testl $4, TLS_FLAGS(%r13)
    jnz .Lwin_tls_send_ready
    call .Lwin_tls_limit_timeout
    testl %eax, %eax
    jnz .Lwin_tls_send_failed
.Lwin_tls_send_ready:
    movq %rbx, %rcx
    movq %rsi, %rdx
    movl %edi, %r8d
    xorl %r9d, %r9d
    call send
    testl %eax, %eax
    jle .Lwin_tls_send_failed
    addq %rax, %rsi
    subl %eax, %edi
    jmp .Lwin_tls_send_next
.Lwin_tls_send_ok:
    xorl %eax, %eax
    jmp .Lwin_tls_send_done
.Lwin_tls_send_failed:
    movl $-1, %eax
.Lwin_tls_send_done:
    addq $40, %rsp
    popq %rdi
    popq %rsi
    popq %rbx
    popq %rbp
    ret

// Limit both Winsock blocking directions to the remaining handshake budget.
// Called before each recv/send while TLS_FLAGS lacks the verified bit.
// r13=session, r14=SOCKET, r15=control. Preserves Win64 nonvolatile registers.
.Lwin_tls_limit_timeout:
    pushq %rbp
    movq %rsp, %rbp
    subq $64, %rsp
    call GetTickCount64
    movq TLS_DEADLINE(%r13), %rdx
    cmpq %rdx, %rax
    jae .Lwin_tls_timeout_expired
    subq %rax, %rdx
    movl %edx, -4(%rbp) // 1..300000 ms, checked on upgrade.
    movq %r14, %rcx
    movl $65535, %edx // SOL_SOCKET
    movl $0x1006, %r8d // SO_RCVTIMEO
    leaq -4(%rbp), %r9
    movq $4, 32(%rsp)
    call setsockopt
    testl %eax, %eax
    jnz .Lwin_tls_timeout_expired
    movq %r14, %rcx
    movl $65535, %edx
    movl $0x1005, %r8d // SO_SNDTIMEO
    leaq -4(%rbp), %r9
    movq $4, 32(%rsp)
    call setsockopt
    testl %eax, %eax
    jnz .Lwin_tls_timeout_expired
    xorl %eax, %eax
    leave
    ret
.Lwin_tls_timeout_expired:
    movl $-1, %eax
    leave
    ret

// Free certificate chain and peer cert returned by Schannel.
.Lwin_tls_free_certs:
    pushq %rbp
    movq %rsp, %rbp
    subq $32, %rsp
    movq TLS_CHAIN(%r13), %rcx
    testq %rcx, %rcx
    jz 1f
    call CertFreeCertificateChain
    movq $0, TLS_CHAIN(%r13)
1:
    movq TLS_CERT(%r13), %rcx
    testq %rcx, %rcx
    jz 2f
    call CertFreeCertificateContext
    movq $0, TLS_CERT(%r13)
2:
    leave
    ret

.Lwin_tls_dispose:
    pushq %rbp
    movq %rsp, %rbp
    subq $48, %rsp
    movq 120(%r15), %r13
    movq $0, 120(%r15)
    movl $0, 144(%r15)
    testq %r13, %r13
    jz .Lwin_tls_dispose_done
    // Any error during InitializeSecurityContext may own an output token.
    movq TLS_OUT_BUFS+8(%r13), %rcx
    testq %rcx, %rcx
    jz 1f
    call FreeContextBuffer
    movq $0, TLS_OUT_BUFS+8(%r13)
1:
    movq TLS_OUT_BUFS+24(%r13), %rcx
    testq %rcx, %rcx
    jz 6f
    call FreeContextBuffer
    movq $0, TLS_OUT_BUFS+24(%r13)
6:
    call .Lwin_tls_free_certs
    testl $2, TLS_FLAGS(%r13)
    jz 2f
    leaq TLS_CTX(%r13), %rcx
    call DeleteSecurityContext
2:
    testl $1, TLS_FLAGS(%r13)
    jz 3f
    leaq TLS_CRED(%r13), %rcx
    call FreeCredentialsHandle
3:
    movq TLS_CIPHER(%r13), %rcx
    testq %rcx, %rcx
    jz 4f
    call free
4:
    movq TLS_PLAIN(%r13), %rcx
    testq %rcx, %rcx
    jz 5f
    call free
5:
    movq TLS_WRITE_BUF(%r13), %rcx
    testq %rcx, %rcx
    jz 7f
    call free
7:
    movq %r13, %rcx
    call free
.Lwin_tls_dispose_done:
    leave
    ret

.section .rdata,"dr"
.p2align 1
.Lwin_tls_schannel_name:
    .short 83, 99, 104, 97, 110, 110, 101, 108, 0
.Lwin_tls_server_eku:
    .asciz "1.3.6.1.5.5.7.3.1"
.text

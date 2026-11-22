	.arch armv8-a
	// Linux AArch64 TLS socket provider in ELF assembly, using OpenSSL's ABI.
	// Exports vpp_native_tls_socket(mode, control) and preserves VM socket layout.
	// Keep X.509 verification, hostname checks, absolute I/O deadlines and cleanup.
	.text
	.align	2
	.p2align 4,,11
	.type	await_socket, %function
await_socket:
	sub	sp, sp, #96
	adrp	x3, :got:__stack_chk_guard
	ldr	x3, [x3, :got_lo12:__stack_chk_guard]
	stp	x29, x30, [sp, 32]
	add	x29, sp, 32
	stp	x19, x20, [sp, 48]
	mov	x19, x2
	add	x20, sp, 8
	stp	x21, x22, [sp, 64]
	mov	w22, w1
	mov	x21, 2147483647
	stp	x23, x24, [sp, 80]
	mov	x24, 13531
	movk	x24, 0xd7b6, lsl 16
	mov	w23, w0
	movk	x24, 0xde82, lsl 32
	ldr	x0, [x3]
	str	x0, [sp, 24]
	mov	x0, 0
	movk	x24, 0x431b, lsl 48
	.p2align 3,,7
.L6:
	mov	x1, x20
	mov	w0, 1
	bl	clock_gettime
	cbnz	w0, .L7
	ldp	x2, x0, [sp, 8]
	smulh	x1, x0, x24
	lsl	x3, x2, 5
	sub	x3, x3, x2
	asr	x1, x1, 18
	add	x3, x2, x3, lsl 2
	sub	x0, x1, x0, asr 63
	add	x3, x0, x3, lsl 3
	cmp	x19, x3
	bls	.L7
	sub	x3, x19, x3
	mov	x0, x20
	cmp	x3, x21
	mov	x1, 1
	csel	x2, x3, x21, le
	str	w23, [sp, 8]
	strh	w22, [sp, 12]
	strh	wzr, [sp, 14]
	bl	poll
	cmp	w0, 0
	ble	.L5
	ldrsh	w0, [sp, 14]
	mov	w1, 40
	tst	w0, w1
	bne	.L7
	tst	w0, w22
	csetm	w0, eq
.L1:
	adrp	x1, :got:__stack_chk_guard
	ldr	x1, [x1, :got_lo12:__stack_chk_guard]
	ldr	x3, [sp, 24]
	ldr	x2, [x1]
	subs	x3, x3, x2
	mov	x2, 0
	bne	.L11
	ldp	x29, x30, [sp, 32]
	ldp	x19, x20, [sp, 48]
	ldp	x21, x22, [sp, 64]
	ldp	x23, x24, [sp, 80]
	add	sp, sp, 96
	ret
	.p2align 2,,3
.L5:
	beq	.L7
	bl	__errno_location
	ldr	w0, [x0]
	cmp	w0, 4
	beq	.L6
	.p2align 3,,7
.L7:
	mov	w0, -1
	b	.L1
.L11:
	bl	__stack_chk_fail
	.size	await_socket, .-await_socket
	.align	2
	.p2align 4,,11
	.global	vpp_native_tls_socket
	.type	vpp_native_tls_socket, %function
vpp_native_tls_socket:
	stp	x29, x30, [sp, -96]!
	adrp	x2, :got:__stack_chk_guard
	ldr	x2, [x2, :got_lo12:__stack_chk_guard]
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	mov	w20, w0
	stp	x25, x26, [sp, 64]
	sub	sp, sp, #720
	ldr	x0, [x2]
	str	x0, [sp, 712]
	mov	x0, 0
	cbz	x1, .L14
	mov	x19, x1
	cmp	w20, 14
	beq	.L103
	sub	w0, w20, #11
	cmp	w0, 2
	bhi	.L14
	str	x21, [sp, 752]
	str	x22, [sp, 760]
	bl	vpp_native_memory_base
	mov	x22, x0
	cbz	x0, .L100
	cmp	w20, 11
	bne	.L99
	ldr	x0, [x19, 120]
	cbnz	x0, .L100
	str	x27, [sp, 800]
	str	x28, [sp, 808]
	str	wzr, [x19, 144]
.L19:
	add	x21, sp, 72
	add	x27, sp, 200
	mov	x0, x21
	bl	sigemptyset
	mov	w1, 13
	mov	x0, x21
	bl	sigaddset
	mov	x2, x27
	mov	x1, x21
	mov	w0, 0
	bl	pthread_sigmask
	cbnz	w0, .L101
	add	x28, sp, 328
	mov	x0, x28
	bl	sigpending
	cbnz	w0, .L104
	str	x23, [sp, 768]
	str	x24, [sp, 776]
	ldp	w0, w2, [x19, 12]
	str	w0, [sp, 8]
	ldr	w24, [x19]
	ldr	w23, [x19, 20]
	cmp	w20, 11
	beq	.L105
	ldr	x25, [x19, 120]
	cbz	x25, .L53
	ldr	x0, [x25, 8]
	cbz	x0, .L53
	ldr	w0, [x19, 144]
	cmp	w0, 1
	bne	.L53
	sub	w3, w23, #1
	mov	w1, 65535
	cmp	w3, w1
	bls	.L106
.L53:
	mov	w26, -1
.L49:
	bl	__errno_location
	mov	x19, x0
	mov	w1, 13
	mov	x0, x28
	ldr	w22, [x19]
	bl	sigismember
	cbnz	w0, .L59
	add	x20, sp, 32
	stp	xzr, xzr, [sp, 32]
	b	.L61
	.p2align 2,,3
.L107:
	ldr	w0, [x19]
	cmp	w0, 4
	bne	.L59
.L61:
	mov	x2, x20
	mov	x0, x21
	mov	x1, 0
	bl	sigtimedwait
	tbnz	w0, #31, .L107
.L59:
	mov	x1, x27
	mov	x2, 0
	mov	w0, 2
	bl	pthread_sigmask
	ldr	x21, [sp, 752]
	ldr	x23, [sp, 768]
	ldr	x24, [sp, 776]
	ldr	x27, [sp, 800]
	ldr	x28, [sp, 808]
	str	w22, [x19]
	ldr	x22, [sp, 760]
.L12:
	adrp	x0, :got:__stack_chk_guard
	ldr	x0, [x0, :got_lo12:__stack_chk_guard]
	ldr	x2, [sp, 712]
	ldr	x1, [x0]
	subs	x2, x2, x1
	mov	x1, 0
	bne	.L108
	add	sp, sp, 720
	mov	w0, w26
	ldp	x19, x20, [sp, 16]
	ldp	x25, x26, [sp, 64]
	ldp	x29, x30, [sp], 96
	ret
	.p2align 2,,3
.L99:
	str	x27, [sp, 800]
	str	x28, [sp, 808]
	b	.L19
	.p2align 2,,3
.L105:
	uxtw	x0, w0
	sub	w1, w0, #1
	mov	w0, 37855
	movk	w0, 0x4, lsl 16
	cmp	w1, w0
	bhi	.L53
	sub	w20, w23, #1
	cmp	w20, 252
	bhi	.L53
	orr	w0, w2, w24
	tbnz	w0, #31, .L53
	mov	w0, 5120
	movk	w0, 0x810, lsl 16
	sub	w0, w0, w23
	cmp	w0, w2
	bcc	.L53
	add	x25, sp, 456
	add	x1, x22, w2, uxtw
	mov	x0, x25
	uxtw	x2, w23
	bl	memcpy
	uxtw	x2, w20
	mov	x1, 1
	add	x2, x2, 2
	strb	wzr, [x25, w23, sxtw]
	b	.L25
	.p2align 2,,3
.L110:
	add	x1, x1, 1
	cmp	x2, x1
	beq	.L109
.L25:
	add	x0, x25, x1
	ldrb	w0, [x0, -1]
	sub	w0, w0, #33
	and	w0, w0, 255
	cmp	w0, 93
	bls	.L110
	b	.L53
	.p2align 2,,3
.L103:
	ldr	x20, [x1, 120]
	cbz	x20, .L16
	ldr	x0, [x20, 8]
	bl	SSL_free
	ldr	x0, [x20]
	bl	SSL_CTX_free
	mov	x0, x20
	bl	free
.L16:
	mov	w26, 0
	str	xzr, [x19, 120]
	str	wzr, [x19, 144]
	b	.L12
	.p2align 2,,3
.L106:
	mov	w1, 5120
	movk	w1, 0x810, lsl 16
	sub	w1, w1, w23
	cmp	w1, w2
	bcc	.L53
	ldr	w19, [sp, 8]
	orr	w3, w2, w24
	str	w2, [sp, 12]
	cmp	w3, 0
	sub	w1, w19, #1
	mov	w3, 37855
	movk	w3, 0x4, lsl 16
	ccmp	w1, w3, 2, ge
	bhi	.L53
	add	x1, sp, 32
	bl	clock_gettime
	mov	w26, w0
	cbnz	w0, .L53
	ldp	x4, x1, [sp, 32]
	mov	x3, 13531
	movk	x3, 0xd7b6, lsl 16
	movk	x3, 0xde82, lsl 32
	movk	x3, 0x431b, lsl 48
	lsl	x0, x4, 5
	smulh	x3, x1, x3
	sub	x0, x0, x4
	asr	x3, x3, 18
	add	x0, x4, x0, lsl 2
	sub	x1, x3, x1, asr 63
	adds	x0, x1, x0, lsl 3
	bmi	.L53
	ldr	w2, [sp, 12]
	add	x19, x0, w19, uxtw
	add	x22, x22, w2, uxtw
	bl	ERR_clear_error
	ldr	x0, [x25, 8]
	mov	w2, w23
	mov	x1, x22
	cmp	w20, 12
	beq	.L111
	.p2align 3,,7
.L54:
	bl	SSL_read
	mov	w1, w0
	cmp	w0, 0
	bgt	.L65
	ldr	x0, [x25, 8]
	bl	SSL_get_error
	cmp	w0, 6
	beq	.L49
.L62:
	cmp	w0, 2
	beq	.L112
	cmp	w0, 3
	bne	.L53
	mov	x2, x19
	mov	w0, w24
	mov	w1, 4
	bl	await_socket
	cbnz	w0, .L53
.L102:
	bl	ERR_clear_error
	ldr	x0, [x25, 8]
	mov	w2, w23
	mov	x1, x22
	cmp	w20, 12
	bne	.L54
.L111:
	bl	SSL_write
	mov	w1, w0
	cmp	w0, 0
	bgt	.L65
	ldr	x0, [x25, 8]
	bl	SSL_get_error
	b	.L62
	.p2align 2,,3
.L109:
	mov	x1, 16
	mov	x0, 1
	bl	calloc
	mov	x20, x0
	cbz	x0, .L53
	bl	TLS_client_method
	bl	SSL_CTX_new
	str	x0, [x20]
	cbz	x0, .L26
	mov	x3, 0
	mov	x2, 771
	mov	w1, 123
	bl	SSL_CTX_ctrl
	mov	x22, x0
	cmp	x0, 1
	bne	.L26
	ldr	x0, [x20]
	bl	SSL_CTX_set_default_verify_paths
	cmp	w0, 1
	bne	.L26
	ldr	x0, [x20]
	mov	w1, w22
	mov	x2, 0
	bl	SSL_CTX_set_verify
	ldr	x0, [x20]
	bl	SSL_new
	str	x0, [x20, 8]
	mov	x1, x0
	cbz	x0, .L28
	bl	SSL_get0_param
	mov	x22, x0
	add	x2, sp, 24
	mov	x1, x25
	mov	w0, 2
	bl	inet_pton
	cmp	w0, 1
	beq	.L33
	add	x2, sp, 56
	mov	x1, x25
	mov	w0, 10
	bl	inet_pton
	cmp	w0, 1
	bne	.L113
.L33:
	mov	x1, x25
	mov	x0, x22
	bl	X509_VERIFY_PARAM_set1_ip_asc
	cmp	w0, 1
	bne	.L26
.L32:
	ldr	x0, [x20, 8]
	mov	w1, w24
	bl	SSL_set_fd
	mov	w22, w0
	cmp	w0, 1
	bne	.L26
	mov	w0, w24
	mov	w2, 0
	mov	w1, 3
	bl	fcntl
	tbnz	w0, #31, .L26
	orr	w2, w0, 2048
	mov	w1, 4
	mov	w0, w24
	bl	fcntl
	cbnz	w0, .L26
	mov	w0, w22
	add	x1, sp, 32
	bl	clock_gettime
	mov	w26, w0
	cbnz	w0, .L26
	ldp	x3, x1, [sp, 32]
	mov	x2, 13531
	movk	x2, 0xd7b6, lsl 16
	movk	x2, 0xde82, lsl 32
	movk	x2, 0x431b, lsl 48
	lsl	x0, x3, 5
	smulh	x2, x1, x2
	sub	x0, x0, x3
	asr	x2, x2, 18
	add	x0, x3, x0, lsl 2
	sub	x1, x2, x1, asr 63
	adds	x0, x1, x0, lsl 3
	bmi	.L26
	ldr	w1, [sp, 8]
	add	x22, x0, w1, uxtw
	.p2align 3,,7
.L45:
	bl	ERR_clear_error
	ldr	x0, [x20, 8]
	bl	SSL_connect
	mov	w1, w0
	cmp	w0, 1
	beq	.L41
	ldr	x0, [x20, 8]
	bl	SSL_get_error
	cmp	w0, 2
	beq	.L114
	cmp	w0, 3
	bne	.L26
	mov	x2, x22
	mov	w0, w24
	mov	w1, 4
	bl	await_socket
	cbz	w0, .L45
	.p2align 3,,7
.L26:
	ldr	x1, [x20, 8]
.L28:
	mov	x0, x1
	bl	SSL_free
	ldr	x0, [x20]
	bl	SSL_CTX_free
	mov	x0, x20
	bl	free
	b	.L53
	.p2align 2,,3
.L114:
	mov	x2, x22
	mov	w0, w24
	mov	w1, 1
	bl	await_socket
	cbz	w0, .L45
	b	.L26
	.p2align 2,,3
.L112:
	mov	x2, x19
	mov	w0, w24
	mov	w1, 1
	bl	await_socket
	cbz	w0, .L102
	b	.L53
	.p2align 2,,3
.L41:
	ldr	x0, [x20, 8]
	str	w1, [sp, 8]
	bl	SSL_get_verify_result
	ldr	w1, [sp, 8]
	cbnz	x0, .L26
	ldr	x0, [x20, 8]
	str	w1, [sp, 8]
	bl	SSL_get0_peer_certificate
	ldr	w1, [sp, 8]
	cbz	x0, .L26
	str	x20, [x19, 120]
	str	w1, [x19, 144]
	b	.L49
	.p2align 2,,3
.L113:
	ldr	x0, [x20, 8]
	mov	x1, x25
	bl	SSL_set1_host
	cmp	w0, 1
	bne	.L26
	ldr	x0, [x20, 8]
	mov	x3, x25
	mov	x2, 0
	mov	w1, 55
	bl	SSL_ctrl
	cmp	x0, 1
	beq	.L32
	b	.L26
	.p2align 2,,3
.L65:
	mov	w26, w1
	b	.L49
.L101:
	ldr	x21, [sp, 752]
	ldr	x22, [sp, 760]
	ldr	x27, [sp, 800]
	ldr	x28, [sp, 808]
.L14:
	mov	w26, -1
	b	.L12
	.p2align 2,,3
.L100:
	ldr	x21, [sp, 752]
	mov	w26, -1
	ldr	x22, [sp, 760]
	b	.L12
.L104:
	mov	x1, x27
	mov	x2, 0
	mov	w0, 2
	mov	w26, -1
	bl	pthread_sigmask
	ldr	x21, [sp, 752]
	ldr	x22, [sp, 760]
	ldr	x27, [sp, 800]
	ldr	x28, [sp, 808]
	b	.L12
.L108:
	str	x21, [sp, 752]
	str	x22, [sp, 760]
	str	x23, [sp, 768]
	str	x24, [sp, 776]
	str	x27, [sp, 800]
	str	x28, [sp, 808]
	bl	__stack_chk_fail
	.size	vpp_native_tls_socket, .-vpp_native_tls_socket
	.section	.note.GNU-stack,"",@progbits

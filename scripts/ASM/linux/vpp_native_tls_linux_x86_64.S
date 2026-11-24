	// Linux x86-64 TLS socket provider in ELF assembly, using OpenSSL's ABI.
	// Exports vpp_native_tls_socket(mode, control) and preserves VM socket layout.
	// Keep X.509 verification, hostname checks, absolute I/O deadlines and cleanup.
	.text
	.p2align 4
	.type	await_socket, @function
await_socket:
	pushq	%rbp
	movq	%rsp, %rbp
	pushq	%r15
	movl	%edi, %r15d
	pushq	%r14
	movabsq	$4835703278458516699, %r14
	pushq	%r13
	movl	%esi, %r13d
	pushq	%r12
	pushq	%rbx
	subq	$40, %rsp
	movq	%fs:40, %r12
	movq	%r12, -56(%rbp)
	movq	%rdx, %r12
	.p2align 4
	.p2align 3
.L6:
	leaq	-80(%rbp), %rsi
	movl	$1, %edi
	call	clock_gettime@PLT
	testl	%eax, %eax
	jne	.L7
	movq	-72(%rbp), %rcx
	imulq	$1000, -80(%rbp), %rsi
	movq	%rcx, %rax
	sarq	$63, %rcx
	imulq	%r14
	sarq	$18, %rdx
	subq	%rcx, %rdx
	leaq	(%rsi,%rdx), %rax
	cmpq	%r12, %rax
	jnb	.L7
	xorl	%edx, %edx
	movl	$1, %esi
	leaq	-80(%rbp), %rdi
	movl	%r15d, -80(%rbp)
	movw	%dx, -74(%rbp)
	movq	%r12, %rdx
	subq	%rax, %rdx
	movl	$2147483647, %eax
	movw	%r13w, -76(%rbp)
	cmpq	%rax, %rdx
	cmovg	%rax, %rdx
	call	poll@PLT
	testl	%eax, %eax
	jle	.L5
	movswl	-74(%rbp), %eax
	testb	$40, %al
	jne	.L7
	testl	%r13d, %eax
	sete	%al
	movzbl	%al, %eax
	negl	%eax
.L1:
	movq	-56(%rbp), %rdx
	subq	%fs:40, %rdx
	jne	.L14
	addq	$40, %rsp
	popq	%rbx
	popq	%r12
	popq	%r13
	popq	%r14
	popq	%r15
	popq	%rbp
	ret
	.p2align 4,,10
	.p2align 3
.L5:
	je	.L7
	call	__errno_location@PLT
	cmpl	$4, (%rax)
	je	.L6
	.p2align 4
	.p2align 3
.L7:
	movl	$-1, %eax
	jmp	.L1
.L14:
	call	__stack_chk_fail@PLT
	.size	await_socket, .-await_socket
	.p2align 4
	.globl	vpp_native_tls_socket
	.type	vpp_native_tls_socket, @function
vpp_native_tls_socket:
	endbr64
	pushq	%rbp
	movq	%rsp, %rbp
	pushq	%r15
	pushq	%r14
	pushq	%r13
	pushq	%r12
	pushq	%rbx
	subq	$760, %rsp
	movq	%fs:40, %rax
	movq	%rax, -56(%rbp)
	xorl	%eax, %eax
	testq	%rsi, %rsi
	je	.L17
	movl	%edi, %r13d
	movq	%rsi, %rbx
	cmpl	$14, %edi
	je	.L116
	leal	-11(%rdi), %eax
	cmpl	$2, %eax
	ja	.L17
	call	vpp_native_memory_base@PLT
	movq	%rax, -792(%rbp)
	testq	%rax, %rax
	je	.L17
	cmpl	$11, %r13d
	jne	.L22
	cmpq	$0, 120(%rbx)
	jne	.L17
	movl	$0, 144(%rbx)
.L22:
	leaq	-704(%rbp), %r12
	movq	%r12, %rdi
	call	sigemptyset@PLT
	movl	$13, %esi
	movq	%r12, %rdi
	call	sigaddset@PLT
	leaq	-576(%rbp), %rax
	xorl	%edi, %edi
	movq	%r12, %rsi
	movq	%rax, %rdx
	movq	%rax, -768(%rbp)
	call	pthread_sigmask@PLT
	testl	%eax, %eax
	jne	.L17
	leaq	-448(%rbp), %rax
	movq	%rax, %rdi
	movq	%rax, -776(%rbp)
	call	sigpending@PLT
	testl	%eax, %eax
	jne	.L117
	movl	12(%rbx), %eax
	movl	16(%rbx), %r9d
	movslq	20(%rbx), %r8
	movl	(%rbx), %r14d
	movl	%eax, -780(%rbp)
	cmpl	$11, %r13d
	je	.L118
	movq	120(%rbx), %rcx
	testq	%rcx, %rcx
	je	.L58
	cmpq	$0, 8(%rcx)
	je	.L58
	cmpl	$1, 144(%rbx)
	jne	.L58
	leal	-1(%r8), %eax
	cmpl	$65535, %eax
	jbe	.L119
.L58:
	movl	$-1, -760(%rbp)
.L54:
	call	__errno_location@PLT
	movq	-776(%rbp), %rdi
	movl	$13, %esi
	movl	(%rax), %r13d
	movq	%rax, %rbx
	call	sigismember@PLT
	testl	%eax, %eax
	jne	.L64
	pxor	%xmm0, %xmm0
	movaps	%xmm0, -736(%rbp)
	jmp	.L66
	.p2align 4,,10
	.p2align 3
.L120:
	cmpl	$4, (%rbx)
	jne	.L64
.L66:
	xorl	%esi, %esi
	leaq	-736(%rbp), %rdx
	movq	%r12, %rdi
	call	sigtimedwait@PLT
	testl	%eax, %eax
	js	.L120
.L64:
	movq	-768(%rbp), %rsi
	xorl	%edx, %edx
	movl	$2, %edi
	call	pthread_sigmask@PLT
	movl	%r13d, (%rbx)
.L15:
	movq	-56(%rbp), %rax
	subq	%fs:40, %rax
	jne	.L121
	movl	-760(%rbp), %eax
	addq	$760, %rsp
	popq	%rbx
	popq	%r12
	popq	%r13
	popq	%r14
	popq	%r15
	popq	%rbp
	ret
	.p2align 4,,10
	.p2align 3
.L118:
	subl	$1, %eax
	cmpl	$299999, %eax
	ja	.L58
	leal	-1(%r8), %eax
	cmpl	$252, %eax
	ja	.L58
	movl	%r14d, %ecx
	orl	%r9d, %ecx
	js	.L58
	movl	$135271424, %edx
	subl	%r8d, %edx
	cmpl	%r9d, %edx
	jb	.L58
	movq	-792(%rbp), %rcx
	leaq	-320(%rbp), %r15
	movq	%r15, %rdi
	leaq	(%rcx,%r9), %rsi
	cmpl	$8, %r8d
	jnb	.L122
.L28:
	xorl	%edx, %edx
	testb	$4, %r8b
	jne	.L123
	testb	$2, %r8b
	jne	.L124
.L30:
	testb	$1, %r8b
	jne	.L125
.L31:
	movb	$0, -320(%rbp,%r8)
	movq	%r15, %rdx
	leaq	-319(%rbp,%rax), %rcx
	.p2align 5
	.p2align 4
	.p2align 3
.L32:
	movzbl	(%rdx), %eax
	subl	$33, %eax
	cmpb	$93, %al
	ja	.L58
	addq	$1, %rdx
	cmpq	%rcx, %rdx
	jne	.L32
	movl	$16, %esi
	movl	$1, %edi
	call	calloc@PLT
	movq	%rax, %r13
	testq	%rax, %rax
	je	.L58
	call	TLS_client_method@PLT
	movq	%rax, %rdi
	call	SSL_CTX_new@PLT
	movq	%rax, 0(%r13)
	testq	%rax, %rax
	je	.L33
	xorl	%ecx, %ecx
	movl	$771, %edx
	movl	$123, %esi
	movq	%rax, %rdi
	call	SSL_CTX_ctrl@PLT
	cmpq	$1, %rax
	jne	.L33
	movq	0(%r13), %rdi
	call	SSL_CTX_set_default_verify_paths@PLT
	cmpl	$1, %eax
	jne	.L33
	movq	0(%r13), %rdi
	xorl	%edx, %edx
	movl	$1, %esi
	call	SSL_CTX_set_verify@PLT
	movq	0(%r13), %rdi
	call	SSL_new@PLT
	movq	%rax, 8(%r13)
	movq	%rax, %rdi
	testq	%rax, %rax
	je	.L35
	call	SSL_get0_param@PLT
	leaq	-740(%rbp), %rdx
	movq	%r15, %rsi
	movl	$2, %edi
	movq	%rax, -760(%rbp)
	call	inet_pton@PLT
	cmpl	$1, %eax
	je	.L39
	leaq	-720(%rbp), %rdx
	movq	%r15, %rsi
	movl	$10, %edi
	call	inet_pton@PLT
	cmpl	$1, %eax
	jne	.L126
.L39:
	movq	-760(%rbp), %rdi
	movq	%r15, %rsi
	call	X509_VERIFY_PARAM_set1_ip_asc@PLT
	cmpl	$1, %eax
	jne	.L33
.L38:
	movq	8(%r13), %rdi
	movl	%r14d, %esi
	call	SSL_set_fd@PLT
	cmpl	$1, %eax
	jne	.L33
	xorl	%edx, %edx
	xorl	%eax, %eax
	movl	$3, %esi
	movl	%r14d, %edi
	call	fcntl@PLT
	testl	%eax, %eax
	js	.L33
	orb	$8, %ah
	movl	$4, %esi
	movl	%r14d, %edi
	movl	%eax, %edx
	xorl	%eax, %eax
	call	fcntl@PLT
	testl	%eax, %eax
	jne	.L33
	leaq	-736(%rbp), %rsi
	movl	$1, %edi
	call	clock_gettime@PLT
	movl	%eax, -760(%rbp)
	testl	%eax, %eax
	jne	.L33
	movq	-728(%rbp), %rsi
	movabsq	$4835703278458516699, %rax
	imulq	$1000, -736(%rbp), %rcx
	imulq	%rsi
	sarq	$63, %rsi
	sarq	$18, %rdx
	subq	%rsi, %rdx
	addq	%rdx, %rcx
	js	.L33
	movl	-780(%rbp), %eax
	leaq	(%rax,%rcx), %r15
	jmp	.L51
	.p2align 4,,10
	.p2align 3
.L50:
	movq	%r15, %rdx
	movl	$4, %esi
	movl	%r14d, %edi
	call	await_socket
.L49:
	testl	%eax, %eax
	jne	.L33
.L51:
	call	ERR_clear_error@PLT
	movq	8(%r13), %rdi
	call	SSL_connect@PLT
	movq	8(%r13), %rdi
	cmpl	$1, %eax
	je	.L47
	movl	%eax, %esi
	call	SSL_get_error@PLT
	cmpl	$2, %eax
	je	.L127
	cmpl	$3, %eax
	je	.L50
.L33:
	movq	8(%r13), %rdi
.L35:
	call	SSL_free@PLT
	movq	0(%r13), %rdi
	call	SSL_CTX_free@PLT
	movq	%r13, %rdi
	call	free@PLT
	jmp	.L58
	.p2align 4,,10
	.p2align 3
.L116:
	movq	120(%rsi), %r12
	testq	%r12, %r12
	je	.L19
	movq	8(%r12), %rdi
	call	SSL_free@PLT
	movq	(%r12), %rdi
	call	SSL_CTX_free@PLT
	movq	%r12, %rdi
	call	free@PLT
.L19:
	movq	$0, 120(%rbx)
	movl	$0, 144(%rbx)
	movl	$0, -760(%rbp)
	jmp	.L15
	.p2align 4,,10
	.p2align 3
.L125:
	movzbl	(%rsi,%rdx), %ecx
	movb	%cl, (%rdi,%rdx)
	jmp	.L31
	.p2align 4,,10
	.p2align 3
.L123:
	movl	(%rsi), %edx
	movl	%edx, (%rdi)
	movl	$4, %edx
	testb	$2, %r8b
	je	.L30
.L124:
	movzwl	(%rsi,%rdx), %ecx
	movw	%cx, (%rdi,%rdx)
	addq	$2, %rdx
	testb	$1, %r8b
	je	.L31
	jmp	.L125
	.p2align 4,,10
	.p2align 3
.L122:
	movl	%r8d, %ecx
	shrl	$3, %ecx
	rep movsq
	jmp	.L28
	.p2align 4,,10
	.p2align 3
.L119:
	movl	$135271424, %eax
	subl	%r8d, %eax
	cmpl	%r9d, %eax
	jb	.L58
	movl	%r14d, %eax
	orl	%r9d, %eax
	js	.L58
	movl	-780(%rbp), %ebx
	leal	-1(%rbx), %eax
	cmpl	$299999, %eax
	ja	.L58
	leaq	-736(%rbp), %rsi
	movl	$1, %edi
	movl	%r9d, -780(%rbp)
	movl	%r8d, -784(%rbp)
	movq	%rcx, -800(%rbp)
	call	clock_gettime@PLT
	movl	%eax, -760(%rbp)
	testl	%eax, %eax
	jne	.L58
	movq	-728(%rbp), %rdi
	movabsq	$4835703278458516699, %rax
	imulq	$1000, -736(%rbp), %rsi
	imulq	%rdi
	sarq	$63, %rdi
	sarq	$18, %rdx
	subq	%rdi, %rdx
	addq	%rdx, %rsi
	js	.L58
	movl	-780(%rbp), %r9d
	movl	%ebx, %eax
	movq	-792(%rbp), %rbx
	movl	%r14d, -780(%rbp)
	movq	%r12, -792(%rbp)
	movl	-784(%rbp), %r14d
	leaq	(%rax,%rsi), %r15
	movq	-800(%rbp), %r12
	addq	%r9, %rbx
	jmp	.L63
	.p2align 4,,10
	.p2align 3
.L59:
	call	SSL_read@PLT
	testl	%eax, %eax
	jg	.L70
	movq	8(%r12), %rdi
	movl	%eax, %esi
	call	SSL_get_error@PLT
	cmpl	$6, %eax
	je	.L112
.L67:
	cmpl	$2, %eax
	je	.L128
	cmpl	$3, %eax
	jne	.L113
	movl	-780(%rbp), %edi
	movq	%r15, %rdx
	movl	$4, %esi
	call	await_socket
.L62:
	testl	%eax, %eax
	jne	.L113
.L63:
	call	ERR_clear_error@PLT
	movq	8(%r12), %rdi
	movl	%r14d, %edx
	movq	%rbx, %rsi
	cmpl	$12, %r13d
	jne	.L59
	call	SSL_write@PLT
	testl	%eax, %eax
	jg	.L70
	movq	8(%r12), %rdi
	movl	%eax, %esi
	call	SSL_get_error@PLT
	jmp	.L67
	.p2align 4,,10
	.p2align 3
.L127:
	movq	%r15, %rdx
	movl	$1, %esi
	movl	%r14d, %edi
	call	await_socket
	jmp	.L49
	.p2align 4,,10
	.p2align 3
.L113:
	movq	-792(%rbp), %r12
	jmp	.L58
	.p2align 4,,10
	.p2align 3
.L128:
	movl	-780(%rbp), %edi
	movq	%r15, %rdx
	movl	$1, %esi
	call	await_socket
	jmp	.L62
	.p2align 4,,10
	.p2align 3
.L47:
	call	SSL_get_verify_result@PLT
	movq	8(%r13), %rdi
	testq	%rax, %rax
	jne	.L35
	call	SSL_get0_peer_certificate@PLT
	testq	%rax, %rax
	je	.L33
	movq	%r13, 120(%rbx)
	movl	$1, 144(%rbx)
	jmp	.L54
	.p2align 4,,10
	.p2align 3
.L126:
	movq	8(%r13), %rdi
	movq	%r15, %rsi
	call	SSL_set1_host@PLT
	movq	8(%r13), %rdi
	cmpl	$1, %eax
	jne	.L35
	xorl	%edx, %edx
	movq	%r15, %rcx
	movl	$55, %esi
	call	SSL_ctrl@PLT
	cmpq	$1, %rax
	je	.L38
	jmp	.L33
.L117:
	movq	-768(%rbp), %rsi
	xorl	%edx, %edx
	movl	$2, %edi
	call	pthread_sigmask@PLT
	.p2align 4
	.p2align 3
.L17:
	movl	$-1, -760(%rbp)
	jmp	.L15
.L70:
	movl	%eax, -760(%rbp)
	movq	-792(%rbp), %r12
	jmp	.L54
.L112:
	movq	-792(%rbp), %r12
	jmp	.L54
.L121:
	call	__stack_chk_fail@PLT
	.size	vpp_native_tls_socket, .-vpp_native_tls_socket
	.section	.note.GNU-stack,"",@progbits
	.section	.note.gnu.property,"a"
	.align 8
	.long	1f - 0f
	.long	4f - 1f
	.long	5
0:
	.string	"GNU"
1:
	.align 8
	.long	0xc0000002
	.long	3f - 2f
2:
	.long	0x3
3:
	.align 8
4:

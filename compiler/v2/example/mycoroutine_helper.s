.intel_syntax noprefix
.code64

.section .text

.global ExitTask
ExitTask:
    mov rdi, [rsp]
    mov edx, 2
    shl rax, 32
    or rax, rdx                      # status = kFinished
    mov qword ptr [rdi + 0x40], 0  # env->task_ctx.rip = NULL
    jmp SuspendTask

.global RunCoro
RunCoro: # CoroStatus RunCoro(CoroEnv* env);
    mov rax, [rdi + 0x40]  # env->task_ctx.rip
    test rax, rax
    jz RunCoroError

    # run_ctx への保存
    mov rdx, [rsp]
    mov [rdi + 0x00], rdx  # env->run_ctx.rip = RunCoro's ret addr
    lea rdx, [rsp + 8]
    mov [rdi + 0x08], rdx  # env->run_ctx.rsp = rsp when RunCoro returned

    mov [rdi + 0x10], rbp  # env->run_ctx.rbp = rbp
    mov [rdi + 0x18], rbx
    mov [rdi + 0x20], r12
    mov [rdi + 0x28], r13
    mov [rdi + 0x30], r14
    mov [rdi + 0x38], r15

    # task_ctx からの復帰
    mov rsp, [rdi + 0x48]  # rsp = env->task_ctx.rsp
    mov rbp, [rdi + 0x50]
    mov rbx, [rdi + 0x58]
    mov r12, [rdi + 0x60]
    mov r13, [rdi + 0x68]
    mov r14, [rdi + 0x70]
    mov r15, [rdi + 0x78]

    mov rsi, r12
    jmp rax  # タスクへジャンプ

RunCoroError:
    # here eax == 0 => status = kNotInitialized
    ret

.global Yield
Yield: # void Yield(CoroEnv* env);
    # task_ctx への保存
    mov rax, [rsp]
    mov [rdi + 0x40], rax  # env->task_ctx.rip = Yeild's ret addr
    lea rax, [rsp + 8]
    mov [rdi + 0x48], rax  # env->task_ctx.rsp = rsp when Yeild returned

    mov [rdi + 0x50], rbp  # env->task_ctx.rbp = rbp
    mov [rdi + 0x58], rbx
    mov [rdi + 0x60], r12
    mov [rdi + 0x68], r13
    mov [rdi + 0x70], r14
    mov [rdi + 0x78], r15

    mov eax, 1             # status = kRunnable

SuspendTask:
    # run_ctx からの復帰
    mov rsp, [rdi + 0x08]  # rsp = env->run_ctx.rsp
    mov rbp, [rdi + 0x10]
    mov rbx, [rdi + 0x18]
    mov r12, [rdi + 0x20]
    mov r13, [rdi + 0x28]
    mov r14, [rdi + 0x30]
    mov r15, [rdi + 0x38]

    mov rcx, [rdi + 0x00]  # env->run_ctx.rip
    jmp rcx  # RunCoro の次の行までジャンプ

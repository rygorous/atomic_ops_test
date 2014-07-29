        bits 64
        section .text

%macro public_fn 1
        global %1
%1:
%endmacro

%macro perf_start 1
        public_fn %1
        mov r9, rcx ; save first arg into r9
        rdtsc
        shl rdx, 32
        or rdx, rax
        mov r8, rdx

        mov ecx, 10000
.lp:
%endmacro

%macro perf_end 0
        dec ecx
        jnz .lp

        rdtsc
        shl rdx, 32
        or rax, rdx
        sub rax, r8
        push rbx
        push rax
        cpuid
        pop rax
        pop rbx

        ret
%endmacro

%macro manual_xadd 2 ; addr, amount
%%try:
        mov rax, %1
        lea rdx, [rax+%2]
        lock cmpxchg %1, rdx
        jne %%try
%endmacro

; ---- test kernels

        ; test_add
        perf_start test_add

        add qword [r9], rax
        add qword [r9+8], rax
        add qword [r9+16], rax
        add qword [r9+24], rax

        perf_end

        ; test_add_mfence
        perf_start test_add_mfence

        add qword [r9], rax
        mfence
        add qword [r9+8], rax
        mfence
        add qword [r9+16], rax
        mfence
        add qword [r9+24], rax
        mfence

        perf_end

        ; test_lockadd
        perf_start test_lockadd

        lock add qword [r9], rax
        lock add qword [r9+8], rax
        lock add qword [r9+16], rax
        lock add qword [r9+24], rax

        perf_end

        ; test_xadd
        perf_start test_xadd

        lock xadd qword [r9], rax
        lock xadd qword [r9+8], rax
        lock xadd qword [r9+16], rax
        lock xadd qword [r9+24], rax

        perf_end

        ; test_cmpxchg
        perf_start test_cmpxchg

        manual_xadd [r9], rbx
        manual_xadd [r9+8], rbx
        manual_xadd [r9+16], rbx
        manual_xadd [r9+24], rbx

        perf_end

        ; test_swap
        perf_start test_swap

        xchg [r9], rax
        xchg [r9+8], rdx
        xchg [r9+16], rax
        xchg [r9+24], rdx

        perf_end

        ; test_lockadd_unalign
        perf_start test_lockadd_unalign

        lock add qword [r9+33], 1
        lock add qword [r9+41], 1
        lock add qword [r9+49], 1
        lock add qword [r9+57], 1

        perf_end

; ---- interference kernels

        public_fn interference_read

        mov edx, 10000000
.lp:
        mov rax, [rcx]
        mov rax, [rcx+8]
        mov rax, [rcx+16]
        mov rax, [rcx+24]
        dec edx
        jnz .lp

        ret

        public_fn interference_write

        mov edx, 10000000
.lp:
        ; NOTE: mix of reads and writes here;
        ; with pure writes it's easy to completely starve the cmpxchg variants.
        add rax, [rcx]
        mov [rcx], rdx
        add rax, [rcx+8]
        mov [rcx+8], rdx
        add rax, [rcx+16]
        mov [rcx+16], rdx
        add rax, [rcx+24]
        mov [rcx+24], rdx
        dec edx
        jnz .lp

        ret


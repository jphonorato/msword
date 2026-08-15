/*
 * System V `_setjmp` for the Winelib build.
 *
 * Opus reaches setjmp through Opus/lib/qsetjmp.h, whose OPUS_X64 branch uses
 * the host <setjmp.h>; glibc expands `setjmp(env)` to `_setjmp(env)`.  When
 * winegcc links the final binary that undefined `_setjmp` is satisfied by a
 * generated PE import thunk against Wine's msvcrt instead of by glibc, and
 * Wine's msvcrt `_setjmp` is Microsoft-x64: it takes the jmp_buf in RCX (and
 * the frame in RDX).  Every Opus caller is System V and passes the buffer in
 * RDI, so the imported routine ignored it and wrote its 256-byte
 * _JUMP_BUFFER over whatever stale pointer RCX happened to hold.
 *
 * That is the AV that blocked `opus_word1_about_test`: in LbcFormatPage
 * (Opus/wordtech/layout.c:290) RCX still held `*vhpllbs`, the `struct PL`
 * block HplInit had just returned nine lines earlier, so `SetLayoutAbort()`
 * splattered register state across that PL header.  `fExternal` picked up the
 * low half of RSP, and at teardown FreeHpl (Opus/wordtech/clsplc.c:465) took
 * the non-external PL for an external one and called FreeHq on the bogus HQ
 * read out of `rglbs[0]`, faulting in OpusFreeH.
 *
 * Defining `_setjmp` here leaves the symbol defined at link time, so winebuild
 * no longer generates the msvcrt import thunk and every caller reaches glibc's
 * System V implementation.  It has to be a tail jump: a C wrapper would leave
 * a stack frame behind that no longer exists by the time longjmp returns into
 * it.  glibc's own `_setjmp` is exactly `__sigsetjmp(env, 0)`.
 *
 * `longjmp` is pinned the same way, and for the same reason.  It is not enough
 * that the current link happens to resolve it to glibc: on this container 14
 * Wine import archives define `longjmp` (and `_setjmp`, and `_setjmpex`)
 * alongside msvcrt -- including `libntdll.a`, which every winelib target
 * links.  Feeding a glibc `jmp_buf` to Wine's Microsoft-x64 `longjmp` is the
 * same silent catastrophe in the other direction, so leaving that half to
 * link order would just re-arm the trap.  None of those archives define
 * `_longjmp`, which makes it a safe forwarding target; in glibc `longjmp`,
 * `_longjmp` and `siglongjmp` are all weak aliases of one `__libc_siglongjmp`
 * (verified: same address in `libc.so.6`), so the forward is exact.  Behaviour
 * stays data-driven through `env->__mask_was_saved`, which `__sigsetjmp(env,
 * 0)` leaves clear, so the sigprocmask restore is skipped as it should be.
 *
 * `src/cmake/AssertNoWineCrtSetjmp.cmake` runs after the link and fails the
 * build if any `__imp_` thunk for this family comes back, so a future
 * toolchain or link-order change surfaces as a build error instead of a wild
 * write during layout.
 */

#if defined(__GNUC__) && !defined(_MSC_VER) && defined(__x86_64__)

__asm__(
    "\t.text\n"
    "\t.globl _setjmp\n"
    "\t.type _setjmp, @function\n"
    "_setjmp:\n"
    "\t.cfi_startproc\n"
    "\txorl %esi, %esi\n"
    "\tjmp __sigsetjmp@PLT\n"
    "\t.cfi_endproc\n"
    "\t.size _setjmp, .-_setjmp\n"

    "\t.globl longjmp\n"
    "\t.type longjmp, @function\n"
    "longjmp:\n"
    "\t.cfi_startproc\n"
    "\tjmp _longjmp@PLT\n"
    "\t.cfi_endproc\n"
    "\t.size longjmp, .-longjmp\n");

#endif

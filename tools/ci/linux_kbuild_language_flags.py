"""C language ABI options from the frozen RV64 Linux 6.6.143 Kbuild.

Preprocessing a .c into a .i retains tokens, but does not retain compiler
invocation semantics.  The original Linux KBUILD_CFLAGS include both options;
replay must pass them to both MiniC arms or EFI's UTF-16 indirect calls will
be rejected against the target's normal 32-bit wchar_t default.

This is specific to the frozen RV64 corpus, not a substitute for recording
per-owner Kbuild options for other kernels or architectures.
"""

RV64_LINUX_KBUILD_LANGUAGE_FLAGS = ("-fshort-wchar", "-funsigned-char")

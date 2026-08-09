#pragma once

/*
 * Compatibility definitions used while compiling Microsoft's original
 * OpusEtAl/tools/src build-time tools (MKCMD, MKDLG, MERGEELX, BITAPP) as
 * native host executables with GCC instead of MSVC. These tools never link
 * against Wine or the Windows SDK; they are ordinary command-line programs
 * that run once, at build time, to regenerate the tables the original
 * Dialog Editor and resource compiler would have produced. This header
 * exists at that same boundary as port/original/opus_x64_compat.h, but for
 * the host-tool build rather than the WORD1 product build.
 */

#if defined(__GNUC__) && !defined(_MSC_VER)

#include <strings.h>

/* Both OpusEtAl/tools/src/mkcmd.c (mkcmd.c:13, "#define strcmpi _stricmp")
 * and OpusEtAl/tools/src/mkdlg.c (mkdlg.c:718, a direct call to strcmpi())
 * ultimately need Microsoft's case-insensitive string compare. MSVC's CRT
 * supplies _stricmp() and, for source compatibility, a deprecated strcmpi()
 * alias for it; GCC's CRT has neither name. POSIX strcasecmp() is the exact
 * equivalent, so this header supplies only the one primitive Microsoft's
 * CRT itself supplies, _stricmp. Each translation unit maps whichever
 * spelling it uses onto that primitive itself, the same way mkcmd.c already
 * does, instead of this shared header guessing which spelling every
 * caller wants. */
#ifndef _stricmp
#define _stricmp strcasecmp
#endif

#endif /* __GNUC__ && !_MSC_VER */

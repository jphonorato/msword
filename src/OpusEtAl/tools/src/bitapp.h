#define FALSE	0
#define TRUE	1
#define NULL	0

#define FIG_CURSOR	1
#define FIG_BITMAP	2
#define FIG_ICON	3

#define EVEN_MASK	0xaaaa
#define ODD_MASK	0x5555

#define WRONGPARAM	1
#define BADFILE		2
#define BADFIGURE	3
#define BADEOF		4
#define BADSWITCH	5

#ifdef OPUS_X64_TOOL
#define FAR
#else
#define FAR	far
#endif
#define NEAR
#define LONG	long
#define VOID	void

typedef unsigned char	BYTE;
typedef unsigned short	WORD;
#if defined(OPUS_X64_TOOL) && defined(__GNUC__) && !defined(_MSC_VER)
#include <stdint.h>
/* struct tagBITMAP (below) serializes bmBits as a 32-bit DWORD: the
 * Win16-era on-disk width of a far pointer, always ignored on read. MSVC's
 * "long" stays 32 bits even in its x64 LLP64 data model, so the plain
 * typedef below is exact when this tool is built with MSVC. GCC's x64 host
 * build instead uses the LP64 data model, where "long" is 64 bits; with
 * that width, fread(&bm, sizeof(BITMAP), ...) over-reads every serialized
 * bitmap resource by 4 bytes and desynchronizes the rest of the file,
 * eventually surfacing as "Unexpected End Of File Reached". Pin the on-disk
 * width explicitly for that one toolchain instead of following its native
 * "long". */
typedef uint32_t DWORD;
#else
typedef unsigned long  DWORD;
#endif
typedef int	  BOOL;
typedef char	 *PSTR;
typedef char NEAR *NPSTR;
typedef char FAR *LPSTR;
typedef int  FAR *LPINT;

#ifdef OPUS_X64_TOOL
#pragma pack(push, 2)
#endif

typedef struct tagBITMAP {
	short      bmType;
	short      bmWidth;
	short      bmHeight;
	short      bmWidthBytes;
	BYTE       bmPlanes;
	BYTE       bmBitsPixel;
#ifdef OPUS_X64_TOOL
	DWORD      bmBits;       /* serialized Win16 far pointer, always ignored */
#else
	LPSTR      bmBits;
#endif
} BITMAP;

typedef struct {
	short      xHotspot;
	short      yHotspot;
	short      cx;
	short      cy;
	short      WidthBytes;
	short      clr;
} RCI;

#ifdef OPUS_X64_TOOL
#pragma pack(pop)
#endif

/* formatting bin */

/* F K P   O N   D I S K */

/* An FKP is one 512-byte sector.  The rgfc array at the front used to be
	native FC values, so on this LP64 build each entry was 8 bytes, the
	page held fewer runs, and a .doc was not byte compatible with the
	MSVC x64 build (where long is 4).  On file every rgfc entry is now
	cbFcFkp = 4 bytes little endian, the same width as a packed FIB/PLC
	fc.  The cache page *is* the on-file image, so the struct overlay
	must not use native FC: FcFkp/PutFcFkp (filewin.c) are the only
	routines that know the packed layout.  cbFkp and ifcFkpNil follow
	the packed stride, never sizeof(FC). */
#define cbFcFkp     4
#define cbFkp       (512/*cbSector*/ - cbFcFkp - 1)
struct FKP
	{
	char    rgfc[cbFcFkp]; /* first packed FC; crun+1 entries of cbFcFkp */
	/*char  rgb[1];   crun entries based on FKP, points to chpx or papx*/
	char    rgb[cbFkp];
	char    crun;   /* number of runs */
	};
/* the overlay is the sector: no padding, last byte is crun */
typedef char cbFkpPageIsSector[(sizeof(struct FKP) == 512) ? 1 : -1];
typedef char cbFcFkpIsFour[(cbFcFkp == 4) ? 1 : -1];

#ifdef MAC
#define ifcFkpNil (-1)	/* native code depends on this */
#else
#define ifcFkpNil (512 /*cbSector*/ / cbFcFkp)
#endif

/* byte just past rgfc[crun], where the crun one-byte property offsets start */
#define HpchAfterRgfcFkp(hpfkp, crun) \
		(((char HUGE *)(hpfkp)) + ((int)(crun) + 1) * cbFcFkp)

FC      FcFkp();
void    PutFcFkp();

#define cbFkpPre35   (128/*cbSector*/ - cbFcFkp - 1)
struct FKPO
	{
	FC      rgfc[1]; /* crun + 1 entries from fcFirst to fcLim */
	/*char  rgb[1];   crun entries based on FKP, points to chpx or papx*/
	char    rgb[cbFkpPre35];
	char    crun;   /* number of runs */
	};

/* There are never more than three of these: 2 global for the scratch file
	(chp and pap), and one for the file we are currently writing. */
struct FKPD
	{ /* FKP Descriptor (used for maintaining insert properties) */
	int     bFreeFirst;   /* offset to next run to add */
	int     bFreeLim;       /* offset to byte after last unused byte */
	PN      pn;     /* pn of working FKP in scratch file */
	FC      fcFirst;
	int     fPlcIncomplete; 
	struct  CHP     chp;
	};

struct FKPDP
	{ /* subset for Paras */
	int     bFreeFirst;
	int     bFreeLim;
	PN      pn;
	FC      fcFirst;
	};

struct FKPDT
	{ /* subset for Text */
	int     bFreeFirst;     /* first unused byte on page pn if != pnNil */
	int     bFreeLim;       /* not used */
	PN      pn;             /* if != pnNil, pn of partially full page */
	FC      fcLim;          /* end of last written rgch. */
	};


#ifdef COMMENT
/* since the following "stuctures" are not word aligned in an FKP,
the definition here is just a comment */
/* Character properties encoded as a differential */
struct CHPX
	{
	char    cb;     /* Number of bytes stored in chp */
	struct CHP chp;
	};

/* Paragraph properties encoded as a differential */
struct PAPX
	{
	char    cb;     /* Number of bytes stored in rest of PAPX */
	char    stc;
	int     paph;
	struct PRL grpprl;
	};
#endif

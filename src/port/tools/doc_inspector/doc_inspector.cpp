/* doc_inspector -- structural reader for Word 1.x (Opus) .doc files.
 *
 * A host-native tool: plain C++20, no <windows.h>, no Wine, no GUI.  It is
 * built by port/tools/host/CMakeLists.txt alongside the other build-machine
 * generators, so it can be run on a .doc without a Wine prefix.
 *
 * What it reads, and where the layout comes from (all paths are src/):
 *
 *   - The FIB, as packed on disk: 105 little-endian 4-byte words, in the
 *     field order of Opus/filewin.c's CbBltFibPacked().  cwFibDisk/cbFibDisk
 *     and the iwFib* indices are in Opus/wordtech/file.h.  The FIB starts at
 *     the first byte of page pn (pn0 for the first document in the file), and
 *     pages are cbSector = 512 bytes.
 *
 *   - The CHPX and PAPX FKPs, one per 512-byte page, reached through the bin
 *     tables plcfbteChpx / plcfbtePapx.  Layout in Opus/wordtech/fkp.h, walked
 *     in Opus/wordtech/fetch.c (BFromFc) and written in
 *     Opus/wordtech/inssubs.c (C_FAddRun).
 *
 *   - Every PLC named by the FIB, using the on-file shape documented beside
 *     cbCpDisk in Opus/wordtech/file.h and implemented in create.c's
 *     HplcReadPlcf/ReadIntoExtPlc: ccp cp's of cbCpDisk = 4 bytes, followed by
 *     ccp-1 "foo" records of the PLC's own record size.
 *
 *   - The section table plcfsed, record by record: struct SED is packed to
 *     kCbSedDisk = 8 bytes by PackSed() in Opus/filewin.c, so its fn, its
 *     grpf bits and its fcSepx are checked against the file the SEPX they
 *     point at has to fit in.
 *
 *   - The table rows in the PAPX FKPs: every papx grpprl is walked the way
 *     CchPrl() walks it, and each sprmTDefTable record is decoded against
 *     the layout prm.h defines -- itcMac, the rgdxaCenter column positions
 *     and the rgtc prefix.  A grpprl that does not end exactly on its last
 *     byte is how a writer and a reader that disagree about a length field
 *     show up from outside the engine.
 *
 * The one subtlety that makes this tool more than a hexdump: the cp/fc arrays
 * of a PLC, every field of the FIB, every plcfsed record and (since PutFcFkp)
 * every FKP rgfc entry are 4-byte quantities on disk regardless of the host.
 * A .doc from before that last change holds native FC in its FKPs: 8 bytes
 * where long is 8.  The tool still detects that so it can name the file as
 * predating the pack (see ScoreFcWidth); --fc-width overrides.
 */

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{

/* ---------------------------------------------------------------- limits */

/* Opus/wordtech/file.h */
constexpr std::size_t kSector = 512;
constexpr int kCbCpDisk = 4;
constexpr int kCbFibWord = 4;
constexpr int kCwFibDisk = 105;
constexpr std::size_t kCbFibDisk = kCwFibDisk * kCbFibWord;
constexpr unsigned kMagicOpus = 0xA59B;
constexpr unsigned kMagicPmWord = 0xA59C;
constexpr int kNFibCurrent = 33;
constexpr int kNFibMinDoc = 18;
constexpr int kNFibBackCurrent = 25;

/* fcMax in file.h is 32K pages; nothing legitimate is bigger, and the cap
 * keeps a corrupt header from making us allocate the world. */
constexpr std::size_t kFileSizeMax = 32ul * 1024ul * kSector;

/* ------------------------------------------------------- the packed FIB */

/* Word indices into the packed FIB.  This list is CbBltFibPacked()'s walk in
 * Opus/filewin.c, in order; iwFibWIdent/iwFibNFib/iwFibGrpfFib/iwFibFcMin
 * agree with the same names in file.h. */
enum Iw
	{
	iwWIdent = 0,
	iwNFib,
	iwNProduct,
	iwNLocale,
	iwPnNext,
	iwGrpfFib,
	iwNFibBack,
	iwRgwSpare0,            /* 5 words */
	iwFcMin = 12,
	iwFcMac,
	iwCbMac,
	iwFcSpare0,
	iwFcSpare1,
	iwFcSpare2,
	iwFcSpare3,
	iwCcpText,
	iwCcpFtn,
	iwCcpHdd,
	iwCcpMcr,
	iwCcpAtn,
	iwCcpSpare0,
	iwCcpSpare1,
	iwCcpSpare2,
	iwCcpSpare3,
	iwFcStshfOrig,
	iwCbStshfOrig,
	iwFcStshf,
	iwCbStshf,
	iwFcPlcffndRef,
	iwCbPlcffndRef,
	iwFcPlcffndTxt,
	iwCbPlcffndTxt,
	iwFcPlcfandRef,
	iwCbPlcfandRef,
	iwFcPlcfandTxt,
	iwCbPlcfandTxt,
	iwFcPlcfsed,
	iwCbPlcfsed,
	iwFcPlcfpgd,
	iwCbPlcfpgd,
	iwFcPlcfphe,
	iwCbPlcfphe,
	iwFcSttbfglsy,
	iwCbSttbfglsy,
	iwFcPlcfglsy,
	iwCbPlcfglsy,
	iwFcPlcfhdd,
	iwCbPlcfhdd,
	iwFcPlcfbteChpx,
	iwCbPlcfbteChpx,
	iwFcPlcfbtePapx,
	iwCbPlcfbtePapx,
	iwFcPlcfsea,
	iwCbPlcfsea,
	iwFcSttbfffn,
	iwCbSttbfffn,
	iwFcPlcffldMom,
	iwCbPlcffldMom,
	iwFcPlcffldHdr,
	iwCbPlcffldHdr,
	iwFcPlcffldFtn,
	iwCbPlcffldFtn,
	iwFcPlcffldAtn,
	iwCbPlcffldAtn,
	iwFcPlcffldMcr,
	iwCbPlcffldMcr,
	iwFcSttbfbkmk,
	iwCbSttbfbkmk,
	iwFcPlcfbkf,
	iwCbPlcfbkf,
	iwFcPlcfbkl,
	iwCbPlcfbkl,
	iwFcCmds,
	iwCbCmds,
	iwFcPlcmcr,
	iwCbPlcmcr,
	iwFcSttbfmcr,
	iwCbSttbfmcr,
	iwFcPrEnv,
	iwCbPrEnv,
	iwFcWss,
	iwCbWss,
	iwFcDop,
	iwCbDop,
	iwFcSttbfAssoc,
	iwCbSttbfAssoc,
	iwFcClx,
	iwCbClx,
	iwFcPlcfpgdFtn,
	iwCbPlcfpgdFtn,
	iwFcSpare4,
	iwCbSpare4,
	iwFcSpare5,
	iwCbSpare5,
	iwFcSpare6,
	iwCbSpare6,
	iwWSpare4,
	iwPnChpFirst,
	iwPnPapFirst,
	iwCpnBteChp,
	iwCpnBtePap,
	iwMax
	};

static_assert(static_cast<int>(iwMax) == kCwFibDisk,
		"the packed FIB walk must match cwFibDisk in file.h");

/* the file status bits packed into word iwGrpfFib (file.h) */
constexpr std::uint32_t kFFibDot = 0x0001;
constexpr std::uint32_t kFFibGlsy = 0x0002;
constexpr std::uint32_t kFFibComplex = 0x0004;
constexpr std::uint32_t kFFibHasPic = 0x0008;
constexpr std::uint32_t kWFibQuickSaves = 0x00f0;
constexpr int kShftFibQuickSaves = 4;

/* ------------------------------------------------------------ reporting */

class Report
	{
public:
	void Problem(const std::string& text)
		{
		problems_.push_back(text);
		}

	void Note(const std::string& text)
		{
		notes_.push_back(text);
		}

	const std::vector<std::string>& Problems() const { return problems_; }
	const std::vector<std::string>& Notes() const { return notes_; }

private:
	std::vector<std::string> problems_;
	std::vector<std::string> notes_;
	};

std::string Hex(std::uint32_t value, int digits = 4)
	{
	std::ostringstream out;
	out << "0x" << std::hex << std::setfill('0') << std::setw(digits) << value;
	return out.str();
	}

void Field(const char *name, const std::string& value,
		const char *verdict = nullptr)
	{
	std::cout << "  " << std::left << std::setw(22) << name << ": " << value;
	if (verdict != nullptr)
		std::cout << "   [" << verdict << ']';
	std::cout << '\n';
	}

void Field(const char *name, long long value, const char *verdict = nullptr)
	{
	Field(name, std::to_string(value), verdict);
	}

void Section(const char *title)
	{
	std::cout << '\n' << title << '\n'
			<< std::string(std::strlen(title), '-') << '\n';
	}

/* ---------------------------------------------------------------- reader */

class Doc
	{
public:
	bool Load(const char *path, Report& report);

	std::size_t Size() const { return bytes_.size(); }
	const std::vector<unsigned char>& Bytes() const { return bytes_; }

	bool InRange(std::size_t offset, std::size_t count) const
		{
		return offset <= bytes_.size() && count <= bytes_.size() - offset;
		}

	/* One 4-byte little-endian on-disk word, sign extended the way
	 * LFromDisk() in Opus/filewin.c extends it. */
	std::int32_t DiskLong(std::size_t offset) const
		{
		const std::uint32_t raw =
				static_cast<std::uint32_t>(bytes_[offset]) |
				(static_cast<std::uint32_t>(bytes_[offset + 1]) << 8) |
				(static_cast<std::uint32_t>(bytes_[offset + 2]) << 16) |
				(static_cast<std::uint32_t>(bytes_[offset + 3]) << 24);
		return static_cast<std::int32_t>(raw);
		}

	/* An FC or a CP read at native width out of an FKP or a PLC foo run.
	 * fc_width is 4 or 8; both are little endian and signed. */
	std::int64_t NativeSigned(std::size_t offset, int fc_width) const
		{
		std::uint64_t raw = 0;
		for (int i = 0; i < fc_width; ++i)
			raw |= static_cast<std::uint64_t>(bytes_[offset + i]) << (8 * i);
		if (fc_width == 4)
			return static_cast<std::int32_t>(static_cast<std::uint32_t>(raw));
		return static_cast<std::int64_t>(raw);
		}

private:
	std::vector<unsigned char> bytes_;
	};

bool Doc::Load(const char *path, Report& report)
	{
	std::ifstream input(path, std::ios::binary);
	if (!input)
		{
		report.Problem(std::string("cannot open ") + path);
		return false;
		}
	input.seekg(0, std::ios::end);
	const std::streamoff length = input.tellg();
	if (length < 0)
		{
		report.Problem(std::string("cannot size ") + path);
		return false;
		}
	if (static_cast<std::size_t>(length) > kFileSizeMax)
		{
		report.Problem("file is larger than the fcMax bound of the format (" +
				std::to_string(static_cast<long long>(length)) + " bytes)");
		return false;
		}
	input.seekg(0, std::ios::beg);
	bytes_.assign(static_cast<std::size_t>(length), 0);
	if (length > 0 && !input.read(reinterpret_cast<char *>(bytes_.data()),
			length))
		{
		report.Problem(std::string("short read on ") + path);
		return false;
		}
	return true;
	}

/* --------------------------------------------------------------- the FIB */

struct Fib
	{
	std::int32_t w[kCwFibDisk] = {};
	std::size_t base = 0;           /* byte offset of the packed FIB */

	std::int32_t At(int iw) const { return w[iw]; }
	std::uint32_t Uns(int iw) const
		{
		return static_cast<std::uint32_t>(w[iw]);
		}
	};

bool ReadFib(const Doc& doc, std::size_t pn, Fib& fib, Report& report)
	{
	fib.base = pn * kSector;
	if (!doc.InRange(fib.base, kCbFibDisk))
		{
		report.Problem("the FIB at page " + std::to_string(pn) +
				" runs past the end of the file (need " +
				std::to_string(kCbFibDisk) + " bytes at offset " +
				std::to_string(fib.base) + ")");
		return false;
		}
	for (int iw = 0; iw < kCwFibDisk; ++iw)
		fib.w[iw] = doc.DiskLong(fib.base + iw * kCbFibWord);
	return true;
	}

/* --------------------------------------------- SED and TAP on-disk shapes */

/* Opus/wordtech/doc.h.  A plcfsed record is two 4-byte little-endian words
 * -- the grpf (fSpare, fUnk, fn), then fcSepx -- whatever sizeof(FC) is in
 * the build that wrote it, because savefast.c now packs it through
 * PackSed().  A .doc from before that change holds the native struct
 * instead: 8 bytes where FC is 4, and 16 where FC is 8, the middle four of
 * them alignment padding that nothing ever initialised. */
constexpr int kCbSedDisk = 8;
constexpr int kCbSedNativeFc8 = 16;
constexpr int kCbSedWord = 4;
constexpr std::uint32_t kFSedSpare = 0x0001;
constexpr std::uint32_t kFSedUnk = 0x0002;
constexpr std::uint32_t kWSedFn = 0xfffc;
constexpr int kShftSedFn = 2;
constexpr int kFnMax = 50;              /* Opus/wordtech/word.h */
constexpr int kCchSepxMax = 128;        /* Opus/wordtech/prm.h */

/* Opus/wordtech/prm.h.  sprmTDefTable is the only sprm whose length field is
 * wider than a byte, and the only way a TAP reaches a file: rgdxaCenter and
 * rgtc travel inside a PAPX as this one record.
 *
 *      +0                     sprmTDefTable
 *      +kIbTDefTableCb        cb, 4 bytes little endian
 *      +kIbTDefTableItcMac    itcMac, one byte
 *      +kIbTDefTableRgdxa     rgdxaCenter, (itcMac + 1) ints
 *                             rgtc, the differing prefix of the TC array
 *
 * cb is the whole record less two, which is what CchPrl() assumes when it
 * adds 2 back.  The kIb*Old values are the same layout with a 2-byte int;
 * the reader in prl.c used to use them against a writer that had already
 * moved to a 4-byte one, so a record written that way reads itcMac out of
 * the middle of its own length field.  The tool knows both so it can say
 * which shape it is looking at. */
constexpr int kSprmNoop = 0;
constexpr int kSprmPChgTabsPapx = 15;
constexpr int kSprmPChgTabs = 23;
constexpr int kSprmPFInTable = 24;
constexpr int kSprmPFTtp = 25;
constexpr int kSprmTFirst = 146;        /* sprmTJc */
constexpr int kSprmTDefTable = 152;
constexpr int kSprmTLast = 163;         /* sprmTSetBrc */
constexpr int kSprmMax = 164;
constexpr int kCbTDefTableCb = 4;
constexpr int kIbTDefTableCb = 1;
constexpr int kIbTDefTableItcMac = kIbTDefTableCb + kCbTDefTableCb;
constexpr int kIbTDefTableRgdxa = kIbTDefTableItcMac + 1;
constexpr int kIbTDefTableItcMacOld = 3;

constexpr int kItcMax = 32;             /* Opus/wordtech/props.h */
constexpr int kCbInt = 4;               /* sizeof(int) in every live build */
constexpr int kCbTc = 5 * kCbInt;       /* struct TC: grpf plus four brc's */
constexpr int kCbPhe = 3 * kCbInt;      /* struct PHE, Opus/wordtech/word.h */
constexpr int kCbFcFkp = 4;             /* Opus/wordtech/fkp.h cbFcFkp */

/* dnsprm[].cch from Opus/wordtech/prcsubs.c, WIN branch: the whole length of
 * the sprm in bytes, or 0 when the sprm carries its own length. */
const unsigned char kSprmCch[kSprmMax] =
	{
	1, 3, 2, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0,
	3, 3, 3, 3, 3, 3, 3, 0, 2, 2, 3, 3, 3, 2, 3, 3,
	3, 3, 3, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 2, 2, 2, 0, 0, 1, 0, 2, 2, 2, 2,
	2, 2, 2, 2, 3, 2, 4, 2, 0, 2, 2, 2, 2, 2, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 2, 2, 3, 3, 2, 2, 3, 3, 2, 2, 2,
	2, 3, 3, 3, 3, 2, 2, 3, 3, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 3, 3, 3, 0, 0, 0, 0, 3, 0, 0, 0, 0, 5, 3,
	5, 3, 3, 6,
	};

/* ------------------------------------------------------------ PLC tables */

/* One PLC named by the FIB.  foo4/foo8 are the record's size on disk when FC
 * is 4 and 8 bytes wide; every one of them is the constant the engine passes
 * to HplcReadPlcf() in Opus/create.c.  struct SED is packed to kCbSedDisk
 * and FKP rgfc to kCbFcFkp, so no live foo size depends on the width any
 * more -- an 8-byte FKP rgfc is how a file from before PutFcFkp looks. */
struct PlcSpec
	{
	const char *name;
	int iw_fc;
	int iw_cb;
	int foo4;
	int foo8;
	const char *record;     /* the foo record type, for the listing */
	bool key_is_fc;         /* the "cp" array actually holds fc's */
	};

const PlcSpec kPlcSpecs[] =
	{
	{"plcfsed",      iwFcPlcfsed,      iwCbPlcfsed,
			kCbSedDisk, kCbSedDisk, "SED",  false},
	{"plcfpgd",      iwFcPlcfpgd,      iwCbPlcfpgd,      20, 20, "PGD",  false},
	{"plcffldMom",   iwFcPlcffldMom,   iwCbPlcffldMom,    8,  8, "FLD",  false},
	{"plcffldHdr",   iwFcPlcffldHdr,   iwCbPlcffldHdr,    8,  8, "FLD",  false},
	{"plcffldFtn",   iwFcPlcffldFtn,   iwCbPlcffldFtn,    8,  8, "FLD",  false},
	{"plcffldAtn",   iwFcPlcffldAtn,   iwCbPlcffldAtn,    8,  8, "FLD",  false},
	{"plcffldMcr",   iwFcPlcffldMcr,   iwCbPlcffldMcr,    8,  8, "FLD",  false},
	{"plcfbteChpx",  iwFcPlcfbteChpx,  iwCbPlcfbteChpx,   4,  4, "BTE",  true},
	{"plcfbtePapx",  iwFcPlcfbtePapx,  iwCbPlcfbtePapx,   4,  4, "BTE",  true},
	{"plcfhdd",      iwFcPlcfhdd,      iwCbPlcfhdd,       0,  0, "-",    false},
	{"plcffndRef",   iwFcPlcffndRef,   iwCbPlcffndRef,    4,  4, "FRD",  false},
	{"plcffndTxt",   iwFcPlcffndTxt,   iwCbPlcffndTxt,    0,  0, "-",    false},
	{"plcfandRef",   iwFcPlcfandRef,   iwCbPlcfandRef,    6,  6, "ATRD", false},
	{"plcfandTxt",   iwFcPlcfandTxt,   iwCbPlcfandTxt,    0,  0, "-",    false},
	{"plcfphe",      iwFcPlcfphe,      iwCbPlcfphe,      12, 12, "PHE",  false},
	{"plcfglsy",     iwFcPlcfglsy,     iwCbPlcfglsy,      0,  0, "-",    false},
	{"plcfsea",      iwFcPlcfsea,      iwCbPlcfsea,       6,  6, "SEA",  false},
	{"plcfbkf",      iwFcPlcfbkf,      iwCbPlcfbkf,       4,  4, "BKF",  false},
	{"plcfbkl",      iwFcPlcfbkl,      iwCbPlcfbkl,       0,  0, "-",    false},
	{"plcmcr",       iwFcPlcmcr,       iwCbPlcmcr,        4,  4, "MCR",  false},
	{"plcfpgdFtn",   iwFcPlcfpgdFtn,   iwCbPlcfpgdFtn,   20, 20, "PGD",  false},
	};

/* Specs are looked up by name so the kPlcSpecs order stays a presentation
 * choice rather than something the rest of the tool depends on. */
const PlcSpec& SpecNamed(const char *wanted)
	{
	for (const PlcSpec& spec : kPlcSpecs)
		if (std::strcmp(spec.name, wanted) == 0)
			return spec;
	std::abort();
	}

const PlcSpec& BteSpec(bool is_papx)
	{
	return SpecNamed(is_papx ? "plcfbtePapx" : "plcfbteChpx");
	}

struct PlcView
	{
	bool present = false;           /* cb != 0 */
	bool aligned = false;           /* cb is a whole number of entries */
	std::int64_t fc = 0;
	std::int64_t cb = 0;
	int foo = 0;
	long long ccp = 0;              /* count of cp's */
	long long records = 0;          /* ccp - 1 */
	std::vector<std::int64_t> cps;
	std::size_t foo_base = 0;       /* byte offset of the foo run */
	bool readable = false;          /* the whole PLC is inside the file */
	};

/* Decompose a PLC exactly the way HplcReadPlcf()/ReadIntoExtPlc() do. */
PlcView ViewPlc(const Doc& doc, const Fib& fib, const PlcSpec& spec,
		int fc_width)
	{
	PlcView view;
	view.fc = fib.At(spec.iw_fc);
	view.cb = fib.Uns(spec.iw_cb);
	view.foo = (fc_width == 8) ? spec.foo8 : spec.foo4;
	if (view.cb == 0)
		return view;
	view.present = true;
	if (view.cb < kCbCpDisk)
		return view;

	const long long stride = view.foo + kCbCpDisk;
	view.records = (view.cb - kCbCpDisk) / stride;
	view.ccp = view.records + 1;
	view.aligned = (view.cb - kCbCpDisk) % stride == 0;

	if (view.fc < 0 || !doc.InRange(static_cast<std::size_t>(view.fc),
			static_cast<std::size_t>(view.cb)))
		return view;
	view.readable = true;
	view.cps.reserve(static_cast<std::size_t>(view.ccp));
	for (long long i = 0; i < view.ccp; ++i)
		view.cps.push_back(doc.DiskLong(
				static_cast<std::size_t>(view.fc) + i * kCbCpDisk));
	view.foo_base = static_cast<std::size_t>(view.fc) +
			static_cast<std::size_t>(view.ccp) * kCbCpDisk;
	return view;
	}

/* --------------------------------------------------------------- the FKPs */

/* One decoded run of an FKP page. */
struct FkpRun
	{
	std::int64_t fc_first = 0;
	std::int64_t fc_lim = 0;
	int b_word = 0;                 /* the stored offset, in 16-bit words */
	int offset = 0;                 /* b_word << 1, a byte offset, 0 = none */
	int cb_stored = 0;              /* the run's length byte as written */
	int cb_total = 0;               /* bytes the whole record occupies */
	bool ok = true;
	std::string why;
	};

struct FkpPage
	{
	std::size_t pn = 0;
	int crun = 0;
	bool ok = false;
	std::string why;
	std::vector<FkpRun> runs;
	std::int64_t fc_first = 0;
	std::int64_t fc_lim = 0;
	};

/* Decode one 512-byte FKP page.  is_papx selects the length convention: a
 * PAPX stores a count of 16-bit words in its first byte (nFib >= 25), a CHPX
 * stores a count of bytes -- Opus/wordtech/inssubs.c, fStoreCw = fPara. */
FkpPage ReadFkpPage(const Doc& doc, std::size_t pn, int fc_width,
		bool is_papx, std::int64_t fc_ceiling)
	{
	FkpPage page;
	page.pn = pn;
	const std::size_t base = pn * kSector;
	if (!doc.InRange(base, kSector))
		{
		page.why = "page runs past the end of the file";
		return page;
		}

	page.crun = doc.Bytes()[base + kSector - 1];
	/* bFreeFirst after crun runs, from C_FAddRun: crun * (cbFcFkp + 1) +
	 * cbFcFkp.  It must still leave room for one property byte. */
	const int run_table_end = page.crun * (fc_width + 1) + fc_width;
	const int crun_max =
			(static_cast<int>(kSector) - 1 - fc_width) / (fc_width + 1);
	if (page.crun < 1 || page.crun > crun_max)
		{
		page.why = "crun " + std::to_string(page.crun) +
				" outside 1.." + std::to_string(crun_max) +
				" for a " + std::to_string(fc_width) + "-byte FC";
		return page;
		}

	std::int64_t previous = 0;
	for (int irun = 0; irun <= page.crun; ++irun)
		{
		const std::int64_t fc =
				doc.NativeSigned(base + irun * fc_width, fc_width);
		if (irun == 0)
			page.fc_first = fc;
		else if (fc <= previous)
			{
			page.why = "rgfc is not strictly increasing at index " +
					std::to_string(irun);
			return page;
			}
		if (fc < 0 || (fc_ceiling > 0 && fc > fc_ceiling))
			{
			page.why = "rgfc[" + std::to_string(irun) + "] = " +
					std::to_string(fc) + " is outside 0.." +
					std::to_string(fc_ceiling);
			return page;
			}
		previous = fc;
		}
	page.fc_lim = previous;

	for (int irun = 0; irun < page.crun; ++irun)
		{
		FkpRun run;
		run.fc_first = doc.NativeSigned(base + irun * fc_width, fc_width);
		run.fc_lim = doc.NativeSigned(base + (irun + 1) * fc_width, fc_width);
		run.b_word = doc.Bytes()[base + (page.crun + 1) * fc_width + irun];
		run.offset = run.b_word << 1;
		if (run.offset == 0)
			{
			/* b == 0 is the "default properties" encoding, not a run. */
			page.runs.push_back(run);
			continue;
			}
		if (run.offset < run_table_end ||
				run.offset >= static_cast<int>(kSector) - 1)
			{
			run.ok = false;
			run.why = "property offset " + std::to_string(run.offset) +
					" is outside the free area " +
					std::to_string(run_table_end) + ".." +
					std::to_string(kSector - 2);
			page.runs.push_back(run);
			continue;
			}
		run.cb_stored = doc.Bytes()[base + run.offset];
		run.cb_total = 1 + (is_papx ? run.cb_stored * 2 : run.cb_stored);
		if (run.offset + run.cb_total > static_cast<int>(kSector) - 1)
			{
			run.ok = false;
			run.why = "property of " + std::to_string(run.cb_total) +
					" bytes at " + std::to_string(run.offset) +
					" overruns the page";
			}
		page.runs.push_back(run);
		}

	page.ok = true;
	return page;
	}

/* The page numbers of one property stream's FKPs.  The bin table normally
 * lists them all; when it is short, Opus/openrare.c's FFillMissingBtePns()
 * fills the tail sequentially from pnChpFirst/pnPapFirst, so we do too. */
std::vector<std::size_t> BinTablePns(const Doc& doc, const Fib& fib,
		const PlcView& bte, bool is_papx)
	{
	std::vector<std::size_t> pns;
	if (bte.readable)
		{
		for (long long i = 0; i < bte.records; ++i)
			pns.push_back(static_cast<std::size_t>(static_cast<std::uint32_t>(
					doc.DiskLong(bte.foo_base + i * bte.foo))));
		}

	const long long cpn = fib.Uns(is_papx ? iwCpnBtePap : iwCpnBteChp);
	if (fib.At(iwPnNext) != 0 || fib.Uns(iwGrpfFib) & kFFibComplex)
		return pns;             /* compound or fast-saved: no simple fill */
	const long long missing = cpn - static_cast<long long>(pns.size());
	if (missing <= 0)
		return pns;
	std::size_t next = (missing >= cpn)
			? fib.Uns(is_papx ? iwPnPapFirst : iwPnChpFirst)
			: pns.back() + 1;
	for (long long i = 0; i < missing; ++i)
		pns.push_back(next++);
	return pns;
	}

/* ------------------------------------------------------- grpprl walking */

/* One sprm found inside a grpprl. */
struct SprmRun
	{
	int ib = 0;                     /* offset from the start of the grpprl */
	int sprm = 0;
	int cb = 0;                     /* whole sprm, its own byte included */
	};

/* Walk cb bytes of grpprl at `base` exactly the way CchPrl()
 * (Opus/wordtech/prl.c) and the grpprl loop in prcsubs.c walk it.  Sets ok
 * false and fills why as soon as a sprm does not fit.  A walk that ends
 * anywhere but exactly on the last byte is what a writer and a reader that
 * disagree about a length field look like from outside the engine. */
std::vector<SprmRun> WalkGrpprl(const Doc& doc, std::size_t base, int cb,
		bool& ok, std::string& why)
	{
	std::vector<SprmRun> runs;
	ok = false;
	why.clear();
	if (cb < 0 || !doc.InRange(base, static_cast<std::size_t>(cb)))
		{
		why = "the grpprl runs past the end of the file";
		return runs;
		}
	const unsigned char *p = doc.Bytes().data() + base;
	int ib = 0;
	while (ib < cb)
		{
		SprmRun run;
		run.ib = ib;
		run.sprm = p[ib];
		if (run.sprm == kSprmNoop)
			{
			/* a single 0 pads a PAPX out to the word boundary its length
			 * byte counts in */
			++ib;
			continue;
			}
		if (run.sprm >= kSprmMax)
			{
			why = "byte " + std::to_string(run.sprm) + " at +" +
					std::to_string(ib) + " is not a sprm";
			return runs;
			}
		int cch = kSprmCch[run.sprm];
		if (cch == 0)
			{
			if (run.sprm == kSprmTDefTable)
				{
				if (ib + kIbTDefTableCb + kCbTDefTableCb > cb)
					{
					why = "sprmTDefTable at +" + std::to_string(ib) +
							" has no room for its length field";
					return runs;
					}
				cch = static_cast<int>(
						doc.DiskLong(base + ib + kIbTDefTableCb));
				}
			else
				{
				if (ib + 2 > cb)
					{
					why = "sprm " + std::to_string(run.sprm) + " at +" +
							std::to_string(ib) + " has no length byte";
					return runs;
					}
				cch = p[ib + 1];
				if (cch == 255 && run.sprm == kSprmPChgTabs)
					{
					/* the two-part tab list: a delete run, then an add run */
					if (ib + 3 > cb)
						{
						why = "sprmPChgTabs at +" + std::to_string(ib) +
								" is truncated";
						return runs;
						}
					cch = p[ib + 2] * 4 + 1;
					if (ib + 2 + cch >= cb)
						{
						why = "sprmPChgTabs at +" + std::to_string(ib) +
								" names more deleted tabs than it holds";
						return runs;
						}
					cch += p[ib + 2 + cch] * 3 + 1;
					}
				}
			cch += 2;
			}
		if (cch <= 0 || ib + cch > cb)
			{
			why = "sprm " + std::to_string(run.sprm) + " at +" +
					std::to_string(ib) + " claims " + std::to_string(cch) +
					" bytes with " + std::to_string(cb - ib) + " left";
			return runs;
			}
		run.cb = cch;
		runs.push_back(run);
		ib += cch;
		}
	ok = true;
	return runs;
	}

/* One sprmTDefTable record, decoded with the layout prm.h defines. */
struct TDefTable
	{
	int ib = 0;                     /* offset inside the grpprl */
	int cb_stored = 0;              /* the wide length field */
	int cb_total = 0;               /* cb_stored + 2, the whole record */
	int itc_mac = 0;
	int cb_rgtc = 0;
	std::vector<int> centers;
	bool ok = true;
	std::string why;
	bool old_layout = false;        /* reads as the 2-byte-int offsets */
	};

TDefTable ReadTDefTable(const Doc& doc, std::size_t base, const SprmRun& run)
	{
	TDefTable table;
	table.ib = run.ib;
	table.cb_stored =
			static_cast<int>(doc.DiskLong(base + run.ib + kIbTDefTableCb));
	table.cb_total = table.cb_stored + 2;
	table.itc_mac = doc.Bytes()[base + run.ib + kIbTDefTableItcMac];
	if (table.itc_mac < 1 || table.itc_mac > kItcMax)
		{
		table.ok = false;
		table.why = "itcMac " + std::to_string(table.itc_mac) + " at +" +
				std::to_string(kIbTDefTableItcMac) + " is outside 1.." +
				std::to_string(kItcMax);
		const int old_itc =
				doc.Bytes()[base + run.ib + kIbTDefTableItcMacOld];
		table.old_layout = old_itc >= 1 && old_itc <= kItcMax;
		return table;
		}
	table.cb_rgtc = table.cb_stored - (table.itc_mac + 2) * kCbInt;
	if (table.cb_rgtc < 0 || table.cb_rgtc > kItcMax * kCbTc)
		{
		table.ok = false;
		table.why = "the rgtc prefix works out to " +
				std::to_string(table.cb_rgtc) + " bytes, outside 0.." +
				std::to_string(kItcMax * kCbTc);
		return table;
		}
	if (kIbTDefTableRgdxa + kCbInt * (table.itc_mac + 1) > table.cb_total)
		{
		table.ok = false;
		table.why = "cb " + std::to_string(table.cb_stored) +
				" has no room for " + std::to_string(table.itc_mac + 1) +
				" column positions";
		return table;
		}
	for (int i = 0; i <= table.itc_mac; ++i)
		table.centers.push_back(static_cast<int>(doc.DiskLong(
				base + run.ib + kIbTDefTableRgdxa + i * kCbInt)));
	for (std::size_t i = 1; i < table.centers.size(); ++i)
		if (table.centers[i] <= table.centers[i - 1])
			{
			table.ok = false;
			table.why = "rgdxaCenter does not increase at column " +
					std::to_string(i) + " (" +
					std::to_string(table.centers[i - 1]) + " then " +
					std::to_string(table.centers[i]) + ')';
			break;
			}
	return table;
	}

/* -------------------------------------------------- FC width autodetection */

/* Score how well the file reads as one of the two FKP rgfc widths.  After
 * PutFcFkp the packed form is always 4 bytes; 8 only appears in files that
 * predate that change.  The PLC loop is kept so a foo type that grows a
 * width-dependent form again is picked up without anyone having to remember
 * this function. */
int ScoreFcWidth(const Doc& doc, const Fib& fib, int fc_width)
	{
	int score = 0;
	const std::int64_t cb_mac = fib.At(iwCbMac);

	for (const PlcSpec& spec : kPlcSpecs)
		{
		if (spec.foo4 == spec.foo8)
			continue;           /* width independent: proves nothing */
		const PlcView view = ViewPlc(doc, fib, spec, fc_width);
		if (!view.present)
			continue;
		score += view.aligned ? 4 : -4;
		}

	for (int pass = 0; pass < 2; ++pass)
		{
		const bool is_papx = pass == 1;
		const PlcSpec& bte_spec = BteSpec(is_papx);
		const PlcView bte = ViewPlc(doc, fib, bte_spec, fc_width);
		const std::vector<std::size_t> pns =
				BinTablePns(doc, fib, bte, is_papx);
		bool first = true;
		for (std::size_t pn : pns)
			{
			const FkpPage page =
					ReadFkpPage(doc, pn, fc_width, is_papx, cb_mac);
			if (!page.ok)
				{
				score -= 3;
				continue;
				}
			score += 3;
			if (first && page.fc_first == fib.At(iwFcMin))
				score += 3;
			for (const FkpRun& run : page.runs)
				score += run.ok ? 1 : -2;
			first = false;
			}
		}
	return score;
	}

/* ------------------------------------------------------------- the report */

std::string ProductVersion(std::uint32_t n_product)
	{
	/* the decode OpusEtAl/tools/src/dnatfile.c prints */
	std::ostringstream out;
	out << ((n_product >> 13) & 0x07) << '.' << std::setfill('0')
			<< std::setw(2) << ((n_product >> 7) & 0x3f) << " ("
			<< ((n_product >> 1) & 0x3f) << ((n_product & 1) ? "XX" : "00")
			<< ')';
	return out.str();
	}

void PrintFib(const Doc& doc, const Fib& fib, Report& report)
	{
	Section("FIB -- file information block");

	const std::uint32_t w_ident = fib.Uns(iwWIdent) & 0xffff;
	const char *ident_verdict = "OK";
	if (w_ident == kMagicOpus)
		Field("wIdent", Hex(w_ident) + "  (wMagic, Word for Windows / Opus)",
				ident_verdict);
	else if (w_ident == kMagicPmWord)
		Field("wIdent", Hex(w_ident) + "  (wMagicPmWord)", ident_verdict);
	else
		{
		Field("wIdent", Hex(w_ident) + "  (expected " + Hex(kMagicOpus) + ')',
				"BAD");
		report.Problem("wIdent is " + Hex(w_ident) + ", not wMagic " +
				Hex(kMagicOpus) + ": this is not a native Word 1.x document");
		}

	const std::int32_t n_fib = fib.At(iwNFib);
	const char *fib_verdict = "OK";
	if (n_fib < kNFibMinDoc)
		{
		fib_verdict = "TOO OLD";
		report.Problem("nFib " + std::to_string(n_fib) +
				" is below nFibMinDoc " + std::to_string(kNFibMinDoc));
		}
	else if (n_fib > kNFibCurrent)
		{
		fib_verdict = "NEWER THAN nFibCurrent";
		report.Note("nFib " + std::to_string(n_fib) + " is past nFibCurrent " +
				std::to_string(kNFibCurrent) + "; fields may be misread");
		}
	Field("nFib", std::to_string(n_fib) + "  (current " +
			std::to_string(kNFibCurrent) + ", minimum readable " +
			std::to_string(kNFibMinDoc) + ')', fib_verdict);

	const std::int32_t n_fib_back = fib.At(iwNFibBack);
	Field("nFibBack", std::to_string(n_fib_back) + "  (current " +
			std::to_string(kNFibBackCurrent) + ')',
			n_fib_back > kNFibCurrent ? "FUTURE FORMAT" : "OK");
	if (n_fib_back > kNFibCurrent)
		report.Problem("nFibBack " + std::to_string(n_fib_back) +
				" is past nFibCurrent: the engine would reject this file");

	Field("nProduct", Hex(fib.Uns(iwNProduct) & 0xffff) + "  [" +
			ProductVersion(fib.Uns(iwNProduct)) + ']');
	Field("nLocale", fib.At(iwNLocale));
	Field("pnNext", std::to_string(fib.At(iwPnNext)) +
			(fib.At(iwPnNext) != 0 ? "  (compound file: a second FIB follows)"
					: "  (no appended document)"));

	const std::uint32_t grpf = fib.Uns(iwGrpfFib);
	std::ostringstream flags;
	flags << "fDot=" << ((grpf & kFFibDot) != 0)
			<< " fGlsy=" << ((grpf & kFFibGlsy) != 0)
			<< " fComplex=" << ((grpf & kFFibComplex) != 0)
			<< " fHasPic=" << ((grpf & kFFibHasPic) != 0)
			<< " cQuickSaves="
			<< ((grpf & kWFibQuickSaves) >> kShftFibQuickSaves);
	Field("grpfFib", flags.str());

	Section("FIB -- extents");
	const std::int64_t fc_min = fib.At(iwFcMin);
	const std::int64_t fc_mac = fib.At(iwFcMac);
	const std::int64_t cb_mac = fib.At(iwCbMac);

	const bool fc_min_ok = fc_min >= static_cast<std::int64_t>(kCbFibDisk) &&
			static_cast<std::size_t>(fc_min) <= doc.Size();
	Field("fcMin", fc_min, fc_min_ok ? "OK" : "OUT OF RANGE");
	if (!fc_min_ok)
		report.Problem("fcMin " + std::to_string(fc_min) +
				" is not a plausible text start for a " +
				std::to_string(doc.Size()) + "-byte file");

	const bool fc_mac_ok = fc_mac >= fc_min && fc_mac <= cb_mac;
	Field("fcMac", fc_mac, fc_mac_ok ? "OK" : "OUT OF RANGE");
	if (!fc_mac_ok)
		report.Problem("fcMac " + std::to_string(fc_mac) +
				" is not inside fcMin.." + std::to_string(cb_mac));

	/* cbMac is the logical end of the file -- the last byte the engine wrote.
	 * Nothing truncates or sector-pads a saved document (there is no
	 * SetEndOfFile in Opus/filewin.c), so a clean save has cbMac exactly equal
	 * to the file length; a longer file is slack left by an earlier, larger
	 * save and is only worth a note. */
	const bool cb_mac_ok = cb_mac > 0 &&
			static_cast<std::size_t>(cb_mac) <= doc.Size();
	Field("cbMac", std::to_string(cb_mac) + "  (file is " +
			std::to_string(doc.Size()) + " bytes)",
			cb_mac_ok ? (static_cast<std::size_t>(cb_mac) == doc.Size()
					? "OK" : "SHORT OF THE FILE") : "PAST THE END OF FILE");
	if (!cb_mac_ok)
		report.Problem("cbMac " + std::to_string(cb_mac) +
				" is past the end of the " + std::to_string(doc.Size()) +
				"-byte file: the document is truncated");
	else if (static_cast<std::size_t>(cb_mac) != doc.Size())
		report.Note("the file is " +
				std::to_string(doc.Size() - static_cast<std::size_t>(cb_mac)) +
				" bytes longer than cbMac; that tail is slack from an earlier "
				"save and is not part of the document");

	std::ostringstream ccp;
	ccp << "text " << fib.At(iwCcpText) << ", ftn " << fib.At(iwCcpFtn)
			<< ", hdd " << fib.At(iwCcpHdd) << ", mcr " << fib.At(iwCcpMcr)
			<< ", atn " << fib.At(iwCcpAtn);
	Field("ccp*", ccp.str());

	const std::int64_t ccp_total = static_cast<std::int64_t>(fib.At(iwCcpText)) +
			fib.At(iwCcpFtn) + fib.At(iwCcpHdd) + fib.At(iwCcpMcr) +
			fib.At(iwCcpAtn);
	const bool simple = (grpf & kFFibComplex) == 0;
	if (simple)
		{
		/* In a non-complex file the text is exactly fcMin..fcMac. */
		const bool spans = (fc_mac - fc_min) == ccp_total;
		Field("fcMac - fcMin", std::to_string(fc_mac - fc_min) +
				"  vs sum of ccp* " + std::to_string(ccp_total),
				spans ? "OK" : "MISMATCH");
		if (!spans)
			report.Problem("a non-complex file must have fcMac - fcMin == the "
					"sum of the ccp's; got " + std::to_string(fc_mac - fc_min) +
					" vs " + std::to_string(ccp_total));
		}
	else
		report.Note("fComplex is set: the text lives in the piece table at "
				"fcClx, not in fcMin..fcMac");

	Field("pnChpFirst / cpnBteChp",
			std::to_string(fib.Uns(iwPnChpFirst)) + " / " +
			std::to_string(fib.Uns(iwCpnBteChp)));
	Field("pnPapFirst / cpnBtePap",
			std::to_string(fib.Uns(iwPnPapFirst)) + " / " +
			std::to_string(fib.Uns(iwCpnBtePap)));
	}

void PrintFibWordDump(const Fib& fib)
	{
	Section("FIB -- all 105 packed words");
	for (int iw = 0; iw < kCwFibDisk; ++iw)
		{
		std::cout << "  [" << std::setw(3) << iw << "] +"
				<< std::setw(4) << (iw * kCbFibWord) << "  "
				<< std::setw(12) << fib.At(iw) << "  "
				<< Hex(static_cast<std::uint32_t>(fib.At(iw)), 8) << '\n';
		}
	}

void PrintFkpStream(const Doc& doc, const Fib& fib, int fc_width,
		bool is_papx, int max_runs, Report& report)
	{
	const char *label = is_papx ? "FKP -- paragraph properties (PAPX)"
			: "FKP -- character properties (CHPX)";
	Section(label);

	Field("rgfc stride", std::to_string(fc_width) +
			" bytes  (cbFcFkp = " + std::to_string(kCbFcFkp) +
			", packed little endian)");
	if (fc_width != kCbFcFkp)
		report.Note(std::string(is_papx ? "PAPX" : "CHPX") +
				" FKP rgfc is " + std::to_string(fc_width) +
				"-byte: this file predates PutFcFkp(), and was written by a "
				"build whose FC was " + std::to_string(fc_width) +
				" bytes wide");

	const PlcSpec& bte_spec = BteSpec(is_papx);
	const PlcView bte = ViewPlc(doc, fib, bte_spec, fc_width);
	Field(bte_spec.name, "fc " + std::to_string(bte.fc) + ", cb " +
			std::to_string(bte.cb) + " -> " + std::to_string(bte.ccp) +
			" fc's / " + std::to_string(bte.records) + " bin entries",
			bte.present ? (bte.aligned ? "OK" : "MISALIGNED") : "EMPTY");
	if (bte.present && !bte.aligned)
		report.Problem(std::string(bte_spec.name) + " cb " +
				std::to_string(bte.cb) + " is not " + std::to_string(kCbCpDisk) +
				" + n * " + std::to_string(bte.foo + kCbCpDisk));
	if (bte.present && !bte.readable)
		report.Problem(std::string(bte_spec.name) + " at fc " +
				std::to_string(bte.fc) + " runs past the end of the file");

	const std::vector<std::size_t> pns = BinTablePns(doc, fib, bte, is_papx);
	if (pns.empty())
		{
		std::cout << "  no FKP pages\n";
		if (fib.Uns(is_papx ? iwCpnBtePap : iwCpnBteChp) != 0)
			report.Problem(std::string(is_papx ? "cpnBtePap" : "cpnBteChp") +
					" is non-zero but no FKP page could be located");
		return;
		}

	const long long cpn = fib.Uns(is_papx ? iwCpnBtePap : iwCpnBteChp);
	if (cpn != static_cast<long long>(pns.size()))
		report.Note(std::string(is_papx ? "cpnBtePap" : "cpnBteChp") + " is " +
				std::to_string(cpn) + " but " + std::to_string(pns.size()) +
				" FKP page(s) were located");

	std::int64_t expected_first = fib.At(iwFcMin);
	for (std::size_t index = 0; index < pns.size(); ++index)
		{
		const FkpPage page = ReadFkpPage(doc, pns[index], fc_width, is_papx,
				fib.At(iwCbMac));
		std::cout << "  page " << pns[index] << " (offset "
				<< pns[index] * kSector << ')';
		if (!page.ok)
			{
			std::cout << ": UNREADABLE -- " << page.why << '\n';
			report.Problem("FKP page " + std::to_string(pns[index]) + " (" +
					(is_papx ? "PAPX" : "CHPX") + "): " + page.why);
			continue;
			}
		std::cout << ": crun " << page.crun << ", fc " << page.fc_first
				<< ".." << page.fc_lim << '\n';

		if (page.fc_first != expected_first)
			{
			report.Problem("FKP page " + std::to_string(pns[index]) + " (" +
					(is_papx ? "PAPX" : "CHPX") + ") starts at fc " +
					std::to_string(page.fc_first) + ", but the previous page " +
					"(or fcMin) ends at " + std::to_string(expected_first));
			std::cout << "      GAP: expected this page to start at fc "
					<< expected_first << '\n';
			}
		expected_first = page.fc_lim;

		/* The bin table's own fc for this page must be the page's fcFirst;
		 * that is the invariant PutCpPlc() maintains in C_FAddRun(). */
		if (bte.readable && index < bte.cps.size() &&
				bte.cps[index] != page.fc_first)
			{
			report.Problem(std::string(bte_spec.name) + "[" +
					std::to_string(index) + "] is fc " +
					std::to_string(bte.cps[index]) + " but FKP page " +
					std::to_string(pns[index]) + " starts at fc " +
					std::to_string(page.fc_first));
			}

		int shown = 0;
		for (int irun = 0; irun < page.crun; ++irun)
			{
			const FkpRun& run = page.runs[static_cast<std::size_t>(irun)];
			if (!run.ok)
				report.Problem("FKP page " + std::to_string(pns[index]) +
						" run " + std::to_string(irun) + ": " + run.why);
			if (max_runs >= 0 && shown >= max_runs)
				continue;
			++shown;
			std::cout << "      [" << std::setw(3) << irun << "] fc "
					<< run.fc_first << ".." << run.fc_lim;
			if (run.offset == 0)
				std::cout << "  default properties";
			else
				std::cout << "  " << (is_papx ? "papx" : "chpx") << " @ "
						<< run.offset << " (word " << run.b_word << "), "
						<< run.cb_total << " bytes";
			if (!run.ok)
				std::cout << "  <<< " << run.why;
			std::cout << '\n';
			}
		if (max_runs >= 0 && page.crun > shown)
			std::cout << "      ... " << (page.crun - shown)
					<< " more run(s), use --runs=all\n";
		}

	/* The property stream has to cover exactly the document's text. */
	const std::int64_t fc_mac = fib.At(iwFcMac);
	const bool covered = expected_first == fc_mac;
	std::cout << "  coverage: fcMin " << fib.At(iwFcMin) << " .. "
			<< expected_first << " (fcMac " << fc_mac << ")   ["
			<< (covered ? "OK" : "SHORT") << "]\n";
	if (!covered)
		report.Problem(std::string(is_papx ? "PAPX" : "CHPX") +
				" FKPs cover up to fc " + std::to_string(expected_first) +
				", but fcMac is " + std::to_string(fc_mac));

	if (bte.readable && !bte.cps.empty() &&
			bte.cps.back() != expected_first && expected_first == fc_mac)
		report.Problem(std::string(bte_spec.name) + " ends at fc " +
				std::to_string(bte.cps.back()) + ", not at fcMac " +
				std::to_string(fc_mac));
	}

void PrintPlcTables(const Doc& doc, const Fib& fib, int fc_width,
		bool verbose, Report& report)
	{
	Section("PLC tables");
	std::cout << "  on disk a PLC is ccp cp's of " << kCbCpDisk
			<< " bytes followed by ccp-1 records of its own size\n\n";
	std::cout << "  " << std::left << std::setw(14) << "name"
			<< std::setw(11) << "fc" << std::setw(9) << "cb"
			<< std::setw(7) << "rec" << std::setw(7) << "size"
			<< std::setw(7) << "ccp" << "state\n";

	for (const PlcSpec& spec : kPlcSpecs)
		{
		const PlcView view = ViewPlc(doc, fib, spec, fc_width);
		std::string state;
		if (!view.present)
			state = "empty";
		else if (!view.aligned)
			state = "MISALIGNED";
		else if (!view.readable)
			state = "OUT OF FILE";
		else
			state = "ok";

		if (!view.present && !verbose)
			continue;

		std::cout << "  " << std::left << std::setw(14) << spec.name
				<< std::setw(11) << view.fc << std::setw(9) << view.cb
				<< std::setw(7) << spec.record << std::setw(7) << view.foo
				<< std::setw(7) << (view.present ? view.ccp : 0)
				<< state << '\n';

		if (!view.present)
			continue;
		if (!view.aligned)
			report.Problem(std::string(spec.name) + ": cb " +
					std::to_string(view.cb) + " is not " +
					std::to_string(kCbCpDisk) + " + n * " +
					std::to_string(view.foo + kCbCpDisk) +
					" -- the table does not divide into whole entries");
		if (!view.readable)
			{
			report.Problem(std::string(spec.name) + " at fc " +
					std::to_string(view.fc) + " length " +
					std::to_string(view.cb) + " runs past the " +
					std::to_string(doc.Size()) + "-byte file");
			continue;
			}

		/* cp arrays are non-decreasing by construction; an out-of-order or
		 * out-of-range entry is the classic sign of a truncated table. */
		const std::int64_t ceiling = spec.key_is_fc
				? fib.At(iwCbMac)
				: std::max<std::int64_t>(fib.At(iwCcpText), 0) +
						fib.At(iwCcpFtn) + fib.At(iwCcpHdd) +
						fib.At(iwCcpMcr) + fib.At(iwCcpAtn);
		for (std::size_t i = 0; i < view.cps.size(); ++i)
			{
			if (i > 0 && view.cps[i] < view.cps[i - 1])
				report.Problem(std::string(spec.name) + ": cp[" +
						std::to_string(i) + "] = " +
						std::to_string(view.cps[i]) + " goes backwards from " +
						std::to_string(view.cps[i - 1]));
			if (view.cps[i] < 0 || (ceiling > 0 && view.cps[i] > ceiling))
				report.Problem(std::string(spec.name) + ": cp[" +
						std::to_string(i) + "] = " +
						std::to_string(view.cps[i]) + " is outside 0.." +
						std::to_string(ceiling));
			}

		if (verbose)
			{
			std::cout << "      cps:";
			for (std::size_t i = 0; i < view.cps.size(); ++i)
				std::cout << ' ' << view.cps[i];
			std::cout << '\n';
			}
		}

	/* Asked-for table that the format does not have.  Word 1.x's FIB has no
	 * plcfed slot: the closest live tables are the five field PLCs
	 * (plcffldMom/Hdr/Ftn/Atn/Mcr, foo = struct FLD) listed above, and the
	 * EDL "display line" plc of Opus/wordtech/disp.h is an in-memory
	 * structure that is never written to a file. */
	std::cout << "\n  note: the Word 1.x FIB has no \"plcfed\" field; the "
			"field PLCs (plcffld*)\n        above are the nearest on-disk "
			"table, and the EDL plc is memory only.\n";
	}

/* --------------------------------------------------------- section table */

/* One plcfsed record, decoded the way UnpackSed() in Opus/filewin.c does. */
struct SedView
	{
	std::uint32_t grpf = 0;
	int fn = 0;
	bool f_spare = false;
	bool f_unk = false;
	std::int32_t fc_sepx = 0;
	};

SedView ReadSed(const Doc& doc, std::size_t offset)
	{
	SedView sed;
	sed.grpf = static_cast<std::uint32_t>(doc.DiskLong(offset));
	sed.f_spare = (sed.grpf & kFSedSpare) != 0;
	sed.f_unk = (sed.grpf & kFSedUnk) != 0;
	sed.fn = static_cast<int>((sed.grpf & kWSedFn) >> kShftSedFn);
	sed.fc_sepx = doc.DiskLong(offset + kCbSedWord);
	return sed;
	}

void PrintSectionTable(const Doc& doc, const Fib& fib, int fc_width,
		Report& report)
	{
	Section("plcfsed -- section table");

	const PlcSpec& spec = SpecNamed("plcfsed");
	const PlcView view = ViewPlc(doc, fib, spec, fc_width);
	Field("fcPlcfsed", view.fc);
	Field("cbPlcfsed", view.cb);
	Field("SED on disk", std::to_string(kCbSedDisk) +
			" bytes  (grpf word then fcSepx, both 4-byte little endian)");
	if (!view.present)
		{
		std::cout << "  empty: the document is a single default section\n";
		return;
		}
	Field("sections", std::to_string(view.records) + "  (" +
			std::to_string(view.ccp) + " cp's)",
			view.aligned ? "OK" : "MISALIGNED");

	if (!view.aligned)
		{
		/* the shape a build that blitted struct SED straight out leaves */
		const long long native = kCbSedNativeFc8 + kCbCpDisk;
		if ((view.cb - kCbCpDisk) % native == 0)
			report.Note("plcfsed divides evenly at a " +
					std::to_string(kCbSedNativeFc8) + "-byte SED: this file "
					"predates PackSed(), and was written by a build whose FC "
					"was 8 bytes wide");
		return;                 /* PrintPlcTables already filed the problem */
		}
	if (!view.readable)
		return;                 /* likewise */

	const std::int64_t cb_mac = fib.At(iwCbMac);
	std::cout << "\n  " << std::left << std::setw(6) << "ised"
			<< std::setw(11) << "cpFirst" << std::setw(11) << "cpLim"
			<< std::setw(6) << "fn" << std::setw(7) << "fUnk"
			<< std::setw(12) << "fcSepx" << "sepx\n";

	for (long long i = 0; i < view.records; ++i)
		{
		const std::size_t at = view.foo_base +
				static_cast<std::size_t>(i) * kCbSedDisk;
		const std::string where = "plcfsed[" + std::to_string(i) + "]";
		const SedView sed = ReadSed(doc, at);

		/* Nothing but the three declared fields lives in the grpf word.  A
		 * record still carrying the native struct would show the fcSepx of
		 * the previous entry, or a stack fragment out of the alignment
		 * padding, in the high half. */
		if ((sed.grpf & ~(kFSedSpare | kFSedUnk | kWSedFn)) != 0)
			report.Problem(where + ": the grpf word is " +
					Hex(sed.grpf, 8) + ", which has bits set outside "
					"fSpare/fUnk/fn");
		if (sed.fn > kFnMax)
			report.Problem(where + ": fn " + std::to_string(sed.fn) +
					" is past fnMax " + std::to_string(kFnMax));

		std::string sepx;
		if (sed.fc_sepx == -1)
			sepx = "fcNil, section uses the defaults";
		else if (sed.fc_sepx < 0 ||
				!doc.InRange(static_cast<std::size_t>(sed.fc_sepx), 1))
			{
			sepx = "OUT OF FILE";
			report.Problem(where + ": fcSepx " +
					std::to_string(sed.fc_sepx) + " is outside the " +
					std::to_string(doc.Size()) + "-byte file");
			}
		else
			{
			const int cch = doc.Bytes()[static_cast<std::size_t>(sed.fc_sepx)];
			sepx = std::to_string(cch) + "-byte grpprl";
			if (cch >= kCchSepxMax)
				{
				sepx += "  <<< over cchSepxMax";
				report.Problem(where + ": the sepx at fc " +
						std::to_string(sed.fc_sepx) + " claims " +
						std::to_string(cch) + " bytes, cchSepxMax is " +
						std::to_string(kCchSepxMax));
				}
			else if (!doc.InRange(static_cast<std::size_t>(sed.fc_sepx),
					static_cast<std::size_t>(cch) + 1))
				{
				sepx += "  <<< runs past the file";
				report.Problem(where + ": the sepx at fc " +
						std::to_string(sed.fc_sepx) + " runs past the end "
						"of the file");
				}
			else
				{
				bool ok = false;
				std::string why;
				WalkGrpprl(doc, static_cast<std::size_t>(sed.fc_sepx) + 1,
						cch, ok, why);
				if (!ok)
					{
					sepx += "  <<< " + why;
					report.Problem(where + ": the sepx at fc " +
							std::to_string(sed.fc_sepx) +
							" does not walk -- " + why);
					}
				if (cb_mac > 0 && sed.fc_sepx + 1 + cch > cb_mac)
					report.Problem(where + ": the sepx at fc " +
							std::to_string(sed.fc_sepx) + " ends past "
							"cbMac " + std::to_string(cb_mac));
				}
			}

		std::cout << "  " << std::left << std::setw(6) << i
				<< std::setw(11) << view.cps[static_cast<std::size_t>(i)]
				<< std::setw(11) << view.cps[static_cast<std::size_t>(i) + 1]
				<< std::setw(6) << sed.fn
				<< std::setw(7) << (sed.f_unk ? "yes" : "no")
				<< std::setw(12) << sed.fc_sepx << sepx << '\n';
		}
	}

/* --------------------------------------------------- table row properties */

/* Walk the PAPX FKPs looking for table rows: a papx whose grpprl carries
 * sprmPFInTable/sprmPFTtp or any sgcTap sprm, and above all sprmTDefTable,
 * which is the only place a TAP's column geometry reaches a file. */
void PrintTableFkps(const Doc& doc, const Fib& fib, int fc_width,
		int max_rows, Report& report)
	{
	Section("FKP -- table row properties (sgcTap)");

	const PlcSpec& bte_spec = BteSpec(true /* PAPX */);
	const PlcView bte = ViewPlc(doc, fib, bte_spec, fc_width);
	const std::vector<std::size_t> pns = BinTablePns(doc, fib, bte, true);

	long long papx_walked = 0;
	long long papx_failed = 0;
	long long cells = 0;
	long long rows = 0;
	long long defs = 0;
	int shown = 0;

	for (std::size_t pn : pns)
		{
		const FkpPage page =
				ReadFkpPage(doc, pn, fc_width, true, fib.At(iwCbMac));
		if (!page.ok)
			continue;           /* PrintFkpStream already filed the problem */
		const std::size_t base = pn * kSector;

		for (int irun = 0; irun < page.crun; ++irun)
			{
			const FkpRun& run = page.runs[static_cast<std::size_t>(irun)];
			if (run.offset == 0 || !run.ok)
				continue;

			const std::string where = "PAPX FKP page " + std::to_string(pn) +
					" run " + std::to_string(irun) + " (fc " +
					std::to_string(run.fc_first) + ".." +
					std::to_string(run.fc_lim) + ')';

			/* a papx is [cb][stc][PHE][grpprl], and for nFib >= 25 cb counts
			 * 16-bit words -- Opus/create.c's ApplyPapxToPap */
			const int cb_grpprl = run.cb_stored * 2 - 1 - kCbPhe;
			if (cb_grpprl < 0)
				{
				report.Problem(where + ": the papx is " +
						std::to_string(run.cb_stored * 2) + " bytes, too "
						"short for a stc and a PHE");
				++papx_failed;
				continue;
				}
			const std::size_t grpprl =
					base + run.offset + 2 + static_cast<std::size_t>(kCbPhe);

			bool ok = false;
			std::string why;
			const std::vector<SprmRun> sprms =
					WalkGrpprl(doc, grpprl, cb_grpprl, ok, why);
			++papx_walked;
			if (!ok)
				{
				++papx_failed;
				report.Problem(where + ": the grpprl does not walk -- " + why);
				continue;
				}

			bool f_in_table = false;
			bool f_ttp = false;
			int c_tap = 0;
			std::vector<TDefTable> tables;
			for (const SprmRun& sprm : sprms)
				{
				if (sprm.sprm == kSprmPFInTable && sprm.cb >= 2)
					f_in_table = doc.Bytes()[grpprl + sprm.ib + 1] != 0;
				else if (sprm.sprm == kSprmPFTtp && sprm.cb >= 2)
					f_ttp = doc.Bytes()[grpprl + sprm.ib + 1] != 0;
				else if (sprm.sprm >= kSprmTFirst && sprm.sprm <= kSprmTLast)
					{
					++c_tap;
					if (sprm.sprm == kSprmTDefTable)
						tables.push_back(ReadTDefTable(doc, grpprl, sprm));
					}
				}
			if (f_in_table)
				++cells;
			if (f_ttp)
				++rows;
			defs += static_cast<long long>(tables.size());

			if (!f_in_table && !f_ttp && c_tap == 0)
				continue;

			/* Opus only ever puts table geometry on the row-end mark, so a
			 * sprmTDefTable anywhere else means the walk landed somewhere
			 * it should not have. */
			if (!tables.empty() && !f_ttp)
				report.Problem(where + ": sprmTDefTable on a papx that is "
						"not a row end (sprmPFTtp is not set)");

			const bool interesting = max_rows < 0 || shown < max_rows;
			if (interesting)
				{
				++shown;
				std::cout << "  fc " << run.fc_first << ".." << run.fc_lim
						<< "  page " << pn << " run " << irun << ": "
						<< (f_ttp ? "row end" : "cell")
						<< ", " << c_tap << " tap sprm(s), grpprl "
						<< cb_grpprl << " bytes\n";
				}

			for (const TDefTable& table : tables)
				{
				if (interesting)
					{
					std::cout << "      sprmTDefTable @ +" << table.ib
							<< ": cb " << table.cb_stored << " (record "
							<< table.cb_total << " bytes), itcMac "
							<< table.itc_mac << ", rgtc " << table.cb_rgtc
							<< " bytes\n";
					if (!table.centers.empty())
						{
						std::cout << "      rgdxaCenter:";
						for (int center : table.centers)
							std::cout << ' ' << center;
						std::cout << '\n';
						}
					if (!table.ok)
						std::cout << "      <<< " << table.why << '\n';
					}
				if (!table.ok)
					{
					report.Problem(where + ": sprmTDefTable at +" +
							std::to_string(table.ib) + ": " + table.why);
					if (table.old_layout)
						report.Note("that sprmTDefTable reads as the 2-byte "
								"int layout (itcMac at +" +
								std::to_string(kIbTDefTableItcMacOld) +
								"): it was written by a build whose reader "
								"and writer disagreed about the length "
								"field -- see cbTDefTableHdr in prm.h");
					}
				}
			}
		}

	if (max_rows >= 0 && (rows + cells) > shown)
		std::cout << "      ... " << ((rows + cells) - shown)
				<< " more table papx, use --runs=all\n";

	if (papx_walked == 0)
		{
		std::cout << "  no PAPX FKP to walk\n";
		return;
		}
	std::cout << "  walked " << papx_walked << " papx grpprl(s): " << rows
			<< " row end(s), " << cells << " in-table papx, " << defs
			<< " sprmTDefTable record(s)";
	if (papx_failed != 0)
		std::cout << ", " << papx_failed << " UNWALKABLE";
	std::cout << '\n';
	if (rows == 0 && cells == 0 && defs == 0)
		std::cout << "  this document has no tables\n";
	}

void PrintUsage(std::ostream& out)
	{
	out << "usage: doc_inspector [options] FILE.doc\n"
			"\n"
			"Parses a Word 1.x (Opus) document without Wine and reports\n"
			"whether its FIB, FKPs, PLC tables, section table (plcfsed)\n"
			"and table row properties (sprmTDefTable) are structurally\n"
			"sound.\n"
			"\n"
			"options:\n"
			"  --fc-width=auto|4|8  width of FKP rgfc entries (default auto;\n"
			"                       4 is the packed form PutFcFkp writes, 8 is\n"
			"                       a file from before that change)\n"
			"  --page=N             read the FIB from page N (default 0);\n"
			"                       use pnNext for the second document of a\n"
			"                       compound file\n"
			"  --runs=N|all         FKP runs to print per page (default 8)\n"
			"  --verbose            list empty PLCs, cp arrays and every\n"
			"                       packed FIB word\n"
			"  --help               this text\n"
			"\n"
			"exit status: 0 structurally valid, 1 problems found,\n"
			"             2 bad usage or unreadable file\n";
	}

}       /* namespace */

int main(int argc, char **argv)
	{
	const char *path = nullptr;
	int fc_width = 0;               /* 0 = autodetect */
	int max_runs = 8;
	std::size_t fib_page = 0;
	bool verbose = false;

	for (int i = 1; i < argc; ++i)
		{
		const std::string arg = argv[i];
		if (arg == "--help" || arg == "-h")
			{
			PrintUsage(std::cout);
			return 0;
			}
		if (arg == "--verbose" || arg == "-v")
			verbose = true;
		else if (arg.rfind("--fc-width=", 0) == 0)
			{
			const std::string value = arg.substr(11);
			if (value == "auto")
				fc_width = 0;
			else if (value == "4")
				fc_width = 4;
			else if (value == "8")
				fc_width = 8;
			else
				{
				std::cerr << "doc_inspector: --fc-width must be auto, 4 or 8\n";
				return 2;
				}
			}
		else if (arg.rfind("--runs=", 0) == 0)
			{
			const std::string value = arg.substr(7);
			max_runs = (value == "all") ? -1 : std::atoi(value.c_str());
			}
		else if (arg.rfind("--page=", 0) == 0)
			fib_page = static_cast<std::size_t>(
					std::strtoul(arg.substr(7).c_str(), nullptr, 10));
		else if (!arg.empty() && arg[0] == '-')
			{
			std::cerr << "doc_inspector: unknown option " << arg << '\n';
			PrintUsage(std::cerr);
			return 2;
			}
		else if (path == nullptr)
			path = argv[i];
		else
			{
			std::cerr << "doc_inspector: one file at a time\n";
			return 2;
			}
		}

	if (path == nullptr)
		{
		PrintUsage(std::cerr);
		return 2;
		}

	Report report;
	Doc doc;
	if (!doc.Load(path, report))
		{
		for (const std::string& problem : report.Problems())
			std::cerr << "doc_inspector: " << problem << '\n';
		return 2;
		}

	std::cout << "doc_inspector: " << path << '\n';
	Field("file size", std::to_string(doc.Size()) + " bytes");
	/* Page-addressed structures are read a whole sector at a time, but the
	 * file itself stops at the last byte written, so a partial final sector is
	 * normal and is reported for information only. */
	Field("sectors", std::to_string(doc.Size() / kSector) + " x " +
			std::to_string(kSector) + " bytes" +
			(doc.Size() % kSector == 0 ? "" : " + " +
					std::to_string(doc.Size() % kSector) +
					" bytes in a partial last page"));

	Fib fib;
	if (!ReadFib(doc, fib_page, fib, report))
		{
		for (const std::string& problem : report.Problems())
			std::cerr << "doc_inspector: " << problem << '\n';
		return 2;
		}
	Field("FIB page", std::to_string(fib_page) + "  (offset " +
			std::to_string(fib.base) + ", " + std::to_string(kCwFibDisk) +
			" words of " + std::to_string(kCbFibWord) + " = " +
			std::to_string(kCbFibDisk) + " bytes)");

	if (fc_width == 0)
		{
		const int score8 = ScoreFcWidth(doc, fib, 8);
		const int score4 = ScoreFcWidth(doc, fib, 4);
		fc_width = (score4 >= score8) ? 4 : 8;
		Field("FKP rgfc stride", std::to_string(fc_width) +
				" bytes  (autodetected; score 8-byte " +
				std::to_string(score8) + ", 4-byte " +
				std::to_string(score4) + ')');
		if (score8 == score4)
			report.Note("the two FKP rgfc widths score equally: too little "
					"structure in this file to tell them apart, assuming the "
					"packed 4-byte form");
		if (fc_width != kCbFcFkp)
			report.Note("autodetected an 8-byte FKP rgfc: this file predates "
					"PutFcFkp()");
		}
	else
		Field("FKP rgfc stride", std::to_string(fc_width) +
				" bytes  (forced by --fc-width)");
	std::cout << "  (FKP rgfc, the FIB, the PLC cp arrays and every plcfsed "
			"record are 4-byte\n   quantities -- see cbFcFkp in fkp.h and "
			"cbCpDisk in file.h.  An 8-byte rgfc\n   is a file from before "
			"PutFcFkp.)\n";

	PrintFib(doc, fib, report);
	if (verbose)
		PrintFibWordDump(fib);
	PrintFkpStream(doc, fib, fc_width, false /* CHPX */, max_runs, report);
	PrintFkpStream(doc, fib, fc_width, true /* PAPX */, max_runs, report);
	PrintTableFkps(doc, fib, fc_width, max_runs, report);
	PrintPlcTables(doc, fib, fc_width, verbose, report);
	PrintSectionTable(doc, fib, fc_width, report);

	if (fib.At(iwPnNext) != 0)
		report.Note("pnNext is " + std::to_string(fib.At(iwPnNext)) +
				": rerun with --page=" + std::to_string(fib.At(iwPnNext)) +
				" to inspect the appended document");

	Section("Verdict");
	for (const std::string& note : report.Notes())
		std::cout << "  note: " << note << '\n';
	for (const std::string& problem : report.Problems())
		std::cout << "  problem: " << problem << '\n';

	if (report.Problems().empty())
		{
		std::cout << "\n  STRUCTURALLY VALID -- FIB, FKPs, PLC tables, "
				"section table and\n  table row properties are self "
				"consistent.\n";
		return 0;
		}
	std::cout << "\n  NOT STRUCTURALLY VALID -- " << report.Problems().size()
			<< " problem(s) above.\n";
	return 1;
	}

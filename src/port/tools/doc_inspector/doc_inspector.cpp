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
 * The one subtlety that makes this tool more than a hexdump: the cp/fc arrays
 * of a PLC and every field of the FIB are 4 bytes on disk regardless of the
 * host, but the FKP rgfc array and the PLC foo records are written at *native*
 * width.  On this LP64 Winelib build FC is a 64-bit long, so a .doc it writes
 * is not byte compatible with one from the MSVC x64 build, where long is 4
 * bytes.  Both are self consistent, so the tool detects which one it is
 * looking at rather than assuming (see DetectFcWidth); --fc-width overrides.
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

/* ------------------------------------------------------------ PLC tables */

/* One PLC named by the FIB.  foo4/foo8 are sizeof() of the PLC's record type
 * when FC is 4 and 8 bytes wide; every one of them is the constant the engine
 * passes to HplcReadPlcf() in Opus/create.c, and only struct SED embeds an FC,
 * so only cbSED changes with the width. */
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
	{"plcfsed",      iwFcPlcfsed,      iwCbPlcfsed,       8, 16, "SED",  false},
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

/* The two bin tables are looked up by name so the kPlcSpecs order stays a
 * presentation choice rather than something the FKP code depends on. */
const PlcSpec& BteSpec(bool is_papx)
	{
	const char *wanted = is_papx ? "plcfbtePapx" : "plcfbteChpx";
	for (const PlcSpec& spec : kPlcSpecs)
		if (std::strcmp(spec.name, wanted) == 0)
			return spec;
	std::abort();
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
	/* bFreeFirst after crun runs, from C_FAddRun: crun * (sizeof(FC) + 1) +
	 * sizeof(FC).  It must still leave room for one property byte. */
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

/* -------------------------------------------------- FC width autodetection */

/* Score how well the file reads as one of the two native FC widths.  The
 * discriminators are the only two things whose on-disk size depends on it:
 * cbSED (so plcfsed's entry count divides evenly) and the FKP rgfc arrays. */
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

void PrintUsage(std::ostream& out)
	{
	out << "usage: doc_inspector [options] FILE.doc\n"
			"\n"
			"Parses a Word 1.x (Opus) document without Wine and reports\n"
			"whether its FIB, FKPs and PLC tables are structurally sound.\n"
			"\n"
			"options:\n"
			"  --fc-width=auto|4|8  width of the native FC in the FKPs and\n"
			"                       PLC records (default auto)\n"
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
		fc_width = (score4 > score8) ? 4 : 8;
		Field("native FC width", std::to_string(fc_width) +
				" bytes  (autodetected; score 8-byte " +
				std::to_string(score8) + ", 4-byte " + std::to_string(score4) +
				')');
		if (score8 == score4)
			report.Note("the two FC widths score equally: too little "
					"structure in this file to tell them apart, assuming 8");
		}
	else
		Field("native FC width", std::to_string(fc_width) +
				" bytes  (forced by --fc-width)");
	std::cout << "  (a .doc written by the LP64 Winelib build uses an 8-byte "
			"FC in its FKPs\n   and PLC records; the MSVC x64 build uses 4 -- "
			"see cbCpDisk in file.h)\n";

	PrintFib(doc, fib, report);
	if (verbose)
		PrintFibWordDump(fib);
	PrintFkpStream(doc, fib, fc_width, false /* CHPX */, max_runs, report);
	PrintFkpStream(doc, fib, fc_width, true /* PAPX */, max_runs, report);
	PrintPlcTables(doc, fib, fc_width, verbose, report);

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
		std::cout << "\n  STRUCTURALLY VALID -- FIB, FKPs and PLC tables are "
				"self consistent.\n";
		return 0;
		}
	std::cout << "\n  NOT STRUCTURALLY VALID -- " << report.Problems().size()
			<< " problem(s) above.\n";
	return 1;
	}

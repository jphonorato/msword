#include "word.h"
DEBUGASSERTSZ
#include "heap.h"
#include "doc.h"
#include "file.h"
#include "disp.h"
#include <stdio.h>

#include "opus_x64_layout.h"

int OpusDodIsMother(const void* pdod)
{
	return ((const struct DOD*)pdod)->fMother != 0;
}

int OpusDodIsDocument(const void* pdod)
{
	return ((const struct DOD*)pdod)->fDoc != 0;
}

int OpusDodIsTemplate(const void* pdod)
{
	return ((const struct DOD*)pdod)->fDot != 0;
}

int OpusDodMotherDoc(const void* pdod)
{
	return ((const struct DOD*)pdod)->doc;
}

int OpusDodTemplateDoc(const void* pdod)
{
	return ((const struct DOD*)pdod)->docDot;
}

long OpusDodCpMac(const void* pdod)
{
	return ((const struct DOD*)pdod)->cpMac;
}

int OpusWwdMw(const void* pwwd)
{
	return ((const struct WWD*)pwwd)->mw;
}

void OpusWwdCoordinateOffsets(const void* pwwdVoid, int* page_x, int* page_y,
		int* window_x, int* window_y)
{
	const struct WWD* pwwd = (const struct WWD*)pwwdVoid;
	*page_x = pwwd->rcePage.xeLeft;
	*page_y = pwwd->rcePage.yeTop;
	*window_x = pwwd->xwMin;
	*window_y = pwwd->ywMin;
}

void* OpusSttbData(void* psttbVoid)
{
	struct STTB* psttb = (struct STTB*)psttbVoid;
	if (psttb->fExternal)
		return HpOfHq(psttb->hqrgbst);
	return psttb->rgbst;
}

int OpusSttbCount(const void* psttb)
{
	return ((const struct STTB*)psttb)->ibstMac;
}

int OpusSttbUsesStyleRules(const void* psttb)
{
	return ((const struct STTB*)psttb)->fStyleRules != 0;
}

size_t OpusSttbDataSize(void** hsttb)
{
	struct STTB* psttb;
	size_t cbHandle;
	if (hsttb == NULL || *hsttb == NULL)
		return 0;
	psttb = (struct STTB*)*hsttb;
	if (psttb->fExternal)
		return (size_t)CbOfHq(psttb->hqrgbst);
	cbHandle = OpusCbOfH(hsttb);
	return cbHandle > offset(STTB, rgbst)
		? cbHandle - offset(STTB, rgbst) : 0;
}

void* OpusPlcCpData(void* pplcVoid)
{
	struct PLC* pplc = (struct PLC*)pplcVoid;
	if (pplc->fExternal)
		return HpOfHq(pplc->hqplce);
	return pplc->rgcp;
}

int OpusPlcIMac(const void* pplc)
{
	return ((const struct PLC*)pplc)->iMac;
}

int OpusPlcIMax(const void* pplc)
{
	return ((const struct PLC*)pplc)->iMax;
}

int OpusPlcEntrySize(const void* pplc)
{
	return ((const struct PLC*)pplc)->cb;
}

int OpusPlcAdjustIndex(const void* pplc)
{
	return ((const struct PLC*)pplc)->icpAdjust;
}

long OpusPlcAdjustment(const void* pplc)
{
	return ((const struct PLC*)pplc)->dcpAdjust;
}

int OpusPlcHasMultipleCps(const void* pplc)
{
	return ((const struct PLC*)pplc)->fMult != 0;
}

void OpusPlcSetHint(void* pplc, int index)
{
	((struct PLC*)pplc)->icpHint = index;
}

void OpusPlcSetAdjustIndex(void* pplc, int index)
{
	((struct PLC*)pplc)->icpAdjust = index;
}

void OpusPlcClearAdjustment(void* pplcVoid)
{
	struct PLC* pplc = (struct PLC*)pplcVoid;
	pplc->icpAdjust = pplc->iMac + 1;
	pplc->dcpAdjust = cp0;
}

void* OpusPlData(void* pplVoid)
{
	struct PL* ppl = (struct PL*)pplVoid;
	int brgfoo;
	char* rgfoo;

	if (pplVoid == NULL)
		return NULL;
	brgfoo = ppl->brgfoo;
	/* HplInit stores data at cbPLBase. A smashed header (or PLLBS
	   overlay that wrote rglbs over fExternal) can leave brgfoo out of
	   range. Task 3 (opus_x64_setjmp.cpp) fixed the one known cause of
	   this kind of smash; this clamp is now a last-resort guard, not
	   the expected path -- log every time it fires so a real recurrence
	   (or a legitimate PL whose header is bigger than this heuristic
	   expects) is visible instead of silently mispaginating. */
	if (brgfoo < (int)offset(PL, rgbHead) || brgfoo > 4096)
		{
		fprintf(stderr,
			"OpusPlData: brgfoo=%d out of range [%d,4096], PL header "
			"likely corrupted; falling back to cbPLBase\n",
			brgfoo, (int)offset(PL, rgbHead));
		brgfoo = (int)offset(PL, rgbHead);
		}
	rgfoo = ((char*)ppl) + brgfoo;
	/* PLLBS has rglbs at the same offset as PL.fExternal. Only the
	   boolean 1 is a real external HQ; any other nonzero is payload. */
	if (ppl->fExternal == 1)
		return HpOfHq(*((HQ*)rgfoo));
	return rgfoo;
}

int OpusPlCount(const void* ppl)
{
	return ((const struct PL*)ppl)->iMac;
}

int OpusPlEntrySize(const void* ppl)
{
	return ((const struct PL*)ppl)->cb;
}

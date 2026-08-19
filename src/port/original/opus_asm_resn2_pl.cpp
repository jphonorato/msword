#include "opus_x64_layout.h"

#include <cstddef>

/* AMD64 translation of Opus/asm/resn2.asm:HpInPl. */

extern "C" unsigned char* HpInPl(void** hpl, const int index) {
    if (hpl == nullptr || *hpl == nullptr) {
        return nullptr;
    }
    auto* const base = static_cast<unsigned char*>(OpusPlData(*hpl));
    if (base == nullptr) {
        return nullptr;
    }
    const int cb = OpusPlEntrySize(*hpl);
    /* Opus/asm/resn2.asm:1340's Assert(iFoo >= 0 && iFoo < pplFoo->iMac)
       is the only original bounds check, DEBUG-only -- the real routine
       has no release-build guard and no "give back element 0" fallback.
       nullptr (not base) on an invalid index/cb: callers throughout
       Opus/wordtech treat any non-null HpInPl return as a valid entry
       at that index, so silently substituting element 0 would read/write
       the wrong slot undetected. */
    if (cb <= 0 || index < 0) {
        return nullptr;
    }
    return base + static_cast<std::size_t>(index) *
                      static_cast<std::size_t>(cb);
}

extern "C" unsigned char* PInPl(void** hpl, const int index) {
    return HpInPl(hpl, index);
}

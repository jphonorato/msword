#include "opus_x64_layout.h"

#include <cstdint>
#include <cstring>
#include <limits>

/*
 * AMD64 translations of the flat-memory utility and document-range slice of
 * Opus/asm/resn.asm, Opus/asm/resn2.asm, and Opus/asm/fetchn2.asm.  The old
 * routines spent most of their instructions managing far pointers and the
 * 16-bit calling convention; the algorithms below are the C descriptions
 * embedded beside that assembly, with the same public entry names.
 */

extern "C" void** mpdochdod[];

namespace {

/* Opus/ch.h: ccpEop = 2 under CRLF (defined for every engine target in
 * CMakeLists.txt), 1 only under the MAC branch this port never builds. */
constexpr long kCcpEop = 2;
constexpr int kDocNil = 0;

struct OpusCa {
    long cpFirst;
    long cpLim;
    int doc;
};

struct OpusRect {
    int left;
    int top;
    int right;
    int bottom;
};

void* DodOrNull(const int doc) {
    void** const handle = mpdochdod[doc];
    return handle == nullptr ? nullptr : *handle;
}

void* MoveBytes(const void* source, void* destination,
                const std::size_t byte_count) {
    if (byte_count != 0) {
        std::memmove(destination, source, byte_count);
    }
    return static_cast<unsigned char*>(destination) + byte_count;
}

}  // namespace

extern "C" long CpMax(const long first, const long second) {
    return first > second ? first : second;
}

extern "C" long CpMin(const long first, const long second) {
    return first < second ? first : second;
}

extern "C" unsigned int umax(const unsigned int first,
                              const unsigned int second) {
    return first > second ? first : second;
}

extern "C" unsigned int umin(const unsigned int first,
                              const unsigned int second) {
    return first < second ? first : second;
}

extern "C" int N_abs(const int value) {
    return value < 0 ? -value : value;
}

extern "C" int NMultDiv(const int value, const int numerator,
                         const int denominator) {
    const std::int64_t product = static_cast<std::int64_t>(value) * numerator;
    const bool negative = (product < 0) != (denominator < 0);
    const std::uint64_t magnitude = product < 0
        ? static_cast<std::uint64_t>(-product)
        : static_cast<std::uint64_t>(product);
    const std::uint64_t divisor = denominator < 0
        ? static_cast<std::uint64_t>(-static_cast<std::int64_t>(denominator))
        : static_cast<std::uint64_t>(denominator);

    std::uint64_t quotient = std::numeric_limits<std::uint16_t>::max();
    if (divisor != 0) {
        quotient = (magnitude + divisor / 2) / divisor;
    }
    if (quotient > 0x7fffU) {
        quotient = 0x7fffU;
    }
    const int result = static_cast<int>(quotient);
    return negative ? -result : result;
}

extern "C" unsigned int UMultDiv(const unsigned int value,
                                  const unsigned int numerator,
                                  const unsigned int denominator) {
    if (denominator == 0) {
        return 0xffffU;
    }
    const std::uint64_t product = static_cast<std::uint64_t>(value) * numerator;
    const std::uint64_t quotient =
        (product + static_cast<std::uint64_t>(denominator) / 2) / denominator;
    return quotient > 0xffffU ? 0xffffU : static_cast<unsigned int>(quotient);
}

extern "C" void* bltbyte(const void* source, void* destination,
                          const int byte_count) {
    return MoveBytes(source, destination, static_cast<std::size_t>(byte_count));
}

extern "C" void* bltbyteNat(const void* source, void* destination,
                             const int byte_count) {
    return bltbyte(source, destination, byte_count);
}

extern "C" void* bltbx(const void* source, void* destination,
                        const int byte_count) {
    return bltbyte(source, destination, byte_count);
}

extern "C" void* bltbxNat(const void* source, void* destination,
                           const int byte_count) {
    return bltbyte(source, destination, byte_count);
}

extern "C" void* blt(const void* source, void* destination,
                      const int word_count) {
    return MoveBytes(source, destination,
                     static_cast<std::size_t>(word_count) * sizeof(int));
}

extern "C" void* bltNat(const void* source, void* destination,
                         const int word_count) {
    return blt(source, destination, word_count);
}

extern "C" void* bltx(const void* source, void* destination,
                       const int word_count) {
    return blt(source, destination, word_count);
}

extern "C" void* bltxNat(const void* source, void* destination,
                          const int word_count) {
    return blt(source, destination, word_count);
}

extern "C" void* bltbcx(const int value, void* destination,
                          const int byte_count) {
    std::memset(destination, static_cast<unsigned char>(value),
                static_cast<std::size_t>(byte_count));
    return destination;
}

extern "C" void* bltcx(const int value, int* destination,
                        const int word_count) {
    for (int index = 0; index < word_count; ++index) {
        destination[index] = value;
    }
    return destination;
}

extern "C" int FNeRgch(const void* first, const void* second,
                        const int byte_count) {
    return byte_count != 0 &&
           std::memcmp(first, second, static_cast<std::size_t>(byte_count)) != 0;
}

extern "C" int FNeRgw(const int* first, const int* second,
                       const int word_count) {
    return FNeRgch(first, second,
                   static_cast<int>(sizeof(int)) * word_count);
}

extern "C" int FNeSt(const unsigned char* first,
                      const unsigned char* second) {
    return first[0] != second[0] || FNeRgch(first + 1, second + 1, first[0]);
}

extern "C" int CchNonZeroPrefix(const unsigned char* bytes, int count) {
    while (count > 0 && bytes[count - 1] == 0) {
        --count;
    }
    return count;
}

extern "C" int DrcToRc(const OpusRect* dimensions, OpusRect* rectangle) {
    rectangle->left = dimensions->left;
    rectangle->top = dimensions->top;
    rectangle->right = dimensions->left + dimensions->right;
    rectangle->bottom = dimensions->top + dimensions->bottom;
    return 0;
}

extern "C" int RcToDrc(const OpusRect* rectangle, OpusRect* dimensions) {
    dimensions->left = rectangle->left;
    dimensions->top = rectangle->top;
    dimensions->right = rectangle->right - rectangle->left;
    dimensions->bottom = rectangle->bottom - rectangle->top;
    return 0;
}

extern "C" int FEmptyRc(const OpusRect* rectangle) {
    return rectangle->top >= rectangle->bottom ||
           rectangle->left >= rectangle->right;
}

extern "C" int FSectRc(const OpusRect* first, const OpusRect* second,
                        OpusRect* destination) {
    destination->left = first->left > second->left ? first->left : second->left;
    destination->top = first->top > second->top ? first->top : second->top;
    destination->right = first->right < second->right
        ? first->right : second->right;
    destination->bottom = first->bottom < second->bottom
        ? first->bottom : second->bottom;
    if (destination->left > destination->right) {
        destination->left = destination->right;
    }
    if (destination->top > destination->bottom) {
        destination->top = destination->bottom;
    }
    return !FEmptyRc(destination);
}

extern "C" int FIsectIval(const OpusCa* range, const long first,
                           const long limit) {
    return range->cpLim >= first && range->cpFirst <= limit;
}

extern "C" char* StringMap(const char* text, const int counted,
                            const int location) {
    (void)location;
    if (!counted) {
        return const_cast<char*>(text);
    }
    thread_local unsigned char buffers[16][256];
    thread_local unsigned int next_buffer = 0;
    unsigned char* const result = buffers[next_buffer++ % 16];
    std::size_t count = std::strlen(text);
    if (count > 254) {
        count = 254;
    }
    result[0] = static_cast<unsigned char>(count);
    std::memcpy(result + 1, text, count);
    result[count + 1] = 0;
    return reinterpret_cast<char*>(result);
}

extern "C" long CpMac2Doc(const int doc) {
    void* const pdod = DodOrNull(doc);
    return pdod == nullptr ? 0 : OpusDodCpMac(pdod);
}

extern "C" long CpMacDoc(const int doc) {
    return CpMac2Doc(doc) - 2 * kCcpEop;
}

extern "C" long CpMac1Doc(const int doc) {
    return CpMac2Doc(doc) - kCcpEop;
}

extern "C" long CpMacDocEdit(const int doc) {
    return CpMac2Doc(doc) - 3 * kCcpEop;
}

extern "C" OpusCa* PcaSet(OpusCa* const range, const int doc,
                            const long cp_first, const long cp_lim) {
    range->doc = doc;
    range->cpFirst = cp_first;
    range->cpLim = cp_lim;
    return range;
}

extern "C" OpusCa* PcaSetDcp(OpusCa* const range, const int doc,
                               const long cp_first, const long dcp) {
    return PcaSet(range, doc, cp_first, cp_first + dcp);
}

extern "C" OpusCa* PcaSetWholeDoc(OpusCa* const range, const int doc) {
    return PcaSet(range, doc, 0, CpMacDocEdit(doc));
}

extern "C" OpusCa* PcaSetNil(OpusCa* const range) {
    return PcaSet(range, kDocNil, 0, 0);
}

extern "C" OpusCa* PcaPoint(OpusCa* const range, const int doc,
                              const long cp) {
    return PcaSet(range, doc, cp, cp);
}

extern "C" long DcpCa(const OpusCa* const range) {
    return range->cpLim - range->cpFirst;
}

extern "C" int FInCa(const int doc, const long cp,
                      const OpusCa* const range) {
    return range->doc == doc && range->cpFirst <= cp && cp < range->cpLim;
}

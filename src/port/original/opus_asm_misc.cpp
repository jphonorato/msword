#include "opus_x64_compat.h"
#include "opus_x64_heap.h"
#if defined(__GNUC__) && !defined(_MSC_VER)
#include "OpusShellMemory.h"    /* Qt-2 B3: OpusMem* */
#endif

#include <algorithm>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

namespace {

/* DKT tags from Opus/el.h (stored as char in DKD.rgdkt[]). */
constexpr int kDktInt = 0;
constexpr int kDktLong = 1;
constexpr int kDktSingle = 2;
constexpr int kDktDouble = 3;
constexpr int kDktString = 4;

template <std::size_t... Index>
long invoke_macro_ints(void* procedure, const int* arguments,
                       std::index_sequence<Index...>) {
    using Procedure =
        long(__cdecl*)(std::conditional_t<true, int,
            std::integral_constant<std::size_t, Index>>...);
    return reinterpret_cast<Procedure>(procedure)(arguments[Index]...);
}

/* Logical arguments as integer/pointer-sized values (MS x64 / Wine: both
 * ints and pointers travel in GPRs).  Doubles are still passed as two
 * consecutive int slots via the untyped path only — mixed float+GPR
 * dispatch is out of scope for the dktString fix. */
template <std::size_t... Index>
long invoke_macro_ptrs(void* procedure, void* const* arguments,
                       std::index_sequence<Index...>) {
    using Procedure =
        long(__cdecl*)(std::conditional_t<true, void*,
            std::integral_constant<std::size_t, Index>>...);
    return reinterpret_cast<Procedure>(procedure)(arguments[Index]...);
}

long invoke_macro_ptrs_n(void* procedure, void* const* arguments, int count) {
    switch (count) {
    case 0: return invoke_macro_ptrs(procedure, arguments, std::index_sequence<>{});
    case 1: return invoke_macro_ptrs(procedure, arguments, std::make_index_sequence<1>{});
    case 2: return invoke_macro_ptrs(procedure, arguments, std::make_index_sequence<2>{});
    case 3: return invoke_macro_ptrs(procedure, arguments, std::make_index_sequence<3>{});
    case 4: return invoke_macro_ptrs(procedure, arguments, std::make_index_sequence<4>{});
    case 5: return invoke_macro_ptrs(procedure, arguments, std::make_index_sequence<5>{});
    case 6: return invoke_macro_ptrs(procedure, arguments, std::make_index_sequence<6>{});
    case 7: return invoke_macro_ptrs(procedure, arguments, std::make_index_sequence<7>{});
    case 8: return invoke_macro_ptrs(procedure, arguments, std::make_index_sequence<8>{});
    case 9: return invoke_macro_ptrs(procedure, arguments, std::make_index_sequence<9>{});
    case 10: return invoke_macro_ptrs(procedure, arguments, std::make_index_sequence<10>{});
    case 11: return invoke_macro_ptrs(procedure, arguments, std::make_index_sequence<11>{});
    case 12: return invoke_macro_ptrs(procedure, arguments, std::make_index_sequence<12>{});
    case 13: return invoke_macro_ptrs(procedure, arguments, std::make_index_sequence<13>{});
    case 14: return invoke_macro_ptrs(procedure, arguments, std::make_index_sequence<14>{});
    case 15: return invoke_macro_ptrs(procedure, arguments, std::make_index_sequence<15>{});
    case 16: return invoke_macro_ptrs(procedure, arguments, std::make_index_sequence<16>{});
    default: return 0;
    }
}

long invoke_macro_ints_n(void* procedure, const int* arguments, int count) {
    switch (count) {
    case 0: return invoke_macro_ints(procedure, arguments, std::index_sequence<>{});
    case 1: return invoke_macro_ints(procedure, arguments, std::make_index_sequence<1>{});
    case 2: return invoke_macro_ints(procedure, arguments, std::make_index_sequence<2>{});
    case 3: return invoke_macro_ints(procedure, arguments, std::make_index_sequence<3>{});
    case 4: return invoke_macro_ints(procedure, arguments, std::make_index_sequence<4>{});
    case 5: return invoke_macro_ints(procedure, arguments, std::make_index_sequence<5>{});
    case 6: return invoke_macro_ints(procedure, arguments, std::make_index_sequence<6>{});
    case 7: return invoke_macro_ints(procedure, arguments, std::make_index_sequence<7>{});
    case 8: return invoke_macro_ints(procedure, arguments, std::make_index_sequence<8>{});
    case 9: return invoke_macro_ints(procedure, arguments, std::make_index_sequence<9>{});
    case 10: return invoke_macro_ints(procedure, arguments, std::make_index_sequence<10>{});
    case 11: return invoke_macro_ints(procedure, arguments, std::make_index_sequence<11>{});
    case 12: return invoke_macro_ints(procedure, arguments, std::make_index_sequence<12>{});
    case 13: return invoke_macro_ints(procedure, arguments, std::make_index_sequence<13>{});
    case 14: return invoke_macro_ints(procedure, arguments, std::make_index_sequence<14>{});
    case 15: return invoke_macro_ints(procedure, arguments, std::make_index_sequence<15>{});
    case 16: return invoke_macro_ints(procedure, arguments, std::make_index_sequence<16>{});
    default: return 0;
    }
}

struct NativePlaybackEvent {
    unsigned int message;
    unsigned int virtual_key;
};

struct NativePlaybackQueue {
    int current;
    int count;
    NativePlaybackEvent events[1];
};

}  // namespace

extern "C" {

int N_WCompSzSrt(char* first, char* second, int case_sensitive);
extern HANDLE* lphevtHead;
extern HANDLE* lphrgbKeyState;

/* index() is a POSIX function that glibc declares as
 * "char *index(const char *, int)" through <strings.h>, reached here via
 * Wine's <windows.h> -> <string.h>.  Redefining it with C linkage and a
 * different parameter type is a hard conflict, and the semantics are
 * identical anyway: first occurrence of the character, with the terminating
 * NUL considered part of the string, NULL when absent.  So on this toolchain
 * the platform's own index() is used and this translation is skipped.  MSVC
 * has no index(), so its build keeps the definition below. */
#if !defined(__GNUC__) || defined(_MSC_VER)
char* index(char* text, const int character) {
    if (text == nullptr) {
        return nullptr;
    }
    const unsigned char wanted = static_cast<unsigned char>(character);
    for (;;) {
        if (static_cast<unsigned char>(*text) == wanted) {
            return text;
        }
        if (*text++ == '\0') {
            return nullptr;
        }
    }
}
#endif

int LbcCmpLbox(unsigned int, unsigned char** first,
               unsigned char** second) {
    if (first == nullptr || second == nullptr || *first == nullptr ||
        *second == nullptr) {
        return 0;
    }
    const int comparison = N_WCompSzSrt(
        reinterpret_cast<char*>(*first + 1),
        reinterpret_cast<char*>(*second + 1), 0);
    if (comparison == 0) return 0;   // lbcEq
    if (comparison > 0) return 3;    // lbcGt
    if (comparison == -2) return 2;  // lbcLt
    return 1;                        // lbcPrefix
}

long LPushMacroArgsTyped(void* procedure, const int* arguments,
                         const int argument_count, const unsigned char* dkts,
                         const int dkt_count) {
    if (procedure == nullptr || argument_count < 0 || argument_count > 16 ||
        (argument_count != 0 && arguments == nullptr)) {
        return 0;
    }
    /* Untyped path: one int register/stack slot per array element (legacy). */
    if (dkts == nullptr || dkt_count <= 0) {
        return invoke_macro_ints_n(procedure, arguments, argument_count);
    }

    /* Decode DKT stream into logical GPR arguments (ints and pointers).
     * dktString: two int slots hold lo32/hi32 of a pointer (writer under
     * LP64).  dktLong: one int (32-bit Windows LONG after exp.c a').
     * dktDouble/dktSingle: two/one int slots kept as separate GPR args
     * (same as untyped layout; float XMM dispatch not introduced here). */
    void* logical[16] = {};
    int n_logical = 0;
    int slot = 0;
    for (int i = 0; i < dkt_count; ++i) {
        if (n_logical >= 16) {
            return 0;
        }
        const int dkt = static_cast<int>(dkts[i]);
        switch (dkt) {
        case kDktString: {
            if (slot + 1 >= argument_count) {
                return 0;
            }
            const auto lo = static_cast<std::uint32_t>(arguments[slot]);
            const auto hi = static_cast<std::uint32_t>(arguments[slot + 1]);
            const auto up = static_cast<std::uintptr_t>(lo) |
                            (static_cast<std::uintptr_t>(hi) << 32);
            logical[n_logical++] = reinterpret_cast<void*>(up);
            slot += 2;
            break;
        }
        case kDktDouble: {
            /* Two int slots → one logical 64-bit payload in a GPR (bit pattern).
             * Callees expecting a true double in XMM still need a later fix. */
            if (slot + 1 >= argument_count) {
                return 0;
            }
            const auto lo = static_cast<std::uint32_t>(arguments[slot]);
            const auto hi = static_cast<std::uint32_t>(arguments[slot + 1]);
            const auto bits = static_cast<std::uint64_t>(lo) |
                              (static_cast<std::uint64_t>(hi) << 32);
            logical[n_logical++] = reinterpret_cast<void*>(
                static_cast<std::uintptr_t>(bits));
            slot += 2;
            break;
        }
        case kDktInt:
        case kDktLong:
        case kDktSingle:
        default: {
            if (slot >= argument_count) {
                return 0;
            }
            logical[n_logical++] = reinterpret_cast<void*>(
                static_cast<std::intptr_t>(arguments[slot]));
            slot += 1;
            break;
        }
        }
    }
    /* Trailing untyped slots (should not happen if cwArgs matches types). */
    while (slot < argument_count && n_logical < 16) {
        logical[n_logical++] = reinterpret_cast<void*>(
            static_cast<std::intptr_t>(arguments[slot]));
        slot += 1;
    }
    return invoke_macro_ptrs_n(procedure, logical, n_logical);
}

long LPushMacroArgs(void* procedure, const int* arguments,
                    const int argument_count) {
    return LPushMacroArgsTyped(procedure, arguments, argument_count, nullptr, 0);
}

long MemUsed(const int memory_type) {
    if ((memory_type & 1) == 0) {
        return 0;  // no expanded-memory arena exists in the flat port
    }
    const std::size_t used = OpusHeapBytesUsed();
    return static_cast<long>((std::min)(
        used, static_cast<std::size_t>((std::numeric_limits<long>::max)())));
}

LRESULT CALLBACK PlaybackHook(const int code, const WPARAM parameter,
                              const LPARAM message_pointer) {
    if (code != HC_GETNEXT && code != HC_SKIP) {
        return CallNextHookEx(nullptr, code, parameter, message_pointer);
    }
    if (lphevtHead == nullptr || *lphevtHead == nullptr) {
        return 0;
    }

    HANDLE queue_handle = *lphevtHead;
#if defined(__GNUC__) && !defined(_MSC_VER)
    /* Qt-2 B3: mismo bloque hevt que eldde.c aloca con GMEM_SENDKEYS
     * (-> OPUS_MEM_LOWER -> HGLOBAL real via passthrough).  Se pasa por
     * el contrato para que productor y consumidor de la familia A usen
     * la misma puerta, no porque el GlobalLock crudo fallara. */
    auto* queue = static_cast<NativePlaybackQueue*>(
        OpusMemLock(reinterpret_cast<OpusHandle>(queue_handle)));
#else
    auto* queue = static_cast<NativePlaybackQueue*>(GlobalLock(queue_handle));
#endif
    if (queue == nullptr) {
        return 0;
    }

    bool finished = false;
    if (code == HC_SKIP) {
        ++queue->current;
        finished = queue->current >= queue->count;
    } else if (message_pointer != 0 && queue->current >= 0 &&
               queue->current < queue->count) {
        auto* output = reinterpret_cast<EVENTMSG*>(message_pointer);
        const NativePlaybackEvent& event = queue->events[queue->current];
        output->message = event.message;
        output->paramL = event.virtual_key;
        output->paramH = 1;
        output->time = 0;
        output->hwnd = nullptr;
    }
#if defined(__GNUC__) && !defined(_MSC_VER)
    OpusMemUnlock(reinterpret_cast<OpusHandle>(queue_handle));
#else
    GlobalUnlock(queue_handle);
#endif

    if (finished) {
#if defined(__GNUC__) && !defined(_MSC_VER)
        OpusMemFree(reinterpret_cast<OpusHandle>(queue_handle));
#else
        GlobalFree(queue_handle);
#endif
        *lphevtHead = nullptr;
        if (lphrgbKeyState != nullptr && *lphrgbKeyState != nullptr) {
            HANDLE state_handle = *lphrgbKeyState;
#if defined(__GNUC__) && !defined(_MSC_VER)
            auto* state = static_cast<unsigned char*>(
                OpusMemLock(reinterpret_cast<OpusHandle>(state_handle)));
#else
            auto* state = static_cast<unsigned char*>(GlobalLock(state_handle));
#endif
            if (state != nullptr) {
                SetKeyboardState(state);
#if defined(__GNUC__) && !defined(_MSC_VER)
                OpusMemUnlock(reinterpret_cast<OpusHandle>(state_handle));
#else
                GlobalUnlock(state_handle);
#endif
            }
#if defined(__GNUC__) && !defined(_MSC_VER)
            OpusMemFree(reinterpret_cast<OpusHandle>(state_handle));
#else
            GlobalFree(state_handle);
#endif
            *lphrgbKeyState = nullptr;
        }
    }
    return 0;
}

}  // extern "C"

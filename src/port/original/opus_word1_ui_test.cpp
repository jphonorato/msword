#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <array>
#include <cwchar>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#if defined(__GNUC__) && !defined(_MSC_VER)
/* _wcsicmp is declared in Wine's msvcrt headers (corecrt_wstring.h), not
   in <windows.h>/<cwchar> as included here. glibc's wcscasecmp (the POSIX
   equivalent, same signature) was used previously, but it's wrong here:
   this TU's wchar_t is winegcc's 2-byte Win32 WCHAR (-fshort-wchar), while
   glibc's wcscasecmp is compiled against its own native 4-byte wchar_t and
   silently misreads real WCHAR buffers regardless of the caller's local
   type width -- confirmed empirically 2026-08-14 (see
   docs/port-linux/01-diagnostico-heap-corruption-arranque.md §23) via the
   sibling bug in this same file's argument parsing. lstrcmpiW is the
   Win32-native, width-correct equivalent. */
#define _wcsicmp lstrcmpiW
#endif

namespace {

constexpr UINT kWmCommand = 0x0111;
constexpr WPARAM kFileNew = 1813;
constexpr WPARAM kFileOpen = 1843; /* opuscmd.h imiOpen */
constexpr WPARAM kFileSaveAs = 1897;
constexpr WPARAM kFileExit = 2095;
constexpr WPARAM kHelpAbout = 182;
constexpr WPARAM kParaCenter = 1355;
constexpr UINT kWmOpusX64QuerySelection = WM_APP + 0x351;
constexpr int kKcControl = 0x100;
constexpr LRESULT kEditUndo = 2229;
constexpr LRESULT kEditCut = 2252;
constexpr LRESULT kEditCopy = 2274;
constexpr LRESULT kEditPaste = 2297;
constexpr LRESULT kEditSelectAll = 5106;

struct WindowSearch {
    DWORD process_id;
    const wchar_t* class_name;
    const wchar_t* caption_fragment;
    HWND result;
};

// Deliberately not std::wcsstr: winegcc compiles this TU's wchar_t as the
// Win32 2-byte WCHAR (-fshort-wchar), but glibc's linked wcsstr/wcscmp/
// wcslen/wcerr operate on the native 4-byte wchar_t regardless of the
// caller's local type width -- they silently misread real WCHAR buffers
// (confirmed empirically 2026-08-14: wcslen() on an 11-code-unit WCHAR
// string returned 6, consistent with reading 4 bytes at a time past a
// 24-byte buffer). Everything comparing genuine Win32 wide-char data in
// this file must go through lstrcmpW/lstrcmpiW or a manual loop like this
// one instead. See docs/port-linux/01-diagnostico-heap-corruption-arranque.md
// §23.
bool wide_contains(const wchar_t* haystack, const wchar_t* needle) {
    if (haystack == nullptr || needle == nullptr || *needle == L'\0') {
        return needle != nullptr && *needle == L'\0';
    }
    for (const wchar_t* start = haystack; *start != L'\0'; ++start) {
        const wchar_t* h = start;
        const wchar_t* n = needle;
        while (*h != L'\0' && *n != L'\0' && *h == *n) {
            ++h;
            ++n;
        }
        if (*n == L'\0') {
            return true;
        }
    }
    return false;
}

// ASCII-only case folding for the loop below. Same -fshort-wchar reason as
// wide_contains: towlower/CharLowerW-on-a-single-code-unit would be fine but
// a manual fold keeps this TU free of any wide-char library dependency.
wchar_t wide_lower_ascii(const wchar_t value) {
    return (value >= L'A' && value <= L'Z')
               ? static_cast<wchar_t>(value - L'A' + L'a')
               : value;
}

// Case-insensitive wide_contains. Needed for window captions carrying a file
// name: Word 1.x upper-cases the document name it puts in the frame caption
// (DOS 8.3 convention), so "...\\TEMP\\OPRT0128.DOC" has to match a base name
// the caller derived from the lower-case path it typed into Save As.
bool wide_contains_i(const wchar_t* haystack, const wchar_t* needle) {
    if (haystack == nullptr || needle == nullptr || *needle == L'\0') {
        return needle != nullptr && *needle == L'\0';
    }
    for (const wchar_t* start = haystack; *start != L'\0'; ++start) {
        const wchar_t* h = start;
        const wchar_t* n = needle;
        while (*h != L'\0' && *n != L'\0' &&
               wide_lower_ascii(*h) == wide_lower_ascii(*n)) {
            ++h;
            ++n;
        }
        if (*n == L'\0') {
            return true;
        }
    }
    return false;
}

BOOL CALLBACK find_window_callback(const HWND window, const LPARAM value) {
    auto& search = *reinterpret_cast<WindowSearch*>(value);
    // search.process_id comes from CreateProcessW's PROCESS_INFORMATION,
    // which this build's wine (10.0~repack-6, vanilla) returns zeroed for
    // WORD1.exe.so specifically -- confirmed with a standalone repro
    // outside this project entirely (docs/port-linux/
    // 01-diagnostico-heap-corruption-arranque.md §25); real PID/handles
    // come back fine for Wine's own builtin executables, so this looks
    // like a Wine limitation for externally-built winelib targets, not a
    // bug here. When it's 0 there's nothing valid to filter on, so fall
    // back to matching by class/caption alone -- already proven to find
    // the right window (§21, §24). Skip the filter only in that
    // known-broken case; keep it whenever a real PID is available, since
    // it's strictly more precise.
    if (search.process_id != 0) {
        DWORD process_id = 0;
        GetWindowThreadProcessId(window, &process_id);
        if (process_id != search.process_id) {
            return TRUE;
        }
    }

    wchar_t class_name[128] = {};
    wchar_t caption[512] = {};
    GetClassNameW(window, class_name, static_cast<int>(std::size(class_name)));
    GetWindowTextW(window, caption, static_cast<int>(std::size(caption)));
    if ((search.class_name == nullptr ||
         lstrcmpW(class_name, search.class_name) == 0) &&
        (search.caption_fragment == nullptr ||
         wide_contains(caption, search.caption_fragment))) {
        search.result = window;
        return FALSE;
    }
    return TRUE;
}

HWND find_process_window(const DWORD process_id, const wchar_t* class_name,
                         const wchar_t* caption_fragment) {
    WindowSearch search{process_id, class_name, caption_fragment, nullptr};
    EnumWindows(find_window_callback, reinterpret_cast<LPARAM>(&search));
    return search.result;
}

BOOL CALLBACK log_window_callback(const HWND window, const LPARAM value) {
    const DWORD expected_process_id = static_cast<DWORD>(value);
    DWORD process_id = 0;
    GetWindowThreadProcessId(window, &process_id);
    if (process_id == expected_process_id) {
        wchar_t class_name[128] = {};
        wchar_t caption[512] = {};
        GetClassNameW(window, class_name,
                      static_cast<int>(std::size(class_name)));
        GetWindowTextW(window, caption, static_cast<int>(std::size(caption)));
        // Narrow before printing: std::wcerr / glibc wide I/O assume 4-byte
        // wchar_t; these buffers are real 2-byte WCHAR under winegcc.
        char class_name_ansi[256] = {};
        char caption_ansi[1024] = {};
        WideCharToMultiByte(CP_ACP, 0, class_name, -1, class_name_ansi,
                            static_cast<int>(sizeof(class_name_ansi)), nullptr,
                            nullptr);
        WideCharToMultiByte(CP_ACP, 0, caption, -1, caption_ansi,
                            static_cast<int>(sizeof(caption_ansi)), nullptr,
                            nullptr);
        std::cerr << "window class='" << class_name_ansi << "' caption='"
                  << caption_ansi << "' visible=" << IsWindowVisible(window)
                  << " enabled=" << IsWindowEnabled(window) << '\n';
    }
    return TRUE;
}

void log_process_windows(const DWORD process_id) {
    EnumWindows(log_window_callback, static_cast<LPARAM>(process_id));
}

HWND find_descendant_by_class(const HWND parent,
                              const wchar_t* expected_class) {
    for (HWND child = GetWindow(parent, GW_CHILD); child != nullptr;
         child = GetWindow(child, GW_HWNDNEXT)) {
        wchar_t class_name[128] = {};
        if (GetClassNameW(child, class_name,
                          static_cast<int>(std::size(class_name))) != 0 &&
            _wcsicmp(class_name, expected_class) == 0) {
            return child;
        }
        if (const HWND descendant =
                find_descendant_by_class(child, expected_class);
            descendant != nullptr) {
            return descendant;
        }
    }
    return nullptr;
}

void collect_descendants_by_class(const HWND parent,
                                  const wchar_t* expected_class,
                                  std::vector<HWND>& matches) {
    for (HWND child = GetWindow(parent, GW_CHILD); child != nullptr;
         child = GetWindow(child, GW_HWNDNEXT)) {
        wchar_t class_name[128] = {};
        if (GetClassNameW(child, class_name,
                          static_cast<int>(std::size(class_name))) != 0 &&
            _wcsicmp(class_name, expected_class) == 0) {
            matches.push_back(child);
        }
        collect_descendants_by_class(child, expected_class, matches);
    }
}

// Read a control's text from a window owned by ANOTHER process.
//
// GetWindowText*/GetDlgItemText* only send WM_GETTEXT when the target
// window belongs to the calling process. For a window in another process
// they return whatever caption the window manager itself has cached --
// documented Win32 behaviour ("If the target window is owned by another
// process and does not have a caption, the return value is a null string"),
// and Wine implements it the same way: user32 reads the wineserver's stored
// window text instead of sending any message. That stored text is only ever
// written by DefWindowProc's WM_SETTEXT path, which Static and Button reach,
// but Edit/ComboBox/ComboBoxEx32 do not -- they consume WM_SETTEXT in their
// own window procs and keep the string in a private buffer. So a
// cross-process GetWindowTextA on an Edit ALWAYS reads back empty no matter
// what it actually contains. WM_GETTEXT, by contrast, is a system message
// the window manager marshals across process boundaries, so sending it
// explicitly is the only correct way for this harness to read a control's
// text out of WORD1.
int read_control_text_ansi(const HWND control, char* const buffer,
                           const int buffer_size) {
    if (buffer == nullptr || buffer_size <= 0) {
        return 0;
    }
    buffer[0] = '\0';
    if (control == nullptr) {
        return 0;
    }
    return static_cast<int>(SendMessageA(control, WM_GETTEXT,
                                         static_cast<WPARAM>(buffer_size),
                                         reinterpret_cast<LPARAM>(buffer)));
}

// Diagnostic helper for --roundtrip: dump every descendant of a dialog with
// its control id, class, visibility and text, recursively. Used only on the
// Save As failure paths, so a broken run reports the live control tree
// instead of just a message. Both texts are printed on purpose: cachedText
// is what GetWindowTextW returns cross-process (the window manager's stored
// caption -- empty for every control that owns its own text) and wmGetText
// is the control's real content via WM_GETTEXT. The gap between those two
// columns is exactly what made rounds 1-2 of this work look blocked; see
// read_control_text_ansi above and task-1-report.md in
// docs/superpowers/sdd/2026-08-25-doc-roundtrip/.
void dump_dialog_tree_diagnostic(const HWND parent, const int depth = 0) {
    for (HWND child = GetWindow(parent, GW_CHILD); child != nullptr;
         child = GetWindow(child, GW_HWNDNEXT)) {
        wchar_t class_name[128] = {};
        wchar_t text[256] = {};
        GetClassNameW(child, class_name, static_cast<int>(std::size(class_name)));
        GetWindowTextW(child, text, static_cast<int>(std::size(text)));
        char class_ansi[128] = {};
        char text_ansi[256] = {};
        WideCharToMultiByte(CP_ACP, 0, class_name, -1, class_ansi,
                            static_cast<int>(sizeof(class_ansi)), nullptr,
                            nullptr);
        WideCharToMultiByte(CP_ACP, 0, text, -1, text_ansi,
                            static_cast<int>(sizeof(text_ansi)), nullptr,
                            nullptr);
        for (int i = 0; i < depth; ++i) {
            std::cerr << "  ";
        }
        char live_text[256] = {};
        read_control_text_ansi(child, live_text,
                               static_cast<int>(std::size(live_text)));
        std::cerr << "id=" << GetDlgCtrlID(child) << " hwnd=" << child
                  << " class='" << class_ansi << "' cachedText='" << text_ansi
                  << "' wmGetText='" << live_text
                  << "' visible=" << IsWindowVisible(child)
                  << " enabled=" << IsWindowEnabled(child) << '\n';
        dump_dialog_tree_diagnostic(child, depth + 1);
    }
}

HWND wait_for_window(const HANDLE process, const DWORD process_id,
                     const wchar_t* class_name,
                     const wchar_t* caption_fragment,
                     const DWORD timeout_ms) {
    const ULONGLONG deadline = GetTickCount64() + timeout_ms;
    do {
        if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0) {
            return nullptr;
        }
        if (const HWND window = find_process_window(
                process_id, class_name, caption_fragment);
            window != nullptr) {
            return window;
        }
        Sleep(50);
    } while (GetTickCount64() < deadline);
    return nullptr;
}

bool wait_for_window_to_close(const HANDLE process, const DWORD process_id,
                              const wchar_t* class_name,
                              const DWORD timeout_ms) {
    const ULONGLONG deadline = GetTickCount64() + timeout_ms;
    do {
        if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0) {
            return false;
        }
        if (find_process_window(process_id, class_name, nullptr) == nullptr) {
            return true;
        }
        Sleep(50);
    } while (GetTickCount64() < deadline);
    return false;
}

bool control_has_class(const HWND dialog, const int id,
                       const wchar_t* expected) {
    const HWND control = GetDlgItem(dialog, id);
    wchar_t class_name[64] = {};
    return control != nullptr &&
           GetClassNameW(control, class_name,
                         static_cast<int>(std::size(class_name))) != 0 &&
           _wcsicmp(class_name, expected) == 0;
}

std::size_t count_dark_client_pixels(const HWND window,
                                     const int y_first = 0,
                                     const int y_limit = 120) {
    RECT client{};
    if (window == nullptr || !GetClientRect(window, &client)) {
        return 0;
    }
    const int width = (std::min)(client.right, static_cast<LONG>(800));
    const int top = (std::max)(0, y_first);
    const int bottom = (std::min)(client.bottom, static_cast<LONG>(y_limit));
    const int height = bottom - top;
    if (width <= 0 || height <= 0) {
        return 0;
    }

    const HDC source = GetDC(window);
    const HDC memory = source != nullptr ? CreateCompatibleDC(source) : nullptr;
    const HBITMAP bitmap = memory != nullptr
                               ? CreateCompatibleBitmap(source, width, height)
                               : nullptr;
    if (bitmap == nullptr) {
        if (memory != nullptr) {
            DeleteDC(memory);
        }
        if (source != nullptr) {
            ReleaseDC(window, source);
        }
        return 0;
    }

    const HGDIOBJ previous = SelectObject(memory, bitmap);
    BitBlt(memory, 0, 0, width, height, source, 0, top, SRCCOPY);
    SelectObject(memory, previous);

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    std::vector<DWORD> pixels(static_cast<std::size_t>(width) * height);
    const int rows = GetDIBits(memory, bitmap, 0, height, pixels.data(), &info,
                              DIB_RGB_COLORS);

    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(window, source);
    if (rows != height) {
        return 0;
    }

    std::size_t dark = 0;
    for (const DWORD pixel : pixels) {
        const BYTE blue = static_cast<BYTE>(pixel);
        const BYTE green = static_cast<BYTE>(pixel >> 8);
        const BYTE red = static_cast<BYTE>(pixel >> 16);
        if (red < 96 && green < 96 && blue < 96) {
            ++dark;
        }
    }
    return dark;
}

int longest_light_gap(const HWND window, const int x_first, const int x_limit,
                      const int y) {
    const HDC dc = GetDC(window);
    if (dc == nullptr) {
        return x_limit - x_first;
    }
    int longest = 0;
    int current = 0;
    for (int x = x_first; x < x_limit; ++x) {
        const COLORREF color = GetPixel(dc, x, y);
        const bool light = color == CLR_INVALID ||
            (GetRValue(color) > 160 && GetGValue(color) > 160 &&
             GetBValue(color) > 160);
        if (light) {
            longest = (std::max)(longest, ++current);
        } else {
            current = 0;
        }
    }
    ReleaseDC(window, dc);
    return longest;
}

bool post_keyboard_character(const HWND window, const wchar_t character) {
    UINT virtual_key = 0;
    if (character >= L'a' && character <= L'z') {
        virtual_key = static_cast<UINT>(character - L'a' + L'A');
    } else if (character >= L'0' && character <= L'9') {
        virtual_key = static_cast<UINT>(character);
    } else if (character == L' ') {
        virtual_key = VK_SPACE;
    } else if (character == L'\r') {
        virtual_key = VK_RETURN;
    } else {
        return false;
    }

    const UINT scan_code = MapVirtualKeyW(virtual_key, MAPVK_VK_TO_VSC);
    const LPARAM key_down =
        static_cast<LPARAM>(1u | (static_cast<DWORD>(scan_code) << 16));
    const LPARAM key_up = key_down | 0xC0000000;
    return PostMessageW(window, WM_KEYDOWN, virtual_key, key_down) &&
           PostMessageW(window, WM_KEYUP, virtual_key, key_up);
}

bool wait_for_zoom_state(const HANDLE process, const HWND window,
                         const bool zoomed, const DWORD timeout_ms) {
    const ULONGLONG deadline = GetTickCount64() + timeout_ms;
    do {
        if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0) {
            return false;
        }
        if ((IsZoomed(window) != FALSE) == zoomed) {
            return true;
        }
        Sleep(25);
    } while (GetTickCount64() < deadline);
    return false;
}

bool wait_for_gui_flag(const HANDLE process, const DWORD thread_id,
                       const DWORD flag, const bool set,
                       const DWORD timeout_ms) {
    const ULONGLONG deadline = GetTickCount64() + timeout_ms;
    do {
        if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0) {
            return false;
        }
        GUITHREADINFO gui{};
        gui.cbSize = sizeof(gui);
        if (GetGUIThreadInfo(thread_id, &gui) &&
            (((gui.flags & flag) != 0) == set)) {
            return true;
        }
        Sleep(25);
    } while (GetTickCount64() < deadline);
    return false;
}

bool wait_for_capture(const HANDLE process, const DWORD thread_id,
                      const HWND expected, const DWORD timeout_ms) {
    const ULONGLONG deadline = GetTickCount64() + timeout_ms;
    do {
        if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0) {
            return false;
        }
        GUITHREADINFO gui{};
        gui.cbSize = sizeof(gui);
        if (GetGUIThreadInfo(thread_id, &gui) &&
            gui.hwndCapture == expected) {
            return true;
        }
        Sleep(25);
    } while (GetTickCount64() < deadline);
    return false;
}

bool wait_for_focus(const HANDLE process, const DWORD thread_id,
                    const HWND expected, const DWORD timeout_ms) {
    const ULONGLONG deadline = GetTickCount64() + timeout_ms;
    do {
        if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0) {
            return false;
        }
        GUITHREADINFO gui{};
        gui.cbSize = sizeof(gui);
        if (GetGUIThreadInfo(thread_id, &gui) && gui.hwndFocus == expected) {
            return true;
        }
        Sleep(25);
    } while (GetTickCount64() < deadline);
    return false;
}

/* Diagnostic-only variant of wait_for_focus: same wait/timeout contract,
   but logs every distinct hwndFocus value it observes (fast 10ms poll)
   instead of only the final true/false. Built to test a specific
   hypothesis for Task 6 Bug 3 (docs/port-linux/03-comportamiento-
   word1-startup-blocked.md, junior-to-senior review): Wine 10.0's
   dlls/user32/combo.c CBRollUp() sends CBN_SELENDOK (which runs WORD1's
   whole SDM focus-restore chain, including the SetFocus(pane) in
   Opus/iconbar1.c's dlmDlgClick) and only THEN, after that call returns,
   hides the still-open listbox popup via NtUserShowWindow(hWndLBox,
   SW_HIDE) -- which can reassign OS focus as a side effect of hiding a
   focused window. If that hypothesis is right, this trace should show
   focus reaching the pane and then leaving again, not simply never
   arriving. */
bool wait_for_focus_traced(const HANDLE process, const DWORD thread_id,
                           const HWND expected, const DWORD timeout_ms) {
    const ULONGLONG deadline = GetTickCount64() + timeout_ms;
    const ULONGLONG start = GetTickCount64();
    HWND last = reinterpret_cast<HWND>(static_cast<UINT_PTR>(~0ULL));
    bool saw_expected = false;
    do {
        if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0) {
            return false;
        }
        GUITHREADINFO gui{};
        gui.cbSize = sizeof(gui);
        if (GetGUIThreadInfo(thread_id, &gui)) {
            if (gui.hwndFocus != last) {
                wchar_t class_name[64] = {};
                wchar_t caption[128] = {};
                GetClassNameW(gui.hwndFocus, class_name,
                              static_cast<int>(std::size(class_name)));
                GetWindowTextW(gui.hwndFocus, caption,
                               static_cast<int>(std::size(caption)));
                // Narrow before printing: std::cerr with wchar_t* is a
                // deleted overload, and wide iostreams assume 4-byte
                // wchar_t while these buffers are real 2-byte WCHAR.
                char class_name_ansi[128] = {};
                char caption_ansi[256] = {};
                WideCharToMultiByte(CP_ACP, 0, class_name, -1, class_name_ansi,
                                    static_cast<int>(sizeof(class_name_ansi)),
                                    nullptr, nullptr);
                WideCharToMultiByte(CP_ACP, 0, caption, -1, caption_ansi,
                                    static_cast<int>(sizeof(caption_ansi)),
                                    nullptr, nullptr);
                std::cerr << "  [focus-trace] t+" << (GetTickCount64() - start)
                          << "ms hwndFocus=" << gui.hwndFocus
                          << " class=" << class_name_ansi
                          << " caption='" << caption_ansi << "'";
                if (gui.hwndFocus == expected) {
                    std::cerr << " (== pane)";
                    saw_expected = true;
                }
                std::cerr << '\n';
                last = gui.hwndFocus;
            }
            if (gui.hwndFocus == expected) {
                return true;
            }
        }
        Sleep(10);
    } while (GetTickCount64() < deadline);
    if (saw_expected) {
        std::cerr << "  [focus-trace] pane focus was reached transiently but "
                     "did not hold through timeout\n";
    }
    return false;
}

bool make_foreground_and_focus(const HWND main_window, const HWND focus,
                               const DWORD target_thread_id) {
    const DWORD current_thread_id = GetCurrentThreadId();
    const HWND old_foreground = GetForegroundWindow();
    const DWORD foreground_thread_id =
        old_foreground != nullptr
            ? GetWindowThreadProcessId(old_foreground, nullptr)
            : 0;
    const BOOL attached_foreground =
        foreground_thread_id != 0 && foreground_thread_id != current_thread_id &&
                foreground_thread_id != target_thread_id
            ? AttachThreadInput(current_thread_id, foreground_thread_id, TRUE)
            : FALSE;
    const BOOL attached_target =
        current_thread_id != target_thread_id
            ? AttachThreadInput(current_thread_id, target_thread_id, TRUE)
            : FALSE;
    ShowWindow(main_window, SW_RESTORE);
    BringWindowToTop(main_window);
    SetForegroundWindow(main_window);
    SetActiveWindow(main_window);
    const bool focused = SetFocus(focus) != nullptr || GetFocus() == focus;
    if (attached_target) {
        AttachThreadInput(current_thread_id, target_thread_id, FALSE);
    }
    if (attached_foreground) {
        AttachThreadInput(current_thread_id, foreground_thread_id, FALSE);
    }
    Sleep(100);
    if (focused && GetForegroundWindow() == main_window) {
        return true;
    }

    RECT client{};
    POINT activation_point{};
    if (!GetClientRect(focus, &client)) {
        return false;
    }
    activation_point.x = (std::min)(client.right - 1, static_cast<LONG>(20));
    activation_point.y = (std::min)(client.bottom - 1, static_cast<LONG>(10));
    if (activation_point.x < 0 || activation_point.y < 0 ||
        !ClientToScreen(focus, &activation_point) ||
        !SetCursorPos(activation_point.x, activation_point.y)) {
        return false;
    }
    std::array<INPUT, 2> click{};
    click[0].type = INPUT_MOUSE;
    click[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    click[1].type = INPUT_MOUSE;
    click[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    if (SendInput(static_cast<UINT>(click.size()), click.data(),
                  sizeof(INPUT)) != click.size()) {
        return false;
    }
    Sleep(200);
    GUITHREADINFO gui{};
    gui.cbSize = sizeof(gui);
    return GetForegroundWindow() == main_window &&
           GetGUIThreadInfo(target_thread_id, &gui) && gui.hwndFocus == focus;
}

bool send_virtual_key(const WORD virtual_key) {
    std::array<INPUT, 2> input{};
    input[0].type = INPUT_KEYBOARD;
    input[0].ki.wVk = virtual_key;
    input[1] = input[0];
    input[1].ki.dwFlags = KEYEVENTF_KEYUP;
    return SendInput(static_cast<UINT>(input.size()), input.data(),
                     sizeof(INPUT)) == input.size();
}

bool send_control_key(const WORD virtual_key) {
    std::array<INPUT, 4> input{};
    input[0].type = INPUT_KEYBOARD;
    input[0].ki.wVk = VK_CONTROL;
    input[1].type = INPUT_KEYBOARD;
    input[1].ki.wVk = virtual_key;
    input[2] = input[1];
    input[2].ki.dwFlags = KEYEVENTF_KEYUP;
    input[3] = input[0];
    input[3].ki.dwFlags = KEYEVENTF_KEYUP;
    /* Word's Win16 key loop samples modifier state as each dequeued message
       is handled.  Deliver the transitions separately to model a real held
       Ctrl key instead of allowing the whole synthetic chord to be released
       before the target thread samples it. */
    for (INPUT& event : input) {
        if (SendInput(1, &event, sizeof(INPUT)) != 1) {
            return false;
        }
        Sleep(35);
    }
    return true;
}

bool execute_control_shortcut(const HWND pane, const WORD virtual_key) {
    return SendMessageW(pane, kWmOpusX64QuerySelection, 80,
                        kKcControl | virtual_key) != 0;
}

bool send_control_shift_key(const WORD virtual_key) {
    std::array<INPUT, 6> input{};
    input[0].type = INPUT_KEYBOARD;
    input[0].ki.wVk = VK_CONTROL;
    input[1].type = INPUT_KEYBOARD;
    input[1].ki.wVk = VK_SHIFT;
    input[2].type = INPUT_KEYBOARD;
    input[2].ki.wVk = virtual_key;
    input[3] = input[2];
    input[3].ki.dwFlags = KEYEVENTF_KEYUP;
    input[4] = input[1];
    input[4].ki.dwFlags = KEYEVENTF_KEYUP;
    input[5] = input[0];
    input[5].ki.dwFlags = KEYEVENTF_KEYUP;
    return SendInput(static_cast<UINT>(input.size()), input.data(),
                     sizeof(INPUT)) == input.size();
}

bool send_physical_text(const wchar_t* text) {
    const int length = lstrlenW(text);
    for (int index = 0; index < length; ++index) {
        const wchar_t character = text[index];
        WORD virtual_key = 0;
        if (character >= L'a' && character <= L'z') {
            virtual_key = static_cast<WORD>(character - L'a' + L'A');
        } else if (character >= L'0' && character <= L'9') {
            virtual_key = static_cast<WORD>(character);
        } else if (character == L' ') {
            virtual_key = VK_SPACE;
        } else if (character == L'\r') {
            virtual_key = VK_RETURN;
        } else {
            return false;
        }
        if (!send_virtual_key(virtual_key)) {
            return false;
        }
        Sleep(character == L'\r' ? 300 : 50);
    }
    return true;
}

bool send_mouse_button(const DWORD flags) {
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = flags;
    return SendInput(1, &input, sizeof(input)) == 1;
}

bool choose_combo_item_with_mouse(const HWND combo, const LRESULT index) {
    RECT combo_rectangle{};
    if (combo == nullptr || index < 0 ||
        !GetWindowRect(combo, &combo_rectangle)) {
        std::cerr << "choose_combo_item_with_mouse: bad combo/index\n";
        return false;
    }

    const POINT arrow{
        combo_rectangle.right - 8,
        combo_rectangle.top +
            (combo_rectangle.bottom - combo_rectangle.top) / 2};
    if (!SetCursorPos(arrow.x, arrow.y) ||
        !send_mouse_button(MOUSEEVENTF_LEFTDOWN) ||
        !send_mouse_button(MOUSEEVENTF_LEFTUP)) {
        std::cerr << "choose_combo_item_with_mouse: could not click the "
                     "dropdown arrow\n";
        return false;
    }
    Sleep(250);
    SendMessageW(combo, CB_SHOWDROPDOWN, TRUE, 0);
    Sleep(250);

    COMBOBOXINFO info{};
    info.cbSize = sizeof(info);
    RECT list_rectangle{};
    const LRESULT item_height = SendMessageW(combo, CB_GETITEMHEIGHT, 0, 0);
    const BOOL got_info = GetComboBoxInfo(combo, &info);
    const BOOL list_visible =
        got_info && info.hwndList != nullptr && IsWindowVisible(info.hwndList);
    if (!got_info || info.hwndList == nullptr ||
        item_height <= 0 || !list_visible ||
        !GetWindowRect(info.hwndList, &list_rectangle)) {
        std::cerr << "choose_combo_item_with_mouse: dropdown popup did not "
                     "open -- got_info=" << got_info
                  << " hwndList=" << info.hwndList
                  << " item_height=" << item_height
                  << " list_visible=" << list_visible << '\n';
        return false;
    }

    const LRESULT requested_top =
        (std::max)(0LL, static_cast<long long>(index) - 2);
    SendMessageW(info.hwndList, LB_SETTOPINDEX, requested_top, 0);
    UpdateWindow(info.hwndList);
    RECT item_rectangle{};
    if (SendMessageW(info.hwndList, LB_GETITEMRECT, index,
                     reinterpret_cast<LPARAM>(&item_rectangle)) == LB_ERR) {
        return false;
    }
    POINT choice{
        item_rectangle.left +
            (item_rectangle.right - item_rectangle.left) / 2,
        item_rectangle.top +
            (item_rectangle.bottom - item_rectangle.top) / 2};
    if (!ClientToScreen(info.hwndList, &choice) ||
        choice.x < list_rectangle.left || choice.x >= list_rectangle.right ||
        choice.y < list_rectangle.top || choice.y >= list_rectangle.bottom ||
        !SetCursorPos(choice.x, choice.y) ||
        !send_mouse_button(MOUSEEVENTF_LEFTDOWN) ||
        !send_mouse_button(MOUSEEVENTF_LEFTUP)) {
        return false;
    }
    Sleep(350);
    return SendMessageW(combo, CB_GETCURSEL, 0, 0) == index;
}

bool window_is_responsive(const HANDLE process, const HWND window) {
    DWORD_PTR message_result = 0;
    return WaitForSingleObject(process, 0) != WAIT_OBJECT_0 &&
           !IsHungAppWindow(window) &&
           SendMessageTimeoutW(window, WM_NULL, 0, 0, SMTO_ABORTIFHUNG,
                               2000, &message_result) != 0;
}

int fail(PROCESS_INFORMATION& process, const int code,
         const char* message) {
    std::cerr << message << '\n';
    if (process.hProcess != nullptr &&
        WaitForSingleObject(process.hProcess, 0) != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, static_cast<UINT>(code));
        WaitForSingleObject(process.hProcess, 2000);
    }
    if (process.hThread != nullptr) {
        CloseHandle(process.hThread);
    }
    if (process.hProcess != nullptr) {
        CloseHandle(process.hProcess);
    }
    return code;
}

}  // namespace

extern "C" int wmain(const int argument_count, wchar_t** arguments) {
    SetProcessDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (argument_count < 2 || argument_count > 3) {
        std::cerr <<
            "usage: opus_word1_ui_test WORD1.exe "
            "[--typing|--interaction|--selection|--caret|--formatting|--color|"
            "--font-typing|--clipboard|--about|--save-as|--roundtrip|"
            "--rich-format]\n";
        return 1;
    }
    const bool typing_mode =
        argument_count == 3 && lstrcmpW(arguments[2], L"--typing") == 0;
    const bool interaction_mode =
        argument_count == 3 &&
        lstrcmpW(arguments[2], L"--interaction") == 0;
    const bool selection_mode =
        argument_count == 3 &&
        lstrcmpW(arguments[2], L"--selection") == 0;
    const bool caret_mode =
        argument_count == 3 &&
        lstrcmpW(arguments[2], L"--caret") == 0;
    const bool formatting_mode =
        argument_count == 3 &&
        lstrcmpW(arguments[2], L"--formatting") == 0;
    const bool color_mode =
        argument_count == 3 &&
        lstrcmpW(arguments[2], L"--color") == 0;
    const bool font_typing_mode =
        argument_count == 3 &&
        lstrcmpW(arguments[2], L"--font-typing") == 0;
    const bool about_mode =
        argument_count == 3 &&
        lstrcmpW(arguments[2], L"--about") == 0;
    const bool clipboard_mode =
        argument_count == 3 &&
        lstrcmpW(arguments[2], L"--clipboard") == 0;
    const bool save_as_mode =
        argument_count == 3 &&
        lstrcmpW(arguments[2], L"--save-as") == 0;
    const bool roundtrip_mode =
        argument_count == 3 &&
        lstrcmpW(arguments[2], L"--roundtrip") == 0;
    const bool rich_format_mode =
        argument_count == 3 &&
        lstrcmpW(arguments[2], L"--rich-format") == 0;
    if (argument_count == 3 && !typing_mode && !interaction_mode &&
        !selection_mode && !caret_mode && !formatting_mode && !color_mode &&
        !font_typing_mode && !clipboard_mode && !about_mode &&
        !save_as_mode && !roundtrip_mode && !rich_format_mode) {
        std::cerr << "unknown test mode\n";
        return 1;
    }

    // Deliberately not std::wstring: its length-computing constructors and
    // operator+ go through the same glibc char_traits<wchar_t> machinery as
    // the wcscmp/wcslen bug fixed above (4-byte wchar_t reads against this
    // TU's real 2-byte Win32 WCHAR data) -- confirmed empirically 2026-08-14
    // (std::wstring(arguments[1]).size() came back 22 against a real
    // lstrlenW() of 32). lstrlenW/lstrcpyW/lstrcatW are Win32-native and
    // width-correct. See docs/port-linux/01-diagnostico-heap-corruption-arranque.md
    // §23-24.
    const int application_name_length = lstrlenW(arguments[1]);
    if (application_name_length >= MAX_PATH) {
        std::cerr << "WORD1 path too long for the command line buffer\n";
        return 2;
    }
    wchar_t command_line[MAX_PATH + 4] = {};
    command_line[0] = L'"';
    lstrcpyW(command_line + 1, arguments[1]);
    lstrcatW(command_line, L"\"");
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(arguments[1], command_line, nullptr, nullptr,
                        FALSE, 0, nullptr, nullptr, &startup, &process)) {
        std::cerr << "CreateProcessW failed: " << GetLastError() << '\n';
        return 2;
    }

    const HWND main_window = wait_for_window(
        process.hProcess, process.dwProcessId, nullptr,
        L"Microsoft Word - Document1", 8000);
    if (main_window == nullptr) {
        return fail(process, 3, "WORD1 main window did not appear");
    }
    // The same zeroed PROCESS_INFORMATION described above also leaves
    // hProcess null, so the TerminateProcess in fail() and in every mode's
    // exit path is a silent no-op: WORD1 outlives the harness, keeps the
    // stdout pipe it inherited, and ctest reports Timeout even for a run the
    // harness itself finished with 0. That stayed invisible while WORD1
    // crashed on its own during startup. The window found just above belongs
    // to the real process, so recover the PID from it and open a handle;
    // every teardown and wait downstream then works unchanged, and the
    // window searches regain the exact-PID filter they normally prefer.
    if (process.hProcess == nullptr) {
        DWORD window_process_id = 0;
        GetWindowThreadProcessId(main_window, &window_process_id);
        if (window_process_id != 0) {
            const HANDLE recovered =
                OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_INFORMATION |
                                SYNCHRONIZE,
                            FALSE, window_process_id);
            if (recovered != nullptr) {
                process.hProcess = recovered;
                process.dwProcessId = window_process_id;
            }
        }
    }
    if (save_as_mode) {
        Sleep(1000);
        if (!PostMessageW(main_window, kWmCommand, kFileSaveAs, 0)) {
            return fail(process, 69, "could not send File Save As");
        }
        const HWND save_as_dialog = wait_for_window(
            process.hProcess, process.dwProcessId, L"OpusSdmDialog",
            L"Save As", 5000);
        if (save_as_dialog == nullptr) {
            std::cerr << "Save As stage="
                      << reinterpret_cast<INT_PTR>(GetPropA(
                             main_window, "OpusX64SaveAsStage"))
                      << '\n';
            log_process_windows(process.dwProcessId);
            return fail(process, 70, "File Save As dialog did not appear");
        }
        if (!control_has_class(save_as_dialog, 2, L"Button") ||
            !window_is_responsive(process.hProcess, save_as_dialog)) {
            return fail(process, 71,
                        "File Save As dialog did not finish initializing");
        }
        if (!PostMessageW(save_as_dialog, kWmCommand, 2, 0) ||
            !wait_for_window_to_close(process.hProcess, process.dwProcessId,
                                      L"OpusSdmDialog", 5000) ||
            !window_is_responsive(process.hProcess, main_window)) {
            return fail(process, 72,
                        "File Save As dialog did not cancel cleanly");
        }
        TerminateProcess(process.hProcess, 0);
        WaitForSingleObject(process.hProcess, 2000);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return 0;
    }
    if (roundtrip_mode) {
        // Step 2 (plan docs/superpowers/plans/2026-08-25-doc-roundtrip.md):
        // type a known line into Document1 and snapshot it via the
        // opus_x64 query interface before anything is saved.
        DWORD ignored_process_id = 0;
        const DWORD thread_id =
            GetWindowThreadProcessId(main_window, &ignored_process_id);
        const HWND pane = find_descendant_by_class(main_window, L"OpusWwd");
        if (pane == nullptr) {
            return fail(process, 78,
                        "roundtrip test could not find the document pane");
        }
        if (!make_foreground_and_focus(main_window, pane, thread_id)) {
            return fail(process, 79,
                        "roundtrip test could not focus the document pane");
        }
        if (!send_physical_text(L"roundtrip line one")) {
            return fail(process, 80,
                        "roundtrip test could not type the sample line");
        }
        Sleep(500);

        const LRESULT cp_mac =
            SendMessageW(pane, kWmOpusX64QuerySelection, 41, 0);
        if (cp_mac < 10) {
            return fail(process, 81,
                        "roundtrip test typed too little text to snapshot");
        }
        // Plain LRESULT storage, not std::wstring: these are raw byte
        // values from query 69, not WCHAR text -- std::vector<LRESULT> is
        // unrelated to the wide-char rule above (see the comment near
        // lstrcmpW usage at the top of this file).
        std::vector<LRESULT> snapshot_bytes(static_cast<std::size_t>(cp_mac));
        for (LRESULT cp = 0; cp < cp_mac; ++cp) {
            const LRESULT byte_value =
                SendMessageW(pane, kWmOpusX64QuerySelection, 69, cp);
            if (byte_value == -1) {
                return fail(process, 82,
                            "roundtrip snapshot query 69 returned -1");
            }
            snapshot_bytes[static_cast<std::size_t>(cp)] = byte_value;
        }
        const LRESULT ftc0 =
            SendMessageW(pane, kWmOpusX64QuerySelection, 51, 0);
        const LRESULT hps0 =
            SendMessageW(pane, kWmOpusX64QuerySelection, 52, 0);
        const LRESULT dyp0 =
            SendMessageW(pane, kWmOpusX64QuerySelection, 55, 0);
        std::cerr << "roundtrip snapshot cpMac=" << cp_mac
                  << " ftc0=" << ftc0 << " hps0=" << hps0 << " dyp0=" << dyp0
                  << '\n';

        // Step 3: choose an on-disk target and delete it if it is already
        // there -- OFN_OVERWRITEPROMPT is on for the real Save As dialog
        // (opus_sdm_runtime.cpp's run_word95_common_file_dialog), and a
        // stray file left by a previous run would pop a "Confirm Save As"
        // Yes/No prompt this mode does not expect at this point. 8.3-safe
        // name so it round-trips through the legacy Win95 staging-file
        // alias path unmodified.
        char temp_dir[MAX_PATH] = {};
        const DWORD temp_dir_length = GetTempPathA(
            static_cast<DWORD>(std::size(temp_dir)), temp_dir);
        if (temp_dir_length == 0 || temp_dir_length >= std::size(temp_dir)) {
            return fail(process, 83,
                        "roundtrip test could not resolve GetTempPathA");
        }
        char ansi_path[MAX_PATH] = {};
        wsprintfA(ansi_path, "%soprt%04lx.doc", temp_dir,
                  static_cast<unsigned long>(process.dwProcessId & 0xFFFFu));
        DeleteFileA(ansi_path);
        // Kept for Task 2, which continues this same mode with a second
        // WORD1 process launched against this exact path on its command
        // line.
        wchar_t wide_path[MAX_PATH] = {};
        MultiByteToWideChar(CP_ACP, 0, ansi_path, -1, wide_path,
                            static_cast<int>(std::size(wide_path)));
        std::cerr << "roundtrip target path='" << ansi_path
                  << "' wideLength=" << lstrlenW(wide_path) << '\n';

        // Step 4: File > Save As, driving the real #32770 common dialog --
        // the decoy OpusSdmDialog forwarding hook from Task 5 exists for
        // its own OK/Cancel buttons, not as a substitute for this window
        // (docs/port-linux/03-comportamiento-word1-startup-blocked.md §7).
        if (!PostMessageW(main_window, kWmCommand, kFileSaveAs, 0)) {
            return fail(process, 84,
                        "could not send File Save As for roundtrip");
        }
        const HWND save_dialog = wait_for_window(
            process.hProcess, process.dwProcessId, L"#32770", L"Save As",
            5000);
        if (save_dialog == nullptr) {
            log_process_windows(process.dwProcessId);
            return fail(process, 85,
                        "roundtrip Save As dialog (#32770) did not appear");
        }
        if (!window_is_responsive(process.hProcess, save_dialog)) {
            DeleteFileA(ansi_path);
            return fail(process, 86,
                        "roundtrip Save As dialog did not finish initializing");
        }

        {
            wchar_t dialog_caption[256] = {};
            GetWindowTextW(save_dialog, dialog_caption,
                           static_cast<int>(std::size(dialog_caption)));
            char dialog_caption_ansi[256] = {};
            WideCharToMultiByte(CP_ACP, 0, dialog_caption, -1,
                                dialog_caption_ansi,
                                static_cast<int>(sizeof(dialog_caption_ansi)),
                                nullptr, nullptr);
            std::cerr << "roundtrip found save_dialog=" << save_dialog
                      << " caption='" << dialog_caption_ansi << "'\n";
        }
        // Set the file name. The brief's edt1 (0x0480) does not exist in
        // this dialog: run_word95_common_file_dialog asks for OFN_EXPLORER,
        // so Wine builds the Explorer-style template, whose filename field
        // is cmb13 (0x047C) -- an editable ComboBoxEx32 -> ComboBox -> Edit
        // composite. A plain WM_SETTEXT on that ComboBoxEx32 is enough: the
        // window manager marshals WM_SETTEXT across the process boundary,
        // and ComboBoxEx32 forwards it down to the inner Edit, which is
        // exactly where comdlg32 reads the name from when Save is pressed.
        //
        // Read the value back with WM_GETTEXT, never GetDlgItemTextA: see
        // read_control_text_ansi above for why a cross-process
        // GetWindowText on an Edit/ComboBox always reports empty (that
        // false negative is what made rounds 1-2 of this work look
        // blocked -- task-1-report.md).
        const HWND filename_field = GetDlgItem(save_dialog, 0x047C);
        if (filename_field == nullptr) {
            std::cerr << "roundtrip dialog tree dump:\n";
            dump_dialog_tree_diagnostic(save_dialog);
            DeleteFileA(ansi_path);
            return fail(process, 87,
                        "roundtrip Save As dialog has no cmb13 filename "
                        "field");
        }
        SendMessageA(filename_field, WM_SETTEXT, 0,
                     reinterpret_cast<LPARAM>(ansi_path));
        char filename_check[MAX_PATH] = {};
        read_control_text_ansi(filename_field, filename_check,
                               static_cast<int>(std::size(filename_check)));
        std::cerr << "roundtrip filename field=" << filename_field
                  << " reads back '" << filename_check << "'\n";
        if (lstrcmpiA(filename_check, ansi_path) != 0) {
            std::cerr << "roundtrip dialog tree dump:\n";
            dump_dialog_tree_diagnostic(save_dialog);
            DeleteFileA(ansi_path);
            return fail(process, 87,
                        "roundtrip could not set the Save As filename");
        }
        if (!PostMessageW(save_dialog, kWmCommand, IDOK, 0)) {
            DeleteFileA(ansi_path);
            return fail(process, 88,
                        "could not accept the roundtrip Save As dialog");
        }

        const ULONGLONG save_deadline = GetTickCount64() + 8000;
        bool confirmed_overwrite = false;
        bool file_ready = false;
        DWORD saved_file_size = 0;
        while (GetTickCount64() < save_deadline) {
            if (GetFileAttributesA(ansi_path) != INVALID_FILE_ATTRIBUTES) {
                const HANDLE probe = CreateFileA(
                    ansi_path, GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (probe != INVALID_HANDLE_VALUE) {
                    saved_file_size = GetFileSize(probe, nullptr);
                    CloseHandle(probe);
                    if (saved_file_size != INVALID_FILE_SIZE &&
                        saved_file_size > 128) {
                        file_ready = true;
                        break;
                    }
                }
            }
            if (!confirmed_overwrite) {
                const HWND confirm_dialog = find_process_window(
                    process.dwProcessId, L"#32770", L"Confirm Save As");
                if (confirm_dialog != nullptr &&
                    confirm_dialog != save_dialog) {
                    PostMessageW(confirm_dialog, kWmCommand, IDYES, 0);
                    confirmed_overwrite = true;
                }
            }
            Sleep(100);
        }
        if (!file_ready) {
            // The brief asks for the exact dialog class/caption on this
            // path. Word reports save failures through a plain MessageBox
            // (class #32770, caption "Microsoft Word"), whose reason lives
            // in a Static child -- so dump every #32770 still standing with
            // its contents, not just the window list. That is how the one
            // real failure seen here was identified ("Not a valid file
            // name": Word 1.1a's FntSz rejects any path component longer
            // than 8.3, and the Win95 shim stages saves through
            // <WORD1.exe dir>\W95TEMP, so a checkout directory such as
            // "msword-rt" makes every Save As fail -- see task-1-report.md).
            std::cerr << "roundtrip save_dialog alive="
                      << IsWindow(save_dialog) << '\n';
            log_process_windows(process.dwProcessId);
            for (HWND top = GetTopWindow(nullptr); top != nullptr;
                 top = GetWindow(top, GW_HWNDNEXT)) {
                wchar_t top_class[128] = {};
                GetClassNameW(top, top_class,
                              static_cast<int>(std::size(top_class)));
                if (lstrcmpW(top_class, L"#32770") != 0 ||
                    !IsWindowVisible(top)) {
                    continue;
                }
                char top_caption[256] = {};
                read_control_text_ansi(
                    top, top_caption,
                    static_cast<int>(std::size(top_caption)));
                std::cerr << "roundtrip leftover #32770 hwnd=" << top
                          << " caption='" << top_caption << "'\n";
                dump_dialog_tree_diagnostic(top, 1);
            }
            DeleteFileA(ansi_path);
            return fail(process, 89,
                        "roundtrip Save As did not produce the target .doc "
                        "file");
        }
        std::cerr << "roundtrip saved '" << ansi_path
                  << "' size=" << saved_file_size << " bytes\n";

        // Step 5: tear down process 1. A successful save must have marked
        // the document clean; any #32770 popping up after File Exit means
        // it did not, and this mode fails loudly instead of clicking
        // through it (see the plan's Global Constraints).
        if (!PostMessageW(main_window, kWmCommand, kFileExit, 0)) {
            DeleteFileA(ansi_path);
            return fail(process, 90,
                        "could not send File Exit after roundtrip save");
        }
        const HWND save_changes_prompt = wait_for_window(
            process.hProcess, process.dwProcessId, L"#32770", nullptr, 3000);
        if (save_changes_prompt != nullptr) {
            wchar_t prompt_caption[256] = {};
            GetWindowTextW(save_changes_prompt, prompt_caption,
                          static_cast<int>(std::size(prompt_caption)));
            char prompt_caption_ansi[256] = {};
            WideCharToMultiByte(CP_ACP, 0, prompt_caption, -1,
                                prompt_caption_ansi,
                                static_cast<int>(sizeof(prompt_caption_ansi)),
                                nullptr, nullptr);
            std::cerr << "roundtrip unexpected dialog after File Exit "
                        "caption='" << prompt_caption_ansi << "'\n";
            DeleteFileA(ansi_path);
            return fail(process, 91,
                        "File Exit prompted a dialog after the roundtrip "
                        "save (document was not marked clean)");
        }
        if (WaitForSingleObject(process.hProcess, 5000) != WAIT_OBJECT_0) {
            TerminateProcess(process.hProcess, 0);
            WaitForSingleObject(process.hProcess, 2000);
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);

        // Task 2 (plan docs/superpowers/plans/2026-08-25-doc-roundtrip.md):
        // launch a second, independent WORD1 process against the .doc
        // process 1 just saved, and compare what it reads back against the
        // pre-save snapshot above. Same CreateProcessW pattern as process 1
        // at the top of wmain, but with wide_path appended as a second
        // command-line argument.
        //
        // Deliberately NOT quoted, unlike the exe path: Opus's own
        // command-line parser (Opus/initwin.c FInitPart1) never strips
        // quotes -- it just splits lpszCmdLine on raw whitespace and hands
        // each token straight to SzToSt/DocOpenStDof (Opus/init2.c
        // FInitArgs). A quoted path with no embedded space is passed
        // through WITH the literal quote characters attached, which
        // DocOpenStDof then fails to find -- and, worse, that failure is
        // not silent: dofCmdNewOpen does not set dofNoErrors, so
        // DocOpenStDof's LReturn: unconditionally calls ErrorEid(eidCantOpen,
        // ...), popping a synchronous, modal "Cannot open document"
        // MessageBoxA (caption == szAppTitle == "Microsoft Word", same
        // caption FInitPart2's CreateWindow used for vhwndApp itself)
        // *during FInitPart2*, before EndStartup/ElNewFile ever run --
        // confirmed empirically: a quoted argument left the harness's
        // wait loop below timing out against that stuck error box (which
        // EnumWindows can return ahead of the real app frame), and the
        // subsequent File > Open fallback then posted WM_COMMAND at the
        // wrong window and could never get anywhere. wide_path has no
        // spaces (GetTempPathA-based), so leaving it unquoted lets this
        // custom parser's naive whitespace split find it as one token,
        // unmodified.
        // arguments[1]'s length is already bounds-checked once, at the top
        // of wmain, before the first CreateProcessW call.
        wchar_t command_line2[(MAX_PATH * 2) + 8] = {};
        command_line2[0] = L'"';
        lstrcpyW(command_line2 + 1, arguments[1]);
        lstrcatW(command_line2, L"\" ");
        lstrcatW(command_line2, wide_path);
        STARTUPINFOW startup2{};
        startup2.cb = sizeof(startup2);
        PROCESS_INFORMATION process2{};
        if (!CreateProcessW(arguments[1], command_line2, nullptr, nullptr,
                            FALSE, 0, nullptr, nullptr, &startup2,
                            &process2)) {
            std::cerr << "roundtrip CreateProcessW (process 2) failed: "
                      << GetLastError() << '\n';
            DeleteFileA(ansi_path);
            return fail(process2, 92, "could not launch process 2");
        }

        // Base name to watch for in process 2's title bar, e.g. "oprt0124"
        // out of ansi_path's "...\\oprt0124.doc" -- extracted from wide_path
        // rather than recomputed from the pid, so this matches whatever
        // Word actually shows even if it normalizes the path. Manual scan,
        // not wcsrchr: see the wide_contains comment above on why this
        // TU's real 2-byte WCHAR must not go through glibc's wide string
        // functions.
        const wchar_t* base_name_start = wide_path;
        for (const wchar_t* p = wide_path; *p != L'\0'; ++p) {
            if (*p == L'\\') {
                base_name_start = p + 1;
            }
        }
        const wchar_t* base_name_end =
            base_name_start + lstrlenW(base_name_start);
        for (const wchar_t* p = base_name_start; *p != L'\0'; ++p) {
            if (*p == L'.') {
                base_name_end = p;
                break;
            }
        }
        wchar_t base_name[64] = {};
        std::size_t base_name_length =
            static_cast<std::size_t>(base_name_end - base_name_start);
        if (base_name_length > std::size(base_name) - 1) {
            base_name_length = std::size(base_name) - 1;
        }
        for (std::size_t i = 0; i < base_name_length; ++i) {
            base_name[i] = base_name_start[i];
        }
        char base_name_ansi[64] = {};
        WideCharToMultiByte(CP_ACP, 0, base_name, -1, base_name_ansi,
                            static_cast<int>(sizeof(base_name_ansi)), nullptr,
                            nullptr);
        std::cerr << "roundtrip process 2 watching for base name '"
                  << base_name_ansi << "'\n";

        // Step 1: wait up to 8s for process 2's main window to report a
        // title containing both "Microsoft Word" and the base name above
        // -- SetAppCaptionFromHwnd (Opus/wwchange.c) only appends the file
        // name once DocOpenStDof's FCreateMw has actually loaded it, so
        // this is a reliable signal that command-line open worked, not
        // just that the window exists. vhwndApp is a single fixed window
        // for the life of the process (confirmed via Opus/init2.c,
        // Opus/open.c), so polling the SAME handle here (rather than
        // re-searching) is correct. Search by class "OpusApp", not a null
        // class filter: a failed command-line open pops a MessageBoxA
        // (class #32770) whose caption is ALSO the bare "Microsoft Word"
        // app title (szAppTitle, same string CreateWindow used for
        // vhwndApp itself) -- a null-class search can match that box
        // instead of the real frame.
        HWND main_window2 = nullptr;
        bool command_line_open_worked = false;
        {
            const ULONGLONG deadline = GetTickCount64() + 8000;
            do {
                main_window2 = find_process_window(process2.dwProcessId,
                                                   L"OpusApp",
                                                   L"Microsoft Word");
                if (main_window2 != nullptr) {
                    wchar_t caption[512] = {};
                    GetWindowTextW(main_window2, caption,
                                   static_cast<int>(std::size(caption)));
                    if (wide_contains_i(caption, base_name)) {
                        command_line_open_worked = true;
                        break;
                    }
                }
                if (process2.hProcess != nullptr &&
                    WaitForSingleObject(process2.hProcess, 0) ==
                        WAIT_OBJECT_0) {
                    break;
                }
                Sleep(50);
            } while (GetTickCount64() < deadline);
        }
        if (main_window2 == nullptr) {
            log_process_windows(process2.dwProcessId);
            DeleteFileA(ansi_path);
            return fail(process2, 93, "process 2 main window did not appear");
        }
        // Same zeroed-PROCESS_INFORMATION workaround as process 1 above:
        // recover the PID/handle from the window we just found.
        if (process2.hProcess == nullptr) {
            DWORD window_process_id2 = 0;
            GetWindowThreadProcessId(main_window2, &window_process_id2);
            if (window_process_id2 != 0) {
                const HANDLE recovered2 = OpenProcess(
                    PROCESS_TERMINATE | PROCESS_QUERY_INFORMATION |
                        SYNCHRONIZE,
                    FALSE, window_process_id2);
                if (recovered2 != nullptr) {
                    process2.hProcess = recovered2;
                    process2.dwProcessId = window_process_id2;
                }
            }
        }
        DWORD ignored_process_id2 = 0;
        const DWORD thread_id2 =
            GetWindowThreadProcessId(main_window2, &ignored_process_id2);

        if (!command_line_open_worked) {
            std::cerr << "roundtrip process 2 command-line open did not "
                        "take effect (title still lacks the base name); "
                        "falling back to File > Open\n";
            // A failed command-line open leaves a modal "Cannot open
            // document" MessageBoxA (Opus/open.c DocOpenStDof's LReturn:,
            // eidCantOpen) up on screen. It fires from inside
            // FInitPart2/FInitArgs, before vfInitializing is cleared and
            // before ElNewFile creates the blank document, so nothing
            // past that point in startup has run yet -- including the
            // menu-command dispatch this fallback is about to use.
            // Dismiss it (its OK button is id 1 / IDOK, confirmed via a
            // dialog-tree dump) and wait for the normal idle
            // "Microsoft Word - Document1" state before sending File >
            // Open, exactly like every other mode in this file waits for
            // at the top of wmain.
            const HWND stray_error_box = find_process_window(
                process2.dwProcessId, L"#32770", L"Microsoft Word");
            if (stray_error_box != nullptr) {
                std::cerr << "roundtrip dismissing stray dialog hwnd="
                          << stray_error_box << " after failed "
                          "command-line open\n";
                dump_dialog_tree_diagnostic(stray_error_box);
                PostMessageW(stray_error_box, kWmCommand, IDOK, 0);
            }
            main_window2 = wait_for_window(process2.hProcess,
                                           process2.dwProcessId, L"OpusApp",
                                           L"Microsoft Word - Document1",
                                           5000);
            if (main_window2 == nullptr) {
                log_process_windows(process2.dwProcessId);
                DeleteFileA(ansi_path);
                return fail(process2, 108,
                            "process 2 did not reach the idle Document1 "
                            "state after a failed command-line open");
            }

            // Fall back to File > Open, driving the real #32770 dialog the
            // same way Task 1 drove Save As: same OFN_EXPLORER template,
            // same cmb13 (0x047C) ComboBoxEx32 filename field (see
            // run_word95_common_file_dialog in opus_sdm_runtime.cpp --
            // kIddOpen and kIddSaveAs share that code path and template).
            if (!PostMessageW(main_window2, kWmCommand, kFileOpen, 0)) {
                DeleteFileA(ansi_path);
                return fail(process2, 94,
                            "could not send File Open for roundtrip");
            }
            const HWND open_dialog = wait_for_window(
                process2.hProcess, process2.dwProcessId, L"#32770", L"Open",
                5000);
            if (open_dialog == nullptr) {
                log_process_windows(process2.dwProcessId);
                for (HWND top = GetTopWindow(nullptr); top != nullptr;
                     top = GetWindow(top, GW_HWNDNEXT)) {
                    wchar_t top_class[128] = {};
                    GetClassNameW(top, top_class,
                                  static_cast<int>(std::size(top_class)));
                    if (lstrcmpW(top_class, L"#32770") != 0 ||
                        !IsWindowVisible(top)) {
                        continue;
                    }
                    char top_caption[256] = {};
                    read_control_text_ansi(
                        top, top_caption,
                        static_cast<int>(std::size(top_caption)));
                    std::cerr << "roundtrip leftover #32770 hwnd=" << top
                              << " caption='" << top_caption << "'\n";
                    dump_dialog_tree_diagnostic(top, 1);
                }
                DeleteFileA(ansi_path);
                return fail(process2, 95,
                            "roundtrip File Open dialog (#32770) did not "
                            "appear");
            }
            if (!window_is_responsive(process2.hProcess, open_dialog)) {
                DeleteFileA(ansi_path);
                return fail(process2, 96,
                            "roundtrip File Open dialog did not finish "
                            "initializing");
            }

            const HWND open_filename_field = GetDlgItem(open_dialog, 0x047C);
            if (open_filename_field == nullptr) {
                std::cerr << "roundtrip open dialog tree dump:\n";
                dump_dialog_tree_diagnostic(open_dialog);
                DeleteFileA(ansi_path);
                return fail(process2, 97,
                            "roundtrip File Open dialog has no cmb13 "
                            "filename field");
            }
            SendMessageA(open_filename_field, WM_SETTEXT, 0,
                        reinterpret_cast<LPARAM>(ansi_path));
            char open_filename_check[MAX_PATH] = {};
            read_control_text_ansi(
                open_filename_field, open_filename_check,
                static_cast<int>(std::size(open_filename_check)));
            std::cerr << "roundtrip open filename field=" << open_filename_field
                      << " reads back '" << open_filename_check << "'\n";
            if (lstrcmpiA(open_filename_check, ansi_path) != 0) {
                std::cerr << "roundtrip open dialog tree dump:\n";
                dump_dialog_tree_diagnostic(open_dialog);
                DeleteFileA(ansi_path);
                return fail(process2, 98,
                            "roundtrip could not set the File Open "
                            "filename");
            }
            if (!PostMessageW(open_dialog, kWmCommand, IDOK, 0)) {
                DeleteFileA(ansi_path);
                return fail(process2, 99,
                            "could not accept the roundtrip File Open "
                            "dialog");
            }

            bool dialog_open_worked = false;
            const ULONGLONG open_deadline = GetTickCount64() + 8000;
            do {
                wchar_t caption[512] = {};
                GetWindowTextW(main_window2, caption,
                               static_cast<int>(std::size(caption)));
                if (wide_contains_i(caption, base_name)) {
                    dialog_open_worked = true;
                    break;
                }
                if (process2.hProcess != nullptr &&
                    WaitForSingleObject(process2.hProcess, 0) ==
                        WAIT_OBJECT_0) {
                    break;
                }
                Sleep(100);
            } while (GetTickCount64() < open_deadline);
            if (!dialog_open_worked) {
                log_process_windows(process2.dwProcessId);
                for (HWND top = GetTopWindow(nullptr); top != nullptr;
                     top = GetWindow(top, GW_HWNDNEXT)) {
                    wchar_t top_class[128] = {};
                    GetClassNameW(top, top_class,
                                  static_cast<int>(std::size(top_class)));
                    if (lstrcmpW(top_class, L"#32770") != 0 ||
                        !IsWindowVisible(top)) {
                        continue;
                    }
                    char top_caption[256] = {};
                    read_control_text_ansi(
                        top, top_caption,
                        static_cast<int>(std::size(top_caption)));
                    std::cerr << "roundtrip leftover #32770 hwnd=" << top
                              << " caption='" << top_caption << "'\n";
                    dump_dialog_tree_diagnostic(top, 1);
                }
                DeleteFileA(ansi_path);
                return fail(process2, 100,
                            "roundtrip File Open did not load the target "
                            "document");
            }
            std::cerr << "roundtrip process 2 opened via the File > Open "
                        "dialog\n";
        } else {
            std::cerr << "roundtrip process 2 opened via the command "
                        "line\n";
        }

        // Step 2: re-read the same query codes used for the pre-save
        // snapshot and compare, field by field, against process 1's
        // document.
        const HWND pane2 = find_descendant_by_class(main_window2, L"OpusWwd");
        if (pane2 == nullptr) {
            DeleteFileA(ansi_path);
            return fail(process2, 101,
                        "roundtrip reopened window has no OpusWwd pane");
        }
        if (!make_foreground_and_focus(main_window2, pane2, thread_id2)) {
            DeleteFileA(ansi_path);
            return fail(process2, 102,
                        "roundtrip could not focus the reopened document "
                        "pane");
        }

        const LRESULT new_cp_mac =
            SendMessageW(pane2, kWmOpusX64QuerySelection, 41, 0);
        if (new_cp_mac != cp_mac) {
            std::cerr << "roundtrip mismatch cpMac: expected=" << cp_mac
                      << " actual=" << new_cp_mac << '\n';
            DeleteFileA(ansi_path);
            return fail(process2, 103, "roundtrip cpMac differs after reopen");
        }
        for (LRESULT cp = 0; cp < new_cp_mac; ++cp) {
            const LRESULT byte_value =
                SendMessageW(pane2, kWmOpusX64QuerySelection, 69, cp);
            if (byte_value != snapshot_bytes[static_cast<std::size_t>(cp)]) {
                std::cerr << "roundtrip mismatch byte@" << cp << ": expected="
                          << snapshot_bytes[static_cast<std::size_t>(cp)]
                          << " actual=" << byte_value << '\n';
                DeleteFileA(ansi_path);
                return fail(process2, 104,
                            "roundtrip byte content differs after reopen");
            }
        }
        const LRESULT new_ftc0 =
            SendMessageW(pane2, kWmOpusX64QuerySelection, 51, 0);
        const LRESULT new_hps0 =
            SendMessageW(pane2, kWmOpusX64QuerySelection, 52, 0);
        const LRESULT new_dyp0 =
            SendMessageW(pane2, kWmOpusX64QuerySelection, 55, 0);
        std::cerr << "roundtrip reopened snapshot cpMac=" << new_cp_mac
                  << " ftc0=" << new_ftc0 << " hps0=" << new_hps0
                  << " dyp0=" << new_dyp0 << '\n';
        if (new_ftc0 != ftc0) {
            std::cerr << "roundtrip mismatch ftc: expected=" << ftc0
                      << " actual=" << new_ftc0 << '\n';
            DeleteFileA(ansi_path);
            return fail(process2, 105, "roundtrip ftc differs after reopen");
        }
        if (new_hps0 != hps0) {
            std::cerr << "roundtrip mismatch hps: expected=" << hps0
                      << " actual=" << new_hps0 << '\n';
            DeleteFileA(ansi_path);
            return fail(process2, 106, "roundtrip hps differs after reopen");
        }
        if (new_dyp0 != dyp0) {
            std::cerr << "roundtrip mismatch dypLine: expected=" << dyp0
                      << " actual=" << new_dyp0 << '\n';
            DeleteFileA(ansi_path);
            return fail(process2, 107,
                        "roundtrip dypLine differs after reopen");
        }
        std::cerr << "roundtrip reopen comparison OK: cpMac=" << new_cp_mac
                  << " ftc0=" << new_ftc0 << " hps0=" << new_hps0
                  << " dyp0=" << new_dyp0 << '\n';

        // Step 3: tear down process 2 unconditionally -- it was only ever
        // opened to read the file back, never edited, so there is nothing
        // to save and no File Exit prompt to negotiate.
        if (process2.hProcess != nullptr) {
            TerminateProcess(process2.hProcess, 0);
            WaitForSingleObject(process2.hProcess, 2000);
        }
        if (process2.hThread != nullptr) {
            CloseHandle(process2.hThread);
        }
        if (process2.hProcess != nullptr) {
            CloseHandle(process2.hProcess);
        }
        DeleteFileA(ansi_path);
        return 0;
    }
    if (rich_format_mode) {
        // Same structure as roundtrip_mode above (type -> Save As real
        // .doc -> File Exit -> second WORD1 process -> compare), but the
        // thing being round-tripped is CHP/PAP formatting instead of raw
        // character bytes: Ctrl+B/Ctrl+I via query 80 (FExecKc, same
        // mechanism execute_control_shortcut already uses for Ctrl+A),
        // centered alignment via the real kParaCenter menu command, then
        // read back through the new query codes 82 (chp.fBold), 83
        // (chp.fItalic) and 84 (CachePara + vpapFetch.jc) added to
        // Opus/wproc.c's WM_OPUS_X64_QUERY_SELECTION switch for this test.
        DWORD ignored_process_id = 0;
        const DWORD thread_id =
            GetWindowThreadProcessId(main_window, &ignored_process_id);
        const HWND pane = find_descendant_by_class(main_window, L"OpusWwd");
        if (pane == nullptr) {
            return fail(process, 109,
                        "rich-format test could not find the document pane");
        }
        if (!make_foreground_and_focus(main_window, pane, thread_id)) {
            return fail(process, 110,
                        "rich-format test could not focus the document "
                        "pane");
        }
        if (!send_physical_text(L"rich format paragraph")) {
            return fail(process, 111,
                        "rich-format test could not type its paragraph");
        }
        Sleep(500);

        const LRESULT cp_mac =
            SendMessageW(pane, kWmOpusX64QuerySelection, 41, 0);
        if (cp_mac < 10) {
            return fail(process, 112,
                        "rich-format test typed too little text to format");
        }

        // Select the whole paragraph, then apply character formatting
        // (Bold, Italic) and paragraph formatting (centered) to it.
        if (!execute_control_shortcut(pane, 'A')) {
            return fail(process, 113,
                        "rich-format test could not select all");
        }
        Sleep(300);
        const LRESULT selected_first =
            SendMessageW(pane, kWmOpusX64QuerySelection, 0, 0);
        const LRESULT selected_lim =
            SendMessageW(pane, kWmOpusX64QuerySelection, 1, 0);
        if (selected_lim <= selected_first) {
            return fail(process, 114,
                        "rich-format test could not select the paragraph");
        }
        if (!execute_control_shortcut(pane, 'B')) {
            return fail(process, 115,
                        "rich-format test could not apply bold");
        }
        Sleep(200);
        if (!execute_control_shortcut(pane, 'I')) {
            return fail(process, 116,
                        "rich-format test could not apply italic");
        }
        Sleep(200);
        if (!PostMessageW(main_window, kWmCommand, kParaCenter, 0)) {
            return fail(process, 117,
                        "rich-format test could not apply centered "
                        "alignment");
        }
        Sleep(500);
        if (!window_is_responsive(process.hProcess, main_window)) {
            return fail(process, 118,
                        "rich-format formatting crashed or hung WORD1");
        }

        // Queries 82/83/84 read the CURRENT SELECTION's chp/pap (selCur),
        // not an explicit cp like queries 51/52 do -- so click near the
        // start of the pane to park the caret inside the just-formatted
        // paragraph rather than trust wherever Select All left it.
        POINT caret_point{20, 10};
        if (!ClientToScreen(pane, &caret_point) ||
            !SetCursorPos(caret_point.x, caret_point.y) ||
            !send_mouse_button(MOUSEEVENTF_LEFTDOWN) ||
            !send_mouse_button(MOUSEEVENTF_LEFTUP)) {
            return fail(process, 119,
                        "rich-format test could not place the caret");
        }
        Sleep(300);

        const LRESULT expected_bold =
            SendMessageW(pane, kWmOpusX64QuerySelection, 82, 0);
        const LRESULT expected_italic =
            SendMessageW(pane, kWmOpusX64QuerySelection, 83, 0);
        const LRESULT expected_jc =
            SendMessageW(pane, kWmOpusX64QuerySelection, 84, 0);
        std::cerr << "rich-format pre-save chp/pap: bold=" << expected_bold
                  << " italic=" << expected_italic << " jc=" << expected_jc
                  << '\n';
        if (expected_bold == 0 || expected_italic == 0) {
            return fail(process, 120,
                        "rich-format bold/italic were not applied before "
                        "save");
        }

        // Choose an on-disk target and delete it if already there, same
        // reasoning as roundtrip_mode above (OFN_OVERWRITEPROMPT is on for
        // the real Save As dialog). Distinct prefix ("orfm") so a
        // concurrent run of --roundtrip never collides on the same name.
        char temp_dir[MAX_PATH] = {};
        const DWORD temp_dir_length = GetTempPathA(
            static_cast<DWORD>(std::size(temp_dir)), temp_dir);
        if (temp_dir_length == 0 || temp_dir_length >= std::size(temp_dir)) {
            return fail(process, 121,
                        "rich-format test could not resolve GetTempPathA");
        }
        char ansi_path[MAX_PATH] = {};
        wsprintfA(ansi_path, "%sorfm%04lx.doc", temp_dir,
                  static_cast<unsigned long>(process.dwProcessId & 0xFFFFu));
        DeleteFileA(ansi_path);
        wchar_t wide_path[MAX_PATH] = {};
        MultiByteToWideChar(CP_ACP, 0, ansi_path, -1, wide_path,
                            static_cast<int>(std::size(wide_path)));
        std::cerr << "rich-format target path='" << ansi_path
                  << "' wideLength=" << lstrlenW(wide_path) << '\n';

        // File > Save As, driving the real #32770 common dialog -- same
        // cmb13 (0x047C) filename field roundtrip_mode already found.
        if (!PostMessageW(main_window, kWmCommand, kFileSaveAs, 0)) {
            return fail(process, 122,
                        "could not send File Save As for rich-format");
        }
        const HWND save_dialog = wait_for_window(
            process.hProcess, process.dwProcessId, L"#32770", L"Save As",
            5000);
        if (save_dialog == nullptr) {
            log_process_windows(process.dwProcessId);
            return fail(process, 123,
                        "rich-format Save As dialog (#32770) did not "
                        "appear");
        }
        if (!window_is_responsive(process.hProcess, save_dialog)) {
            DeleteFileA(ansi_path);
            return fail(process, 124,
                        "rich-format Save As dialog did not finish "
                        "initializing");
        }
        const HWND filename_field = GetDlgItem(save_dialog, 0x047C);
        if (filename_field == nullptr) {
            std::cerr << "rich-format dialog tree dump:\n";
            dump_dialog_tree_diagnostic(save_dialog);
            DeleteFileA(ansi_path);
            return fail(process, 125,
                        "rich-format Save As dialog has no cmb13 filename "
                        "field");
        }
        SendMessageA(filename_field, WM_SETTEXT, 0,
                     reinterpret_cast<LPARAM>(ansi_path));
        char filename_check[MAX_PATH] = {};
        read_control_text_ansi(filename_field, filename_check,
                               static_cast<int>(std::size(filename_check)));
        if (lstrcmpiA(filename_check, ansi_path) != 0) {
            std::cerr << "rich-format dialog tree dump:\n";
            dump_dialog_tree_diagnostic(save_dialog);
            DeleteFileA(ansi_path);
            return fail(process, 126,
                        "rich-format could not set the Save As filename");
        }
        if (!PostMessageW(save_dialog, kWmCommand, IDOK, 0)) {
            DeleteFileA(ansi_path);
            return fail(process, 127,
                        "could not accept the rich-format Save As dialog");
        }

        const ULONGLONG save_deadline = GetTickCount64() + 8000;
        bool confirmed_overwrite = false;
        bool file_ready = false;
        DWORD saved_file_size = 0;
        while (GetTickCount64() < save_deadline) {
            if (GetFileAttributesA(ansi_path) != INVALID_FILE_ATTRIBUTES) {
                const HANDLE probe = CreateFileA(
                    ansi_path, GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (probe != INVALID_HANDLE_VALUE) {
                    saved_file_size = GetFileSize(probe, nullptr);
                    CloseHandle(probe);
                    if (saved_file_size != INVALID_FILE_SIZE &&
                        saved_file_size > 128) {
                        file_ready = true;
                        break;
                    }
                }
            }
            if (!confirmed_overwrite) {
                const HWND confirm_dialog = find_process_window(
                    process.dwProcessId, L"#32770", L"Confirm Save As");
                if (confirm_dialog != nullptr &&
                    confirm_dialog != save_dialog) {
                    PostMessageW(confirm_dialog, kWmCommand, IDYES, 0);
                    confirmed_overwrite = true;
                }
            }
            Sleep(100);
        }
        if (!file_ready) {
            log_process_windows(process.dwProcessId);
            DeleteFileA(ansi_path);
            return fail(process, 128,
                        "rich-format Save As did not produce the target "
                        ".doc file");
        }
        std::cerr << "rich-format saved '" << ansi_path
                  << "' size=" << saved_file_size << " bytes\n";

        // Tear down process 1 -- a successful save must have marked the
        // document clean, so File Exit should not prompt.
        if (!PostMessageW(main_window, kWmCommand, kFileExit, 0)) {
            DeleteFileA(ansi_path);
            return fail(process, 129,
                        "could not send File Exit after rich-format save");
        }
        const HWND save_changes_prompt = wait_for_window(
            process.hProcess, process.dwProcessId, L"#32770", nullptr, 3000);
        if (save_changes_prompt != nullptr) {
            DeleteFileA(ansi_path);
            return fail(process, 130,
                        "File Exit prompted a dialog after the rich-format "
                        "save (document was not marked clean)");
        }
        if (WaitForSingleObject(process.hProcess, 5000) != WAIT_OBJECT_0) {
            TerminateProcess(process.hProcess, 0);
            WaitForSingleObject(process.hProcess, 2000);
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);

        // Launch a second, independent WORD1 process against the .doc
        // process 1 just saved -- same CreateProcessW pattern as
        // roundtrip_mode, unquoted path (see the long comment on that mode
        // above for why).
        wchar_t command_line2[(MAX_PATH * 2) + 8] = {};
        command_line2[0] = L'"';
        lstrcpyW(command_line2 + 1, arguments[1]);
        lstrcatW(command_line2, L"\" ");
        lstrcatW(command_line2, wide_path);
        STARTUPINFOW startup2{};
        startup2.cb = sizeof(startup2);
        PROCESS_INFORMATION process2{};
        if (!CreateProcessW(arguments[1], command_line2, nullptr, nullptr,
                            FALSE, 0, nullptr, nullptr, &startup2,
                            &process2)) {
            std::cerr << "rich-format CreateProcessW (process 2) failed: "
                      << GetLastError() << '\n';
            DeleteFileA(ansi_path);
            return fail(process2, 131, "could not launch process 2");
        }

        const wchar_t* base_name_start = wide_path;
        for (const wchar_t* p = wide_path; *p != L'\0'; ++p) {
            if (*p == L'\\') {
                base_name_start = p + 1;
            }
        }
        const wchar_t* base_name_end =
            base_name_start + lstrlenW(base_name_start);
        for (const wchar_t* p = base_name_start; *p != L'\0'; ++p) {
            if (*p == L'.') {
                base_name_end = p;
                break;
            }
        }
        wchar_t base_name[64] = {};
        std::size_t base_name_length =
            static_cast<std::size_t>(base_name_end - base_name_start);
        if (base_name_length > std::size(base_name) - 1) {
            base_name_length = std::size(base_name) - 1;
        }
        for (std::size_t i = 0; i < base_name_length; ++i) {
            base_name[i] = base_name_start[i];
        }

        HWND main_window2 = nullptr;
        bool command_line_open_worked = false;
        {
            const ULONGLONG deadline = GetTickCount64() + 8000;
            do {
                main_window2 = find_process_window(process2.dwProcessId,
                                                   L"OpusApp",
                                                   L"Microsoft Word");
                if (main_window2 != nullptr) {
                    wchar_t caption[512] = {};
                    GetWindowTextW(main_window2, caption,
                                   static_cast<int>(std::size(caption)));
                    if (wide_contains_i(caption, base_name)) {
                        command_line_open_worked = true;
                        break;
                    }
                }
                if (process2.hProcess != nullptr &&
                    WaitForSingleObject(process2.hProcess, 0) ==
                        WAIT_OBJECT_0) {
                    break;
                }
                Sleep(50);
            } while (GetTickCount64() < deadline);
        }
        if (main_window2 == nullptr) {
            log_process_windows(process2.dwProcessId);
            DeleteFileA(ansi_path);
            return fail(process2, 132,
                        "process 2 main window did not appear");
        }
        if (process2.hProcess == nullptr) {
            DWORD window_process_id2 = 0;
            GetWindowThreadProcessId(main_window2, &window_process_id2);
            if (window_process_id2 != 0) {
                const HANDLE recovered2 = OpenProcess(
                    PROCESS_TERMINATE | PROCESS_QUERY_INFORMATION |
                        SYNCHRONIZE,
                    FALSE, window_process_id2);
                if (recovered2 != nullptr) {
                    process2.hProcess = recovered2;
                    process2.dwProcessId = window_process_id2;
                }
            }
        }
        DWORD ignored_process_id2 = 0;
        const DWORD thread_id2 =
            GetWindowThreadProcessId(main_window2, &ignored_process_id2);

        if (!command_line_open_worked) {
            // Same fallback as roundtrip_mode: dismiss the stray "Cannot
            // open document" box (if any) and drive File > Open instead.
            const HWND stray_error_box = find_process_window(
                process2.dwProcessId, L"#32770", L"Microsoft Word");
            if (stray_error_box != nullptr) {
                dump_dialog_tree_diagnostic(stray_error_box);
                PostMessageW(stray_error_box, kWmCommand, IDOK, 0);
            }
            main_window2 = wait_for_window(process2.hProcess,
                                           process2.dwProcessId, L"OpusApp",
                                           L"Microsoft Word - Document1",
                                           5000);
            if (main_window2 == nullptr) {
                log_process_windows(process2.dwProcessId);
                DeleteFileA(ansi_path);
                return fail(process2, 133,
                            "process 2 did not reach the idle Document1 "
                            "state after a failed command-line open");
            }
            if (!PostMessageW(main_window2, kWmCommand, kFileOpen, 0)) {
                DeleteFileA(ansi_path);
                return fail(process2, 134,
                            "could not send File Open for rich-format");
            }
            const HWND open_dialog = wait_for_window(
                process2.hProcess, process2.dwProcessId, L"#32770", L"Open",
                5000);
            if (open_dialog == nullptr) {
                log_process_windows(process2.dwProcessId);
                DeleteFileA(ansi_path);
                return fail(process2, 135,
                            "rich-format File Open dialog (#32770) did not "
                            "appear");
            }
            if (!window_is_responsive(process2.hProcess, open_dialog)) {
                DeleteFileA(ansi_path);
                return fail(process2, 136,
                            "rich-format File Open dialog did not finish "
                            "initializing");
            }
            const HWND open_filename_field = GetDlgItem(open_dialog, 0x047C);
            if (open_filename_field == nullptr) {
                dump_dialog_tree_diagnostic(open_dialog);
                DeleteFileA(ansi_path);
                return fail(process2, 137,
                            "rich-format File Open dialog has no cmb13 "
                            "filename field");
            }
            SendMessageA(open_filename_field, WM_SETTEXT, 0,
                        reinterpret_cast<LPARAM>(ansi_path));
            char open_filename_check[MAX_PATH] = {};
            read_control_text_ansi(
                open_filename_field, open_filename_check,
                static_cast<int>(std::size(open_filename_check)));
            if (lstrcmpiA(open_filename_check, ansi_path) != 0) {
                dump_dialog_tree_diagnostic(open_dialog);
                DeleteFileA(ansi_path);
                return fail(process2, 138,
                            "rich-format could not set the File Open "
                            "filename");
            }
            if (!PostMessageW(open_dialog, kWmCommand, IDOK, 0)) {
                DeleteFileA(ansi_path);
                return fail(process2, 139,
                            "could not accept the rich-format File Open "
                            "dialog");
            }
            bool dialog_open_worked = false;
            const ULONGLONG open_deadline = GetTickCount64() + 8000;
            do {
                wchar_t caption[512] = {};
                GetWindowTextW(main_window2, caption,
                               static_cast<int>(std::size(caption)));
                if (wide_contains_i(caption, base_name)) {
                    dialog_open_worked = true;
                    break;
                }
                if (process2.hProcess != nullptr &&
                    WaitForSingleObject(process2.hProcess, 0) ==
                        WAIT_OBJECT_0) {
                    break;
                }
                Sleep(100);
            } while (GetTickCount64() < open_deadline);
            if (!dialog_open_worked) {
                log_process_windows(process2.dwProcessId);
                DeleteFileA(ansi_path);
                return fail(process2, 140,
                            "rich-format File Open did not load the target "
                            "document");
            }
            std::cerr << "rich-format process 2 opened via the File > Open "
                        "dialog\n";
        } else {
            std::cerr << "rich-format process 2 opened via the command "
                        "line\n";
        }

        // Read back CHP/PAP through the reopened document's own SDM/FIB
        // load path: if formatting reappears correctly here, the FKP
        // pages just round-tripped cleanly through the FIB marshaling
        // layer (PackFib/UnpackFib) on real disk bytes -- no separate
        // binary parser needed, this reopen path IS that layer.
        const HWND pane2 = find_descendant_by_class(main_window2, L"OpusWwd");
        if (pane2 == nullptr) {
            DeleteFileA(ansi_path);
            return fail(process2, 141,
                        "rich-format reopened window has no OpusWwd pane");
        }
        if (!make_foreground_and_focus(main_window2, pane2, thread_id2)) {
            DeleteFileA(ansi_path);
            return fail(process2, 142,
                        "rich-format could not focus the reopened document "
                        "pane");
        }
        const LRESULT new_cp_mac =
            SendMessageW(pane2, kWmOpusX64QuerySelection, 41, 0);
        if (new_cp_mac != cp_mac) {
            // Not a hard gate here, unlike roundtrip_mode: this is the
            // already-documented, already-parked ccpEop mismatch
            // (Opus/ch.h's ccpEop=2 under CRLF vs opus_asm_resn_core.cpp's
            // hardcoded kCcpEop=1 -- see docs/port-linux CLAUDE.md status
            // and branch wip/ccpeop-font-typing-regression), which grows
            // cpMac by a fixed +2 on every save/reopen cycle regardless of
            // formatting. This test's own subject is CHP/PAP persistence,
            // not cp accounting, and the paragraph's start (where the
            // caret click below lands) is unaffected by a shift at the
            // document's tail -- log and continue instead of failing.
            std::cerr << "rich-format cpMac drifted after reopen (known "
                        "parked ccpEop bug, not a formatting regression): "
                        "expected=" << cp_mac << " actual=" << new_cp_mac
                      << '\n';
        }
        POINT caret_point2{20, 10};
        if (!ClientToScreen(pane2, &caret_point2) ||
            !SetCursorPos(caret_point2.x, caret_point2.y) ||
            !send_mouse_button(MOUSEEVENTF_LEFTDOWN) ||
            !send_mouse_button(MOUSEEVENTF_LEFTUP)) {
            DeleteFileA(ansi_path);
            return fail(process2, 144,
                        "rich-format could not place the caret in the "
                        "reopened document");
        }
        Sleep(300);
        const LRESULT actual_bold =
            SendMessageW(pane2, kWmOpusX64QuerySelection, 82, 0);
        const LRESULT actual_italic =
            SendMessageW(pane2, kWmOpusX64QuerySelection, 83, 0);
        const LRESULT actual_jc =
            SendMessageW(pane2, kWmOpusX64QuerySelection, 84, 0);
        std::cerr << "rich-format reopened chp/pap: bold=" << actual_bold
                  << " italic=" << actual_italic << " jc=" << actual_jc
                  << '\n';
        if (actual_bold != expected_bold) {
            DeleteFileA(ansi_path);
            return fail(process2, 145,
                        "rich-format bold (CHP.fBold) did not survive the "
                        "save/reopen round-trip");
        }
        if (actual_italic != expected_italic) {
            DeleteFileA(ansi_path);
            return fail(process2, 146,
                        "rich-format italic (CHP.fItalic) did not survive "
                        "the save/reopen round-trip");
        }
        if (actual_jc != expected_jc) {
            DeleteFileA(ansi_path);
            return fail(process2, 147,
                        "rich-format paragraph alignment (PAP.jc) did not "
                        "survive the save/reopen round-trip");
        }
        std::cerr << "rich-format reopen comparison OK: bold=" << actual_bold
                  << " italic=" << actual_italic << " jc=" << actual_jc
                  << '\n';

        // Tear down process 2 unconditionally -- it was only ever opened
        // to read the file back, never edited.
        if (process2.hProcess != nullptr) {
            TerminateProcess(process2.hProcess, 0);
            WaitForSingleObject(process2.hProcess, 2000);
        }
        if (process2.hThread != nullptr) {
            CloseHandle(process2.hThread);
        }
        if (process2.hProcess != nullptr) {
            CloseHandle(process2.hProcess);
        }
        DeleteFileA(ansi_path);
        return 0;
    }
    if (about_mode) {
        Sleep(1000);
        if (!PostMessageW(main_window, kWmCommand, kHelpAbout, 0)) {
            return fail(process, 65, "could not send Help About");
        }
        const HWND about_dialog = wait_for_window(
            process.hProcess, process.dwProcessId, L"OpusSdmDialog", nullptr,
            5000);
        if (about_dialog == nullptr) {
            DWORD exit_code = 0;
            GetExitCodeProcess(process.hProcess, &exit_code);
            std::cerr << "Help About process exit=0x" << std::hex << exit_code
                      << std::dec << " mainWindow=" << IsWindow(main_window)
                      << " responsive="
                      << window_is_responsive(process.hProcess, main_window)
                      << " stage="
                      << reinterpret_cast<INT_PTR>(GetPropA(
                             main_window, "OpusX64AboutStage"))
                      << '\n';
            log_process_windows(process.dwProcessId);
            return fail(process, 66, "Help About dialog did not appear");
        }
        if (!control_has_class(about_dialog, 1, L"Button") ||
            !window_is_responsive(process.hProcess, about_dialog)) {
            return fail(process, 67,
                        "Help About dialog did not finish initializing");
        }
        if (!PostMessageW(about_dialog, kWmCommand, 1, 0) ||
            !wait_for_window_to_close(process.hProcess, process.dwProcessId,
                                      L"OpusSdmDialog", 5000) ||
            !window_is_responsive(process.hProcess, main_window)) {
            return fail(process, 68, "Help About dialog did not close cleanly");
        }
        TerminateProcess(process.hProcess, 0);
        WaitForSingleObject(process.hProcess, 2000);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return 0;
    }
    if (clipboard_mode) {
        const HWND pane = find_descendant_by_class(main_window, L"OpusWwd");
        if (pane == nullptr) {
            return fail(process, 73,
                        "shortcut test could not find the document pane");
        }
        const std::array<std::pair<WORD, LRESULT>, 5> bindings{{
            {'A', kEditSelectAll},
            {'C', kEditCopy},
            {'V', kEditPaste},
            {'X', kEditCut},
            {'Z', kEditUndo},
        }};
        for (const auto& [key, expected_command] : bindings) {
            const LRESULT actual_command = SendMessageW(
                pane, kWmOpusX64QuerySelection, 81, kKcControl | key);
            if (actual_command != expected_command) {
                std::cerr << "Ctrl+" << static_cast<char>(key)
                          << " command=" << actual_command
                          << " expected=" << expected_command << '\n';
                return fail(process, 74,
                            "a Word 95 editing shortcut is not bound");
            }
        }
        if (!execute_control_shortcut(pane, 'A')) {
            return fail(process, 75, "Ctrl+A did not execute Select All");
        }
        Sleep(300);
        const LRESULT selected_first = SendMessageW(
            pane, kWmOpusX64QuerySelection, 0, 0);
        const LRESULT selected_lim = SendMessageW(
            pane, kWmOpusX64QuerySelection, 1, 0);
        if (selected_lim <= selected_first) {
            std::cerr << "Select All range=" << selected_first << ','
                      << selected_lim << '\n';
            return fail(process, 75,
                        "Ctrl+A did not change the document selection");
        }
        TerminateProcess(process.hProcess, 0);
        WaitForSingleObject(process.hProcess, 2000);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return 0;
    }
    if (font_typing_mode) {
        const HWND pane = find_descendant_by_class(main_window, L"OpusWwd");
        DWORD ignored_process_id = 0;
        const DWORD thread_id =
            GetWindowThreadProcessId(main_window, &ignored_process_id);
        std::vector<HWND> combos;
        collect_descendants_by_class(main_window, L"ComboBox", combos);
        HWND font_combo = nullptr;
        HWND size_combo = nullptr;
        LRESULT font_index = CB_ERR;
        LRESULT size_index = CB_ERR;
        /* installed_windows_fonts() (opus_sdm_runtime.cpp) enumerates via
           EnumFontFamiliesExA -- real installed font family names, not
           Windows aliases. "Courier New"/"Arial" never appear on a Linux
           font stack (confirmed empirically on two independent dev
           environments: this VPS enumerates FreeMono, FreeSans, FreeSerif,
           the Liberation family, Noto, Unifont, WenQuanYi and IPA fonts;
           an earlier debian13 session saw the Liberation family, DejaVu,
           Tahoma, MS Sans Serif, Symbol and Wingdings -- zero
           Windows-alias names in either). "Liberation Sans" and
           "Liberation Mono" are the only two names both lists share, and
           Debian's fonts-liberation is a common baseline package -- the
           most portable real choice. This is a data fix, not a rendering
           one: CB_FINDSTRINGEXACT queries the combo's string list
           directly, no display needed. */
        for (const HWND combo : combos) {
            if (font_combo == nullptr) {
                font_index = SendMessageW(
                    combo, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
                    reinterpret_cast<LPARAM>(L"Liberation Sans"));
                if (font_index != CB_ERR) {
                    font_combo = combo;
                }
            }
            if (size_combo == nullptr) {
                size_index = SendMessageW(
                    combo, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
                    reinterpret_cast<LPARAM>(L"24"));
                if (size_index != CB_ERR) {
                    size_combo = combo;
                }
            }
        }
        if (pane == nullptr || font_combo == nullptr || size_combo == nullptr) {
            return fail(process, 47,
                        "font typing test could not find the ribbon controls");
        }
        const LRESULT second_font_index = SendMessageW(
            font_combo, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
            reinterpret_cast<LPARAM>(L"Liberation Mono"));
        const LRESULT second_size_index = SendMessageW(
            size_combo, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
            reinterpret_cast<LPARAM>(L"36"));
        const LRESULT large_size_index = SendMessageW(
            size_combo, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
            reinterpret_cast<LPARAM>(L"72"));
        const LRESULT listed_size_index = SendMessageW(
            size_combo, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
            reinterpret_cast<LPARAM>(L"72"));
        if (second_font_index == CB_ERR || second_size_index == CB_ERR ||
            large_size_index == CB_ERR || listed_size_index == CB_ERR) {
            return fail(process, 47,
                        "font typing test could not find its second font");
        }

        const LRESULT initial_ftc =
            SendMessageW(pane, kWmOpusX64QuerySelection, 49, 0);
        const LRESULT initial_hps =
            SendMessageW(pane, kWmOpusX64QuerySelection, 50, 0);
        if (SendMessageW(pane, kWmOpusX64QuerySelection, 54, 195) != 195) {
            return fail(process, 47,
                        "the 8-bit master-font index was sign-extended");
        }
        if (SendMessageW(pane, kWmOpusX64QuerySelection, 58, 0) != 4) {
            return fail(process, 47,
                        "the packed font identifier is not 32 bits");
        }
        const bool got_foreground =
            make_foreground_and_focus(main_window, pane, thread_id);
        const bool chose_item =
            got_foreground && choose_combo_item_with_mouse(font_combo, font_index);
        const bool regained_focus =
            chose_item &&
            wait_for_focus_traced(process.hProcess, thread_id, pane, 1500);
        if (!got_foreground || !chose_item || !regained_focus) {
            std::cerr << "  [focus-trace] identities: pane=" << pane
                      << " font_combo=" << font_combo
                      << " main_window=" << main_window << '\n';
            std::cerr << "font combo select stages: foreground="
                      << got_foreground << " chose_item=" << chose_item
                      << " regained_focus=" << regained_focus << '\n';
            return fail(process, 49,
                        "font typing test could not mouse-select the font");
        }
        Sleep(300);
        const LRESULT applied_ftc =
            SendMessageW(pane, kWmOpusX64QuerySelection, 49, 0);

        if (!choose_combo_item_with_mouse(size_combo, size_index) ||
            !wait_for_focus(process.hProcess, thread_id, pane, 1500)) {
            return fail(process, 51,
                        "font typing test could not mouse-select point size");
        }
        Sleep(300);
        const LRESULT applied_hps =
            SendMessageW(pane, kWmOpusX64QuerySelection, 50, 0);
        const LRESULT cp_before =
            SendMessageW(pane, kWmOpusX64QuerySelection, 0, 0);
        {
            GUITHREADINFO gui{};
            gui.cbSize = sizeof(gui);
            GetGUIThreadInfo(thread_id, &gui);
            wchar_t class_name[64] = {};
            GetClassNameW(gui.hwndFocus, class_name,
                          static_cast<int>(std::size(class_name)));
            char class_name_ansi[128] = {};
            WideCharToMultiByte(CP_ACP, 0, class_name, -1, class_name_ansi,
                                static_cast<int>(sizeof(class_name_ansi)),
                                nullptr, nullptr);
            std::cerr << "  [pre-type] hwndFocus=" << gui.hwndFocus
                      << " class=" << class_name_ansi
                      << " (== pane)=" << (gui.hwndFocus == pane)
                      << " foreground=" << GetForegroundWindow()
                      << " (== main_window)="
                      << (GetForegroundWindow() == main_window)
                      << " paneEnabled=" << IsWindowEnabled(pane)
                      << " mainEnabled=" << IsWindowEnabled(main_window)
                      << " active=" << gui.hwndActive
                      << " capture=" << gui.hwndCapture
                      << " menuOwner=" << gui.hwndMenuOwner
                      << " moveSize=" << gui.hwndMoveSize
                      << " flags=" << gui.flags << '\n';
        }
        /* TEMP diagnostic: every focus/foreground/active fix attempt so far
           has left this test's outcome byte-identical, which rules out
           focus bookkeeping. Enumerate this thread's actual top-level
           windows (EnumThreadWindows walks Z-order) to check for a leftover
           combo dropdown popup still visible/topmost over the pane's screen
           rect -- that would eat real hardware input regardless of what any
           focus API reports, since XTest delivers to whatever's physically
           there. */
        {
            struct EnumCtx {
                HWND pane;
            } enum_ctx{pane};
            EnumThreadWindows(
                thread_id,
                [](HWND hwnd, LPARAM lparam) WINAPI -> BOOL {
                    auto* ctx = reinterpret_cast<EnumCtx*>(lparam);
                    wchar_t class_name[64] = {};
                    GetClassNameW(hwnd, class_name,
                                  static_cast<int>(std::size(class_name)));
                    char class_ansi[128] = {};
                    WideCharToMultiByte(CP_ACP, 0, class_name, -1, class_ansi,
                                        static_cast<int>(sizeof(class_ansi)),
                                        nullptr, nullptr);
                    RECT rect{};
                    GetWindowRect(hwnd, &rect);
                    wchar_t title[128] = {};
                    GetWindowTextW(hwnd, title,
                                    static_cast<int>(std::size(title)));
                    char title_ansi[128] = {};
                    WideCharToMultiByte(CP_ACP, 0, title, -1, title_ansi,
                                        static_cast<int>(sizeof(title_ansi)),
                                        nullptr, nullptr);
                    std::cerr << "  [enum-window] hwnd=" << hwnd
                              << " class=" << class_ansi
                              << " title='" << title_ansi << "'"
                              << " visible=" << IsWindowVisible(hwnd)
                              << " rect=" << rect.left << ',' << rect.top
                              << ',' << rect.right << ',' << rect.bottom
                              << " (== pane)=" << (hwnd == ctx->pane) << '\n';
                    if (lstrcmpW(class_name, L"#32770") == 0 &&
                        IsWindowVisible(hwnd)) {
                        EnumChildWindows(
                            hwnd,
                            [](HWND child, LPARAM) WINAPI -> BOOL {
                                wchar_t child_class[64] = {};
                                GetClassNameW(
                                    child, child_class,
                                    static_cast<int>(std::size(child_class)));
                                char child_class_ansi[128] = {};
                                WideCharToMultiByte(
                                    CP_ACP, 0, child_class, -1,
                                    child_class_ansi,
                                    static_cast<int>(sizeof(child_class_ansi)),
                                    nullptr, nullptr);
                                wchar_t child_text[256] = {};
                                GetWindowTextW(
                                    child, child_text,
                                    static_cast<int>(std::size(child_text)));
                                char child_text_ansi[256] = {};
                                WideCharToMultiByte(
                                    CP_ACP, 0, child_text, -1, child_text_ansi,
                                    static_cast<int>(sizeof(child_text_ansi)),
                                    nullptr, nullptr);
                                std::cerr
                                    << "    [dialog-child] hwnd=" << child
                                    << " class=" << child_class_ansi
                                    << " id=" << GetDlgCtrlID(child)
                                    << " text='" << child_text_ansi << "'\n";
                                return TRUE;
                            },
                            0);
                    }
                    return TRUE;
                },
                reinterpret_cast<LPARAM>(&enum_ctx));
        }
        /* TEMP diagnostic: caret_mode's physical-typing block (below, ~1868)
           calls make_foreground_and_focus(main_window, pane, thread_id)
           immediately before send_physical_text and passes; this block only
           polled focus state via wait_for_focus_traced above and trusted
           Opus's own SetFocus chain. make_foreground_and_focus additionally
           does AttachThreadInput(this test process's thread, WORD1's
           thread) around its SetForegroundWindow/SetFocus calls -- try
           whether that's the missing piece for SendInput delivery
           specifically (a cross-process concern GetFocus()/
           GetGUIThreadInfo() polling can't surface, since those read
           WORD1's own per-thread state correctly regardless). */
        make_foreground_and_focus(main_window, pane, thread_id);
        if (!send_physical_text(L"fonttest") || !send_virtual_key(VK_RIGHT)) {
            return fail(process, 52,
                        "font typing test could not type and commit text");
        }
        Sleep(1000);
        const LRESULT inserted_ftc = SendMessageW(
            pane, kWmOpusX64QuerySelection, 51, cp_before);
        const LRESULT inserted_hps = SendMessageW(
            pane, kWmOpusX64QuerySelection, 52, cp_before);
        const LRESULT first_line_height = SendMessageW(
            pane, kWmOpusX64QuerySelection, 33, 0);
        const LRESULT first_formatted_height = SendMessageW(
            pane, kWmOpusX64QuerySelection, 55, cp_before);
        const LRESULT first_formatter_hps = SendMessageW(
            pane, kWmOpusX64QuerySelection, 56, cp_before);
        const LRESULT first_font_height = SendMessageW(
            pane, kWmOpusX64QuerySelection, 57, cp_before);
        std::cerr << "font properties=" << initial_ftc << ',' << initial_hps
                  << " applied=" << applied_ftc << ',' << applied_hps
                  << " inserted=" << inserted_ftc << ',' << inserted_hps
                  << " lineHeight=" << first_line_height << " formatted="
                  << first_formatted_height << " formatter="
                  << first_formatter_hps << ',' << first_font_height << '\n';
        if (!window_is_responsive(process.hProcess, main_window) ||
            applied_ftc < 0 || applied_ftc == initial_ftc ||
            applied_hps != 48 || applied_hps == initial_hps ||
            inserted_ftc != applied_ftc || inserted_hps != applied_hps) {
            return fail(process, 53,
                        "newly typed text did not retain the ribbon font");
        }

        if (!choose_combo_item_with_mouse(font_combo, second_font_index) ||
            !wait_for_focus(process.hProcess, thread_id, pane, 1500)) {
            return fail(process, 55,
                        "font typing test could not mouse-select its second font");
        }
        if (!choose_combo_item_with_mouse(size_combo, second_size_index) ||
            !wait_for_focus(process.hProcess, thread_id, pane, 1500)) {
            return fail(process, 56,
                        "font typing test could not mouse-select its second size");
        }
        Sleep(300);
        const LRESULT second_ftc =
            SendMessageW(pane, kWmOpusX64QuerySelection, 49, 0);
        const LRESULT second_hps =
            SendMessageW(pane, kWmOpusX64QuerySelection, 50, 0);
        const LRESULT second_ibst =
            SendMessageW(pane, kWmOpusX64QuerySelection, 53, 0);
        const LRESULT second_cp =
            SendMessageW(pane, kWmOpusX64QuerySelection, 0, 0);
        if (!send_physical_text(L" secondfont") ||
            !send_virtual_key(VK_RIGHT)) {
            return fail(process, 57,
                        "font typing test could not type its second run");
        }
        Sleep(1000);
        const LRESULT second_inserted_ftc = SendMessageW(
            pane, kWmOpusX64QuerySelection, 51, second_cp);
        const LRESULT second_inserted_hps = SendMessageW(
            pane, kWmOpusX64QuerySelection, 52, second_cp);
        const LRESULT first_inserted_hps_after_second = SendMessageW(
            pane, kWmOpusX64QuerySelection, 52, cp_before);
        const LRESULT second_line_height = SendMessageW(
            pane, kWmOpusX64QuerySelection, 33, 0);
        const LRESULT second_formatted_height = SendMessageW(
            pane, kWmOpusX64QuerySelection, 55, second_cp);
        const LRESULT second_formatter_hps = SendMessageW(
            pane, kWmOpusX64QuerySelection, 56, second_cp);
        const LRESULT second_font_height = SendMessageW(
            pane, kWmOpusX64QuerySelection, 57, second_cp);
        const LRESULT formatted_chp_hps = SendMessageW(
            pane, kWmOpusX64QuerySelection, 59, second_cp);
        const LRESULT formatted_fcid_hps = SendMessageW(
            pane, kWmOpusX64QuerySelection, 60, second_cp);
        const LRESULT formatted_chp_count = SendMessageW(
            pane, kWmOpusX64QuerySelection, 61, second_cp);
        const LRESULT formatted_ich_max = SendMessageW(
            pane, kWmOpusX64QuerySelection, 62, second_cp);
        const LRESULT formatted_cp_first = SendMessageW(
            pane, kWmOpusX64QuerySelection, 13, 0);
        const LRESULT formatted_cp_lim = SendMessageW(
            pane, kWmOpusX64QuerySelection, 14, 0);
        const LRESULT formatted_ich_count = SendMessageW(
            pane, kWmOpusX64QuerySelection, 17, 0);
        std::cerr << " second=" << second_ftc << ',' << second_hps
                  << " masterFont=" << second_ibst
                  << " secondInserted=" << second_inserted_ftc << ','
                  << second_inserted_hps << " firstStill="
                  << first_inserted_hps_after_second << " lineHeight="
                  << second_line_height << " formatted="
                  << second_formatted_height << " formatter="
                  << second_formatter_hps << ',' << second_font_height
                  << " formattedRuns=" << formatted_chp_count << ':'
                  << formatted_chp_hps << ',' << formatted_fcid_hps << '@'
                  << formatted_ich_max << " lineRange="
                  << formatted_cp_first << ',' << formatted_cp_lim << '/'
                  << formatted_ich_count
                  << '\n';
        if (!window_is_responsive(process.hProcess, main_window) ||
            second_ftc < 0 || second_ftc == applied_ftc ||
            second_hps != 72 || second_inserted_ftc != second_ftc ||
            second_inserted_hps != second_hps || first_line_height <= 0 ||
            second_formatted_height <= first_formatted_height ||
            formatted_chp_hps != 72 || formatted_fcid_hps != 72) {
            return fail(process, 58,
                        "the second typed run did not retain font and size");
        }

        const std::size_t mixed_line_pixels =
            count_dark_client_pixels(pane, 0, 300);
        if (!send_physical_text(L"\r")) {
            return fail(process, 59,
                        "font typing test could not start its next line");
        }
        Sleep(400);
        const std::size_t after_enter_pixels =
            count_dark_client_pixels(pane, 0, 300);
        InvalidateRect(pane, nullptr, TRUE);
        UpdateWindow(pane);
        Sleep(400);
        const std::size_t after_forced_repaint_pixels =
            count_dark_client_pixels(pane, 0, 300);
        /* Client-y offset from document layout y=0 to this pane's client
           pixel coordinates (i.e. the page top margin), measured directly
           at this exact point in the test with a temporary 5px sweep of
           count_dark_client_pixels() over [120,240):
             sweep5=120:0 125:0 130:0 135:0 140:21 145:14 150:29 155:169
             160:140 165:112 170:97 175:0 180:3 185:61 190:118 195:512
             200:287 205:313 210:276 215:402 220:0 225:3 230:0 235:18
           and the layout geometry queried at the same instant (query
           codes 30/32/33, same as the displayLines diagnostic below):
             probeLines=3 [0 y=0 h=36] [1 y=36 h=54] [2 y=90 h=16]
           Dark pixels begin at client y=140 and the line 0/line 1 gap
           falls at client y=175-180 -- both match layout y=0/h=36 (line
           0) and y=36/h=54 (line 1) exactly under offset=140: line 0 ->
           client [140,176), line 1 -> client [176,230). A second,
           independent sweep taken later (after typing the 144hps large
           line) reproduces the identical [140,220) shape for these same
           two lines and shows the large line's own content beginning at
           client y=230, matching its layout y=90 + this same 140px
           offset. Both sweeps are recorded verbatim in
           docs/port-linux/03-comportamiento-word1-startup-blocked.md §8.

           That sweep predates kCcpEop=2 (see docs/port-linux/CLAUDE.md
           status and 01-diagnostico-heap-corruption-arranque.md §30): it
           was taken while opus_asm_resn_core.cpp's CpMacDoc/CpMacDocEdit
           still used the wrong kCcpEop=1, under which paragraph 1 (the
           "fonttest"+" secondfont" mixed-font run) wrapped into 2 display
           lines (probeLines=3 total, including the fresh empty paragraph
           after Enter). Under the correct kCcpEop=2, paragraph 1 fits on
           a single line -- early_display_line_count is 2, not 3, right
           after Enter: line 0 is paragraph 1 (unwrapped), line 1 is the
           fresh, still-empty paragraph the caret now sits in. The second
           band below is therefore expected to read ~0 dark pixels at
           this point (nothing has been typed into it yet) -- only line
           0's band is asserted non-empty here. */
        constexpr int kPageTopMarginY = 140;
        const LRESULT early_display_line_count = SendMessageW(
            pane, kWmOpusX64QuerySelection, 30, 0);
        std::size_t after_enter_first_band = 0;
        std::size_t after_enter_second_band = 0;
        if (early_display_line_count >= 2) {
            const LRESULT line0_y = SendMessageW(
                pane, kWmOpusX64QuerySelection, 32, 0);
            const LRESULT line0_h = SendMessageW(
                pane, kWmOpusX64QuerySelection, 33, 0);
            const LRESULT line1_y = SendMessageW(
                pane, kWmOpusX64QuerySelection, 32, 1);
            const LRESULT line1_h = SendMessageW(
                pane, kWmOpusX64QuerySelection, 33, 1);
            after_enter_first_band = count_dark_client_pixels(
                pane, kPageTopMarginY + static_cast<int>(line0_y),
                kPageTopMarginY + static_cast<int>(line0_y + line0_h));
            after_enter_second_band = count_dark_client_pixels(
                pane, kPageTopMarginY + static_cast<int>(line1_y),
                kPageTopMarginY + static_cast<int>(line1_y + line1_h));
        }
        const LRESULT editable_cp_mac = SendMessageW(
            pane, kWmOpusX64QuerySelection, 41, 0);
        bool fetch_bytes_match = true;
        LRESULT fetch_mismatch_cp = -1;
        LRESULT fetch_raw = -1;
        LRESULT fetch_formatted = -1;
        for (LRESULT cp = 0; cp < editable_cp_mac; ++cp) {
            fetch_raw = SendMessageW(
                pane, kWmOpusX64QuerySelection, 69, cp);
            fetch_formatted = SendMessageW(
                pane, kWmOpusX64QuerySelection, 70, cp);
            if (fetch_raw != fetch_formatted) {
                fetch_bytes_match = false;
                fetch_mismatch_cp = cp;
                break;
            }
        }
        const LRESULT cache_pages = SendMessageW(
            pane, kWmOpusX64QuerySelection, 71, 0);
        const bool cache_pages_separate =
            LOWORD(cache_pages) != HIWORD(cache_pages);
        if (!choose_combo_item_with_mouse(size_combo, large_size_index) ||
            !wait_for_focus(process.hProcess, thread_id, pane, 1500)) {
            return fail(process, 59,
                        "font typing test could not start its larger line");
        }
        const std::size_t after_size_pixels =
            count_dark_client_pixels(pane, 0, 300);
        const LRESULT large_cp =
            SendMessageW(pane, kWmOpusX64QuerySelection, 0, 0);
        if (!send_physical_text(L"largeline") ||
            !send_virtual_key(VK_RIGHT)) {
            return fail(process, 59,
                        "font typing test could not type its larger line");
        }
        Sleep(1200);
        const std::size_t large_line_pixels =
            count_dark_client_pixels(pane, 0, 450);
        const std::size_t large_line_band_pixels =
            count_dark_client_pixels(pane, 131, 292);
        const LRESULT large_inserted_ftc = SendMessageW(
            pane, kWmOpusX64QuerySelection, 51, large_cp);
        const LRESULT large_inserted_hps = SendMessageW(
            pane, kWmOpusX64QuerySelection, 52, large_cp);
        const LRESULT display_line_count = SendMessageW(
            pane, kWmOpusX64QuerySelection, 30, 0);
        std::cerr << " visualPixels=" << mixed_line_pixels << "->"
                  << after_enter_pixels << "->" << after_size_pixels << "->"
                  << large_line_pixels << " larger=" << large_inserted_ftc
                  << ',' << large_inserted_hps << " displayLines="
                  << display_line_count << " bands="
                  << after_enter_first_band << ','
                  << after_enter_second_band << " repaint="
                  << after_forced_repaint_pixels << " largeBand="
                  << large_line_band_pixels << " fetch="
                  << fetch_bytes_match << '/' << cache_pages_separate << '@'
                  << fetch_mismatch_cp << ':' << fetch_raw << '/'
                  << fetch_formatted
                  << " dr="
                  << SendMessageW(pane, kWmOpusX64QuerySelection, 36, 0)
                  << ','
                  << SendMessageW(pane, kWmOpusX64QuerySelection, 37, 0)
                  << " dirty="
                  << SendMessageW(pane, kWmOpusX64QuerySelection, 39, 0)
                  << ','
                  << SendMessageW(pane, kWmOpusX64QuerySelection, 40, 0);
        for (LRESULT line = 0; line < display_line_count; ++line) {
            std::cerr << " [" << line << " cp="
                      << SendMessageW(pane, kWmOpusX64QuerySelection, 31,
                                      line)
                      << " y="
                      << SendMessageW(pane, kWmOpusX64QuerySelection, 32,
                                      line)
                      << " h="
                      << SendMessageW(pane, kWmOpusX64QuerySelection, 33,
                                      line)
                      << " n="
                      << SendMessageW(pane, kWmOpusX64QuerySelection, 34,
                                      line)
                      << " dirty="
                      << SendMessageW(pane, kWmOpusX64QuerySelection, 35,
                                      line)
                      << ']';
        }
        std::cerr << '\n';
        /* display_line_count: 2 lines under the correct kCcpEop=2 layout
           (paragraph 1 unwrapped + paragraph 2 holding "largeline"), not
           the 3 lines the stale kCcpEop=1 sweep above recorded -- see the
           comment on early_display_line_count. after_enter_second_band
           is deliberately not asserted non-empty: it was measured right
           after Enter, before "largeline" was typed, so it is line 1's
           still-empty paragraph and legitimately reads ~0. */
        if (large_inserted_ftc != second_ftc || large_inserted_hps != 144 ||
            display_line_count < 2 || mixed_line_pixels == 0 ||
            after_enter_first_band == 0 ||
            after_forced_repaint_pixels * 4 < mixed_line_pixels * 3 ||
            large_line_band_pixels == 0 ||
            large_line_pixels <= after_forced_repaint_pixels ||
            !fetch_bytes_match || !cache_pages_separate) {
            return fail(process, 60,
                        "mixed-font lines disappeared after resizing");
        }

        POINT resize_drag_start{
            static_cast<LONG>(SendMessageW(
                pane, kWmOpusX64QuerySelection, 4, 0)),
            static_cast<LONG>(SendMessageW(
                pane, kWmOpusX64QuerySelection, 7, 0)) - 2};
        POINT resize_drag_end{5, 5};
        if (!make_foreground_and_focus(main_window, pane, thread_id) ||
            !ClientToScreen(pane, &resize_drag_start) ||
            !ClientToScreen(pane, &resize_drag_end) ||
            !SetCursorPos(resize_drag_start.x, resize_drag_start.y) ||
            !send_mouse_button(MOUSEEVENTF_LEFTDOWN) ||
            !wait_for_capture(process.hProcess, thread_id, pane, 1500) ||
            !SetCursorPos(resize_drag_end.x, resize_drag_end.y)) {
            send_mouse_button(MOUSEEVENTF_LEFTUP);
            return fail(process, 61,
                        "font typing test could not select the document");
        }
        Sleep(200);
        send_mouse_button(MOUSEEVENTF_LEFTUP);
        Sleep(300);
        const LRESULT selected_first = SendMessageW(
            pane, kWmOpusX64QuerySelection, 0, 0);
        const LRESULT selected_lim = SendMessageW(
            pane, kWmOpusX64QuerySelection, 1, 0);
        std::cerr << " selectionBeforeResize=" << selected_first << ','
                  << selected_lim << '\n';
        if (selected_lim <= selected_first ||
            !make_foreground_and_focus(main_window, size_combo, thread_id) ||
            !send_control_key('A') || !send_physical_text(L"48") ||
            !make_foreground_and_focus(main_window, pane, thread_id)) {
            return fail(process, 61,
                        "font typing test could not resize selected text");
        }
        Sleep(1200);
        const LRESULT selected_first_hps = SendMessageW(
            pane, kWmOpusX64QuerySelection, 52, cp_before);
        const LRESULT selected_second_hps = SendMessageW(
            pane, kWmOpusX64QuerySelection, 52, second_cp);
        const LRESULT selected_formatted_height = SendMessageW(
            pane, kWmOpusX64QuerySelection, 55, cp_before);
        std::cerr << " selected=" << selected_first << ',' << selected_lim
                  << " resized=" << selected_first_hps << ','
                  << selected_second_hps << " formatted="
                  << selected_formatted_height << '\n';
        if (selected_first_hps != 96 || selected_second_hps != 96 ||
            selected_formatted_height <= second_formatted_height) {
            return fail(process, 62,
                        "selected text did not retain its point size");
        }

        if (!make_foreground_and_focus(main_window, size_combo, thread_id) ||
            SendMessageW(size_combo, CB_SHOWDROPDOWN, TRUE, 0) == 0) {
            return fail(process, 63,
                        "font typing test could not open the Points list");
        }
        Sleep(200);
        COMBOBOXINFO size_info{};
        size_info.cbSize = sizeof(size_info);
        RECT size_list_rectangle{};
        const LRESULT size_item_height = SendMessageW(
            size_combo, CB_GETITEMHEIGHT, 0, 0);
        if (!GetComboBoxInfo(size_combo, &size_info) ||
            size_info.hwndList == nullptr || size_item_height <= 0 ||
            !GetWindowRect(size_info.hwndList, &size_list_rectangle)) {
            return fail(process, 63,
                        "font typing test could not locate the Points list");
        }
        const LRESULT requested_top =
            (std::max)(0LL, static_cast<long long>(listed_size_index) - 2);
        SendMessageW(size_info.hwndList, LB_SETTOPINDEX, requested_top, 0);
        const LRESULT actual_top = SendMessageW(
            size_info.hwndList, LB_GETTOPINDEX, 0, 0);
        POINT size_list_choice{
            size_list_rectangle.left + 12,
            size_list_rectangle.top + 2 +
                static_cast<LONG>((listed_size_index - actual_top) *
                                  size_item_height + size_item_height / 2)};
        std::cerr << " pointsList=" << size_list_rectangle.left << ','
                  << size_list_rectangle.top << ','
                  << size_list_rectangle.right << ','
                  << size_list_rectangle.bottom << " item="
                  << size_item_height << " index=" << listed_size_index
                  << " top=" << actual_top << " choice="
                  << size_list_choice.x << ',' << size_list_choice.y << '\n';
        if (size_list_choice.y >= size_list_rectangle.bottom ||
            !SetCursorPos(size_list_choice.x, size_list_choice.y) ||
            !send_mouse_button(MOUSEEVENTF_LEFTDOWN) ||
            !send_mouse_button(MOUSEEVENTF_LEFTUP)) {
            return fail(process, 63,
                        "font typing test could not commit Points list");
        }
        Sleep(600);
        DWORD list_exit_code = STILL_ACTIVE;
        GetExitCodeProcess(process.hProcess, &list_exit_code);
        const bool process_after_list =
            WaitForSingleObject(process.hProcess, 0) != WAIT_OBJECT_0;
        const bool returned_to_document = process_after_list &&
            (make_foreground_and_focus(main_window, pane, thread_id) ||
             make_foreground_and_focus(main_window, pane, thread_id));
        Sleep(1200);
        const LRESULT listed_first_hps = SendMessageW(
            pane, kWmOpusX64QuerySelection, 52, cp_before);
        const LRESULT listed_second_hps = SendMessageW(
            pane, kWmOpusX64QuerySelection, 52, second_cp);
        const LRESULT listed_formatted_height = SendMessageW(
            pane, kWmOpusX64QuerySelection, 55, cp_before);
        std::cerr << " listedResize=" << listed_first_hps << ','
                  << listed_second_hps << " formatted="
                  << listed_formatted_height << " documentFocus="
                  << returned_to_document << " process="
                  << process_after_list << "/0x" << std::hex
                  << list_exit_code << std::dec << " windows="
                  << IsWindow(main_window) << ',' << IsWindow(pane) << '\n';
        if (listed_first_hps != 144 || listed_second_hps != 144 ||
            listed_formatted_height <= selected_formatted_height) {
            return fail(process, 64,
                        "Points list did not resize selected text");
        }
        TerminateProcess(process.hProcess, 0);
        WaitForSingleObject(process.hProcess, 2000);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return 0;
    }
    if (formatting_mode) {
        const HWND pane = find_descendant_by_class(main_window, L"OpusWwd");
        DWORD ignored_process_id = 0;
        const DWORD thread_id =
            GetWindowThreadProcessId(main_window, &ignored_process_id);
        if (pane == nullptr ||
            !make_foreground_and_focus(main_window, pane, thread_id) ||
            !send_physical_text(L"formatting selection")) {
            return fail(process, 43,
                        "formatting test could not type its selection");
        }
        Sleep(500);
        POINT drag_start{155, 10};
        POINT drag_end{20, 10};
        if (!ClientToScreen(pane, &drag_start) ||
            !ClientToScreen(pane, &drag_end) ||
            !SetCursorPos(drag_start.x, drag_start.y) ||
            !send_mouse_button(MOUSEEVENTF_LEFTDOWN) ||
            !wait_for_capture(process.hProcess, thread_id, pane, 1500) ||
            !SetCursorPos(drag_end.x, drag_end.y)) {
            send_mouse_button(MOUSEEVENTF_LEFTUP);
            return fail(process, 44,
                        "formatting test could not drag a selection");
        }
        Sleep(150);
        send_mouse_button(MOUSEEVENTF_LEFTUP);
        Sleep(500);
        const LRESULT selected_first =
            SendMessageW(pane, kWmOpusX64QuerySelection, 0, 0);
        const LRESULT selected_lim =
            SendMessageW(pane, kWmOpusX64QuerySelection, 1, 0);
        std::cerr << "format selection=" << selected_first << ','
                  << selected_lim << '\n';
        if (selected_lim <= selected_first ||
            !PostMessageW(main_window, kWmCommand, kParaCenter, 0)) {
            return fail(process, 45,
                        "formatting test could not apply centered alignment");
        }
        Sleep(1500);
        if (!window_is_responsive(process.hProcess, main_window)) {
            return fail(process, 46,
                        "centered alignment crashed or hung WORD1");
        }
        TerminateProcess(process.hProcess, 0);
        WaitForSingleObject(process.hProcess, 2000);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return 0;
    }
    if (color_mode) {
        const HWND pane = find_descendant_by_class(main_window, L"OpusWwd");
        DWORD ignored_process_id = 0;
        const DWORD thread_id =
            GetWindowThreadProcessId(main_window, &ignored_process_id);
        if (pane == nullptr ||
            !make_foreground_and_focus(main_window, pane, thread_id) ||
            !send_physical_text(L"testtest") ||
            !execute_control_shortcut(pane, 'A')) {
            return fail(process, 76,
                        "color test could not create its selection");
        }
        Sleep(500);
        const LRESULT selected_first = SendMessageW(
            pane, kWmOpusX64QuerySelection, 0, 0);
        const LRESULT selected_lim = SendMessageW(
            pane, kWmOpusX64QuerySelection, 1, 0);
        const LRESULT before = SendMessageW(
            pane, kWmOpusX64QuerySelection, 83, 0);
        const LRESULT prm_before = SendMessageW(
            pane, kWmOpusX64QuerySelection, 85, 0);
        const LRESULT applied = SendMessageW(
            pane, kWmOpusX64QuerySelection, 82, 6);
        Sleep(500);
        const LRESULT after = SendMessageW(
            pane, kWmOpusX64QuerySelection, 83, 0);
        const LRESULT selection_color = SendMessageW(
            pane, kWmOpusX64QuerySelection, 79, 0);
        const LRESULT prm_after = SendMessageW(
            pane, kWmOpusX64QuerySelection, 85, 0);
        const LRESULT selection_style = SendMessageW(
            pane, kWmOpusX64QuerySelection, 86, 0);
        std::cerr << "color selection=" << selected_first << ','
                  << selected_lim << " property=" << before << "->" << after
                  << " selectionColor=" << selection_color
                  << " applied=" << applied << " prm=0x" << std::hex
                  << prm_before << "->0x" << prm_after << std::dec
                  << " style=" << selection_style << '\n';
        if (selected_lim <= selected_first || applied == 0 || after != 6) {
            return fail(process, 77,
                        "text color was not stored in the selected characters");
        }
        TerminateProcess(process.hProcess, 0);
        WaitForSingleObject(process.hProcess, 2000);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return 0;
    }
    if (caret_mode) {
        const HWND pane = find_descendant_by_class(main_window, L"OpusWwd");
        if (pane == nullptr) {
            return fail(process, 40, "caret test could not find OpusWwd");
        }
        DWORD ignored_process_id = 0;
        const DWORD thread_id =
            GetWindowThreadProcessId(main_window, &ignored_process_id);
        if (!make_foreground_and_focus(main_window, pane, thread_id)) {
            return fail(process, 41,
                        "caret test could not focus the document pane");
        }
        const auto report = [pane](const char* stage) {
            std::cerr << stage
                      << " cp=" << SendMessageW(
                             pane, kWmOpusX64QuerySelection, 0, 0)
                      << " xw=" << SendMessageW(
                             pane, kWmOpusX64QuerySelection, 4, 0)
                      << " yw=" << SendMessageW(
                             pane, kWmOpusX64QuerySelection, 7, 0)
                      << " dyp=" << SendMessageW(
                             pane, kWmOpusX64QuerySelection, 8, 0)
                      << " on=" << SendMessageW(
                             pane, kWmOpusX64QuerySelection, 9, 0)
                      << " hidden=" << SendMessageW(
                             pane, kWmOpusX64QuerySelection, 10, 0)
                      << " xpFirst=" << SendMessageW(
                             pane, kWmOpusX64QuerySelection, 11, 0)
                      << " xpLim=" << SendMessageW(
                             pane, kWmOpusX64QuerySelection, 12, 0)
                      << " fliCp=" << SendMessageW(
                             pane, kWmOpusX64QuerySelection, 13, 0)
                      << ',' << SendMessageW(
                             pane, kWmOpusX64QuerySelection, 14, 0)
                      << " fliXp=" << SendMessageW(
                             pane, kWmOpusX64QuerySelection, 15, 0)
                      << ',' << SendMessageW(
                             pane, kWmOpusX64QuerySelection, 16, 0)
                      << " ich=" << SendMessageW(
                             pane, kWmOpusX64QuerySelection, 17, 0)
                      << " dxp=" << SendMessageW(
                             pane, kWmOpusX64QuerySelection, 18, 0)
                      << ',' << SendMessageW(
                             pane, kWmOpusX64QuerySelection, 19, 0)
                      << " chrSizes=" << SendMessageW(
                             pane, kWmOpusX64QuerySelection, 24, 0)
                      << ',' << SendMessageW(
                             pane, kWmOpusX64QuerySelection, 25, 0)
                      << ',' << SendMessageW(
                             pane, kWmOpusX64QuerySelection, 26, 0)
                      << ',' << SendMessageW(
                             pane, kWmOpusX64QuerySelection, 27, 0)
                      << ',' << SendMessageW(
                             pane, kWmOpusX64QuerySelection, 28, 0)
                      << ',' << SendMessageW(
                             pane, kWmOpusX64QuerySelection, 29, 0)
                      << '\n';
        };
        const LRESULT initial_x =
            SendMessageW(pane, kWmOpusX64QuerySelection, 4, 0);
        const LRESULT initial_y =
            SendMessageW(pane, kWmOpusX64QuerySelection, 7, 0);
        report("initial");
        if (!send_physical_text(L"ab\rc")) {
            return fail(process, 41, "caret test could not send its keys");
        }
        // A cursor key leaves the original quick-insert loop and commits its
        // reserved 32-byte piece before the diagnostic query reads selCur.
        // At the document end VK_RIGHT does not change the insertion CP.
        if (!send_virtual_key(VK_RIGHT)) {
            return fail(process, 41,
                        "caret test could not commit the insert loop");
        }
        Sleep(1000);
        const LRESULT final_cp =
            SendMessageW(pane, kWmOpusX64QuerySelection, 0, 0);
        const LRESULT final_x =
            SendMessageW(pane, kWmOpusX64QuerySelection, 4, 0);
        const LRESULT final_y =
            SendMessageW(pane, kWmOpusX64QuerySelection, 7, 0);
        const LRESULT final_ins =
            SendMessageW(pane, kWmOpusX64QuerySelection, 2, 0);
        report("final");
        if (final_cp != 5 || final_ins != 1 || final_x <= initial_x ||
            final_y <= initial_y) {
            return fail(process, 42,
                        "typing did not advance the caret across a paragraph");
        }
        TerminateProcess(process.hProcess, 0);
        WaitForSingleObject(process.hProcess, 2000);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return 0;
    }
    if (selection_mode) {
        const HWND pane = find_descendant_by_class(main_window, L"OpusWwd");
        if (pane == nullptr) {
            return fail(process, 36, "selection test could not find OpusWwd");
        }
        DWORD selection_ignored_process_id = 0;
        const DWORD selection_thread_id = GetWindowThreadProcessId(
            main_window, &selection_ignored_process_id);
        /* Every other real-input (SetCursorPos/SendInput) block in this
           file calls this first -- SendInput routes through the OS to
           whichever window actually has focus/activation, unlike
           SendMessageW/PostMessageW. Without it, the clicks below still
           reach WORD1 (the sentence types fine via post_keyboard_character,
           a WM_CHAR post) but resolve to cp=0 regardless of x, the same
           symptom choose_combo_item_with_mouse's callers guard against. */
        make_foreground_and_focus(main_window, pane, selection_thread_id);
        const wchar_t* const sentence = L"physical keyboard input line one";
        const int sentence_length = lstrlenW(sentence);
        for (int index = 0; index < sentence_length; ++index) {
            const wchar_t character = sentence[index];
            if (!post_keyboard_character(pane, character)) {
                return fail(process, 37,
                            "selection test could not post its sentence");
            }
            Sleep(12);
        }
        Sleep(750);
        const LRESULT typed_first =
            SendMessageW(pane, kWmOpusX64QuerySelection, 0, 0);
        const LRESULT typed_lim =
            SendMessageW(pane, kWmOpusX64QuerySelection, 1, 0);
        const LRESULT typed_ins =
            SendMessageW(pane, kWmOpusX64QuerySelection, 2, 0);
        SendMessageW(pane, kWmOpusX64QuerySelection, 55, 0);
        std::cerr << "widths:";
        for (LPARAM ich = 0; ich != 16; ++ich) {
            std::cerr << ' ' << SendMessageW(
                pane, kWmOpusX64QuerySelection, 63, ich);
        }
        std::cerr << " drWidths:";
        for (LPARAM ich = 0; ich != 16; ++ich) {
            std::cerr << ' ' << SendMessageW(
                pane, kWmOpusX64QuerySelection, 64, ich);
        }
        std::cerr << " dpi="
                  << SendMessageW(pane, kWmOpusX64QuerySelection, 65, 0)
                  << ','
                  << SendMessageW(pane, kWmOpusX64QuerySelection, 66, 0)
                  << ','
                  << SendMessageW(pane, kWmOpusX64QuerySelection, 67, 0)
                  << " line0ypTop=" << SendMessageW(
                         pane, kWmOpusX64QuerySelection, 32, 0)
                  << " line0dyp=" << SendMessageW(
                         pane, kWmOpusX64QuerySelection, 33, 0)
                  << " mappings:";
        /* Real input (SetCursorPos + SendInput), not SendMessageW/
           PostMessageW -- WwPaneMouse (Opus/wproc.c) needs the same real
           focus/activation make_foreground_and_focus (above) provides for
           every other real-input block in this file; without both, every
           click here resolved to cp=0 regardless of x. */
        int sentence_end_x = 250;
        bool found_sentence_end_x = false;
        for (int x = 10; x <= 450; x += 10) {
            POINT probe{x, 10};
            if (ClientToScreen(pane, &probe) && SetCursorPos(probe.x, probe.y)) {
                send_mouse_button(MOUSEEVENTF_LEFTDOWN);
                Sleep(20);
                send_mouse_button(MOUSEEVENTF_LEFTUP);
            }
            const LRESULT mapped_cp = SendMessageW(
                pane, kWmOpusX64QuerySelection, 0, 0);
            std::cerr << ' ' << x << '=' << mapped_cp;
            /* The left margin before the first character isn't a constant
               this test can assume (measured ~185-190px here, not 0) --
               find a real x past it instead of guessing one. First x that
               reaches at least half the typed sentence is "near the end"
               without needing to know the exact right margin too. */
            if (!found_sentence_end_x &&
                mapped_cp >= sentence_length / 2) {
                sentence_end_x = x;
                found_sentence_end_x = true;
            }
            Sleep(60);
        }
        std::cerr << " sentence_end_x=" << sentence_end_x << '\n';
        /* The probe loop above is real SendInput clicks close together in
           time and position; without a gap exceeding the system's
           double-click interval, Wine/Win32 merges the next click into a
           double-click (clicked_double below comes back nonzero) instead
           of a fresh single click, which this assertion needs. */
        Sleep(static_cast<DWORD>(GetDoubleClickTime()) + 150);
        POINT sentence_end_click{sentence_end_x, 10};
        if (ClientToScreen(pane, &sentence_end_click) &&
            SetCursorPos(sentence_end_click.x, sentence_end_click.y)) {
            send_mouse_button(MOUSEEVENTF_LEFTDOWN);
            Sleep(20);
            send_mouse_button(MOUSEEVENTF_LEFTUP);
        }
        Sleep(500);
        const LRESULT clicked_first =
            SendMessageW(pane, kWmOpusX64QuerySelection, 0, 0);
        const LRESULT clicked_lim =
            SendMessageW(pane, kWmOpusX64QuerySelection, 1, 0);
        const LRESULT clicked_ins =
            SendMessageW(pane, kWmOpusX64QuerySelection, 2, 0);
        const LRESULT clicked_style =
            SendMessageW(pane, kWmOpusX64QuerySelection, 3, 0);
        const LRESULT clicked_xw =
            SendMessageW(pane, kWmOpusX64QuerySelection, 4, 0);
        const LRESULT clicked_double =
            SendMessageW(pane, kWmOpusX64QuerySelection, 5, 0);
        const LRESULT clicked_sk =
            SendMessageW(pane, kWmOpusX64QuerySelection, 6, 0);
        std::cerr << "typed=" << typed_first << ',' << typed_lim << ','
                  << typed_ins << " clicked=" << clicked_first << ','
                  << clicked_lim << ',' << clicked_ins
                  << " style=" << clicked_style << " xw=" << clicked_xw
                  << " double=" << clicked_double << " sk=" << clicked_sk
                  << '\n';
        if (typed_first != static_cast<LRESULT>(sentence_length) ||
            typed_lim != typed_first || typed_ins != 1) {
            return fail(process, 38,
                        "typing did not leave a canonical insertion selection");
        }
        if (clicked_first < 15 || clicked_first > typed_first ||
            clicked_lim != clicked_first || clicked_ins != 1 ||
            clicked_double != 0 || clicked_sk != 32) {
            return fail(process, 39,
                        "sentence-end click produced an invalid selection");
        }
        TerminateProcess(process.hProcess, 0);
        WaitForSingleObject(process.hProcess, 2000);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return 0;
    }
    if (interaction_mode) {
        DWORD ignored_process_id = 0;
        const DWORD thread_id =
            GetWindowThreadProcessId(main_window, &ignored_process_id);

        if (IsZoomed(main_window)) {
            PostMessageW(main_window, WM_SYSCOMMAND, SC_RESTORE, 0);
            if (!wait_for_zoom_state(process.hProcess, main_window, false,
                                     3000)) {
                return fail(process, 17,
                            "SC_RESTORE was swallowed by the Opus handler");
            }
        }
        if (!PostMessageW(main_window, WM_SYSCOMMAND, SC_MAXIMIZE, 0) ||
            !wait_for_zoom_state(process.hProcess, main_window, true, 3000)) {
            return fail(process, 18,
                        "SC_MAXIMIZE was swallowed by the Opus handler");
        }
        PostMessageW(main_window, WM_SYSCOMMAND, SC_RESTORE, 0);
        if (!wait_for_zoom_state(process.hProcess, main_window, false, 3000)) {
            return fail(process, 19, "WORD1 did not restore from maximize");
        }

        const HWND pane = find_descendant_by_class(main_window, L"OpusWwd");
        RECT window_before_move{};
        if (pane == nullptr ||
            !make_foreground_and_focus(main_window, pane, thread_id) ||
            !GetWindowRect(main_window, &window_before_move)) {
            return fail(process, 30,
                        "could not prepare the native window move test");
        }
        POINT client_origin{};
        if (!ClientToScreen(main_window, &client_origin)) {
            return fail(process, 30,
                        "could not locate the WORD1 nonclient area");
        }
        POINT caption_point{(window_before_move.left + window_before_move.right) /
                                2,
                            -1};
        for (int y = window_before_move.top; y < client_origin.y; ++y) {
            if (SendMessageW(main_window, WM_NCHITTEST, 0,
                             MAKELPARAM(caption_point.x, y)) == HTCAPTION) {
                caption_point.y = y;
                break;
            }
        }
        if (caption_point.y < 0 ||
            !SetCursorPos(caption_point.x, caption_point.y) ||
            !send_mouse_button(MOUSEEVENTF_LEFTDOWN)) {
            return fail(process, 30,
                        "WORD1 did not expose a draggable caption");
        }
        Sleep(100);
        /* A single SetCursorPos teleport from down to up may not clear
           whatever drag threshold (SM_CXDRAG/SM_CYDRAG) or intermediate
           WM_MOUSEMOVE count Wine's internal SC_MOVE loop expects before
           it commits to a real move -- step through it like a real drag
           instead of jumping straight to the end point. */
        for (int step = 1; step <= 8; ++step) {
            SetCursorPos(caption_point.x + step * 5,
                        caption_point.y + step * 3);
            Sleep(15);
        }
        Sleep(100);
        send_mouse_button(MOUSEEVENTF_LEFTUP);
        Sleep(500);
        RECT window_after_move{};
        if (!GetWindowRect(main_window, &window_after_move) ||
            (window_before_move.left == window_after_move.left &&
             window_before_move.top == window_after_move.top)) {
            /* Confirmed environment limitation, not a WORD1 bug: this
               exact SetCursorPos/SendInput drag sequence -- WM_NCHITTEST
               correctly reports HTCAPTION -- also fails to move Wine's
               own builtin notepad.exe under this same Xvfb+openbox
               setup (standalone winegcc repro, no project code). Same
               class of finding as the CreateProcessW zero-PID case
               (docs/port-linux/01-diagnostico-heap-corruption-arranque.md
               §25). Ruled out first: no window manager (retested under
               real openbox, same result) and a single SetCursorPos
               teleport instead of incremental movement (also same
               result) -- neither was the cause. */
            std::cerr << "before=" << window_before_move.left << ','
                      << window_before_move.top << " after="
                      << window_after_move.left << ','
                      << window_after_move.top << " caption_point="
                      << caption_point.x << ',' << caption_point.y << '\n';
            return fail(process, 32,
                        "dragging the caption did not move the WORD1 window");
        }

        if (!PostMessageW(main_window, WM_SYSCOMMAND, SC_KEYMENU, L'f') ||
            !wait_for_gui_flag(process.hProcess, thread_id, GUI_INMENUMODE,
                               true, 3000)) {
            return fail(process, 20,
                        "the File menu did not enter native menu mode");
        }
        if (!post_keyboard_character(main_window, L'n')) {
            return fail(process, 21, "could not choose File New by mnemonic");
        }
        const HWND new_dialog = wait_for_window(
            process.hProcess, process.dwProcessId, L"OpusSdmDialog", L"New",
            5000);
        if (new_dialog == nullptr) {
            return fail(process, 22,
                        "File New did not execute through the real menu loop");
        }
        if (!PostMessageW(new_dialog, kWmCommand, 2, 0) ||
            !wait_for_window_to_close(process.hProcess, process.dwProcessId,
                                      L"OpusSdmDialog", 5000)) {
            return fail(process, 23, "File New dialog did not cancel");
        }

        if (!make_foreground_and_focus(main_window, pane, thread_id)) {
            return fail(process, 24,
                        "could not give real keyboard focus to the document");
        }
        const wchar_t* const physical_text =
            L"physical keyboard input line one\rphysical keyboard input "
            L"line two\rphysical keyboard input line three";
        const LRESULT cp_before_typing =
            SendMessageW(pane, kWmOpusX64QuerySelection, 0, 0);
        const LRESULT caret_x_before_typing =
            SendMessageW(pane, kWmOpusX64QuerySelection, 4, 0);
        const LRESULT caret_y_before_typing =
            SendMessageW(pane, kWmOpusX64QuerySelection, 7, 0);
        const std::size_t dark_pixels_before = count_dark_client_pixels(pane);
        if (!send_physical_text(physical_text)) {
            return fail(process, 25, "SendInput could not type into WORD1");
        }
        // SendInput returns after placing events in the GUI input queue.  Give
        // the original insert loop time to drain the complete long sequence
        // before a synchronous diagnostic message can overtake its tail.
        Sleep(2500);
        const LRESULT typed_cp_first =
            SendMessageW(pane, kWmOpusX64QuerySelection, 0, 0);
        // The first non-key message commits Word's quick-insert block.  Let
        // its normal idle pass finish rebuilding all display lines before
        // issuing more cross-thread diagnostic messages.
        Sleep(750);
        const LRESULT typed_cp_lim =
            SendMessageW(pane, kWmOpusX64QuerySelection, 1, 0);
        const LRESULT typed_is_insertion =
            SendMessageW(pane, kWmOpusX64QuerySelection, 2, 0);
        const LRESULT caret_x_after_typing =
            SendMessageW(pane, kWmOpusX64QuerySelection, 4, 0);
        const LRESULT caret_y_after_typing =
            SendMessageW(pane, kWmOpusX64QuerySelection, 7, 0);
        const LRESULT displayed_line_count =
            SendMessageW(pane, kWmOpusX64QuerySelection, 30, 0);
        std::cerr << "display lines=" << displayed_line_count
				  << " terminalCp="
				  << SendMessageW(pane, kWmOpusX64QuerySelection, 31,
				                  displayed_line_count)
                  << " dr="
                  << SendMessageW(pane, kWmOpusX64QuerySelection, 36, 0)
                  << ','
                  << SendMessageW(pane, kWmOpusX64QuerySelection, 37, 0);
        std::cerr << " dirty="
                  << SendMessageW(pane, kWmOpusX64QuerySelection, 39, 0)
                  << ','
                  << SendMessageW(pane, kWmOpusX64QuerySelection, 40, 0)
                  << " cpMac="
                  << SendMessageW(pane, kWmOpusX64QuerySelection, 41, 0)
				  << '/'
				  << SendMessageW(pane, kWmOpusX64QuerySelection, 46, 0)
                  << " drSize="
                  << SendMessageW(pane, kWmOpusX64QuerySelection, 44, 0)
                  << 'x'
                  << SendMessageW(pane, kWmOpusX64QuerySelection, 42, 0)
                  << " wwSize="
                  << SendMessageW(pane, kWmOpusX64QuerySelection, 45, 0)
                  << 'x'
                  << SendMessageW(pane, kWmOpusX64QuerySelection, 43, 0);
        for (LPARAM line = 0; line < displayed_line_count && line < 8;
             ++line) {
            std::cerr << " [" << line << " cp="
                      << SendMessageW(pane, kWmOpusX64QuerySelection, 31,
                                      line)
                      << " yp="
                      << SendMessageW(pane, kWmOpusX64QuerySelection, 32,
                                      line)
                      << " dyp="
                      << SendMessageW(pane, kWmOpusX64QuerySelection, 33,
                                      line)
                      << " dcp="
                      << SendMessageW(pane, kWmOpusX64QuerySelection, 34,
                                      line)
					  << " dlk="
					  << SendMessageW(pane, kWmOpusX64QuerySelection, 47,
					                  line)
					  << " end="
					  << SendMessageW(pane, kWmOpusX64QuerySelection, 48,
					                  line)
                      << "]";
        }
        std::cerr << '\n';
        const std::size_t dark_pixels_after = count_dark_client_pixels(pane);
        const int physical_text_length = lstrlenW(physical_text);
        LRESULT expected_cp_after_typing =
            cp_before_typing + static_cast<LRESULT>(physical_text_length);
        for (int index = 0; index < physical_text_length; ++index) {
            if (physical_text[index] == L'\r') {
                ++expected_cp_after_typing;
            }
        }
        if (!window_is_responsive(process.hProcess, main_window) ||
            dark_pixels_after < dark_pixels_before + 8) {
            std::cerr << "typing paint state: before=" << dark_pixels_before
                      << " after=" << dark_pixels_after
                      << " cpBefore=" << cp_before_typing
                      << " cpFirst=" << typed_cp_first
                      << " cpLim=" << typed_cp_lim
                      << " fIns=" << typed_is_insertion << '\n';
            return fail(process, 26,
                        "real keyboard input was not painted in the document");
        }

        if (typed_cp_first != expected_cp_after_typing ||
            typed_cp_first != typed_cp_lim || typed_is_insertion != 1 ||
            caret_x_after_typing <= caret_x_before_typing ||
            caret_y_after_typing <= caret_y_before_typing) {
            std::cerr << "selection after typing: cpBefore=" << cp_before_typing
                      << " cpFirst=" << typed_cp_first
                      << " cpLim=" << typed_cp_lim
                      << " fIns=" << typed_is_insertion
                      << " caretBefore=" << caret_x_before_typing << ','
                      << caret_y_before_typing
                      << " caretAfter=" << caret_x_after_typing << ','
                      << caret_y_after_typing << '\n';
            return fail(process, 33,
                        "typing did not leave a valid insertion selection");
        }

        RECT window_rectangle{};
        const LRESULT lines_before_move = SendMessageW(
            pane, kWmOpusX64QuerySelection, 30, 0);
        POINT populated_client_origin{};
        if (!GetWindowRect(main_window, &window_rectangle) ||
            !ClientToScreen(main_window, &populated_client_origin)) {
            return fail(process, 59, "could not move the populated window");
        }
        POINT populated_caption{
            (window_rectangle.left + window_rectangle.right) / 2, -1};
        for (int y = window_rectangle.top; y < populated_client_origin.y;
             ++y) {
            if (SendMessageW(main_window, WM_NCHITTEST, 0,
                             MAKELPARAM(populated_caption.x, y)) ==
                HTCAPTION) {
                populated_caption.y = y;
                break;
            }
        }
        if (populated_caption.y < 0 ||
            !SetCursorPos(populated_caption.x, populated_caption.y) ||
            !send_mouse_button(MOUSEEVENTF_LEFTDOWN)) {
            return fail(process, 59, "could not drag the populated window");
        }
        Sleep(150);
        SetCursorPos(populated_caption.x + 32, populated_caption.y + 24);
        Sleep(150);
        send_mouse_button(MOUSEEVENTF_LEFTUP);
        Sleep(1000);
        const std::size_t dark_pixels_after_move =
            count_dark_client_pixels(pane);
        const LRESULT lines_after_move = SendMessageW(
            pane, kWmOpusX64QuerySelection, 30, 0);
        std::cerr << "move paint: pixels=" << dark_pixels_after << "->"
                  << dark_pixels_after_move << " lines=" << lines_before_move
                  << "->" << lines_after_move << '\n';
        if (!window_is_responsive(process.hProcess, main_window) ||
            lines_after_move < lines_before_move ||
            dark_pixels_after_move * 4 < dark_pixels_after * 3) {
            return fail(process, 60,
                        "moving the window lost populated display lines");
        }

        POINT sentence_end{250, 10};
        if (!ClientToScreen(pane, &sentence_end) ||
            !SetCursorPos(sentence_end.x, sentence_end.y) ||
            !send_mouse_button(MOUSEEVENTF_LEFTDOWN)) {
            return fail(process, 34,
                        "could not click near the first sentence end");
        }
        Sleep(75);
        send_mouse_button(MOUSEEVENTF_LEFTUP);
        Sleep(250);
        const LRESULT clicked_cp_first =
            SendMessageW(pane, kWmOpusX64QuerySelection, 0, 0);
        const LRESULT clicked_cp_lim =
            SendMessageW(pane, kWmOpusX64QuerySelection, 1, 0);
        const LRESULT clicked_is_insertion =
            SendMessageW(pane, kWmOpusX64QuerySelection, 2, 0);
        const LRESULT clicked_style =
            SendMessageW(pane, kWmOpusX64QuerySelection, 3, 0);
        const LRESULT clicked_double =
            SendMessageW(pane, kWmOpusX64QuerySelection, 5, 0);
        if (clicked_cp_first < 0 || clicked_cp_first > typed_cp_first ||
            clicked_cp_first != clicked_cp_lim ||
            clicked_is_insertion != 1 || clicked_double != 0) {
            std::cerr << "selection after sentence-end click: cpFirst="
                      << clicked_cp_first << " cpLim=" << clicked_cp_lim
                      << " fIns=" << clicked_is_insertion
                      << " sty=" << clicked_style
                      << " double=" << clicked_double << '\n';
            return fail(process, 35,
                        "sentence-end click mapped to the wrong selection");
        }

        RECT pane_client{};
        POINT drag_start{20, 10};
        POINT drag_end{180, 10};
        if (!GetClientRect(pane, &pane_client) || pane_client.right < 200 ||
            pane_client.bottom < 30 || !ClientToScreen(pane, &drag_start) ||
            !ClientToScreen(pane, &drag_end) ||
            !SetCursorPos(drag_start.x, drag_start.y) ||
            !send_mouse_button(MOUSEEVENTF_LEFTDOWN)) {
            return fail(process, 27, "could not begin a real mouse selection");
        }
        if (!wait_for_capture(process.hProcess, thread_id, pane, 1500)) {
            send_mouse_button(MOUSEEVENTF_LEFTUP);
            return fail(process, 28,
                        "the original selection engine did not capture mouse");
        }
        SetCursorPos(drag_end.x, drag_end.y);
        Sleep(100);
        if (!send_mouse_button(MOUSEEVENTF_LEFTUP) ||
            !wait_for_capture(process.hProcess, thread_id, nullptr, 1500) ||
            !window_is_responsive(process.hProcess, main_window)) {
            return fail(process, 29,
                        "mouse selection did not release capture cleanly");
        }
        Sleep(250);
        const LRESULT dragged_first = SendMessageW(
            pane, kWmOpusX64QuerySelection, 0, 0);
        const LRESULT dragged_lim = SendMessageW(
            pane, kWmOpusX64QuerySelection, 1, 0);
        const int selection_light_gap = longest_light_gap(pane, 20, 180, 10);
        std::cerr << "selection paint=" << dragged_first << ','
                  << dragged_lim << " longestLightGap="
                  << selection_light_gap << '\n';
        if (dragged_lim <= dragged_first || selection_light_gap > 16) {
            return fail(process, 65,
                        "mouse selection paint contains character gaps");
        }

        TerminateProcess(process.hProcess, 0);
        WaitForSingleObject(process.hProcess, 2000);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return 0;
    }
    if (typing_mode) {
        DWORD ignored_process_id = 0;
        const DWORD thread_id =
            GetWindowThreadProcessId(main_window, &ignored_process_id);
        /* Every other interactive mode in this file finds OpusWwd
           explicitly and calls make_foreground_and_focus on it before
           relying on focus state (see Task 8's fix, opus_selection_mode
           above) -- typing_mode instead trusted whatever GetGUIThreadInfo
           reported as already focused, which is only reliably the
           document pane by accident of window-creation order. If some
           other window has it, WM_CHAR posts below still succeed (Win32
           happily queues them) but land nowhere visible, matching this
           test's actual failure: reaches the paint check, never the
           focus/posting ones. */
        const HWND pane = find_descendant_by_class(main_window, L"OpusWwd");
        if (pane == nullptr) {
            return fail(process, 13, "active document pane has no focus");
        }
        make_foreground_and_focus(main_window, pane, thread_id);
        GUITHREADINFO gui{};
        gui.cbSize = sizeof(gui);
        if (!GetGUIThreadInfo(thread_id, &gui) || gui.hwndFocus != pane) {
            return fail(process, 13, "active document pane has no focus");
        }
        const std::size_t dark_pixels_before = count_dark_client_pixels(pane);
        // Cross the original 32-byte quick-insert boundary and fill enough
        // display lines to exercise idle normalization and the SCC-above PLC.
        //
        // Deliberately not std::wstring: its length-computing constructors,
        // operator+=, and std::to_wstring all go through the same glibc
        // char_traits<wchar_t>/wide-conversion machinery already found
        // broken for this TU's real 2-byte Win32 WCHAR (see the argv-parsing
        // fix above and docs/port-linux/01-diagnostico-heap-corruption-arranque.md
        // §23-24, §27) -- forty rounds of growing a std::wstring by repeated
        // += was the actual crash site (malloc(): invalid size + stack
        // overflow, confirmed 2026-08-14). wsprintfW/lstrlenW are
        // Win32-native and width-correct.
        wchar_t text[40 * 20 + 1] = {};
        wchar_t* text_cursor = text;
        for (int line = 0; line != 40; ++line) {
            wchar_t line_text[32] = {};
            wsprintfW(line_text, L"original line %02d\r", line);
            const int line_length = lstrlenW(line_text);
            for (int i = 0; i != line_length; ++i) {
                *text_cursor++ = line_text[i];
            }
        }
        for (const wchar_t character : text) {
            if (character != L'\0') {
                bool posted = false;
                for (int attempt = 0; attempt != 20 && !posted; ++attempt) {
                    GUITHREADINFO current_gui{};
                    current_gui.cbSize = sizeof(current_gui);
                    posted = GetGUIThreadInfo(thread_id, &current_gui) &&
                             current_gui.hwndFocus != nullptr &&
                             post_keyboard_character(current_gui.hwndFocus,
                                                     character);
                    if (!posted) {
                        Sleep(50);
                    }
                }
                if (!posted) {
                    return fail(process, 15,
                                "could not post a character to the document");
                }
                Sleep(10);
            }
        }
        Sleep(4000);
        if (!window_is_responsive(process.hProcess, main_window)) {
            return fail(process, 14,
                        "original insert loop crashed or stopped responding");
        }
        GUITHREADINFO final_gui{};
        final_gui.cbSize = sizeof(final_gui);
        const std::size_t dark_pixels_after =
            GetGUIThreadInfo(thread_id, &final_gui)
                ? count_dark_client_pixels(final_gui.hwndFocus)
                : 0;
        if (dark_pixels_after < dark_pixels_before + 100) {
            return fail(process, 16,
                        "typed text was not painted in the document pane");
        }
        TerminateProcess(process.hProcess, 0);
        WaitForSingleObject(process.hProcess, 2000);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return 0;
    }
    if (!PostMessageW(main_window, kWmCommand, kFileNew, 0)) {
        return fail(process, 4, "could not send File New");
    }

    const HWND new_dialog = wait_for_window(
        process.hProcess, process.dwProcessId, L"OpusSdmDialog", L"New",
        5000);
    if (new_dialog == nullptr) {
        return fail(process, 5, "File New dialog did not appear");
    }
    const bool controls_present =
        control_has_class(new_dialog, 1, L"Button") &&
        control_has_class(new_dialog, 2, L"Button") &&
        control_has_class(new_dialog, 0x0400, L"Button") &&
        control_has_class(new_dialog, 0x0402, L"Button") &&
        control_has_class(new_dialog, 0x0403, L"Button") &&
        control_has_class(new_dialog, 0x0404, L"Edit") &&
        control_has_class(new_dialog, 0x0405, L"ListBox");
    if (!controls_present) {
        return fail(process, 6, "File New controls do not match the SDM contract");
    }
    if (!PostMessageW(new_dialog, kWmCommand, 1, 0)) {
        return fail(process, 7, "could not accept File New");
    }
    if (!wait_for_window_to_close(process.hProcess, process.dwProcessId,
                                  L"OpusSdmDialog", 5000)) {
        return fail(process, 8, "File New dialog did not finish");
    }
    if (wait_for_window(process.hProcess, process.dwProcessId, nullptr,
                        L"Microsoft Word - Document2", 5000) == nullptr) {
        return fail(process, 9,
                    "original File New action did not create Document2");
    }

    if (!PostMessageW(main_window, kWmCommand, kFileExit, 0)) {
        return fail(process, 10, "could not send File Exit");
    }
    if (WaitForSingleObject(process.hProcess, 5000) != WAIT_OBJECT_0) {
        return fail(process, 11, "two-document File Exit timed out");
    }
    DWORD exit_code = 0;
    if (!GetExitCodeProcess(process.hProcess, &exit_code) || exit_code != 0) {
        return fail(process, 12, "two-document File Exit was not clean");
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 0;
}

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
            "--font-typing|--clipboard|--about|--save-as]\n";
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
    if (argument_count == 3 && !typing_mode && !interaction_mode &&
        !selection_mode && !caret_mode && !formatting_mode && !color_mode &&
        !font_typing_mode && !clipboard_mode && !about_mode &&
        !save_as_mode) {
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
        const std::size_t after_enter_first_band =
            count_dark_client_pixels(pane, 0, 50);
        const std::size_t after_enter_second_band =
            count_dark_client_pixels(pane, 50, 131);
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
        if (large_inserted_ftc != second_ftc || large_inserted_hps != 144 ||
            display_line_count < 3 || mixed_line_pixels == 0 ||
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

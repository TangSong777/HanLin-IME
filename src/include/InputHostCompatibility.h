#pragma once
#include <windows.h>
namespace input_host {
// Shell icon views accept type-to-select but are not text editors. An Edit
// child (F2 rename) must remain eligible even under the same desktop ancestry.
inline bool IsDesktopNonTextWindow(HWND focus) {
  bool desktop = false;
  for (HWND w = focus; w; w = GetParent(w)) {
    wchar_t name[128] = {};
    GetClassNameW(w, name, 128);
    if (_wcsicmp(name, L"Edit") == 0 ||
        _wcsnicmp(name, L"RichEdit", 8) == 0) return false;
    if (_wcsicmp(name, L"Progman") == 0 ||
        _wcsicmp(name, L"WorkerW") == 0) desktop = true;
  }
  return desktop;
}
inline bool DesktopHasNoTextFocus() {
  HWND foreground = GetForegroundWindow();
  GUITHREADINFO info = {sizeof(GUITHREADINFO)};
  DWORD thread = GetWindowThreadProcessId(foreground, nullptr);
  if (!thread || !GetGUIThreadInfo(thread, &info)) return false;
  return IsDesktopNonTextWindow(info.hwndFocus ? info.hwndFocus : foreground);
}
// TSF callbacks can arrive through different window messages. Compare physical
// identity, not the dispatch timestamp or repeat-count metadata.
inline bool SamePendingKey(WPARAM tested, LPARAM flags, const void* context,
                           WPARAM key, LPARAM incoming, const void* current) {
  constexpr LPARAM identity = 0x01ff0000; // scan code and extended-key flag
  return tested == key && context == current &&
         (flags & identity) == (incoming & identity);
}
inline bool PlaceholderExtent(const RECT& caret, const RECT& view) {
  return view.right - view.left > 200 && view.bottom - view.top > 100 &&
         caret.left >= view.left && caret.left <= view.left + 1 &&
         caret.top >= view.top && caret.top <= view.top + 1 &&
         caret.right - caret.left <= 2;
}
inline bool RectTouchesMonitor(const RECT& rc) {
  if (rc.bottom <= rc.top)
    return false;
  RECT probe = rc;
  if (probe.right <= probe.left)
    probe.right = probe.left + 1;
  return ::MonitorFromRect(&probe, MONITOR_DEFAULTTONULL) != nullptr;
}

inline bool GetForegroundCaretRect(HWND foreground,
                            LONG preferred_height,
                            RECT& result) {
  if (!foreground)
    return false;

  GUITHREADINFO info = {};
  info.cbSize = sizeof(info);
  const DWORD thread_id = ::GetWindowThreadProcessId(foreground, nullptr);
  if (!thread_id || !::GetGUIThreadInfo(thread_id, &info))
    return false;

  HWND caret_window = info.hwndCaret;
  RECT caret = info.rcCaret;
  if (caret_window) {
    POINT top_left = {caret.left, caret.top};
    POINT bottom_right = {caret.right, caret.bottom};
    if (!::ClientToScreen(caret_window, &top_left) ||
        !::ClientToScreen(caret_window, &bottom_right))
      return false;
    caret = {top_left.x, top_left.y, bottom_right.x, bottom_right.y};
  } else {
    // Some WinUI controls expose no hwndCaret but still maintain the Win32
    // caret position on their focused child window.
    // GetCaretPos is thread-local: never translate another thread's caret
    // through the foreground child window.
    if (thread_id != GetCurrentThreadId()) return false;
    caret_window = info.hwndFocus ? info.hwndFocus : ::GetFocus();
    POINT point = {};
    if (!caret_window || !::GetCaretPos(&point) ||
        !::ClientToScreen(caret_window, &point))
      return false;
    caret = {point.x, point.y, point.x, point.y};
  }

  if (caret.bottom <= caret.top)
    caret.bottom = caret.top + (preferred_height > 0 ? preferred_height : 20);
  if (caret.right < caret.left)
    caret.right = caret.left;
  if (!RectTouchesMonitor(caret))
    return false;
  result = caret;
  return true;
}
inline RECT FallbackAnchor(const RECT& view) {
  const LONG x = view.left + 16;
  const LONG y = view.bottom - 80;
  return {x, y, x + 1, y + 20};
}
}

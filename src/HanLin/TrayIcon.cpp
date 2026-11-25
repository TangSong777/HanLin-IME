#include "TrayIcon.h"

#include "../include/HanLinIcons.h"

#include <windowsx.h>

namespace hanlin {
namespace {

constexpr UINT kTrayNotification = WM_APP + 2;
constexpr UINT_PTR kRetryTray = 2;
constexpr UINT kRetryInterval = 3000;

HICON LoadStatusIcon(HINSTANCE image, int id) {
  return static_cast<HICON>(LoadImageW(image, MAKEINTRESOURCEW(id), IMAGE_ICON,
      GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
}

std::wstring QuotePath(const std::wstring& path) {
  const auto last = path.find_last_not_of(L'\\');
  const auto trailing = last == std::wstring::npos ? path.size() : path.size() - last - 1;
  return L"\"" + path + std::wstring(trailing, L'\\') + L"\"";
}

}  // namespace

TrayIcon::TrayIcon(HWND window, HINSTANCE image, const std::filesystem::path& directory,
                   std::wstring shared, std::wstring personal, bool show_icon)
    : window_(window),
      tools_(directory / L"HanLinTools.exe"),
      shared_(std::move(shared)),
      personal_(std::move(personal)) {
  if (!show_icon) return;
  taskbar_created_ = RegisterWindowMessageW(L"TaskbarCreated");
  chinese_ = LoadStatusIcon(image, IDI_HANLIN_MODE_CHINESE);
  english_ = LoadStatusIcon(image, IDI_HANLIN_MODE_ENGLISH);
  icon_.cbSize = sizeof(icon_);
  icon_.hWnd = window_;
  icon_.uID = 1;
  icon_.uCallbackMessage = kTrayNotification;
  icon_.hIcon = chinese_;
  wcscpy_s(icon_.szTip, L"寒淋输入法 · 中文");
  Attach();
}

TrayIcon::~TrayIcon() {
  KillTimer(window_, kRetryTray);
  if (attached_) Shell_NotifyIconW(NIM_DELETE, &icon_);
  if (chinese_) DestroyIcon(chinese_);
  if (english_) DestroyIcon(english_);
}

void TrayIcon::Attach() {
  icon_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
  attached_ = icon_.hIcon && Shell_NotifyIconW(NIM_ADD, &icon_);
  if (attached_) {
    icon_.uVersion = NOTIFYICON_VERSION_4;
    version4_ = Shell_NotifyIconW(NIM_SETVERSION, &icon_) != FALSE;
    KillTimer(window_, kRetryTray);
  } else {
    SetTimer(window_, kRetryTray, kRetryInterval, nullptr);
  }
}

void TrayIcon::UpdateMode(bool ascii) {
  if (!icon_.hWnd || ascii == ascii_) return;
  ascii_ = ascii;
  icon_.hIcon = (ascii && english_) ? english_ : chinese_;
  wcscpy_s(icon_.szTip, ascii ? L"寒淋输入法 · 英文" : L"寒淋输入法 · 中文");
  if (!attached_) return;
  icon_.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP;
  if (!Shell_NotifyIconW(NIM_MODIFY, &icon_)) {
    attached_ = false;
    Attach();
  }
}

bool TrayIcon::RequestMenu(POINT point) {
  return PostMessageW(window_, kOpenTrayMenu, static_cast<WPARAM>(point.x),
                      static_cast<LPARAM>(point.y)) != FALSE;
}

POINT TrayIcon::IconPosition() const {
  NOTIFYICONIDENTIFIER identifier{};
  identifier.cbSize = sizeof(identifier);
  identifier.hWnd = window_;
  identifier.uID = icon_.uID;
  RECT item{};
  if (SUCCEEDED(Shell_NotifyIconGetRect(&identifier, &item)))
    return {item.left + (item.right - item.left) / 2, item.top};
  POINT cursor{};
  GetCursorPos(&cursor);
  return cursor;
}

bool TrayIcon::Handle(UINT message, WPARAM word, LPARAM data) {
  if (message == kOpenTrayMenu) {
    OpenMenu({static_cast<LONG>(word), static_cast<LONG>(data)});
  } else if (taskbar_created_ && message == taskbar_created_) {
    if (attached_) Shell_NotifyIconW(NIM_DELETE, &icon_);
    Attach();
  } else if (icon_.hWnd && message == WM_TIMER && word == kRetryTray) {
    Attach();
  } else if (icon_.hWnd && message == kTrayNotification) {
    const auto event = version4_ ? LOWORD(data) : data;
    if (event == WM_CONTEXTMENU || event == NIN_KEYSELECT || event == NIN_SELECT ||
        (!version4_ && (event == WM_RBUTTONUP || event == WM_LBUTTONUP))) {
      const POINT anchor{GET_X_LPARAM(word), GET_Y_LPARAM(word)};
      OpenMenu(version4_ && !(anchor.x == -1 && anchor.y == -1) ? anchor : IconPosition());
    }
  } else {
    return false;
  }
  return true;
}

void TrayIcon::OpenMenu(POINT point) {
  const auto now = GetTickCount64();
  if (opened_ && now - opened_ < 220) return;
  opened_ = now;
  std::wstring arguments = QuotePath(tools_.wstring()) + L" --menu " +
      std::to_wstring(point.x) + L" " + std::to_wstring(point.y) +
      L" --user-data " + QuotePath(personal_) + L" --shared-data " + QuotePath(shared_);
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(tools_.c_str(), arguments.data(), nullptr, nullptr, FALSE, 0,
                      nullptr, tools_.parent_path().c_str(), &startup, &process)) return;
  AllowSetForegroundWindow(process.dwProcessId);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
}

}  // namespace hanlin

#pragma once

#include <windows.h>
#include <shellapi.h>

#include <filesystem>
#include <string>

namespace hanlin {

constexpr UINT kOpenTrayMenu = WM_APP + 3;

class TrayIcon {
 public:
  TrayIcon(HWND window, HINSTANCE image, const std::filesystem::path& directory,
           std::wstring shared, std::wstring personal, bool show_icon);
  ~TrayIcon();
  TrayIcon(const TrayIcon&) = delete;
  TrayIcon& operator=(const TrayIcon&) = delete;

  void UpdateMode(bool ascii);
  bool Handle(UINT message, WPARAM word, LPARAM data);
  bool RequestMenu(POINT point);

 private:
  void Attach();
  void OpenMenu(POINT point);
  POINT IconPosition() const;

  HWND window_;
  UINT taskbar_created_ = 0;
  NOTIFYICONDATAW icon_{};
  HICON chinese_ = nullptr;
  HICON english_ = nullptr;
  std::filesystem::path tools_;
  std::wstring shared_;
  std::wstring personal_;
  ULONGLONG opened_ = 0;
  bool attached_ = false;
  bool version4_ = false;
  bool ascii_ = false;
};

}  // namespace hanlin

#include "stdafx.h"
#include "WeaselPanel.h"
#include "ModeTipPlacement.h"

#include <utility>
#include <ShellScalingApi.h>
#include <VersionHelpers.hpp>
#include <WeaselIPCData.h>
#include <HanLinIcons.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>

namespace {
constexpr UINT kAvoidCandidateMessage = WM_APP + 0x517;
constexpr UINT_PTR kHoverTimer = 20261126;
constexpr UINT_PTR kCaretTimer = 20261127;
constexpr float kHoverDuration = 130.0f;
constexpr float kCaretDurationMs = 160.0f;
constexpr float kCaretSettlePx = 0.125f;

bool SamePreeditIgnoringSeparators(std::wstring_view old_text, std::wstring_view text) {
  // Rime can insert or remove syllable separators when only the caret moves.
  const auto separator = [](wchar_t ch) { return ch == L' ' || ch == L'\''; };
  size_t old_index = 0, index = 0;
  while (true) {
    while (old_index < old_text.size() && separator(old_text[old_index])) ++old_index;
    while (index < text.size() && separator(text[index])) ++index;
    if (old_index == old_text.size() || index == text.size())
      return old_index == old_text.size() && index == text.size();
    if (old_text[old_index++] != text[index++]) return false;
  }
}

int LatinLetterIndex(wchar_t ch) {
  if (ch >= L'a' && ch <= L'z') return ch - L'a';
  if (ch >= L'A' && ch <= L'Z') return 26 + ch - L'A';
  return -1;
}
bool ClientMotionEnabled() {
  BOOL animate = FALSE;
  return SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animate, 0) && animate;
}
const wchar_t* kPanelRole = L"WeaselColorPPanelRole";
BOOL CALLBACK CollectCandidateRects(HWND window, LPARAM param) {
  if (IsWindowVisible(window) &&
      GetPropW(window, kPanelRole) == reinterpret_cast<HANDLE>(1)) {
    RECT bounds;
    if (GetWindowRect(window, &bounds))
      reinterpret_cast<std::vector<RECT>*>(param)->push_back(bounds);
  }
  return TRUE;
}
BOOL CALLBACK NotifyModeTip(HWND window, LPARAM) {
  if (IsWindowVisible(window) &&
      GetPropW(window, kPanelRole) == reinterpret_cast<HANDLE>(2))
    PostMessage(window, kAvoidCandidateMessage, 0, 0);
  return TRUE;
}
}  // namespace

#include "VerticalLayout.h"
#include "HorizontalLayout.h"
#include "FullScreenLayout.h"
#include "VHorizontalLayout.h"

// for IDI_ZH, IDI_EN
#include <resource.h>
#define COLORTRANSPARENT(color) ((color & 0xff000000) == 0)
#define COLORNOTTRANSPARENT(color) ((color & 0xff000000) != 0)
#define TRANS_COLOR 0x00000000
#define GDPCOLOR_FROM_COLORREF(color)                                \
  Gdiplus::Color::MakeARGB(((color >> 24) & 0xff), GetRValue(color), \
                           GetGValue(color), GetBValue(color))
#define HALF_ALPHA_COLOR(color) \
  ((((color & 0xff000000) >> 25) & 0xff) << 24) | (color & 0x00ffffff)

#pragma comment(lib, "Shcore.lib")

inline HICON LoadPngAsIcon(const std::wstring& path) {
  Gdiplus::Bitmap bitmap(path.c_str());
  if (bitmap.GetLastStatus() != Gdiplus::Ok) return nullptr;
  HICON icon = nullptr;
  return bitmap.GetHICON(&icon) == Gdiplus::Ok ? icon : nullptr;
}

inline weasel::ssf::Size GetPngNativeSize(const std::wstring& path) {
  Gdiplus::Bitmap bitmap(path.c_str());
  if (bitmap.GetLastStatus() != Gdiplus::Ok)
    return weasel::ssf::Size{25, 25};
  return weasel::ssf::Size{static_cast<int>(bitmap.GetWidth()),
                            static_cast<int>(bitmap.GetHeight())};
}

template <class t0, class t1, class t2>
inline void LoadIconNecessary(t0& a, t1& b, t2& c, int d) {
  if (a == b)
    return;
  a = b;
  if (b.empty()) {
    c.LoadIconW(d, STATUS_ICON_SIZE, STATUS_ICON_SIZE, LR_DEFAULTCOLOR);
    return;
  }
  HICON icon = (HICON)LoadImage(NULL, b.c_str(), IMAGE_ICON,
                                STATUS_ICON_SIZE, STATUS_ICON_SIZE,
                                LR_LOADFROMFILE);
  // Sogou skins carry mode artwork as PNG rather than ICO.  GDI+ preserves the
  // source alpha when converting that artwork to the HICON Weasel's existing
  // cursor-near indicator code expects.
  if (!icon) icon = LoadPngAsIcon(b);
  if (icon)
    c = icon;
  else
    c.LoadIconW(d, STATUS_ICON_SIZE, STATUS_ICON_SIZE, LR_DEFAULTCOLOR);
}

static inline void ReconfigRoundInfo(IsToRoundStruct& rd,
                                     const int& i,
                                     const int& m_candidateCount) {
  if (i == 0 && m_candidateCount > 1) {
    std::swap(rd.IsTopLeftNeedToRound, rd.IsBottomLeftNeedToRound);
    std::swap(rd.IsTopRightNeedToRound, rd.IsBottomRightNeedToRound);
  }
  if (i == m_candidateCount - 1) {
    std::swap(rd.IsTopLeftNeedToRound, rd.IsBottomLeftNeedToRound);
    std::swap(rd.IsTopRightNeedToRound, rd.IsBottomRightNeedToRound);
  }
}

WeaselPanel::WeaselPanel(weasel::UI& ui)
    : m_layout(NULL),
      m_ctx(ui.ctx()),
      m_octx(ui.octx()),
      m_status(ui.status()),
      m_in_server(ui.InServer()),
      m_style(ui.style()),
      m_ostyle(ui.ostyle()),
      m_candidateCount(0),
      m_lastCandidateCount(0),
      m_current_zhung_icon(),
      m_inputPos(CRect()),
      m_sticky(false),
      dpi(96),
      hide_candidates(false),
      pDWR(ui.pdwr()),
      _UICallback(ui.uiCallback()),
      _m_gdiplusToken(0) {
#ifdef HANLIN_PANEL
  m_iconDisabled.LoadIconW(IDI_HANLIN_APP, STATUS_ICON_SIZE, STATUS_ICON_SIZE, LR_DEFAULTCOLOR);
  m_iconEnabled.LoadIconW(IDI_HANLIN_MODE_CHINESE, STATUS_ICON_SIZE, STATUS_ICON_SIZE, LR_DEFAULTCOLOR);
  m_iconAlpha.LoadIconW(IDI_HANLIN_MODE_ENGLISH, STATUS_ICON_SIZE, STATUS_ICON_SIZE, LR_DEFAULTCOLOR);
#else
  m_iconDisabled.LoadIconW(IDI_RELOAD, STATUS_ICON_SIZE, STATUS_ICON_SIZE,
                           LR_DEFAULTCOLOR);
  m_iconEnabled.LoadIconW(IDI_ZH, STATUS_ICON_SIZE, STATUS_ICON_SIZE,
                          LR_DEFAULTCOLOR);
  m_iconAlpha.LoadIconW(IDI_EN, STATUS_ICON_SIZE, STATUS_ICON_SIZE,
                        LR_DEFAULTCOLOR);
  m_iconFull.LoadIconW(IDI_FULL_SHAPE, STATUS_ICON_SIZE, STATUS_ICON_SIZE,
                       LR_DEFAULTCOLOR);
  m_iconHalf.LoadIconW(IDI_HALF_SHAPE, STATUS_ICON_SIZE, STATUS_ICON_SIZE,
                       LR_DEFAULTCOLOR);
#endif
  // for gdi+ drawings, initialization
  GdiplusStartup(&_m_gdiplusToken, &_m_gdiplusStartupInput, NULL);

  HMONITOR hMonitor = MonitorFromRect(m_inputPos, MONITOR_DEFAULTTONEAREST);
  UINT dpiX = 96, dpiY = 96;
  if (hMonitor) {
    GetDpiForMonitor(hMonitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    m_hMonitor = hMonitor;
  }
  dpi = dpiX;
  // Preload the selected skin and the DirectWrite formats while the panel is
  // being created.  Waiting for the first composition used to make that first
  // candidate window pay for skin parsing and PNG decoding synchronously.
  _SsfReloadSkin();
  _InitFontRes();
  if (SsfActive()) _SsfInitFonts();
  m_ostyle = m_style;
}

WeaselPanel::~WeaselPanel() {
  Gdiplus::GdiplusShutdown(_m_gdiplusToken);
  delete m_layout;
  m_layout = NULL;
  // pDWR.reset();
}

void WeaselPanel::_ResizeWindow() {
  CSize m_size = m_layout->GetContentSize();
  CRect client;
  if (GetClientRect(&client) && client.Size() == m_size) return;
  SetWindowPos(NULL, 0, 0, m_size.cx, m_size.cy,
               SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOREDRAW);
}

// SSF diagnostics.
//
// Enable WITHOUT recompiling by setting the environment variable
// WEASEL_SSF_LOGGING=1 before WeaselServer.exe starts (define
// WEASEL_SSF_LOGGING_FORCE at compile time to make it unconditional).
//
// Logging goes to a file rather than the debug output, because the debug output
// is invisible unless a debugger is attached -- which is exactly the situation
// when a skin silently fails to apply and there is nothing else to go on.
//
// The path comes from WEASEL_SSF_LOG, defaulting to %TEMP%\weasel-ssf.log, so it
// can be redirected without touching the build.
namespace {

#ifdef HANLIN_PANEL
constexpr wchar_t kSsfLogging[] = L"HANLIN_SSF_LOGGING";
constexpr wchar_t kSsfLogPath[] = L"HANLIN_SSF_LOG";
constexpr wchar_t kSsfLogFile[] = L"hanlin-ssf.log";
#else
constexpr wchar_t kSsfLogging[] = L"WEASEL_SSF_LOGGING";
constexpr wchar_t kSsfLogPath[] = L"WEASEL_SSF_LOG";
constexpr wchar_t kSsfLogFile[] = L"weasel-ssf.log";
#endif

bool SsfLogEnabled() {
#ifdef WEASEL_SSF_LOGGING_FORCE
  return true;
#else
  static const bool enabled = [] {
    wchar_t buf[8] = {0};
    const DWORD n =
        ::GetEnvironmentVariableW(kSsfLogging, buf, _countof(buf));
    return n > 0 && buf[0] != L'0';
  }();
  return enabled;
#endif
}

std::wstring SsfLogPath() {
  wchar_t buf[MAX_PATH] = {0};
  const DWORD n =
      ::GetEnvironmentVariableW(kSsfLogPath, buf, _countof(buf));
  if (n > 0 && buf[0]) return std::wstring(buf);
  wchar_t tmp[MAX_PATH] = {0};
  if (::GetTempPathW(_countof(tmp), tmp) == 0) return kSsfLogFile;
  return std::wstring(tmp) + kSsfLogFile;
}

void SsfLog(const std::wstring& msg) {
  if (!SsfLogEnabled()) return;
  // FILE_SHARE_READ|WRITE so a concurrent reader (a tail, or the installer's
  // verification) can never block the IME.
  HANDLE h = ::CreateFileW(SsfLogPath().c_str(), FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return;
  SYSTEMTIME st;
  ::GetLocalTime(&st);
  wchar_t line[1024];
  const int len = swprintf_s<1024>(line, L"[%02d:%02d:%02d.%03d] %ls\r\n",
                                   st.wHour, st.wMinute, st.wSecond,
                                   st.wMilliseconds, msg.c_str());
  if (len > 0) {
    DWORD written = 0;
    ::WriteFile(h, line,
                static_cast<DWORD>(static_cast<size_t>(len) * sizeof(wchar_t)),
                &written, nullptr);
  }
  ::CloseHandle(h);
}

}  // namespace

#define SSFLOG(msg) SsfLog(msg)

void WeaselPanel::_CreateLayout() {
  if (m_layout != NULL)
    delete m_layout;
  ssf_adapter_ = nullptr;
  ssf_caret_x_.reset();

  Layout* layout = NULL;

  // Sogou SSF skin: the adapter implements weasel::Layout on top of the SSF
  // layout engine, so geometry, painting and mouse hit-testing all share one
  // source of truth. When no skin is active this branch is skipped entirely and
  // the stock layouts below are used unchanged.
  SSFLOG(SsfActive()
             ? L"createLayout: SSF active"
             : L"createLayout: SSF NOT active -> stock layouts");
  if (SsfActive()) {
    ssf_adapter_ = new weasel::ssf::SsfLayoutAdapter(
        m_style, m_ctx, m_status, pDWR, ssf_skin_storage_,
        [this](const std::wstring& text,
               weasel::ssf::ITextMeasurer::FontKind kind) {
          return _SsfMeasure(text, kind);
        },
        [this](weasel::ssf::ITextMeasurer::FontKind kind) {
          return _SsfAscent(kind);
        });
    ssf_adapter_->SetOrientation(
        (m_style.layout_type == UIStyle::LAYOUT_VERTICAL ||
         m_style.layout_type == UIStyle::LAYOUT_VERTICAL_FULLSCREEN)
            ? weasel::ssf::Orientation::Vertical
            : weasel::ssf::Orientation::Horizontal);
    ssf_adapter_->SetStatusBarEnabled(m_style.ssf_status_bar);
    // Share the renderer's decoded image cache; avoid opening a PNG on every
    // keystroke just to retrieve the same dimensions.
    const auto mode_tip = ssf_renderer_.images().Get(
        m_status.ascii_mode ? "a2.png" : "cn2.png");
    ssf_adapter_->SetModeTipSize(mode_tip && mode_tip->Valid()
        ? weasel::ssf::Size{mode_tip->Width(), mode_tip->Height()}
        : weasel::ssf::Size{25, 25});
    layout = ssf_adapter_;
  } else if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT) {
    layout = new VHorizontalLayout(m_style, m_ctx, m_status, pDWR);
  } else {
    if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL ||
        m_style.layout_type == UIStyle::LAYOUT_VERTICAL_FULLSCREEN) {
      layout = new VerticalLayout(m_style, m_ctx, m_status, pDWR);
    } else if (m_style.layout_type == UIStyle::LAYOUT_HORIZONTAL ||
               m_style.layout_type == UIStyle::LAYOUT_HORIZONTAL_FULLSCREEN) {
      layout = new HorizontalLayout(m_style, m_ctx, m_status, pDWR);
    }

    if (IS_FULLSCREENLAYOUT(m_style)) {
      layout = new FullScreenLayout(m_style, m_ctx, m_status, m_inputPos,
                                    layout, pDWR);
    }
  }
  m_layout = layout;
}

// 更新界面
void WeaselPanel::Refresh() {
  if (m_ctx.preedit.str != m_octx.preedit.str &&
      (m_ctx.preedit.str.empty() || m_octx.preedit.str.empty() ||
       !SamePreeditIgnoringSeparators(m_octx.preedit.str, m_ctx.preedit.str)))
    _ResetCaretMotion();
  if (weasel::ssf::SuppressEmptyWindow(m_style.ssf_enabled, m_ctx.empty(),
                                       m_status.show_mode_tip)) {
    hide_candidates = true;
    m_candidateCount = 0;
    m_lastCandidateCount = 0;
    m_sticky = false;
    ShowWindow(SW_HIDE);
    return;
  }
  bool should_show_icon =
      SsfActive() ? m_status.show_mode_tip
                  : (m_status.ascii_mode || !m_status.composing ||
                     !m_ctx.aux.empty());
  m_candidateCount = min(m_ctx.cinfo.candies.size(), MAX_CANDIDATES_COUNT);
  if (!std::equal(m_ctx.cinfo.candies.begin(), m_ctx.cinfo.candies.end(),
                  m_octx.cinfo.candies.begin(), m_octx.cinfo.candies.end(),
                  [](const auto& a, const auto& b) { return a.str == b.str; })) {
    _ResetCandidateHover();
  }
  // When the candidate window changes from having content to having no content,
  // reset the sticky state
  if (m_lastCandidateCount > 0 && m_candidateCount == 0) {
    m_sticky = false;
  }
  m_lastCandidateCount = m_candidateCount;
  // check if to hide candidates window
  // show tips status, two kind of situation: 1) only aux strings, don't care
  // icon status; 2)only icon(ascii mode switching)
  bool show_tips =
      m_in_server &&
      ((!m_ctx.aux.empty() && m_ctx.cinfo.empty() && m_ctx.preedit.empty()) ||
       (m_ctx.empty() && should_show_icon));
  // show schema menu status: schema_id == L".default"
  bool show_schema_menu = m_status.schema_id == L".default";
  bool margin_negative =
      (DPI_SCALE(m_style.margin_x) < 0 || DPI_SCALE(m_style.margin_y) < 0);
  // when to hide_cadidates?
  // 1. margin_negative, and not in show tips mode( ascii switching / half-full
  // switching / simp-trad switching / error tips), and not in schema menu
  // 2. inline preedit without candidates
  bool inline_no_candidates =
      (m_style.inline_preedit && m_candidateCount == 0) && !show_tips;
  hide_candidates = inline_no_candidates ||
                    (margin_negative && !show_tips && !show_schema_menu);

  // only RedrawWindow if no need to hide candidates window, or
  // inline_no_candidates
  if (!hide_candidates || inline_no_candidates) {
    if (SsfLogEnabled()) {
      wchar_t buf[320];
      swprintf_s<320>(buf, L"refresh: before _InitFontRes skin='%ls' addr=%p",
                      m_style.ssf_skin.c_str(),
                      static_cast<const void*>(&m_style));
      SSFLOG(buf);
    }
    _SsfReloadSkin();
    _InitFontRes();
    if (SsfLogEnabled()) {
      wchar_t buf[320];
      swprintf_s<320>(buf, L"refresh: after _InitFontRes skin='%ls'",
                      m_style.ssf_skin.c_str());
      SSFLOG(buf);
    }
    if (SsfActive()) _SsfInitFonts();
    _CreateLayout();

    CDCHandle dc = GetDC();
    m_layout->DoLayout(dc, pDWR);
    ReleaseDC(dc);
    _ResizeWindow();
    _RepositionWindow();
    // The SSF path owns an alpha-composited surface. Unlike the stock path it
    // must repaint even when Rime reports an unchanged Context: this happens
    // after a Direct2D target recreation, a monitor/DPI transition, and some
    // hosts that suppress WM_PAINT. Skipping that repaint left an invisible
    // candidate window until the next keystroke.
    if (m_ctx != m_octx || SsfActive()) {
      m_octx = m_ctx;
      RedrawWindow();
    }
  }
}

void WeaselPanel::_InitFontRes(bool forced) {
  HMONITOR hMonitor = MonitorFromRect(_IsModeTip() ? m_caret : m_inputPos,
                                     MONITOR_DEFAULTTONEAREST);
  UINT dpiX = 96, dpiY = 96;
  if (hMonitor)
    GetDpiForMonitor(hMonitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
  // prepare d2d1 resources
  // if style changed, or dpi changed, or pDWR NULL, re-initialize directwrite
  // resources
  if (forced || (pDWR == NULL) || (m_ostyle != m_style) || (dpiX != dpi)) {
    _ResetCaretMotion();
    ssf_caret_x_.reset();
    pDWR.reset();
    // SSF measures and draws with its own formats, including its GDI fallback.
    pDWR = std::make_shared<DirectWriteResources>(m_style, dpiX, !SsfActive());
    pDWR->pRenderTarget->SetTextAntialiasMode(
        (D2D1_TEXT_ANTIALIAS_MODE)m_style.antialias_mode);
    m_ostyle = m_style;
  }
  dpi = dpiX;
  dpiScaleLayout = (float)dpi / 96.0f;
}

static HBITMAP CopyDCToBitmap(HDC hDC, LPRECT lpRect) {
  if (!hDC || !lpRect || IsRectEmpty(lpRect))
    return NULL;
  HDC hMemDC = NULL;
  HBITMAP hBitmap = NULL, hOldBitmap = NULL;
  int nX, nY, nX2, nY2;
  int nWidth, nHeight;

  nX = lpRect->left;
  nY = lpRect->top;
  nX2 = lpRect->right;
  nY2 = lpRect->bottom;
  nWidth = nX2 - nX;
  nHeight = nY2 - nY;

  hMemDC = CreateCompatibleDC(hDC);
  if (!hMemDC)
    return NULL;

  hBitmap = CreateCompatibleBitmap(hDC, nWidth, nHeight);
  if (!hBitmap) {
    DeleteDC(hMemDC);
    return NULL;
  }

  hOldBitmap = (HBITMAP)SelectObject(hMemDC, hBitmap);
  if (!hOldBitmap) {
    DeleteObject(hBitmap);
    DeleteDC(hMemDC);
    return NULL;
  }

  if (!BitBlt(hMemDC, 0, 0, nWidth, nHeight, hDC, nX, nY, SRCCOPY)) {
    // restore and cleanup
    SelectObject(hMemDC, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hMemDC);
    return NULL;
  }

  SelectObject(hMemDC, hOldBitmap);
  DeleteDC(hMemDC);
  return hBitmap;
}

void WeaselPanel::_CaptureRect(CRect& rect) {
  HDC ScreenDC = ::GetDC(NULL);
  CRect rc;
  GetWindowRect(&rc);
  POINT WindowPosAtScreen = {rc.left, rc.top};
  CRect captureRect = rect;
  captureRect.OffsetRect(WindowPosAtScreen);
  // create bitmap first (avoid holding clipboard while capturing)
  HBITMAP bmp = CopyDCToBitmap(ScreenDC, LPRECT(captureRect));
  if (!bmp) {
    ::ReleaseDC(NULL, ScreenDC);
    return;
  }

  // capture input window to clipboard
  if (!OpenClipboard()) {
    DEBUG << "_CaptureRect: OpenClipord ailed";
    DeleteObject(bmp);
    ::ReleaseDC(NULL, ScreenDC);
    return;
  }
  EmptyClipboard();
  if (!SetClipboardData(CF_BITMAP, bmp)) {
    DEBUG << "_CaptureRect: SetClipboardData failed";
    DeleteObject(bmp);
  }
  CloseClipboard();
  ::ReleaseDC(NULL, ScreenDC);
}

LRESULT WeaselPanel::OnMouseActivate(UINT uMsg,
                                     WPARAM wParam,
                                     LPARAM lParam,
                                     BOOL& bHandled) {
  bHandled = true;
  return MA_NOACTIVATE;
}

LRESULT WeaselPanel::OnMouseWheel(UINT uMsg,
                                  WPARAM wParam,
                                  LPARAM lParam,
                                  BOOL& bHandled) {
  int delta = GET_WHEEL_DELTA_WPARAM(wParam);
  if (_UICallback && delta != 0) {
    bool nextpage = delta < 0;
    _UICallback(NULL, NULL, NULL, &nextpage);
  }
  bHandled = true;
  return 0;
}

LRESULT WeaselPanel::OnLeftClickedUp(UINT uMsg,
                                     WPARAM wParam,
                                     LPARAM lParam,
                                     BOOL& bHandled) {
  if (hide_candidates) {
    bHandled = true;
    return 0;
  }
  CPoint point;
  point.x = GET_X_LPARAM(lParam);
  point.y = GET_Y_LPARAM(lParam);

  ::KillTimer(m_hWnd, AUTOREV_TIMER);
  bar_scale_ = 1.0;
  ptimer = 0;
  {
    // select by click
    CRect rect = m_layout->GetCandidateRect((int)m_ctx.cinfo.highlighted);
    if (m_istorepos)
      rect.OffsetRect(0, m_offsetys[m_ctx.cinfo.highlighted]);
    rect.InflateRect(DPI_SCALE(m_style.hilite_padding_x),
                     DPI_SCALE(m_style.hilite_padding_y));
    if (rect.PtInRect(point)) {
      size_t i = m_ctx.cinfo.highlighted;
      if (_UICallback) {
        m_mouse_entry = false;
        _UICallback(&i, NULL, NULL, NULL);
        if (!m_status.composing)
          DestroyWindow();
      }
    } else {
      RedrawWindow();
    }
  }
  bHandled = true;
  return 0;
}

LRESULT WeaselPanel::OnLeftClickedDown(UINT uMsg,
                                       WPARAM wParam,
                                       LPARAM lParam,
                                       BOOL& bHandled) {
  if (hide_candidates) {
    bHandled = true;
    return 0;
  }
  CPoint point;
  point.x = GET_X_LPARAM(lParam);
  point.y = GET_Y_LPARAM(lParam);

  // capture
  if (m_style.click_to_capture) {
    CRect recth = m_layout->GetCandidateRect((int)m_ctx.cinfo.highlighted);
    if (m_istorepos)
      recth.OffsetRect(0, m_offsetys[m_ctx.cinfo.highlighted]);
    recth.InflateRect(DPI_SCALE(m_style.hilite_padding_x),
                      DPI_SCALE(m_style.hilite_padding_y));
    // capture widow
    if (recth.PtInRect(point))
      _CaptureRect(recth);
    else {
      // if shadow_color transparent, decrease the capture rectangle size
      if (COLORTRANSPARENT(m_style.shadow_color) &&
          DPI_SCALE(m_style.shadow_radius) != 0) {
        CRect crc(rcw);
        int shadow_gap = (DPI_SCALE(m_style.shadow_offset_x) == 0 &&
                          DPI_SCALE(m_style.shadow_offset_y) == 0)
                             ? 2 * DPI_SCALE(m_style.shadow_radius)
                             : DPI_SCALE(m_style.shadow_radius) +
                                   DPI_SCALE(m_style.shadow_radius) / 2;
        int ofx = DPI_SCALE(m_style.hilite_padding_x) +
                              abs(DPI_SCALE(m_style.shadow_offset_x)) +
                              shadow_gap >
                          abs(DPI_SCALE(m_style.margin_x))
                      ? DPI_SCALE(m_style.hilite_padding_x) +
                            abs(DPI_SCALE(m_style.shadow_offset_x)) +
                            shadow_gap - abs(DPI_SCALE(m_style.margin_x))
                      : 0;
        int ofy = DPI_SCALE(m_style.hilite_padding_y) +
                              abs(DPI_SCALE(m_style.shadow_offset_y)) +
                              shadow_gap >
                          abs(DPI_SCALE(m_style.margin_y))
                      ? DPI_SCALE(m_style.hilite_padding_y) +
                            abs(DPI_SCALE(m_style.shadow_offset_y)) +
                            shadow_gap - abs(DPI_SCALE(m_style.margin_y))
                      : 0;
        crc.DeflateRect(m_layout->offsetX - ofx, m_layout->offsetY - ofy);
        _CaptureRect(crc);
      } else {
        _CaptureRect(rcw);
      }
    }
  }
  {
    if (!m_style.inline_preedit && m_candidateCount != 0 &&
        COLORNOTTRANSPARENT(m_style.prevpage_color) &&
        COLORNOTTRANSPARENT(m_style.nextpage_color)) {
      // click prepage
      if (m_ctx.cinfo.currentPage != 0) {
        CRect prc = m_layout->GetPrepageRect();
        if (m_istorepos)
          prc.OffsetRect(0, m_offsety_preedit);
        if (prc.PtInRect(point)) {
          bool nextPage = false;
          if (_UICallback)
            _UICallback(NULL, NULL, &nextPage, NULL);
          bHandled = true;
          return 0;
        }
      }
      // click nextpage
      if (!m_ctx.cinfo.is_last_page) {
        CRect prc = m_layout->GetNextpageRect();
        if (m_istorepos)
          prc.OffsetRect(0, m_offsety_preedit);
        if (prc.PtInRect(point)) {
          bool nextPage = true;
          if (_UICallback)
            _UICallback(NULL, NULL, &nextPage, NULL);
          bHandled = true;
          return 0;
        }
      }
    }
    // select by click relative actions
    for (size_t i = 0; i < m_candidateCount && i < MAX_CANDIDATES_COUNT; ++i) {
      CRect rect = m_layout->GetCandidateRect((int)i);
      if (m_istorepos)
        rect.OffsetRect(0, m_offsetys[i]);
      rect.InflateRect(DPI_SCALE(m_style.hilite_padding_x),
                       DPI_SCALE(m_style.hilite_padding_y));
      if (rect.PtInRect(point)) {
        bar_scale_ = 0.8f;
        // modify highlighted
        if (i != m_ctx.cinfo.highlighted) {
          if (_UICallback)
            _UICallback(NULL, &i, NULL, NULL);
        } else {
          RedrawWindow();
        }
        ptimer = UINT_PTR(this);
        ::SetTimer(m_hWnd, AUTOREV_TIMER, 1000, &WeaselPanel::OnTimer);
        bHandled = true;
        return 0;
      }
    }
  }
  bHandled = true;
  return 0;
}

UINT_PTR WeaselPanel::ptimer = 0;
VOID CALLBACK WeaselPanel::OnTimer(_In_ HWND hwnd,
                                   _In_ UINT uMsg,
                                   _In_ UINT_PTR idEvent,
                                   _In_ DWORD dwTime) {
  ::KillTimer(hwnd, idEvent);
  WeaselPanel* self = (WeaselPanel*)ptimer;
  ptimer = 0;
  if (self) {
    self->bar_scale_ = 1.0;
    self->RedrawWindow();
  }
}

LRESULT WeaselPanel::OnMouseMove(UINT uMsg,
                                 WPARAM wParam,
                                 LPARAM lParam,
                                 BOOL& bHandled) {
  if (!m_layout || m_style.hover_type == UIStyle::NONE)
    return 0;
  if (m_mouse_entry == false) {
    TRACKMOUSEEVENT tme;
    tme.cbSize = sizeof(TRACKMOUSEEVENT);
    tme.dwFlags = TME_LEAVE;
    tme.dwHoverTime = 20;  // unit: ms
    tme.hwndTrack = m_hWnd;
    TrackMouseEvent(&tme);
  }
  bHandled = true;
  m_mouse_entry = true;
  CPoint point;
  point.x = GET_X_LPARAM(lParam);
  point.y = GET_Y_LPARAM(lParam);

  // Ignore if mouse screen position not changed
  CPoint ptScreen = point;
  ClientToScreen(&ptScreen);
  if (ptScreen == m_lastMousePos) {
    return 0;
  }
  m_lastMousePos = ptScreen;

  for (size_t i = 0; i < m_candidateCount && i < MAX_CANDIDATES_COUNT; ++i) {
    CRect rect = m_layout->GetCandidateRect((int)i);
    if (m_istorepos)
      rect.OffsetRect(0, m_offsetys[i]);
    rect.InflateRect(DPI_SCALE(m_style.hilite_padding_x),
                     DPI_SCALE(m_style.hilite_padding_y));
    if (rect.PtInRect(point)) {
      if (m_style.hover_type == UIStyle::HoverType::HILITE) {
        if (i != m_ctx.cinfo.highlighted && _UICallback)
          _UICallback(NULL, &i, NULL, NULL);
      } else {
        _SetCandidateHover(static_cast<int>(i));
      }
      return 0;
    }
  }
  _SetCandidateHover(-1);
  return 0;
}

LRESULT WeaselPanel::OnMouseLeave(UINT uMsg,
                                  WPARAM wParam,
                                  LPARAM lParam,
                                  BOOL& bHandled) {
  _SetCandidateHover(-1);
  m_lastMousePos = {-1, -1};
  m_mouse_entry = false;
  return 0;
}

void WeaselPanel::_SetCandidateHover(int index) {
  if (index == hover_target_) return;
  hover_target_ = index;
  hover_starts_ = hover_amounts_;
  hover_start_tick_ = GetTickCount64();
  if (!ClientMotionEnabled() || !::SetTimer(m_hWnd, kHoverTimer, 16, nullptr)) {
    ::KillTimer(m_hWnd, kHoverTimer);
    hover_amounts_.fill(0);
    if (index >= 0) hover_amounts_[index] = 1.0f;
  }
  RedrawWindow();
}

void WeaselPanel::_ResetCandidateHover() {
  ::KillTimer(m_hWnd, kHoverTimer);
  hover_target_ = -1;
  hover_amounts_.fill(0);
  m_lastMousePos = {-1, -1};
}

static float CandidateInkCenterY(const CRect& text, IDWriteTextLayout* layout,
                                 bool vertical = false) {
  DWRITE_OVERHANG_METRICS ink = {};
  if (layout && SUCCEEDED(layout->GetOverhangMetrics(&ink))) {
    const float offset_y = vertical ? (std::max)(0.0f, ink.top) : 0.0f;
    const float top = -ink.top;
    const float bottom = layout->GetMaxHeight() + ink.bottom;
    if (bottom > top) return text.top + offset_y + (top + bottom) * 0.5f;
  }
  return (text.top + text.bottom) * 0.5f;
}

void WeaselPanel::_DrawCandidateHover(CRect bounds, float text_center_y,
                                      float amount, bool selected) {
  if (amount <= 0.002f || !pDWR->pBrush) return;
  CRect inset = rcw;
  inset.DeflateRect((std::max)(2, DPI_SCALE(2)), (std::max)(3, DPI_SCALE(3)));
  const LONG left = (std::max)(bounds.left, inset.left);
  const LONG right = (std::min)(bounds.right, inset.right);
  // Keep the upper and lower padding equal even near the window edge.
  const float half_height = (std::min)({(bounds.Height() - 1.0f) * 0.5f,
      text_center_y - inset.top - 0.5f, inset.bottom - 0.5f - text_center_y});
  if (right - left <= 1 || half_height <= 0.0f) return;
  const float radius = (std::min)(5.0f * dpiScaleLayout, half_height);
  const auto shape = D2D1::RoundedRect(D2D1::RectF(
      left + 0.5f, text_center_y - half_height,
      right - 0.5f, text_center_y + half_height), radius, radius);
  pDWR->SetBrushColor(D2D1::ColorF(0xE48887, amount * (selected ? 0.30f : 0.19f)));
  pDWR->pRenderTarget->FillRoundedRectangle(shape, pDWR->pBrush.Get());
  pDWR->SetBrushColor(D2D1::ColorF(0xD58180, amount * (selected ? 0.74f : 0.52f)));
  pDWR->pRenderTarget->DrawRoundedRectangle(shape, pDWR->pBrush.Get(), (std::max)(1.0f, dpiScaleLayout));
}

void WeaselPanel::_ResetCaretMotion() {
  ::KillTimer(m_hWnd, kCaretTimer);
  caret_motion_start_ = 0;
  caret_positioned_ = false;
}

float WeaselPanel::_CaretPosition(ULONGLONG now) const {
  if (!caret_motion_start_) return caret_target_;
  const float elapsed = static_cast<float>(now - caret_motion_start_);
  const float remaining = 1.0f - (std::min)(1.0f, elapsed / kCaretDurationMs);
  return caret_target_ + (caret_from_ - caret_target_) * remaining * remaining;
}

WeaselPanel::CaretSideBearings WeaselPanel::_MeasureCaretSideBearings(
    wchar_t letter, IDWriteTextFormat* format) {
  const int index = LatinLetterIndex(letter);
  if (index < 0 || !format) return {};
  if (caret_spacing_format_.Get() != format) {
    caret_spacing_format_ = format;
    caret_spacing_.fill({});
  }
  auto& spacing = caret_spacing_[index];
  if (spacing.measured) return spacing;
  spacing.measured = true;
  constexpr float measure_space = 4096.0f;
  ComPtr<IDWriteTextLayout> letter_layout;
  if (FAILED(pDWR->pDWFactory->CreateTextLayout(&letter, 1, format, measure_space,
                                                measure_space, letter_layout.GetAddressOf())))
    return spacing;
  letter_layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
  letter_layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
  DWRITE_TEXT_METRICS metrics = {};
  DWRITE_OVERHANG_METRICS ink = {};
  if (SUCCEEDED(letter_layout->GetMetrics(&metrics)) &&
      SUCCEEDED(letter_layout->GetOverhangMetrics(&ink))) {
    spacing.leading = -ink.left;
    spacing.trailing = metrics.widthIncludingTrailingWhitespace - (measure_space + ink.right);
  }
  return spacing;
}

float WeaselPanel::_BalanceCaretGap(IDWriteTextLayout* layout, IDWriteTextFormat* format,
                                     const std::wstring& text, UINT32 cursor,
                                     float x, float y, const DWRITE_HIT_TEST_METRICS& hit) {
  if (cursor == 0 || cursor >= text.size() || hit.length != 1 || hit.textPosition != cursor ||
      hit.bidiLevel != 0 || LatinLetterIndex(text[cursor - 1]) < 0 ||
      LatinLetterIndex(text[cursor]) < 0)
    return x;
  float left_stop = 0, left_y = 0;
  DWRITE_HIT_TEST_METRICS left = {};
  if (FAILED(layout->HitTestTextPosition(cursor - 1, TRUE, &left_stop, &left_y, &left)) ||
      left_y != y || left.length != 1 || left.textPosition != cursor - 1 || left.bidiLevel != 0)
    return x;
  // Centre on the visible gap rather than the boundary of the advance boxes.
  const float left_ink = left_stop - _MeasureCaretSideBearings(text[cursor - 1], format).trailing;
  const float right_ink = x + _MeasureCaretSideBearings(text[cursor], format).leading;
  return right_ink > left_ink
      ? x + std::clamp((left_ink + right_ink) * 0.5f - x, -dpiScaleLayout, dpiScaleLayout)
      : x;
}

void WeaselPanel::_DrawCaret(float x, float top, float bottom, const D2D1_COLOR_F& color) {
  const float width = (std::max)(1.0f, dpiScaleLayout);
  if (bottom <= top || rcw.Width() < width) return;
  const float half_width = width * 0.5f;
  x = std::clamp(x, half_width, rcw.Width() - half_width);
  const auto now = GetTickCount64();
  if (!caret_positioned_ || x != caret_target_) {
    const bool in_motion = caret_motion_start_ != 0;
    const float from = _CaretPosition(now);
    const bool animate = caret_positioned_ && IsWindowVisible() && ClientMotionEnabled() &&
                         std::abs(x - from) > kCaretSettlePx;
    caret_positioned_ = true;
    caret_from_ = from;
    caret_target_ = x;
    // Keep the timer cadence when key repeats retarget the in-flight caret.
    caret_motion_start_ = animate && (in_motion || ::SetTimer(m_hWnd, kCaretTimer, 16, nullptr)) ? now : 0;
    if (!caret_motion_start_ && in_motion) ::KillTimer(m_hWnd, kCaretTimer);
  }
  const float left = _CaretPosition(now) - half_width;
  const float height = (bottom - top) * 0.72f;
  const float center = (top + bottom) * 0.5f;
  const float radius = width * 0.5f;
  pDWR->SetBrushColor(color);
  pDWR->pRenderTarget->FillRoundedRectangle(
      D2D1::RoundedRect(D2D1::RectF(left, center - height * 0.5f, left + width, center + height * 0.5f),
                        radius, radius), pDWR->pBrush.Get());
}

void WeaselPanel::_HighlightText(CDCHandle& dc,
                                 const CRect& rc,
                                 const COLORREF& color,
                                 const COLORREF& shadowColor,
                                 const int& radius,
                                 const BackType& type = BackType::TEXT,
                                 const IsToRoundStruct& rd = IsToRoundStruct(),
                                 const COLORREF& bordercolor = TRANS_COLOR) {
  // Graphics obj with SmoothingMode
  Gdiplus::Graphics g_back(dc);
  g_back.SetSmoothingMode(Gdiplus::SmoothingMode::SmoothingModeHighQuality);

  // blur buffer
  int blurMarginX = m_layout->offsetX;
  int blurMarginY = m_layout->offsetY;

  GraphicsRoundRectPath* hiliteBackPath;
  if (rd.Hemispherical && type != BackType::BACKGROUND &&
      NOT_FULLSCREENLAYOUT(m_style))
    hiliteBackPath = new GraphicsRoundRectPath(
        rc,
        DPI_SCALE(m_style.round_corner_ex) -
            (DPI_SCALE(m_style.border) % 2 ? DPI_SCALE(m_style.border) / 2 : 0),
        rd.IsTopLeftNeedToRound, rd.IsTopRightNeedToRound,
        rd.IsBottomRightNeedToRound, rd.IsBottomLeftNeedToRound);
  else  // background or current candidate background not out of window
        // background
    hiliteBackPath = new GraphicsRoundRectPath(rc, radius);

  // 必须shadow_color都是非完全透明色才做绘制, 全屏状态不绘制阴影保证响应速度
  if (DPI_SCALE(m_style.shadow_radius) && COLORNOTTRANSPARENT(shadowColor) &&
      NOT_FULLSCREENLAYOUT(m_style)) {
    CRect rect(blurMarginX + DPI_SCALE(m_style.shadow_offset_x),
               blurMarginY + DPI_SCALE(m_style.shadow_offset_y),
               rc.Width() + blurMarginX + DPI_SCALE(m_style.shadow_offset_x),
               rc.Height() + blurMarginY + DPI_SCALE(m_style.shadow_offset_y));
    BYTE r = GetRValue(shadowColor);
    BYTE g = GetGValue(shadowColor);
    BYTE b = GetBValue(shadowColor);
    BYTE alpha = (BYTE)((shadowColor >> 24) & 255);
    Gdiplus::Color shadow_color = Gdiplus::Color::MakeARGB(alpha, r, g, b);
    static Gdiplus::Bitmap* pBitmapDropShadow;
    pBitmapDropShadow = new Gdiplus::Bitmap((INT)rc.Width() + blurMarginX * 2,
                                            (INT)rc.Height() + blurMarginY * 2,
                                            PixelFormat32bppPARGB);

    Gdiplus::Graphics g_shadow(pBitmapDropShadow);
    g_shadow.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    // dropshadow, draw a roundrectangle to blur
    if (DPI_SCALE(m_style.shadow_offset_x) != 0 ||
        DPI_SCALE(m_style.shadow_offset_y) != 0) {
      GraphicsRoundRectPath shadow_path(rect, radius);
      Gdiplus::SolidBrush shadow_brush(shadow_color);
      g_shadow.FillPath(&shadow_brush, &shadow_path);
    }
    // round shadow, draw multilines as base round line
    else {
      int step = alpha / DPI_SCALE(m_style.shadow_radius) / 2;
      Gdiplus::Pen pen_shadow(shadow_color, (Gdiplus::REAL)1);
      for (int i = 0; i < DPI_SCALE(m_style.shadow_radius); i++) {
        GraphicsRoundRectPath round_path(rect, radius + 1 + i);
        g_shadow.DrawPath(&pen_shadow, &round_path);
        shadow_color = Gdiplus::Color::MakeARGB(alpha - i * step, r, g, b);
        pen_shadow.SetColor(shadow_color);
        rect.InflateRect(1, 1);
      }
    }
    DoGaussianBlur(pBitmapDropShadow, (float)DPI_SCALE(m_style.shadow_radius),
                   (float)DPI_SCALE(m_style.shadow_radius));

    g_back.DrawImage(pBitmapDropShadow, rc.left - blurMarginX,
                     rc.top - blurMarginY);

    // free memory
    delete pBitmapDropShadow;
    pBitmapDropShadow = NULL;
  }

  // 必须back_color非完全透明才绘制
  if (COLORNOTTRANSPARENT(color)) {
    Gdiplus::Color back_color = GDPCOLOR_FROM_COLORREF(color);
    Gdiplus::SolidBrush back_brush(back_color);
    g_back.FillPath(&back_brush, hiliteBackPath);
  }
  // draw border, for bordercolor not transparent and border valid
  if (COLORNOTTRANSPARENT(bordercolor) && DPI_SCALE(m_style.border) > 0) {
    Gdiplus::Color border_color = GDPCOLOR_FROM_COLORREF(bordercolor);
    Gdiplus::Pen gPenBorder(border_color,
                            (Gdiplus::REAL)DPI_SCALE(m_style.border));
    // candidate window border
    if (type == BackType::BACKGROUND) {
      GraphicsRoundRectPath bgPath(rc, DPI_SCALE(m_style.round_corner_ex));
      g_back.DrawPath(&gPenBorder, &bgPath);
    } else if (type !=
               BackType::TEXT)  // hilited_candidate_border / candidate_border
      g_back.DrawPath(&gPenBorder, hiliteBackPath);
  }
  // free memory
  delete hiliteBackPath;
  hiliteBackPath = NULL;
}

// draw preedit text, text only
bool WeaselPanel::_DrawPreedit(const Text& text,
                               CDCHandle dc,
                               const CRect& rc) {
  bool drawn = false;
  std::wstring const& t = text.str;
  IDWriteTextFormat1* txtFormat = pDWR->pPreeditTextFormat.Get();

  if (!t.empty()) {
    weasel::TextRange range = m_layout->GetPreeditRange();

    if (range.start < range.end) {
      std::wstring before_str = t.substr(0, range.start);
      std::wstring hilited_str = t.substr(range.start, range.end);
      std::wstring after_str = t.substr(range.end);
      CSize beforeSz = m_layout->GetBeforeSize();
      CSize hilitedSz = m_layout->GetHilitedSize();
      CSize afterSz = m_layout->GetAfterSize();

      int x = rc.left;
      int y = rc.top;

      if (range.start > 0) {
        // zzz
        std::wstring str_before(t.substr(0, range.start));
        CRect rc_before;
        if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT)
          rc_before = CRect(rc.left, y, rc.right, y + beforeSz.cy);
        else
          rc_before = CRect(x, rc.top, rc.left + beforeSz.cx, rc.bottom);
        _TextOut(rc_before, str_before.c_str(), str_before.length(),
                 m_style.text_color, txtFormat);
        if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT)
          y += beforeSz.cy + DPI_SCALE(m_style.hilite_spacing);
        else
          x += beforeSz.cx + DPI_SCALE(m_style.hilite_spacing);
      }
      {
        // zzz[yyy]
        std::wstring str_highlight(
            t.substr(range.start, (size_t)range.end - range.start));
        CRect rc_hi;

        if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT)
          rc_hi = CRect(rc.left, y, rc.right, y + hilitedSz.cy);
        else
          rc_hi = CRect(x, rc.top, x + hilitedSz.cx, rc.bottom);
        _TextOut(rc_hi, str_highlight.c_str(), str_highlight.length(),
                 m_style.hilited_text_color, txtFormat);
        if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT)
          y += rc_hi.Height() + DPI_SCALE(m_style.hilite_spacing);
        else
          x += rc_hi.Width() + DPI_SCALE(m_style.hilite_spacing);
      }
      if (range.end < static_cast<int>(t.length())) {
        // zzz[yyy]xxx
        std::wstring str_after(t.substr(range.end));
        CRect rc_after;
        if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT)
          rc_after = CRect(rc.left, y, rc.right, y + afterSz.cy);
        else
          rc_after = CRect(x, rc.top, x + afterSz.cx, rc.bottom);
        _TextOut(rc_after, str_after.c_str(), str_after.length(),
                 m_style.text_color, txtFormat);
      }
    } else {
      CRect rcText(rc.left, rc.top, rc.right, rc.bottom);
      _TextOut(rcText, t.c_str(), t.length(), m_style.text_color, txtFormat);
    }
    // draw pager mark if not inline_preedit if necessary
    if (m_candidateCount && !m_style.inline_preedit &&
        COLORNOTTRANSPARENT(m_style.prevpage_color) &&
        COLORNOTTRANSPARENT(m_style.nextpage_color)) {
      const std::wstring pre = L"<";
      const std::wstring next = L">";
      CRect prc = m_layout->GetPrepageRect();
      // clickable color / disabled color
      int color =
          m_ctx.cinfo.currentPage ? m_style.prevpage_color : m_style.text_color;
      if (m_istorepos)
        prc.OffsetRect(0, m_offsety_preedit);
      _TextOut(prc, pre.c_str(), pre.length(), color, txtFormat);

      CRect nrc = m_layout->GetNextpageRect();
      // clickable color / disabled color
      color = m_ctx.cinfo.is_last_page ? m_style.text_color
                                       : m_style.nextpage_color;
      if (m_istorepos)
        nrc.OffsetRect(0, m_offsety_preedit);
      _TextOut(nrc, next.c_str(), next.length(), color, txtFormat);
    }
    drawn = true;
  }
  return drawn;
}

// draw hilited back color, back only
bool WeaselPanel::_DrawPreeditBack(const Text& text,
                                   CDCHandle dc,
                                   const CRect& rc) {
  bool drawn = false;
  std::wstring const& t = text.str;
  IDWriteTextFormat1* txtFormat = pDWR->pPreeditTextFormat.Get();

  if (!t.empty()) {
    weasel::TextRange range = m_layout->GetPreeditRange();

    if (range.start < range.end) {
      CSize beforeSz = m_layout->GetBeforeSize();
      CSize hilitedSz = m_layout->GetHilitedSize();

      int x = rc.left;
      int y = rc.top;

      if (range.start > 0) {
        if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT)
          y += beforeSz.cy + DPI_SCALE(m_style.hilite_spacing);
        else
          x += beforeSz.cx + DPI_SCALE(m_style.hilite_spacing);
      }
      {
        CRect rc_hi;
        if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT)
          rc_hi = CRect(rc.left, y, rc.right, y + hilitedSz.cy);
        else
          rc_hi = CRect(x, rc.top, x + hilitedSz.cx, rc.bottom);
        // if preedit rect size smaller than icon, fill the gap to
        // STATUS_ICON_SIZE
        if (m_layout->ShouldDisplayStatusIcon()) {
          if ((m_style.layout_type == UIStyle::LAYOUT_HORIZONTAL ||
               m_style.layout_type == UIStyle::LAYOUT_VERTICAL) &&
              hilitedSz.cy < STATUS_ICON_SIZE)
            rc_hi.InflateRect(0, (STATUS_ICON_SIZE - hilitedSz.cy) / 2);
          if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT &&
              hilitedSz.cx < STATUS_ICON_SIZE)
            rc_hi.InflateRect((STATUS_ICON_SIZE - hilitedSz.cx) / 2, 0);
        }

        rc_hi.InflateRect(DPI_SCALE(m_style.hilite_padding_x),
                          DPI_SCALE(m_style.hilite_padding_y));
        IsToRoundStruct rd = m_layout->GetTextRoundInfo();
        if (m_istorepos) {
          std::swap(rd.IsTopLeftNeedToRound, rd.IsBottomLeftNeedToRound);
          std::swap(rd.IsTopRightNeedToRound, rd.IsBottomRightNeedToRound);
        }
        _HighlightText(dc, rc_hi, m_style.hilited_back_color,
                       m_style.hilited_shadow_color,
                       DPI_SCALE(m_style.round_corner), BackType::TEXT, rd);
      }
    }
    drawn = true;
  }
  return drawn;
}

bool WeaselPanel::_DrawCandidates(CDCHandle& dc, bool back) {
  bool drawn = false;
  const std::vector<Text>& candidates(m_ctx.cinfo.candies);
  const std::vector<Text>& comments(m_ctx.cinfo.comments);
  const std::vector<Text>& labels(m_ctx.cinfo.labels);
  // prevent all text format nullptr
  if (pDWR->pTextFormat.Get() == nullptr &&
      pDWR->pLabelTextFormat.Get() == nullptr &&
      pDWR->pCommentTextFormat.Get() == nullptr) {
    _InitFontRes(true);
  }
  ComPtr<IDWriteTextFormat1> txtFormat = pDWR->pTextFormat;
  ComPtr<IDWriteTextFormat1> labeltxtFormat = pDWR->pLabelTextFormat;
  ComPtr<IDWriteTextFormat1> commenttxtFormat = pDWR->pCommentTextFormat;
  BackType bkType = BackType::CAND;

  CRect rect;
  // draw back color and shadow color, with gdi+
  if (back) {
    // if candidate_shadow_color not transparent, draw candidate shadow first
    if (COLORNOTTRANSPARENT(m_style.candidate_shadow_color)) {
      for (auto i = 0; i < m_candidateCount && i < MAX_CANDIDATES_COUNT; ++i) {
        if (i == m_ctx.cinfo.highlighted)
          continue;  // draw non hilited candidates only
        rect = m_layout->GetCandidateRect((int)i);
        IsToRoundStruct rd = m_layout->GetRoundInfo(i);
        if (m_istorepos) {
          rect.OffsetRect(0, m_offsetys[i]);
          ReconfigRoundInfo(rd, i, m_candidateCount);
        }
        rect.InflateRect(DPI_SCALE(m_style.hilite_padding_x),
                         DPI_SCALE(m_style.hilite_padding_y));
        _HighlightText(dc, rect, 0x00000000, m_style.candidate_shadow_color,
                       DPI_SCALE(m_style.round_corner), bkType, rd);
        drawn = true;
      }
    }
    // draw non highlighted candidates, without shadow
    if ((COLORNOTTRANSPARENT(m_style.candidate_back_color) ||
         COLORNOTTRANSPARENT(m_style.candidate_border_color))) {
      for (auto i = 0; i < m_candidateCount && i < MAX_CANDIDATES_COUNT; ++i) {
        if (i == m_ctx.cinfo.highlighted)
          continue;
        rect = m_layout->GetCandidateRect((int)i);
        IsToRoundStruct rd = m_layout->GetRoundInfo(i);
        if (m_istorepos) {
          rect.OffsetRect(0, m_offsetys[i]);
          ReconfigRoundInfo(rd, i, m_candidateCount);
        }
        rect.InflateRect(DPI_SCALE(m_style.hilite_padding_x),
                         DPI_SCALE(m_style.hilite_padding_y));
        _HighlightText(dc, rect, m_style.candidate_back_color, 0x00000000,
                       DPI_SCALE(m_style.round_corner), bkType, rd,
                       m_style.candidate_border_color);
        drawn = true;
      }
    }
    // draw highlighted background and shadow
    {
      rect = m_layout->GetHighlightRect();
      bool markSt = bar_scale_ == 1.0 || (!m_style.mark_text.empty());
      IsToRoundStruct rd = m_layout->GetRoundInfo(m_ctx.cinfo.highlighted);
      if (m_istorepos) {
        rect.OffsetRect(0, m_offsetys[m_ctx.cinfo.highlighted]);
        ReconfigRoundInfo(rd, m_ctx.cinfo.highlighted, m_candidateCount);
      }
      rect.InflateRect(DPI_SCALE(m_style.hilite_padding_x),
                       DPI_SCALE(m_style.hilite_padding_y));
      _HighlightText(dc, rect, m_style.hilited_candidate_back_color,
                     markSt ? m_style.hilited_candidate_shadow_color : 0,
                     DPI_SCALE(m_style.round_corner), bkType, rd,
                     m_style.hilited_candidate_border_color);
      if (m_style.mark_text.empty() &&
          COLORNOTTRANSPARENT(m_style.hilited_mark_color)) {
        int height =
            min(rect.Height() - DPI_SCALE(m_style.hilite_padding_y) * 2,
                rect.Height() - DPI_SCALE(m_style.round_corner) * 2);
        int width = min(rect.Width() - DPI_SCALE(m_style.hilite_padding_x) * 2,
                        rect.Width() - DPI_SCALE(m_style.round_corner) * 2);
        width = min(width, static_cast<int>(rect.Width() * 0.618));
        height = min(height, static_cast<int>(rect.Height() * 0.618));
        if (bar_scale_ != 1.0f) {
          width = static_cast<int>(width * bar_scale_);
          height = static_cast<int>(height * bar_scale_);
        }
        Gdiplus::Graphics g_back(dc);
        g_back.SetSmoothingMode(
            Gdiplus::SmoothingMode::SmoothingModeHighQuality);
        Gdiplus::Color mark_color =
            GDPCOLOR_FROM_COLORREF(m_style.hilited_mark_color);
        Gdiplus::SolidBrush mk_brush(mark_color);
        if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT) {
          int x = rect.left + (rect.Width() - width) / 2;
          CRect mkrc{x, rect.top, x + width, rect.top + m_layout->mark_height};
          GraphicsRoundRectPath mk_path(mkrc, mkrc.Height() / 2);
          g_back.FillPath(&mk_brush, &mk_path);
        } else {
          int y = rect.top + (rect.Height() - height) / 2;
          CRect mkrc{rect.left, y, rect.left + m_layout->mark_width,
                     y + height};
          GraphicsRoundRectPath mk_path(mkrc, mkrc.Width() / 2);
          g_back.FillPath(&mk_brush, &mk_path);
        }
      }
      drawn = true;
    }
  }
  // draw text with direct write
  else {
    for (int i = 0; i < m_candidateCount; ++i) {
      if (hover_amounts_[i] <= 0.002f) continue;
      auto cell = m_layout->GetCandidateRect(i);
      auto text = m_layout->GetCandidateTextRect(i);
      if (m_istorepos) {
        cell.OffsetRect(0, m_offsetys[i]);
        text.OffsetRect(0, m_offsetys[i]);
      }
      cell.InflateRect(DPI_SCALE(3), DPI_SCALE(1));
      const auto& word = candidates.at(i).str;
      float center_y = (cell.top + cell.bottom) * 0.5f;
      if (txtFormat && !text.IsRectEmpty() && !word.empty() &&
          SUCCEEDED(pDWR->CreateTextLayout(word, static_cast<int>(word.size()), txtFormat.Get(),
                                          static_cast<float>(text.Width()),
                                          static_cast<float>(text.Height())))) {
        const bool vertical = m_style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT;
        if (vertical) {
          pDWR->SetLayoutReadingDirection(DWRITE_READING_DIRECTION_TOP_TO_BOTTOM);
          pDWR->SetLayoutFlowDirection(m_style.vertical_text_left_to_right
              ? DWRITE_FLOW_DIRECTION_LEFT_TO_RIGHT
              : DWRITE_FLOW_DIRECTION_RIGHT_TO_LEFT);
        }
        center_y = CandidateInkCenterY(text, pDWR->pTextLayout.Get(), vertical);
        // Stock horizontal glyphs are hinted one DIP above the layout ink center.
        if (!vertical) center_y -= dpiScaleLayout;
      }
      _DrawCandidateHover(cell, center_y, hover_amounts_[i], i == m_ctx.cinfo.highlighted);
    }
    // begin draw candidate texts
    int label_text_color, candidate_text_color, comment_text_color;
    for (auto i = 0; i < m_candidateCount && i < MAX_CANDIDATES_COUNT; ++i) {
      if (i == m_ctx.cinfo.highlighted) {
        label_text_color = m_style.hilited_label_text_color;
        candidate_text_color = m_style.hilited_candidate_text_color;
        comment_text_color = m_style.hilited_comment_text_color;
      } else {
        label_text_color = m_style.label_text_color;
        candidate_text_color = m_style.candidate_text_color;
        comment_text_color = m_style.comment_text_color;
      }
      // Draw label
      std::wstring label = m_layout->GetLabelText(
          labels, (int)i, m_style.label_text_format.c_str());
      if (!label.empty()) {
        rect = m_layout->GetCandidateLabelRect((int)i);
        if (m_istorepos)
          rect.OffsetRect(0, m_offsetys[i]);
        _TextOut(rect, label.c_str(), label.length(), label_text_color,
                 labeltxtFormat.Get());
      }
      // Draw text
      std::wstring text = candidates.at(i).str;
      if (!text.empty()) {
        rect = m_layout->GetCandidateTextRect((int)i);
        if (m_istorepos)
          rect.OffsetRect(0, m_offsetys[i]);
        _TextOut(rect, text.c_str(), text.length(), candidate_text_color,
                 txtFormat.Get());
      }
      // Draw comment
      std::wstring comment = comments.at(i).str;
      if (!comment.empty() && COLORNOTTRANSPARENT(comment_text_color)) {
        rect = m_layout->GetCandidateCommentRect((int)i);
        if (m_istorepos)
          rect.OffsetRect(0, m_offsetys[i]);
        _TextOut(rect, comment.c_str(), comment.length(), comment_text_color,
                 commenttxtFormat.Get());
      }
      drawn = true;
    }
    // draw highlight mark
    {
      if (!m_style.mark_text.empty() &&
          COLORNOTTRANSPARENT(m_style.hilited_mark_color)) {
        CRect rc = m_layout->GetHighlightRect();
        if (m_istorepos)
          rc.OffsetRect(0, m_offsetys[m_ctx.cinfo.highlighted]);
        rc.InflateRect(DPI_SCALE(m_style.hilite_padding_x),
                       DPI_SCALE(m_style.hilite_padding_y));
        int vgap = m_layout->mark_height
                       ? (rc.Height() - m_layout->mark_height) / 2
                       : 0;
        int hgap =
            m_layout->mark_width ? (rc.Width() - m_layout->mark_width) / 2 : 0;
        CRect hlRc;
        if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT)
          hlRc = CRect(rc.left + hgap,
                       rc.top + DPI_SCALE(m_style.hilite_padding_y),
                       rc.left + hgap + m_layout->mark_width,
                       rc.top + DPI_SCALE(m_style.hilite_padding_y) +
                           m_layout->mark_height);
        else
          hlRc = CRect(rc.left + DPI_SCALE(m_style.hilite_padding_x),
                       rc.top + vgap,
                       rc.left + DPI_SCALE(m_style.hilite_padding_x) +
                           m_layout->mark_width,
                       rc.bottom - vgap);
        _TextOut(hlRc, m_style.mark_text.c_str(), m_style.mark_text.length(),
                 m_style.hilited_mark_color, pDWR->pTextFormat.Get());
      }
    }
  }
  return drawn;
}

bool WeaselPanel::_LoadModeTipIcon(CIcon& icon, int width, int height) const {
  if (!_IsModeTip()) return false;
  const auto resource = m_status.ascii_mode ? IDI_HANLIN_MODE_ENGLISH : IDI_HANLIN_MODE_CHINESE;
  return icon.LoadIconW(resource, width, height, LR_DEFAULTCOLOR) != FALSE;
}

// draw client area
void WeaselPanel::DoPaint(CDCHandle dc) {
  if (weasel::ssf::SuppressEmptyWindow(m_style.ssf_enabled, m_ctx.empty(),
                                       m_status.show_mode_tip)) {
    ShowWindow(SW_HIDE);
    return;
  }
  // turn off WS_EX_TRANSPARENT, for better resp performance
  ModifyStyleEx(WS_EX_TRANSPARENT, WS_EX_LAYERED);
  GetClientRect(&rcw);

  // Per-paint field watch, noisy, so only compiled in when explicitly asked for
  // (define WEASEL_SSF_TRACE_PAINT). The always-on messages live in
  // _SsfReloadSkin(), which only runs when the configured skin changes.
#ifdef WEASEL_SSF_TRACE_PAINT
  {
    wchar_t buf[384];
    swprintf_s<384>(buf,
                    L"doPaint: skin='%ls' enabled=%d active=%d hide_cand=%d "
                    L"style_addr=%p",
                    m_style.ssf_skin.c_str(), m_style.ssf_enabled ? 1 : 0,
                    SsfActive() ? 1 : 0, hide_candidates ? 1 : 0,
                    static_cast<const void*>(&m_style));
    SSFLOG(buf);
  }
#endif

  // Sogou SSF skin path. It composes the whole window itself (per-pixel alpha,
  // 9-sliced backgrounds, skin fonts and colours) and pushes it through the same
  // layered-window update, so the stock drawing below is skipped entirely.
  // Falls through to the stock path when no skin is active.
  if (SsfActive() && !hide_candidates) {
    // A transient device failure must not change the selected skin backend.
    // _SsfDoPaint already restores the surface and falls back to GDI text.
    _SsfDoPaint(dc);
    return;
  }
  const int width = rcw.Width();
  const int height = rcw.Height();
  if (width <= 0 || height <= 0) return;
  // Keep a scratch surface between frames. Round the capacity up to avoid
  // reallocating every time a candidate or the preedit changes by a few pixels.
  if (!stock_dib_.Ensure((width + 31) & ~31, (height + 15) & ~15)) return;
  std::memset(stock_dib_.bits, 0,
              static_cast<size_t>(stock_dib_.width) * stock_dib_.height * 4);
  CDCHandle memDC = stock_dib_.dc;
  bool drawn = false;
  if (!hide_candidates) {
    CRect auxrc = m_layout->GetAuxiliaryRect();
    CRect preeditrc = m_layout->GetPreeditRect();
    if (m_istorepos) {
      CRect* rects = new CRect[m_candidateCount];
      int* btmys = new int[m_candidateCount];
      for (auto i = 0; i < m_candidateCount && i < MAX_CANDIDATES_COUNT; ++i) {
        rects[i] = m_layout->GetCandidateRect(i);
        btmys[i] = rects[i].bottom;
      }
      if (m_candidateCount) {
        if (!m_layout->IsInlinePreedit() && !m_ctx.preedit.str.empty())
          m_offsety_preedit =
              rects[m_candidateCount - 1].bottom - preeditrc.bottom;
        if (!m_ctx.aux.str.empty())
          m_offsety_aux = rects[m_candidateCount - 1].bottom - auxrc.bottom;
      } else {
        m_offsety_preedit = 0;
        m_offsety_aux = 0;
      }
      int base_gap = 0;
      if (!m_ctx.aux.str.empty())
        base_gap = auxrc.Height() + m_style.spacing;
      else if (!m_layout->IsInlinePreedit() && !m_ctx.preedit.str.empty())
        base_gap = preeditrc.Height() + m_style.spacing;

      for (auto i = 0; i < m_candidateCount && i < MAX_CANDIDATES_COUNT; ++i) {
        if (i == 0)
          m_offsetys[i] =
              btmys[m_candidateCount - i - 1] - base_gap - rects[i].bottom;
        else
          m_offsetys[i] = (rects[i - 1].top + m_offsetys[i - 1] -
                           DPI_SCALE(m_style.candidate_spacing)) -
                          rects[i].bottom;
      }
      delete[] rects;
      delete[] btmys;
    }
    // background and candidates back, hilite back drawing start
    if ((!m_ctx.empty() && !m_style.inline_preedit) ||
        (m_style.inline_preedit && (m_candidateCount || !m_ctx.aux.empty()))) {
      CRect backrc = m_layout->GetContentRect();
      _HighlightText(memDC, backrc, m_style.back_color, m_style.shadow_color,
                     DPI_SCALE(m_style.round_corner_ex), BackType::BACKGROUND,
                     IsToRoundStruct(), m_style.border_color);
    }
    if (!m_ctx.aux.str.empty()) {
      if (m_istorepos)
        auxrc.OffsetRect(0, m_offsety_aux);
      drawn |= _DrawPreeditBack(m_ctx.aux, memDC, auxrc);
    }
    if (!m_layout->IsInlinePreedit() && !m_ctx.preedit.str.empty()) {
      if (m_istorepos)
        preeditrc.OffsetRect(0, m_offsety_preedit);
      drawn |= _DrawPreeditBack(m_ctx.preedit, memDC, preeditrc);
    }
    if (m_candidateCount)
      drawn |= _DrawCandidates(memDC, true);
    // background and candidates back, hilite back drawing end

    // begin  texts drawing, if pRenderTarget failed, force to reinit
    // directwrite resources
    if (FAILED(pDWR->pRenderTarget->BindDC(memDC, &rcw))) {
      _InitFontRes(true);
      pDWR->pRenderTarget->BindDC(memDC, &rcw);
    }
    pDWR->pRenderTarget->BeginDraw();
    // draw auxiliary string
    if (!m_ctx.aux.str.empty())
      drawn |= _DrawPreedit(m_ctx.aux, memDC, auxrc);
    // draw preedit string
    if (!m_layout->IsInlinePreedit() && !m_ctx.preedit.str.empty())
      drawn |= _DrawPreedit(m_ctx.preedit, memDC, preeditrc);
    if (!m_layout->IsInlinePreedit() &&
        m_style.layout_type != UIStyle::LAYOUT_VERTICAL_TEXT && !m_ctx.preedit.str.empty()) {
      int cursor = -1;
      for (const auto& attr : m_ctx.preedit.attributes)
        if (attr.range.cursor >= 0) cursor = attr.range.cursor;
      const auto& text = m_ctx.preedit.str;
      if (cursor >= 0 && pDWR->pPreeditTextFormat && pDWR->pBrush &&
          SUCCEEDED(pDWR->CreateTextLayout(text.c_str(), static_cast<int>(text.size()),
            pDWR->pPreeditTextFormat.Get(), static_cast<float>(preeditrc.Width()),
            static_cast<float>(preeditrc.Height()))) && pDWR->pTextLayout) {
        float x = 0, y = 0;
        DWRITE_HIT_TEST_METRICS hit = {};
        const UINT32 stop = (std::min)(static_cast<UINT32>(cursor), static_cast<UINT32>(text.size()));
        if (SUCCEEDED(pDWR->pTextLayout->HitTestTextPosition(stop, FALSE, &x, &y, &hit))) {
          x = _BalanceCaretGap(pDWR->pTextLayout.Get(), pDWR->pPreeditTextFormat.Get(),
                               text, stop, x, y, hit);
          DWRITE_OVERHANG_METRICS overhang = {};
          pDWR->pTextLayout->GetOverhangMetrics(&overhang);
          x += preeditrc.left + (std::max)(0.0f, overhang.left);
          const float top = preeditrc.top + y;
          const auto ink = m_style.hilited_candidate_text_color;
          _DrawCaret(x, top, top + hit.height,
                    D2D1::ColorF(GetRValue(ink) / 255.0f, GetGValue(ink) / 255.0f,
                                 GetBValue(ink) / 255.0f, 1.0f));
        }
      }
    }
    // draw candidates string
    if (m_candidateCount)
      drawn |= _DrawCandidates(memDC);
    if (FAILED(pDWR->pRenderTarget->EndDraw())) {
      _InitFontRes(true);
      // Keep the last good frame while the device is being recreated.
      return;
    }
    // end texts drawing

    // status icon (I guess Metro IME stole my idea :)
    if (m_layout->ShouldDisplayStatusIcon()) {
#ifdef HANLIN_PANEL
      LoadIconNecessary(m_current_zhung_icon, m_style.current_zhung_icon, m_iconEnabled,
                        IDI_HANLIN_MODE_CHINESE);
      LoadIconNecessary(m_current_ascii_icon, m_style.current_ascii_icon, m_iconAlpha,
                        IDI_HANLIN_MODE_ENGLISH);
#else
      if (SsfActive()) {
        // These are Sogou's original hover-state mode glyphs.  Keep this
        // local to the candidate panel: Weasel's language bar accepts ICO
        // files only and must retain its own system-tray artwork.
        LoadIconNecessary(m_current_zhung_icon,
                          ssf_skin_dir_ + L"\\cn2.png", m_iconEnabled,
                          IDI_ZH);
        LoadIconNecessary(m_current_ascii_icon,
                          ssf_skin_dir_ + L"\\a2.png", m_iconAlpha,
                          IDI_EN);
      } else {
        // decide if custom schema zhung icon to show
        LoadIconNecessary(m_current_zhung_icon, m_style.current_zhung_icon,
                          m_iconEnabled, IDI_ZH);
        LoadIconNecessary(m_current_ascii_icon, m_style.current_ascii_icon,
                          m_iconAlpha, IDI_EN);
        LoadIconNecessary(m_current_half_icon, m_style.current_half_icon,
                          m_iconHalf, IDI_HALF_SHAPE);
        LoadIconNecessary(m_current_full_icon, m_style.current_full_icon,
                          m_iconFull, IDI_FULL_SHAPE);
      }
#endif
      CRect iconRect(m_layout->GetStatusIconRect());
      if (m_istorepos && !m_ctx.aux.str.empty())
        iconRect.OffsetRect(0, m_offsety_aux);
      else if (m_istorepos && !m_layout->IsInlinePreedit() &&
               !m_ctx.preedit.str.empty())
        iconRect.OffsetRect(0, m_offsety_preedit);

#ifdef HANLIN_PANEL
      CIcon& icon = m_status.disabled ? m_iconDisabled : (m_status.ascii_mode ? m_iconAlpha : m_iconEnabled);
#else
      CIcon& icon = m_status.disabled
                        ? m_iconDisabled
                        : (SsfActive()
                               ? (m_status.ascii_mode ? m_iconAlpha
                                                      : m_iconEnabled)
                               : (m_status.ascii_mode
                                      ? m_iconAlpha
                                      : (m_status.type == SCHEMA
                                             ? m_iconEnabled
                                             : (m_status.full_shape
                                                    ? m_iconFull
                                                    : m_iconHalf))));
#endif
      CIcon modeTip;
      const bool branded = _LoadModeTipIcon(modeTip, iconRect.Width(), iconRect.Height());
      memDC.DrawIconEx(iconRect.left, iconRect.top, branded ? modeTip : icon,
                       iconRect.Width(), iconRect.Height());
      drawn = true;
    }
    /* Nothing drawn, hide candidate window */
    if (!drawn)
      ShowWindow(SW_HIDE);
  }
  _LayerUpdate(rcw, memDC);
}

// 由于某些软件并不依赖 WM_PAINT 消息来重绘，在消息循环中直接忽略掉了 WM_PAINT
// 消息， 导致 DoPaint() 永远不会被调用，这里手动调用 DoPaint() 强制重绘
void WeaselPanel::RedrawWindow() {
  DoPaint(nullptr);
}

bool WeaselPanel::_LayerUpdate(const CRect& rc, CDCHandle dc) {
  HDC ScreenDC = ::GetDC(NULL);
  if (ScreenDC == NULL)
    return false;
  CRect rect;
  GetWindowRect(&rect);
  POINT WindowPosAtScreen = {rect.left, rect.top};
  POINT PointOriginal = {0, 0};
  SIZE sz = {rc.Width(), rc.Height()};

  BLENDFUNCTION bf = {AC_SRC_OVER, 0, 0XFF, AC_SRC_ALPHA};
  const BOOL updated = UpdateLayeredWindow(m_hWnd, ScreenDC, &WindowPosAtScreen,
                                           &sz, dc, &PointOriginal,
                                           RGB(0, 0, 0), &bf, ULW_ALPHA);
  ReleaseDC(ScreenDC);
  return updated != FALSE;
}

LRESULT WeaselPanel::OnCreate(UINT uMsg,
                              WPARAM wParam,
                              LPARAM lParam,
                              BOOL& bHandled) {
  m_mouse_entry = false;
#ifdef HANLIN_PANEL
  // Receive candidate hover and clicks without taking focus from the editor.
  EnableWindow(TRUE);
#endif
  // The candidate DLL can be in a lower-integrity app than the server.
  // This notification carries no pointers or data; it only requests a scan.
  ChangeWindowMessageFilterEx(m_hWnd, kAvoidCandidateMessage, MSGFLT_ALLOW,
                              nullptr);
  Refresh();
  return TRUE;
}

LRESULT WeaselPanel::OnDestroy(UINT uMsg,
                               WPARAM wParam,
                               LPARAM lParam,
                               BOOL& bHandled) {
  _ResetCandidateHover();
  _ResetCaretMotion();
  stock_dib_.Clear();
  RemovePropW(m_hWnd, kPanelRole);
  m_lastMousePos = {-1, -1};
  m_sticky = false;
  delete m_layout;
  m_layout = NULL;
  return 0;
}

void WeaselPanel::_RepositionModeTip() {
  m_modeTipPlaced = false;
  MONITORINFO monitor{sizeof(monitor)};
  RECT window;
  if (m_caret.bottom > m_caret.top &&
      GetMonitorInfo(MonitorFromRect(m_caret, MONITOR_DEFAULTTONEAREST), &monitor) &&
      ::GetWindowRect(m_hWnd, &window)) {
    std::vector<RECT> candidates;
    EnumWindows(CollectCandidateRects, reinterpret_cast<LPARAM>(&candidates));
    const SIZE size{window.right - window.left, window.bottom - window.top};
    const auto target = weasel::PlaceModeTip(
        m_caret, size, monitor.rcWork, std::move(candidates), (std::max)(2, DPI_SCALE(4)));
    if (target) {
      m_modeTipPlaced = (target->left == window.left && target->top == window.top) ||
          SetWindowPos(HWND_TOPMOST, target->left, target->top, 0, 0,
                       SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOREDRAW);
    }
  }
  if (!m_modeTipPlaced && IsWindowVisible()) ShowWindow(SW_HIDE);
}

LRESULT WeaselPanel::OnTimerMessage(UINT uMsg,
                                    WPARAM wParam,
                                    LPARAM lParam,
                                    BOOL& bHandled) {
  if (wParam == kCaretTimer) {
    if (!caret_motion_start_ || !IsWindowVisible()) {
      _ResetCaretMotion();
      return 0;
    }
    if (!ClientMotionEnabled() ||
        std::abs(_CaretPosition(GetTickCount64()) - caret_target_) <= kCaretSettlePx) {
      ::KillTimer(m_hWnd, kCaretTimer);
      caret_motion_start_ = 0;
    }
    RedrawWindow();
  } else if (wParam == kHoverTimer) {
    if (!IsWindowVisible()) return 0;
    const float t = (std::min)(1.0f, (GetTickCount64() - hover_start_tick_) / kHoverDuration);
    const float rest = 1.0f - t;
    for (int i = 0; i < m_candidateCount; ++i) {
      const float target = i == hover_target_ ? 1.0f : 0.0f;
      hover_amounts_[i] = target + (hover_starts_[i] - target) * rest * rest * rest;
    }
    if (t >= 1.0f) ::KillTimer(m_hWnd, kHoverTimer);
    RedrawWindow();
  } else {
    bHandled = false;
  }
  return 0;
}

LRESULT WeaselPanel::OnDpiChanged(UINT uMsg,
                                  WPARAM wParam,
                                  LPARAM lParam,
                                  BOOL& bHandled) {
  Refresh();
  return LRESULT();
}

LRESULT WeaselPanel::OnAvoidCandidate(UINT, WPARAM, LPARAM, BOOL&) {
  if (IsWindowVisible() && _IsModeTip()) _RepositionModeTip();
  return 0;
}

LRESULT WeaselPanel::OnPanelShow(UINT, WPARAM shown, LPARAM, BOOL& handled) {
  if (!shown) {
    _ResetCaretMotion();
    _ResetCandidateHover();
    m_mouse_entry = false;
  }
  if (GetPropW(m_hWnd, kPanelRole) == reinterpret_cast<HANDLE>(1))
    EnumWindows(NotifyModeTip, 0);
  else if (shown && GetPropW(m_hWnd, kPanelRole) == reinterpret_cast<HANDLE>(2))
    PostMessage(kAvoidCandidateMessage, 0, 0);
  handled = false;
  return 0;
}

void WeaselPanel::MoveTo(RECT const& rc) {
  const bool same_caret = EqualRect(&m_caret, &rc);
  m_caret = rc;
  if (!m_layout)
    return;  // avoid handling nullptr in _RepositionWindow
  if (_IsModeTip()) {
    if (!same_caret || !IsWindowVisible()) _RepositionWindow(true);
    return;
  }
  if (m_layout->ShouldDisplayStatusIcon()) {
    m_sticky = false;
    m_istorepos = false;
    if (!EqualRect(&m_inputPos, &rc) || !IsWindowVisible()) {
      m_inputPos = rc;
      _RepositionWindow(true);
    }
    return;
  }
  m_redraw_by_monitor_change = false;
  // Keep the text-caret anchor separate from the panel's actual top-left.
  // Candidate windows use a six-pixel gap below the caret; comparing the raw
  // caret against an already-offset (or previously clamped) panel position
  // made every key stroke look like a new location and caused visible jumps.
  CRect candidateAnchor(rc);
  candidateAnchor.OffsetRect(0, 6);
  // The conditions for resetting the sticky state:
  // 1. When the input session ends (ctx.empty() is true)
  // 2. When the input position changes significantly (the position change
  // exceeds the threshold)
  // 3. When the content of the candidate window is empty
  bool should_reset_sticky =
      (m_ctx.empty() ||
       (abs(candidateAnchor.left - m_inputPos.left) > 50) ||
       (abs(candidateAnchor.bottom - m_inputPos.bottom) > 50));
  if (should_reset_sticky && m_sticky) {
    m_sticky = false;
    // Force reposition the window
    m_inputPos = candidateAnchor;
    _RepositionWindow(true);
    RedrawWindow();
    return;
  }
  // if ascii_tip_follow_cursor set, move tip icon to mouse cursor
  if (m_style.ascii_tip_follow_cursor && m_ctx.empty() &&
      (!m_status.composing) && m_layout->ShouldDisplayStatusIcon()) {
    // ascii icon follow cursor
    POINT p;
    ::GetCursorPos(&p);
    RECT irc{p.x - STATUS_ICON_SIZE, p.y - STATUS_ICON_SIZE, p.x, p.y};
    m_inputPos = irc;
    _RepositionWindow(true);
    RedrawWindow();
  } else {
    // Word and some Chromium/WinUI controls report the same caret with a
    // one-to-five-pixel vertical wobble. Treat that as one stable anchor. An
    // exact duplicate is stable too; the old predicate accidentally moved on
    // duplicates while suppressing only a subset of non-identical updates.
    const bool sameAnchor =
        abs(candidateAnchor.left - m_inputPos.left) <= 1 &&
        abs(candidateAnchor.top - m_inputPos.top) < 6 &&
        abs(candidateAnchor.bottom - m_inputPos.bottom) < 6;
    if (sameAnchor)
      return;

    m_inputPos = candidateAnchor;
    // buffer current m_istorepos status
    bool m_istorepos_buf = m_istorepos;
    // with parameter to avoid vertical flicker
    _RepositionWindow(true);
    // m_istorepos status changed by _RepositionWindow, or tips to show,
    // redrawing is required
    if (m_istorepos != m_istorepos_buf || !m_ctx.aux.empty() ||
        m_layout->ShouldDisplayStatusIcon() || m_redraw_by_monitor_change)
      RedrawWindow();
  }
}

void WeaselPanel::_RepositionWindow(const bool& adj) {
  if (_IsModeTip()) {
    SetPropW(m_hWnd, kPanelRole, reinterpret_cast<HANDLE>(2));
    _RepositionModeTip();
    return;
  }
  // Never place the window while the caret anchor is still the default CRect().
  // OnCreate() calls Refresh(), which calls this before any caret position has
  // been supplied, so the lookup below resolved x=0/y=0 and moved the freshly
  // created window to the work-area origin.  Nothing had been shown yet, but the
  // window was already parked there when the first Show() arrived, which is the
  // "candidate window flashes in the top-left corner" after switching input
  // methods.  The anchor always arrives before the window is shown, and MoveTo()
  // re-runs this then.
  if (m_inputPos.left == 0 && m_inputPos.top == 0 && m_inputPos.right == 0 &&
      m_inputPos.bottom == 0)
    return;
  if (!m_ctx.empty())
    SetPropW(m_hWnd, kPanelRole, reinterpret_cast<HANDLE>(1));
  else
    RemovePropW(m_hWnd, kPanelRole);
  RECT rcWorkArea;
  memset(&rcWorkArea, 0, sizeof(rcWorkArea));
  HMONITOR hMonitor = MonitorFromRect(m_inputPos, MONITOR_DEFAULTTONEAREST);
  if (hMonitor) {
    MONITORINFO info;
    info.cbSize = sizeof(MONITORINFO);
    if (GetMonitorInfo(hMonitor, &info)) {
      rcWorkArea = info.rcWork;
    }
    if (hMonitor != m_hMonitor) {
      m_hMonitor = hMonitor;
      m_redraw_by_monitor_change = true;
    }
  }
  RECT rcWindow;
  GetWindowRect(&rcWindow);
  int width = (rcWindow.right - rcWindow.left);
  int height = (rcWindow.bottom - rcWindow.top);
  // keep panel visible
  rcWorkArea.right -= width;
  rcWorkArea.bottom -= height;
  int x = m_inputPos.left;
  int y = m_inputPos.bottom;
  if (DPI_SCALE(m_style.shadow_radius)) {
    x -= (DPI_SCALE(m_style.shadow_offset_x) >= 0 ||
          COLORTRANSPARENT(m_style.shadow_color))
             ? m_layout->offsetX
             : (m_layout->offsetX / 2);
    if (adj)
      y -= (DPI_SCALE(m_style.shadow_offset_y) > 0 ||
            COLORTRANSPARENT(m_style.shadow_color))
               ? m_layout->offsetY
               : (m_layout->offsetY / 2);
  }
  // for vertical text layout, flow right to left, make window left side
  if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT &&
      !m_style.vertical_text_left_to_right) {
    x += m_layout->offsetX - width;
    if (DPI_SCALE(m_style.shadow_offset_x) < 0)
      x += m_layout->offsetX;
  }
  if (adj)
    m_istorepos = false;
  if (x > rcWorkArea.right)
    x = rcWorkArea.right;  // over workarea right
  if (x < rcWorkArea.left)
    x = rcWorkArea.left;  // over workarea left
  // show panel above the input focus if we're around the bottom
  if (y > rcWorkArea.bottom || m_sticky) {
    if (!m_sticky)
      m_sticky = true;
    y = m_inputPos.top - height - 6;  // over workarea bottom
    if (DPI_SCALE(m_style.shadow_radius) &&
        DPI_SCALE(m_style.shadow_offset_y) > 0)
      y -= DPI_SCALE(m_style.shadow_offset_y);
    // The SSF layout engine lays candidates out top-down and has no notion of
    // reversing them, so the per-candidate offset table it would feed
    // (_offsetys) is never populated. Leaving m_istorepos set would silently
    // shift hit-testing away from the painted cells, so it is suppressed while a
    // skin is active. See docs/ssf-skin.md section 3.3.
    m_istorepos = (!SsfActive() && m_style.vertical_auto_reverse &&
                   m_style.layout_type == UIStyle::LAYOUT_VERTICAL);
    if (DPI_SCALE(m_style.shadow_radius) > 0)
      y += (DPI_SCALE(m_style.shadow_offset_y) < 0 ||
            COLORTRANSPARENT(m_style.shadow_color))
               ? m_layout->offsetY
               : (m_layout->offsetY / 2);
  }
  if (y < rcWorkArea.top)
    y = rcWorkArea.top;  // over workarea top
  // m_inputPos remains the caret anchor. Never replace its bottom coordinate
  // with the panel's clamped y position: doing so feeds a window coordinate
  // back into the next caret comparison and makes the popup drift.
  RECT previous = {};
  ::GetWindowRect(m_hWnd, &previous);
  if (previous.left != x || previous.top != y)
    SetWindowPos(HWND_TOPMOST, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOREDRAW);
  RECT current = {};
  if (GetPropW(m_hWnd, kPanelRole) == reinterpret_cast<HANDLE>(1) &&
      ::GetWindowRect(m_hWnd, &current) &&
      !EqualRect(&m_lastCandidateRect, &current)) {
    m_lastCandidateRect = current;
    EnumWindows(NotifyModeTip, 0);  // includes size-only changes near a tip
  }
}

// ===========================================================================
// Sogou SSF skin support
// ===========================================================================
//
// Everything below is inert unless a skin is configured and loads successfully:
// SsfActive() gates every entry point, and _CreateLayout()/DoPaint() fall back
// to the stock Weasel path otherwise. That is what keeps the SSF layer purely
// additive.

namespace {

// Read a whole file as raw bytes. Returns an empty vector when the file is
// missing, unreadable, or implausibly large; a skin deleted between deploy and
// load must not take the IME down.
//
// The bytes are handed to SsfIniParser untouched so that its own encoding
// detection (UTF-16LE/BE with BOM, UTF-8, GBK) sees the real BOM and the real
// byte sequence.
std::vector<char> ReadRawFile(const std::wstring& path) {
  std::vector<char> raw;
  HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return raw;

  LARGE_INTEGER size;
  if (!::GetFileSizeEx(h, &size) || size.QuadPart <= 0 ||
      size.QuadPart > (64 << 20)) {
    ::CloseHandle(h);
    return raw;
  }

  raw.resize(static_cast<size_t>(size.QuadPart));
  DWORD read = 0;
  const BOOL ok =
      ::ReadFile(h, raw.data(), static_cast<DWORD>(raw.size()), &read, nullptr);
  ::CloseHandle(h);
  if (!ok || read == 0) {
    raw.clear();
    return raw;
  }
  raw.resize(read);
  return raw;
}

inline D2D1_COLOR_F ToD2DColor(const weasel::ssf::Color& c) {
  return D2D1::ColorF(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f,
                      c.a / 255.0f);
}

// Resolve relative skin paths using this product's user-data location.
std::wstring ResolveUserDataDir() {
#ifdef HANLIN_PANEL
  constexpr auto key = L"Software\\HanLin";
  constexpr auto value = L"UserDataDir";
  constexpr auto fallback = L"%AppData%\\HanLin";
#else
  constexpr auto key = L"Software\\Rime\\Weasel";
  constexpr auto value = L"RimeUserDir";
  constexpr auto fallback = L"%AppData%\\Rime";
#endif
  wchar_t buffer[MAX_PATH] = {0};
  DWORD buf_len = sizeof(buffer);
  if (::RegGetValueW(HKEY_CURRENT_USER, key,
                     value, RRF_RT_REG_SZ, nullptr, buffer,
                     &buf_len) == ERROR_SUCCESS &&
      buffer[0]) {
    return std::wstring(buffer);
  }
  if (::ExpandEnvironmentStringsW(fallback, buffer,
                                  _countof(buffer)) > 0 &&
      buffer[0]) {
    return std::wstring(buffer);
  }
  return std::wstring();
}

}  // namespace



bool WeaselPanel::_SsfReloadSkin() {
  const std::wstring& wanted = m_style.ssf_skin;

  // Log the decision inputs at the point where the branch is taken: if the skin
  // does not apply, this is the first thing worth knowing. The address of the
  // style this panel reads is included so that a mismatch with the style the
  // handler wrote is immediately visible. Only format when logging is on: this
  // runs on every keystroke.
  if (SsfLogEnabled()) {
    wchar_t buf[512];
    swprintf_s<512>(buf,
                    L"reload: enabled=%d skin='%ls' loaded=%d same=%d "
                    L"style_addr=%p",
                    m_style.ssf_enabled ? 1 : 0, wanted.c_str(),
                    ssf_skin_loaded_ ? 1 : 0,
                    (ssf_loaded_skin_id_ == wanted) ? 1 : 0,
                    static_cast<const void*>(&m_style));
    SSFLOG(buf);
  }

  // No skin requested: release anything we hold and report the stock path.
  if (!m_style.ssf_enabled || wanted.empty()) {
    ssf_fallback_ = false;
    if (ssf_skin_loaded_ || !ssf_loaded_skin_id_.empty()) {
      ssf_skin_loaded_ = false;
      ssf_loaded_skin_id_.clear();
      ssf_skin_dir_.clear();
      ssf_renderer_.images().Clear();
      ssf_adapter_ = nullptr;
      SSFLOG(L"[SSF] disabled");
    }
    return SsfActive();
  }

  if (ssf_loaded_skin_id_ == wanted) {
    if (ssf_skin_loaded_) return true;
    if (GetTickCount64() < ssf_retry_after_) return SsfActive();
  }

  // Resolve the skin directory. Relative names are resolved against the Rime
  // user data directory (the registry's RimeUserDir), which is where an
  // extracted .ssf belongs.
  std::wstring dir = wanted;
  const bool absolute = (dir.size() > 1 && (dir[1] == L':' || dir[0] == L'\\' ||
                                            dir[0] == L'/'));
  if (!absolute) {
    std::wstring base = ResolveUserDataDir();
    SSFLOG(L"user data dir (registry/AppData) = '" + base + L"'");
    if (!base.empty()) {
      if (base.back() != L'\\' && base.back() != L'/') base.push_back(L'\\');
      dir = base + wanted;
    }
  }

  // Read the raw bytes and let ParseSkinIni() detect the encoding itself: it
  // already handles UTF-16LE/BE with BOM, UTF-8 (with or without BOM) and GBK.
  //
  // It is important NOT to pre-decode to UTF-16 here and then hand the bytes
  // back to the parser: a BOM-stripped UTF-16 buffer re-entering the detector
  // fails strict UTF-8 validation on its interleaved NUL bytes, falls through to
  // the GBK path, and every character is mangled. That produced a skin that
  // parsed "successfully" with an empty name, an empty font and the default font
  // size -- the layer looked active and drew with the wrong fonts.
  const std::wstring ini_path = dir + L"\\skin.ini";
  const std::vector<char> raw = ReadRawFile(ini_path);
  if (raw.empty()) {
    SSFLOG(L"skin.ini not found or empty: " + ini_path);
    ssf_skin_loaded_ = false;
    ssf_retry_after_ = GetTickCount64() + 2000;
    ssf_loaded_skin_id_ = wanted;  // remember, so we do not retry every frame
    ssf_skin_storage_ = weasel::ssf::Skin();
    ssf_skin_storage_.font_size = 16;
    ssf_skin_storage_.font_en = "Arial";
    ssf_skin_storage_.font_ch = "SimSun";
    ssf_skin_storage_.pinyin_color = {65, 65, 65, 255};
    ssf_skin_storage_.zhongwen_first_color = {65, 65, 65, 255};
    ssf_skin_storage_.zhongwen_color = {255, 108, 110, 255};
    ssf_format_point_[0] = ssf_format_point_[1] = ssf_format_point_[2] = ssf_format_point_[3] = 0;
    ssf_fallback_ = true;
    ssf_adapter_ = nullptr;
    return true;
  }

  ssf_skin_storage_ = weasel::ssf::ParseSkinIni(raw.data(), raw.size());
#ifdef HANLIN_PANEL
  // Keep the original SSF metrics even if an older installer left its
  // skin.ini behind. Use the Windows family name for reliable lookup.
  ssf_skin_storage_.font_size = 16;
  ssf_skin_storage_.font_ch = "SimSun";
  ssf_skin_storage_.font_en = "Arial";
#endif

  ssf_loaded_skin_id_ = wanted;
  ssf_skin_dir_ = dir;
  ssf_renderer_.images().Clear();
  ssf_renderer_.images().SetDirectory(dir);
  ssf_skin_loaded_ = true;
  ssf_fallback_ = false;
  ssf_format_point_[0] = ssf_format_point_[1] = ssf_format_point_[2] =
      ssf_format_point_[3] = 0;  // force a font rebuild

  const bool horizontal =
      m_style.layout_type != UIStyle::LAYOUT_VERTICAL &&
      m_style.layout_type != UIStyle::LAYOUT_VERTICAL_FULLSCREEN;
  const bool prefer_split_background = ssf_skin_storage_.h1.pic.empty();
  ssf_renderer_.WarmUp(ssf_skin_storage_, horizontal, prefer_split_background,
                       m_style.ssf_status_bar);

  SSFLOG(L"[SSF] loaded skin '" + weasel::ssf::Utf8ToWide(
                                  ssf_skin_storage_.skin_name) +
         L"' from " + dir);
  SSFLOG(L"[SSF] font_size=" + std::to_wstring(ssf_skin_storage_.font_size) +
         L" font_ch=" + weasel::ssf::Utf8ToWide(ssf_skin_storage_.font_ch) +
         L" font_en=" + weasel::ssf::Utf8ToWide(ssf_skin_storage_.font_en));

  // Inspect() intentionally uses a scratch store.  Calling it in normal
  // operation made every asset decode once for diagnostics and once again for
  // rendering, which was visible as a delay on the first candidate window.
  if (SsfLogEnabled()) {
    const auto report = ssf_renderer_.Inspect(ssf_skin_storage_, horizontal);
    for (const auto& r : report) {
      if (!r.loaded) {
        SSFLOG(L"[SSF] missing image: " + weasel::ssf::Utf8ToWide(r.name));
      }
    }
  }

  return true;
}

namespace {
void SsfSetCjkFont(IDWriteTextLayout* layout, const std::wstring& text,
                   const std::string& family) {
  const std::wstring face = weasel::ssf::Utf8ToWide(family);
  if (!layout || face.empty()) return;
  for (UINT32 i = 0; i < text.size();) {
    if (text[i] < 0x80) { ++i; continue; }
    const UINT32 start = i;
    while (i < text.size() && text[i] >= 0x80) ++i;
    layout->SetFontFamilyName(face.c_str(), DWRITE_TEXT_RANGE{start, i-start});
  }
}
}

void WeaselPanel::_SsfInitFonts() {
  if (!SsfActive() || pDWR == NULL || pDWR->pDWFactory == NULL) return;

  // SSF's font_size is an authored pixel size, not a typographic point size.
  // Keep the art and glyphs proportional on a scaled monitor, then pass that
  // device-pixel value to the 96-DPI DirectWrite DC render target as DIPs.
  // This makes SSF's font_size=12 render as 12px at 100% DPI.
  const float layout_scale =
      pDWR->dpiScaleLayout > 0.0f ? pDWR->dpiScaleLayout : 1.0f;
  const float pixel_size =
      static_cast<float>((std::max)(1, ssf_skin_storage_.font_size)) *
      layout_scale;
  // Rebuild the formats whenever the effective DPI changes.
  const int dpi_key = static_cast<int>(layout_scale * 96.0f + 0.5f);

  bool ready = true;
  for (int i = 0; i < 4; ++i)
    ready &= ssf_formats_[i] != nullptr && ssf_format_point_[i] == dpi_key;
  if (ready) return;

  const auto ch = weasel::ssf::Utf8ToWide(ssf_skin_storage_.font_ch);
  const auto en = weasel::ssf::Utf8ToWide(ssf_skin_storage_.font_en);

  for (int i = 0; i < 4; ++i) {
    if (ssf_formats_[i] != nullptr && ssf_format_point_[i] == dpi_key) continue;
    ssf_text_cache_[i].clear();

    ComPtr<IDWriteTextFormat> format;
    // Candidate labels are the numeric selectors ("1.", "2." …). Sogou
    // renders them with the CJK face too, so explicitly use SimSun here even
    // though those glyphs are ASCII. Other English runs remain Arial.
    const wchar_t* primary_face =
        (i == weasel::ssf::ITextMeasurer::LABEL && !ch.empty())
            ? ch.c_str()
            : (en.empty() ? L"Arial" : en.c_str());
    HRESULT hr = pDWR->pDWFactory->CreateTextFormat(
        primary_face, nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, pixel_size, L"",
        format.GetAddressOf());
    if (FAILED(hr) || format == nullptr) {
      ssf_formats_[i].Reset();
      ssf_format_point_[i] = dpi_key;
      continue;
    }

    ComPtr<IDWriteTextFormat1> format1;
    if (SUCCEEDED(format->QueryInterface(
            __uuidof(IDWriteTextFormat1),
            reinterpret_cast<void**>(format1.GetAddressOf())))) {
      // Pin the origin to the top-left so measured advances and drawn glyphs
      // start from the same point.
      format1->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
      format1->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
      format1->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
      ssf_formats_[i] = format1;
    } else {
      ssf_formats_[i].Reset();
    }
    ssf_format_point_[i] = dpi_key;

    // Vertical metrics, taken from the same layout that will draw the glyphs.
    ssf_ascent_[i] = 0;
    ssf_line_height_[i] = 0;
    if (ssf_formats_[i] != nullptr) {
      ComPtr<IDWriteTextLayout> tl;
      if (SUCCEEDED(pDWR->pDWFactory->CreateTextLayout(
              L"\u56fd", 1, ssf_formats_[i].Get(), 4096.0f, 4096.0f,
              tl.GetAddressOf())) &&
          tl != nullptr) {
        DWRITE_LINE_METRICS lm = {};
        UINT32 count = 0;
        if (SUCCEEDED(tl->GetLineMetrics(&lm, 1, &count)) && count > 0)
          ssf_ascent_[i] = static_cast<int>(lm.baseline + 0.5f);
        ssf_line_height_[i] = static_cast<int>(lm.height + 0.5f);
      }
    }
    if (ssf_line_height_[i] <= 0)
      ssf_line_height_[i] = static_cast<int>(pixel_size * 1.4f);
    if (ssf_ascent_[i] <= 0)
      ssf_ascent_[i] = ssf_line_height_[i] * 3 / 4;
  }
}

ComPtr<IDWriteTextLayout> WeaselPanel::_SsfTextLayout(const std::wstring& text, int kind) {
  const int i = kind & 3;
  if (!pDWR || !pDWR->pDWFactory || !ssf_formats_[i]) return {};
  auto& cache = ssf_text_cache_[i];
  auto found = cache.find(text);
  if (found != cache.end()) return found->second;
  ComPtr<IDWriteTextLayout> layout;
  if (FAILED(pDWR->pDWFactory->CreateTextLayout(text.c_str(),
      static_cast<UINT32>(text.size()), ssf_formats_[i].Get(),
      100000.0f, 4096.0f, layout.GetAddressOf()))) return {};
  SsfSetCjkFont(layout.Get(), text, ssf_skin_storage_.font_ch);
  // Bound both object count and retained user text over a long session.
  if (cache.size() >= 64) cache.clear();
  if (text.size() <= 4096) cache.emplace(text, layout);
  return layout;
}

bool WeaselPanel::DibSurface::Ensure(int w, int h) {
  if (dc && width == w && height == h) return true;
  Clear();
  dc = ::CreateCompatibleDC(nullptr);
  if (!dc) return false;
  BITMAPINFO bmi = {};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = w; bmi.bmiHeader.biHeight = -h;
  bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;
  bitmap = ::CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!bitmap || !bits) { Clear(); return false; }
  original = ::SelectObject(dc, bitmap);
  if (!original || original == HGDI_ERROR) { original = nullptr; Clear(); return false; }
  width = w; height = h;
  return true;
}

CSize WeaselPanel::_SsfMeasure(const std::wstring& text,
                               weasel::ssf::ITextMeasurer::FontKind kind) {
  CSize size(0, 0);
  const int i = static_cast<int>(kind) & 3;
  if (text.empty() || pDWR == NULL || pDWR->pDWFactory == NULL)
    return size;
  if (ssf_formats_[i] == nullptr) return size;

  auto layout = _SsfTextLayout(text, i);
  if (!layout) return size;

  DWRITE_TEXT_METRICS tm;
  if (SUCCEEDED(layout->GetMetrics(&tm))) {
    size.cx = static_cast<LONG>(ceil(tm.widthIncludingTrailingWhitespace));
    // Height is taken from the line metrics rather than tm.height: the latter
    // varies with the number of reported lines and made the pinyin strip and
    // the candidate row disagree by a pixel on some fonts.
    size.cy = ssf_line_height_[i];
    DWRITE_OVERHANG_METRICS om;
    if (SUCCEEDED(layout->GetOverhangMetrics(&om))) {
      if (om.left > 0) size.cx += static_cast<LONG>(om.left + 1);
      if (om.right > 0) size.cx += static_cast<LONG>(om.right + 1);
    }
  }
  return size;
}

int WeaselPanel::_SsfAscent(weasel::ssf::ITextMeasurer::FontKind kind) {
  const int i = static_cast<int>(kind) & 3;
  return ssf_ascent_[i] > 0 ? ssf_ascent_[i] : 1;
}

void WeaselPanel::_SsfDrawText(CDCHandle memDC) {
  if (ssf_adapter_ == nullptr || pDWR == NULL || pDWR->pRenderTarget == NULL)
    return;

  const weasel::ssf::RenderText& rtext = ssf_adapter_->render_text();
  const weasel::ssf::Skin& skin = ssf_skin_storage_;

  // Rebuild the same runs the compositor produced so painting and layout cannot
  // diverge: the geometry comes from the layout, the colours from the skin.
  const weasel::ssf::SsfLayoutResult& L = ssf_adapter_->layout();

  auto draw = [&](const weasel::ssf::Rect& box, const std::wstring& text,
                  const weasel::ssf::Color& color,
                  weasel::ssf::ITextMeasurer::FontKind kind) {
    if (text.empty() || box.Empty()) return;
    const int i = static_cast<int>(kind) & 3;
    if (ssf_formats_[i] == nullptr) return;

    pDWR->SetBrushColor(ToD2DColor(color));
    auto text_layout = _SsfTextLayout(text, i);
    if (!text_layout) return;

    float x = static_cast<float>(box.left);
    const float y = static_cast<float>(box.top);
    DWRITE_OVERHANG_METRICS om;
    if (SUCCEEDED(text_layout->GetOverhangMetrics(&om)) && om.left > 0)
      x += om.left;
    pDWR->pRenderTarget->DrawTextLayout({x, y}, text_layout.Get(), pDWR->pBrush.Get());
  };

  // Pinyin strip.
  draw(L.pinyin_text, rtext.pinyin, skin.pinyin_color,
       weasel::ssf::ITextMeasurer::PINYIN);
  // Use the same DirectWrite layout as the preedit text to find the caret.
  const weasel::TextRange preedit_range = ssf_adapter_->GetPreeditRange();
  if (!rtext.pinyin.empty() && !L.pinyin_text.Empty() &&
      preedit_range.cursor >= 0) {
    // Animation-only redraws reuse the layout and its caret target.
    if (!ssf_caret_x_) {
      const auto cursor = (std::min)(
          static_cast<size_t>(preedit_range.cursor), rtext.pinyin.size());
      float advance = 0, y = 0;
      DWRITE_HIT_TEST_METRICS hit = {};
      auto layout = _SsfTextLayout(rtext.pinyin, weasel::ssf::ITextMeasurer::PINYIN);
      if (layout && SUCCEEDED(layout->HitTestTextPosition(
          static_cast<UINT32>(cursor), FALSE, &advance, &y, &hit))) {
        advance = _BalanceCaretGap(layout.Get(),
            ssf_formats_[weasel::ssf::ITextMeasurer::PINYIN].Get(),
            rtext.pinyin, static_cast<UINT32>(cursor), advance, y, hit);
      } else {
        advance = static_cast<float>(_SsfMeasure(rtext.pinyin.substr(0, cursor),
            weasel::ssf::ITextMeasurer::PINYIN).cx);
      }
      DWRITE_OVERHANG_METRICS overhang = {};
      if (layout) layout->GetOverhangMetrics(&overhang);
      ssf_caret_x_ = (std::min)(static_cast<float>(L.pinyin_text.right),
          L.pinyin_text.left + advance + (std::max)(0.0f, overhang.left));
    }
    _DrawCaret(*ssf_caret_x_, static_cast<float>(L.pinyin_text.top), static_cast<float>(L.pinyin_text.bottom),
               ToD2DColor(skin.pinyin_color));
  }
  draw(L.aux_text, rtext.aux, skin.comphint_color,
       weasel::ssf::ITextMeasurer::PINYIN);

  // Candidates. The highlighted one uses zhongwen_first_color, the rest
  // zhongwen_color, which is exactly the distinction SSF relies on.
  for (size_t i = 0; i < L.candidates.size() && i < hover_amounts_.size(); ++i) {
    if (hover_amounts_[i] <= 0.002f) continue;
    const auto& candidate = L.candidates[i];
    const auto& box = candidate.box;
    CRect cell(box.left - DPI_SCALE(2), box.top + DPI_SCALE(2),
               box.right + DPI_SCALE(2), box.bottom - DPI_SCALE(2));
    float center_y = (cell.top + cell.bottom) * 0.5f;
    if (i < rtext.texts.size() && !rtext.texts[i].empty() && !candidate.text.Empty()) {
      const auto layout = _SsfTextLayout(rtext.texts[i],
          static_cast<int>(weasel::ssf::ITextMeasurer::CANDIDATE));
      const auto& text = candidate.text;
      center_y = CandidateInkCenterY(
          CRect(text.left, text.top, text.right, text.bottom), layout.Get());
    }
    _DrawCandidateHover(cell, center_y, hover_amounts_[i],
                        static_cast<int>(i) == rtext.highlighted);
  }
  for (size_t i = 0; i < L.candidates.size(); ++i) {
    const bool hl = (static_cast<int>(i) == rtext.highlighted);
    const auto& c = hl ? skin.zhongwen_first_color : skin.zhongwen_color;
    if (i < rtext.labels.size())
      draw(L.candidates[i].label, rtext.labels[i], c,
           weasel::ssf::ITextMeasurer::LABEL);
    if (i < rtext.texts.size())
      draw(L.candidates[i].text, rtext.texts[i], c,
           weasel::ssf::ITextMeasurer::CANDIDATE);
    if (i < rtext.comments.size())
      draw(L.candidates[i].comment, rtext.comments[i], skin.comphint_color,
           weasel::ssf::ITextMeasurer::COMMENT);
  }

  // Page indicator inside the candidate strip.
  if (L.has_page_indicator && !rtext.texts.empty()) {
    const std::wstring page = L"< " +
                              std::to_wstring(m_ctx.cinfo.currentPage + 1) +
                              L" >";
    draw(L.next_page, page, skin.comphint_color,
         weasel::ssf::ITextMeasurer::COMMENT);
  }
}

bool WeaselPanel::_SsfDoPaint(CDCHandle dc) {
  (void)dc;
  if (ssf_adapter_ == nullptr) {
    SSFLOG(L"doPaint: ssf_adapter_ is null -> stock renderer");
    return false;
  }

  if (m_layout->ShouldDisplayStatusIcon()) {
    const auto tip = ssf_renderer_.images().Get(
        m_status.ascii_mode ? "a2.png" : "cn2.png");
    const bool has_skin_tip = tip && tip->Valid();
    const int w = has_skin_tip ? tip->Width() : 25;
    const int h = has_skin_tip ? tip->Height() : 25;
    CIcon modeTip;
    if (_LoadModeTipIcon(modeTip, w, h)) {
      if (!ssf_dib_.Ensure(w, h)) return false;
      std::memset(ssf_dib_.bits, 0, static_cast<size_t>(w) * h * 4);
      if (!::DrawIconEx(ssf_dib_.dc, 0, 0, modeTip, w, h, 0, nullptr, DI_NORMAL)) return false;
      ::GdiFlush();
      return _LayerUpdate(CRect(0, 0, w, h), ssf_dib_.dc);
    }

    // A skin PNG must keep its original alpha and extent when no branded icon is available.
    if (!has_skin_tip || !ssf_dib_.Ensure(w, h)) return false;
    const size_t bytes = static_cast<size_t>(w) * h * 4;
    if (!tip->ToPremultipliedBGRA(static_cast<uint8_t*>(ssf_dib_.bits), bytes))
      return false;
    return _LayerUpdate(CRect(0, 0, w, h), ssf_dib_.dc);
  }

  const weasel::ssf::SsfLayoutResult& layout = ssf_adapter_->layout();
  const int w = (std::max)(1, layout.window.cx);
  const int h = (std::max)(1, layout.window.cy);
  if (SsfLogEnabled()) {
    wchar_t buf[256];
    swprintf_s<256>(buf, L"doPaint: composing SSF surface %dx%d", w, h);
    SSFLOG(buf);
  }

  // 1. Compose the background. This runs on our own RGBA surface so the PNG's
  //    alpha survives; filling a rectangle first would destroy the rounded
  //    corners and the drop shadows Sogou skins rely on.
  weasel::ssf::LayoutOptions lopts;
  lopts.orientation = ssf_adapter_->orientation();
  lopts.scale = ssf_adapter_->scale();
  lopts.prefer_split_background = ssf_skin_storage_.h1.pic.empty();

  weasel::ssf::RenderOptions ropts;
  ropts.scale = lopts.scale;
  ropts.draw_status_bar = m_style.ssf_status_bar;

  weasel::ssf::RenderResult result = ssf_renderer_.Compose(
      ssf_skin_storage_, lopts, ssf_adapter_->input(),
      ssf_adapter_->render_text(), layout, ropts);
  if (!result.surface) return false;

  // 2. Upload to a top-down 32bpp DIB. A compatible bitmap would have no alpha
  //    channel at all, which is the single most common reason a layered IME
  //    window shows black corners.
  if (!ssf_dib_.Ensure(w, h)) return false;
  HDC memDC = ssf_dib_.dc;
  void* bits = ssf_dib_.bits;
  const size_t expected = static_cast<size_t>(w) * h * 4;
  auto restore_surface = [&]() {
    return result.surface->ToPremultipliedBGRA(static_cast<uint8_t*>(bits), expected);
  };
  if (!restore_surface()) return false;

  // 3. Text, through DirectWrite so the glyphs come from the same font the
  //    layout measured.
  bool directwrite_drawn = false;
  if (pDWR != NULL && pDWR->pRenderTarget != NULL &&
      pDWR->pBrush != NULL) {
    CRect rcw(0, 0, w, h);
    HRESULT bind_result = pDWR->pRenderTarget->BindDC(memDC, &rcw);
    if (FAILED(bind_result)) {
      _InitFontRes(true);
      if (pDWR != NULL && pDWR->pRenderTarget != NULL)
        bind_result = pDWR->pRenderTarget->BindDC(memDC, &rcw);
    }
    if (SUCCEEDED(bind_result)) {
      // ID2D1DCRenderTarget::BeginDraw returns void, unlike the other Direct2D
      // render targets, so it must not be wrapped in SUCCEEDED.
      pDWR->pRenderTarget->BeginDraw();
      _SsfDrawText(memDC);
      if (FAILED(pDWR->pRenderTarget->EndDraw())) {
        // EndDraw can discard the DC contents on a device loss. Restore the
        // complete skin before GDI draws this frame's text; otherwise the
        // composition remains active but the layered window becomes blank.
        restore_surface();
        _InitFontRes(true);
      } else {
        directwrite_drawn = true;
      }
    }
  }

  // D2D may temporarily lose its DC render target when an application changes
  // monitor, DPI or graphics device. The background is already in the DIB, so
  // draw the same run list through GDI for this one frame instead of publishing
  // a blank candidate window. The next healthy DirectWrite frame resumes the
  // normal antialiased path.
  if (!directwrite_drawn) {
    ::SetBkMode(memDC, TRANSPARENT);
    for (const weasel::ssf::TextRun& run : result.runs) {
      if (run.text.empty() || run.box.Empty()) continue;
      const std::wstring face = weasel::ssf::ChooseFontFace(
          ssf_skin_storage_, run.text, run.font);
      const int pixel_height = (std::max)(
          1, static_cast<int>(ssf_skin_storage_.font_size * lopts.scale + 0.5));
      HFONT font = ::CreateFontW(
          -pixel_height, 0, 0, 0,
          run.style == weasel::ssf::FontStyle::Bold ? FW_BOLD : FW_NORMAL,
          FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
          CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
          DEFAULT_PITCH | FF_DONTCARE, face.c_str());
      if (font == NULL) continue;
      HGDIOBJ old_font = ::SelectObject(memDC, font);
      ::SetTextColor(memDC, RGB(run.color.r, run.color.g, run.color.b));
      ::TextOutW(memDC, run.box.left, run.box.top, run.text.c_str(),
                 static_cast<int>(run.text.size()));
      ::SelectObject(memDC, old_font);
      ::DeleteObject(font);
    }
  }

  // 4. GDI and DirectWrite both zero the alpha byte of every pixel they touch,
  //    which would punch holes in the skin wherever text sits. Raise alpha back
  //    to opaque inside those rectangles only; anti-aliased skin pixels outside
  //    them are left alone.
  {
    auto* px = static_cast<uint8_t*>(bits);
    for (const weasel::ssf::Rect& r : result.text_rects) {
      const int x0 = (std::max)(0, r.left);
      const int y0 = (std::max)(0, r.top);
      const int x1 = (std::min)(w, r.right);
      const int y1 = (std::min)(h, r.bottom);
      for (int y = y0; y < y1; ++y) {
        uint8_t* row = px + (static_cast<size_t>(y) * w + x0) * 4;
        for (int x = x0; x < x1; ++x, row += 4) {
          if (row[3] == 0) row[3] = 255;
        }
      }
    }
  }

  // 5. Keep DC/DIB storage for the next frame of the same dimensions.
  const bool updated = _LayerUpdate(CRect(0, 0, w, h), memDC);

  return updated;
}

void WeaselPanel::_TextOut(const CRect& rc,
                           const std::wstring& psz,
                           const size_t& cch,
                           const int& inColor,
                           IDWriteTextFormat1* const pTextFormat) {
  if (pTextFormat == NULL)
    return;
  float r = (float)(GetRValue(inColor)) / 255.0f;
  float g = (float)(GetGValue(inColor)) / 255.0f;
  float b = (float)(GetBValue(inColor)) / 255.0f;
  float alpha = (float)((inColor >> 24) & 255) / 255.0f;
  HRESULT hr = S_OK;
  if (pDWR->pBrush == NULL) {
    HR(pDWR->CreateBrush(D2D1::ColorF(r, g, b, alpha)));
  } else
    pDWR->SetBrushColor(D2D1::ColorF(r, g, b, alpha));

  HR(pDWR->CreateTextLayout(psz.c_str(), (int)cch, pTextFormat,
                            (float)rc.Width(), (float)rc.Height()));
  if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT) {
    DWRITE_FLOW_DIRECTION flow = m_style.vertical_text_left_to_right
                                     ? DWRITE_FLOW_DIRECTION_LEFT_TO_RIGHT
                                     : DWRITE_FLOW_DIRECTION_RIGHT_TO_LEFT;
    HR(pDWR->SetLayoutReadingDirection(DWRITE_READING_DIRECTION_TOP_TO_BOTTOM));
    HR(pDWR->SetLayoutFlowDirection(flow));
  }

  // offsetx for font glyph over left
  float offsetx = (float)rc.left;
  float offsety = (float)rc.top;
  // prepare for space when first character overhanged
  DWRITE_OVERHANG_METRICS omt;
  HR(pDWR->GetLayoutOverhangMetrics(&omt));
  if (m_style.layout_type != UIStyle::LAYOUT_VERTICAL_TEXT && omt.left > 0)
    offsetx += omt.left;
  if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT && omt.top > 0)
    offsety += omt.top;

  if (pDWR->pTextLayout != NULL) {
    pDWR->DrawTextLayoutAt({offsetx, offsety});
#if 0
    D2D1_RECT_F rectf =  D2D1::RectF(offsetx, offsety, offsetx + rc.Width(), offsety + rc.Height());
    pDWR->DrawRect(&rectf);
#endif
  }
}

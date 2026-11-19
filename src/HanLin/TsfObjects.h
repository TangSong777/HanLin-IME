#pragma once

#include "Transport.h"

#include <msctf.h>
#include <ctffunc.h>
#include <wrl/client.h>

#include <atomic>

namespace hanlin {

using Microsoft::WRL::ComPtr;

inline constexpr GUID kTip = {0x0857a4ae, 0x7b77, 0x47f3, {0x98,0xec,0xfc,0x98,0xf5,0x98,0x01,0x11}};
inline constexpr GUID kProfile = {0x0edfae44, 0x206d, 0x4a8c, {0x89,0xfe,0x52,0x87,0x35,0x69,0x62,0x3a}};
inline constexpr GUID kPreedit = {0x91563318, 0xaecf, 0x4cd3, {0x91,0x18,0x8e,0x5a,0x94,0xad,0x40,0x90}};
inline constexpr GUID kCandidateElement = {0xe533ee46, 0xbe59, 0x4540, {0xb6,0x4d,0x81,0xc5,0x81,0x3a,0xa2,0xfa}};
inline constexpr LANGID kLanguage = MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED);
inline constexpr wchar_t kName[] = L"寒淋输入法";
inline constexpr DWORD kNoCookie = TF_INVALID_COOKIE;
inline constexpr DWORD kNoElement = static_cast<DWORD>(-1);
extern HINSTANCE module_instance;
extern std::atomic<long> module_refs;

class ComLifetime {
 public:
  ComLifetime() { ++module_refs; }
  ~ComLifetime() { --module_refs; }
  ULONG Retain() { return ++refs_; }
  template <typename T> ULONG Release(T* self) {
    const auto remaining = --refs_;
    if (!remaining) delete self;
    return remaining;
  }

 private:
  std::atomic<ULONG> refs_{1};
};

template <typename Interface>
class SingleInterface : public Interface {
 public:
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (iid != IID_IUnknown && iid != __uuidof(Interface)) return E_NOINTERFACE;
    *out = static_cast<Interface*>(this);
    AddRef();
    return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return lifetime_.Retain(); }
  ULONG STDMETHODCALLTYPE Release() override { return lifetime_.Release(this); }
  virtual ~SingleInterface() = default;

 private:
  ComLifetime lifetime_;
};

HRESULT NewService(REFIID iid, void** out);
HRESULT EnumInputAttributes(IEnumTfDisplayAttributeInfo** out);
HRESULT InputAttribute(REFGUID id, ITfDisplayAttributeInfo** out);

class CandidateElement final : public SingleInterface<ITfCandidateListUIElementBehavior> {
 public:
  using Action = std::function<void(Command, int, uint32_t)>;
  explicit CandidateElement(Action action) : action_(std::move(action)) {}
  bool Update(ITfContext* context, const State& state);
  void Detach() { action_ = {}; document_.Reset(); }
  bool Visible() const { return shown_; }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override;
  HRESULT STDMETHODCALLTYPE GetDescription(BSTR* out) override;
  HRESULT STDMETHODCALLTYPE GetGUID(GUID* out) override;
  HRESULT STDMETHODCALLTYPE Show(BOOL show) override;
  HRESULT STDMETHODCALLTYPE IsShown(BOOL* out) override;
  HRESULT STDMETHODCALLTYPE GetUpdatedFlags(DWORD* out) override;
  HRESULT STDMETHODCALLTYPE GetDocumentMgr(ITfDocumentMgr** out) override;
  HRESULT STDMETHODCALLTYPE GetCount(UINT* out) override;
  HRESULT STDMETHODCALLTYPE GetSelection(UINT* out) override;
  HRESULT STDMETHODCALLTYPE GetString(UINT index, BSTR* out) override;
  HRESULT STDMETHODCALLTYPE GetPageIndex(UINT* indices, UINT capacity, UINT* count) override;
  HRESULT STDMETHODCALLTYPE SetPageIndex(UINT* indices, UINT count) override;
  HRESULT STDMETHODCALLTYPE GetCurrentPage(UINT* out) override;
  HRESULT STDMETHODCALLTYPE SetSelection(UINT index) override;
  HRESULT STDMETHODCALLTYPE Finalize() override;
  HRESULT STDMETHODCALLTYPE Abort() override;

 private:
  bool shown_ = true;
  Action action_;
  ComPtr<ITfDocumentMgr> document_;
  std::vector<std::wstring> items_;
  UINT selected_ = 0;
  uint32_t menu_revision_ = 0;
};

class ModeButton final : public ITfLangBarItemButton, public ITfSource {
 public:
  explicit ModeButton(std::function<void()> toggle) : toggle_(std::move(toggle)) {}
  void Update(bool ascii);
  void Detach() { toggle_ = {}; sink_.Reset(); }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override;
  ULONG STDMETHODCALLTYPE AddRef() override { return lifetime_.Retain(); }
  ULONG STDMETHODCALLTYPE Release() override { return lifetime_.Release(this); }
  HRESULT STDMETHODCALLTYPE GetInfo(TF_LANGBARITEMINFO* info) override;
  HRESULT STDMETHODCALLTYPE GetStatus(DWORD* status) override;
  HRESULT STDMETHODCALLTYPE Show(BOOL show) override;
  HRESULT STDMETHODCALLTYPE GetTooltipString(BSTR* text) override;
  HRESULT STDMETHODCALLTYPE OnClick(TfLBIClick click, POINT, const RECT*) override;
  HRESULT STDMETHODCALLTYPE InitMenu(ITfMenu*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE OnMenuSelect(UINT) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetIcon(HICON* icon) override;
  HRESULT STDMETHODCALLTYPE GetText(BSTR* text) override;
  HRESULT STDMETHODCALLTYPE AdviseSink(REFIID iid, IUnknown* unknown, DWORD* cookie) override;
  HRESULT STDMETHODCALLTYPE UnadviseSink(DWORD cookie) override;

 private:
  ComLifetime lifetime_;
  ComPtr<ITfLangBarItemSink> sink_;
  std::function<void()> toggle_;
  bool shown_ = true;
  bool ascii_ = false;
};

}  // namespace hanlin

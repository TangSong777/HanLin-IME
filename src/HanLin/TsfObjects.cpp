#include "TsfObjects.h"
#include "../include/HanLinIcons.h"

#include <olectl.h>

#include <algorithm>
#include <new>

namespace hanlin {
namespace {

HRESULT Bstr(const wchar_t* text, BSTR* out) {
  if (!out) return E_POINTER;
  *out = SysAllocString(text);
  return *out ? S_OK : E_OUTOFMEMORY;
}

constexpr TF_DISPLAYATTRIBUTE kDisplay = {
    {TF_CT_NONE, {0}}, {TF_CT_NONE, {0}}, TF_LS_DOT, FALSE, {TF_CT_NONE, {0}}, TF_ATTR_INPUT};

class PreeditAttribute final : public SingleInterface<ITfDisplayAttributeInfo> {
 public:
  HRESULT STDMETHODCALLTYPE GetGUID(GUID* out) override {
    if (!out) return E_POINTER;
    *out = kPreedit;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetDescription(BSTR* out) override { return Bstr(kName, out); }
  HRESULT STDMETHODCALLTYPE GetAttributeInfo(TF_DISPLAYATTRIBUTE* out) override {
    if (!out) return E_POINTER;
    *out = value_;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE SetAttributeInfo(const TF_DISPLAYATTRIBUTE* value) override {
    if (!value) return E_POINTER;
    value_ = *value;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE Reset() override { value_ = kDisplay; return S_OK; }

 private:
  TF_DISPLAYATTRIBUTE value_ = kDisplay;
};

class Attributes final : public SingleInterface<IEnumTfDisplayAttributeInfo> {
 public:
  explicit Attributes(bool done = false) : done_(done) {}
  HRESULT STDMETHODCALLTYPE Clone(IEnumTfDisplayAttributeInfo** out) override {
    if (!out) return E_POINTER;
    *out = new (std::nothrow) Attributes(done_);
    return *out ? S_OK : E_OUTOFMEMORY;
  }
  HRESULT STDMETHODCALLTYPE Next(ULONG count, ITfDisplayAttributeInfo** out, ULONG* read) override {
    if (read) *read = 0;
    if (!count) return S_OK;
    if (!out || (count != 1 && !read)) return E_POINTER;
    if (done_) return S_FALSE;
    const auto result = InputAttribute(kPreedit, out);
    if (FAILED(result)) return result;
    if (read) *read = 1;
    done_ = true;
    return count == 1 ? S_OK : S_FALSE;
  }
  HRESULT STDMETHODCALLTYPE Reset() override { done_ = false; return S_OK; }
  HRESULT STDMETHODCALLTYPE Skip(ULONG count) override {
    if (!count) return S_OK;
    const auto exact = !done_ && count == 1;
    done_ = true;
    return exact ? S_OK : S_FALSE;
  }

 private:
  bool done_;
};

}  // namespace

HRESULT EnumInputAttributes(IEnumTfDisplayAttributeInfo** out) {
  if (!out) return E_POINTER;
  *out = new (std::nothrow) Attributes;
  return *out ? S_OK : E_OUTOFMEMORY;
}

HRESULT InputAttribute(REFGUID id, ITfDisplayAttributeInfo** out) {
  if (!out) return E_POINTER;
  *out = nullptr;
  if (id != kPreedit) return E_INVALIDARG;
  *out = new (std::nothrow) PreeditAttribute;
  return *out ? S_OK : E_OUTOFMEMORY;
}

bool CandidateElement::Update(ITfContext* context, const State& state) {
  ComPtr<ITfDocumentMgr> document;
  context->GetDocumentMgr(&document);
  const auto count = state.candidates.size();
  const UINT selected = count ? (std::min)(state.selection, static_cast<UINT>(count - 1)) : 0;
  bool changed = document.Get() != document_.Get() || selected != selected_ || count != items_.size();
  document_ = std::move(document);
  selected_ = selected;
  menu_revision_ = state.menu_revision;
  items_.resize(count);
  for (size_t i = 0; i < count; ++i) {
    const auto& text = state.candidates[i].text;
    if (items_[i] != text) {
      items_[i] = text;
      changed = true;
    }
  }
  return changed;
}

HRESULT CandidateElement::QueryInterface(REFIID iid, void** out) {
  if (!out) return E_POINTER;
  if (iid == IID_ITfUIElement || iid == IID_ITfCandidateListUIElement) {
    *out = static_cast<ITfCandidateListUIElement*>(this);
    AddRef();
    return S_OK;
  }
  return SingleInterface::QueryInterface(iid, out);
}

HRESULT CandidateElement::GetDescription(BSTR* out) { return Bstr(kName, out); }
HRESULT CandidateElement::GetGUID(GUID* out) {
  if (!out) return E_POINTER;
  *out = kCandidateElement;
  return S_OK;
}
HRESULT CandidateElement::Show(BOOL show) { shown_ = show != 0; return S_OK; }
HRESULT CandidateElement::IsShown(BOOL* out) {
  if (!out) return E_POINTER;
  *out = shown_;
  return S_OK;
}
HRESULT CandidateElement::GetUpdatedFlags(DWORD* out) {
  if (!out) return E_POINTER;
  *out = TF_CLUIE_DOCUMENTMGR | TF_CLUIE_COUNT | TF_CLUIE_SELECTION |
      TF_CLUIE_STRING | TF_CLUIE_PAGEINDEX | TF_CLUIE_CURRENTPAGE;
  return S_OK;
}
HRESULT CandidateElement::GetDocumentMgr(ITfDocumentMgr** out) { return document_.CopyTo(out); }
HRESULT CandidateElement::GetCount(UINT* out) {
  if (!out) return E_POINTER;
  *out = static_cast<UINT>(items_.size());
  return S_OK;
}
HRESULT CandidateElement::GetSelection(UINT* out) {
  if (!out) return E_POINTER;
  *out = selected_;
  return S_OK;
}
HRESULT CandidateElement::GetString(UINT index, BSTR* out) {
  if (index >= items_.size()) return E_INVALIDARG;
  return Bstr(items_[index].c_str(), out);
}
HRESULT CandidateElement::GetPageIndex(UINT* indices, UINT capacity, UINT* count) {
  if (!count || (capacity && !indices)) return E_POINTER;
  *count = items_.empty() ? 0 : 1;
  if (capacity && *count) indices[0] = 0;
  return S_OK;
}
HRESULT CandidateElement::SetPageIndex(UINT* indices, UINT count) {
  return count == 1 && indices && indices[0] == 0 ? S_OK : E_INVALIDARG;
}
HRESULT CandidateElement::GetCurrentPage(UINT* out) {
  if (!out) return E_POINTER;
  *out = 0;
  return S_OK;
}
HRESULT CandidateElement::SetSelection(UINT index) {
  if (index >= items_.size()) return E_INVALIDARG;
  if (action_) action_(Command::Highlight, index, menu_revision_);
  return S_OK;
}
HRESULT CandidateElement::Finalize() {
  if (action_ && !items_.empty()) action_(Command::Select, selected_, menu_revision_);
  return S_OK;
}
HRESULT CandidateElement::Abort() {
  if (action_) action_(Command::Clear, 0, menu_revision_);
  return S_OK;
}

HRESULT ModeButton::QueryInterface(REFIID iid, void** out) {
  if (!out) return E_POINTER;
  *out = nullptr;
  if (iid == IID_IUnknown || iid == IID_ITfLangBarItem || iid == IID_ITfLangBarItemButton)
    *out = static_cast<ITfLangBarItemButton*>(this);
  else if (iid == IID_ITfSource) *out = static_cast<ITfSource*>(this);
  else return E_NOINTERFACE;
  AddRef();
  return S_OK;
}

void ModeButton::Update(bool ascii) {
  if (ascii_ == ascii) return;
  ascii_ = ascii;
  if (sink_) sink_->OnUpdate(TF_LBI_BTNALL);
}

HRESULT ModeButton::GetInfo(TF_LANGBARITEMINFO* out) {
  if (!out) return E_POINTER;
  *out = {};
  out->clsidService = kTip;
  out->guidItem = GUID_LBI_INPUTMODE;
  out->dwStyle = TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_SHOWNINTRAY;
  wcscpy_s(out->szDescription, kName);
  return S_OK;
}
HRESULT ModeButton::GetStatus(DWORD* out) {
  if (!out) return E_POINTER;
  *out = shown_ ? 0 : TF_LBI_STATUS_HIDDEN;
  return S_OK;
}
HRESULT ModeButton::Show(BOOL show) {
  shown_ = show != 0;
  if (sink_) sink_->OnUpdate(TF_LBI_STATUS);
  return S_OK;
}
HRESULT ModeButton::GetTooltipString(BSTR* out) { return Bstr(ascii_ ? L"英文输入" : L"中文输入", out); }
HRESULT ModeButton::OnClick(TfLBIClick click, POINT, const RECT*) {
  if (toggle_ && click == TF_LBI_CLK_LEFT) toggle_();
  return S_OK;
}
HRESULT ModeButton::GetText(BSTR* out) { return Bstr(ascii_ ? L"英" : L"中", out); }
HRESULT ModeButton::AdviseSink(REFIID iid, IUnknown* unknown, DWORD* cookie) {
  if (!unknown || !cookie) return E_POINTER;
  if (iid != IID_ITfLangBarItemSink) return CONNECT_E_CANNOTCONNECT;
  if (sink_) return CONNECT_E_ADVISELIMIT;
  const auto result = unknown->QueryInterface(IID_PPV_ARGS(&sink_));
  if (SUCCEEDED(result)) *cookie = 1;
  return result;
}
HRESULT ModeButton::UnadviseSink(DWORD cookie) {
  if (cookie != 1 || !sink_) return CONNECT_E_NOCONNECTION;
  sink_.Reset();
  return S_OK;
}

HRESULT ModeButton::GetIcon(HICON* out) {
  if (!out) return E_POINTER;
  const auto resource = MAKEINTRESOURCEW(ascii_ ? IDI_HANLIN_MODE_ENGLISH : IDI_HANLIN_MODE_CHINESE);
  *out = static_cast<HICON>(LoadImageW(module_instance, resource, IMAGE_ICON,
      GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
  return *out ? S_OK : E_FAIL;
}

}  // namespace hanlin

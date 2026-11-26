#include "TsfObjects.h"

#include <textstor.h>

#include <algorithm>
#include <cstdio>
#include <initializer_list>
#include <string_view>

using hanlin::ComPtr;

class Store final : public ITextStoreACP, public ITfContextOwnerCompositionSink {
 public:
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
    *out = nullptr;
    if (id == IID_IUnknown || id == IID_ITextStoreACP) *out = static_cast<ITextStoreACP*>(this);
    else if (id == IID_ITfContextOwnerCompositionSink) *out = static_cast<ITfContextOwnerCompositionSink*>(this);
    else return E_NOINTERFACE;
    AddRef(); return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
  ULONG STDMETHODCALLTYPE Release() override {
    auto remaining = --refs_;
    if (!remaining) delete this;
    return remaining;
  }
  HRESULT STDMETHODCALLTYPE AdviseSink(REFIID, IUnknown* sink, DWORD) override { return sink->QueryInterface(IID_PPV_ARGS(&sink_)); }
  HRESULT STDMETHODCALLTYPE UnadviseSink(IUnknown*) override { sink_.Reset(); return S_OK; }
  HRESULT STDMETHODCALLTYPE RequestLock(DWORD lock, HRESULT* result) override {
    if (reject_write_lock && (lock & TS_LF_READWRITE) == TS_LF_READWRITE) {
      *result = TS_E_SYNCHRONOUS;
      return S_OK;
    }
    if (lock_) {
      if (lock & TS_LF_SYNC) { *result = TS_E_SYNCHRONOUS; return S_OK; }
      pending_ = lock;
      *result = TS_S_ASYNC;
      return S_OK;
    }
    if (!sink_) return E_UNEXPECTED;
    lock_ = lock;
    *result = sink_->OnLockGranted(lock);
    while (pending_) {
      lock_ = pending_;
      pending_ = 0;
      sink_->OnLockGranted(lock_);
    }
    lock_ = 0;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetStatus(TS_STATUS* status) override {
    *status = {};
    if (read_only_) status->dwDynamicFlags = TS_SD_READONLY;
    return S_OK;
  }
  void SetReadOnly(bool read_only) {
    read_only_ = read_only;
    if (sink_) sink_->OnStatusChange(TS_SD_READONLY);
  }
  HRESULT STDMETHODCALLTYPE QueryInsert(LONG start, LONG end, ULONG, LONG* at, LONG* until) override {
    if (start < 0 || end < start || end > Count()) return TS_E_INVALIDPOS;
    *at = start; *until = end; return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetSelection(ULONG, ULONG count, TS_SELECTION_ACP* selection, ULONG* fetched) override {
    *fetched = count ? 1 : 0;
    if (count) *selection = selected_;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE SetSelection(ULONG count, const TS_SELECTION_ACP* selection) override {
    if (count != 1) return E_INVALIDARG;
    selected_ = *selection;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetText(LONG start, LONG end, WCHAR* output, ULONG capacity, ULONG* read,
                                    TS_RUNINFO* runs, ULONG max_runs, ULONG* written, LONG* next) override {
    if (end < 0) end = Count();
    if (start < 0 || end < start || end > Count()) return TS_E_INVALIDPOS;
    *read = (std::min)(capacity, static_cast<ULONG>(end - start));
    if (*read) text.copy(output, *read, start);
    *written = max_runs && end > start ? 1 : 0;
    if (*written) runs[0] = TS_RUNINFO{*read ? *read : static_cast<ULONG>(end - start), TS_RT_PLAIN};
    *next = capacity ? start + *read : end;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE SetText(DWORD, LONG start, LONG end, const wchar_t* input, ULONG length, TS_TEXTCHANGE* change) override {
    if (start < 0 || end < start || end > Count()) return TS_E_INVALIDPOS;
    if (input && reject_next_commit && std::wstring_view(input, length) == L"你好") {
      reject_next_commit = false;
      return E_FAIL;
    }
    text.replace(start, end - start, input ? input : L"", length);
    if (change) *change = {start, end, start + static_cast<LONG>(length)};
    selected_.acpStart = selected_.acpEnd = start + static_cast<LONG>(length);
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetFormattedText(LONG,LONG,IDataObject**) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetEmbedded(LONG,REFGUID,REFIID,IUnknown**) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE QueryInsertEmbedded(const GUID*,const FORMATETC*,BOOL* allowed) override { *allowed = FALSE; return S_OK; }
  HRESULT STDMETHODCALLTYPE InsertEmbedded(DWORD,LONG,LONG,IDataObject*,TS_TEXTCHANGE*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE InsertTextAtSelection(DWORD flags, const wchar_t* input, ULONG length,
                                                  LONG* start, LONG* end, TS_TEXTCHANGE* change) override {
    auto initial = selected_;
    auto result = S_OK;
    if (!(flags & TS_IAS_QUERYONLY)) result = SetText(0, initial.acpStart, initial.acpEnd, input, length, change);
    if (start) *start = initial.acpStart;
    if (end) *end = (flags & TS_IAS_QUERYONLY) ? initial.acpEnd : selected_.acpEnd;
    return result;
  }
  HRESULT STDMETHODCALLTYPE InsertEmbeddedAtSelection(DWORD,IDataObject*,LONG*,LONG*,TS_TEXTCHANGE*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE RequestSupportedAttrs(DWORD,ULONG,const TS_ATTRID*) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE RequestAttrsAtPosition(LONG,ULONG,const TS_ATTRID*,DWORD) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE RequestAttrsTransitioningAtPosition(LONG,ULONG,const TS_ATTRID*,DWORD) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE FindNextAttrTransition(LONG,LONG halt,ULONG,const TS_ATTRID*,DWORD,LONG* next,BOOL* found,LONG* offset) override {
    *next = halt; *found = FALSE; *offset = 0; return S_OK;
  }
  HRESULT STDMETHODCALLTYPE RetrieveRequestedAttrs(ULONG,TS_ATTRVAL*,ULONG* fetched) override { *fetched = 0; return S_OK; }
  HRESULT STDMETHODCALLTYPE GetEndACP(LONG* end) override { *end = Count(); return S_OK; }
  HRESULT STDMETHODCALLTYPE GetActiveView(TsViewCookie* cookie) override { *cookie = 1; return S_OK; }
  HRESULT STDMETHODCALLTYPE GetACPFromPoint(TsViewCookie,const POINT*,DWORD,LONG* pos) override { *pos = selected_.acpEnd; return S_OK; }
  HRESULT STDMETHODCALLTYPE GetTextExt(TsViewCookie,LONG,LONG,RECT* rect,BOOL* clipped) override { *rect = {220,140,222,158}; *clipped = FALSE; return S_OK; }
  HRESULT STDMETHODCALLTYPE GetScreenExt(TsViewCookie,RECT* rect) override { *rect = {0,0,1920,1080}; return S_OK; }
  HRESULT STDMETHODCALLTYPE GetWnd(TsViewCookie,HWND* window) override { *window = nullptr; return S_OK; }
  HRESULT STDMETHODCALLTYPE OnStartComposition(ITfCompositionView*, BOOL* allow) override { *allow = TRUE; ++starts; return S_OK; }
  HRESULT STDMETHODCALLTYPE OnUpdateComposition(ITfCompositionView*, ITfRange*) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE OnEndComposition(ITfCompositionView*) override { ++ends; return S_OK; }

  std::wstring text;
  int starts = 0;
  int ends = 0;
  bool reject_write_lock = false;
  bool reject_next_commit = false;

 private:
  LONG Count() const { return static_cast<LONG>(text.size()); }
  ULONG refs_ = 1;
  DWORD lock_ = 0;
  DWORD pending_ = 0;
  bool read_only_ = false;
  TS_SELECTION_ACP selected_{0,0,{TS_AE_NONE,FALSE}};
  ComPtr<ITextStoreACPSink> sink_;
};

namespace {

// Keep native text-store locks and ranges, with direct delivery to the test TIP.
// TSF's system key sink registration requires an installed input profile.
class ThreadManager final : public ITfThreadMgr, public ITfKeystrokeMgr {
 public:
  explicit ThreadManager(ITfThreadMgr* native) : native_(native) {}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
    if (!out) return E_POINTER;
    if (id == IID_IUnknown || id == IID_ITfThreadMgr) *out = static_cast<ITfThreadMgr*>(this);
    else if (id == IID_ITfKeystrokeMgr) *out = static_cast<ITfKeystrokeMgr*>(this);
    else return native_->QueryInterface(id, out);
    AddRef();
    return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
  ULONG STDMETHODCALLTYPE Release() override {
    const auto remaining = --refs_;
    if (!remaining) delete this;
    return remaining;
  }
  HRESULT STDMETHODCALLTYPE GetFocus(ITfDocumentMgr** focus) override { return native_->GetFocus(focus); }
  HRESULT STDMETHODCALLTYPE AdviseKeyEventSink(TfClientId, ITfKeyEventSink*, BOOL) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE UnadviseKeyEventSink(TfClientId) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE Activate(TfClientId*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE Deactivate() override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE CreateDocumentMgr(ITfDocumentMgr**) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE EnumDocumentMgrs(IEnumTfDocumentMgrs**) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE SetFocus(ITfDocumentMgr*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE AssociateFocus(HWND, ITfDocumentMgr*, ITfDocumentMgr**) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE IsThreadFocus(BOOL*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetFunctionProvider(REFCLSID, ITfFunctionProvider**) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE EnumFunctionProviders(IEnumTfFunctionProviders**) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetGlobalCompartment(ITfCompartmentMgr**) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetForeground(CLSID*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE TestKeyDown(WPARAM, LPARAM, BOOL*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE TestKeyUp(WPARAM, LPARAM, BOOL*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE KeyDown(WPARAM, LPARAM, BOOL*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE KeyUp(WPARAM, LPARAM, BOOL*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetPreservedKey(ITfContext*, const TF_PRESERVEDKEY*, GUID*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE IsPreservedKey(REFGUID, const TF_PRESERVEDKEY*, BOOL*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE PreserveKey(TfClientId, REFGUID, const TF_PRESERVEDKEY*, const WCHAR*, ULONG) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE UnpreserveKey(REFGUID, const TF_PRESERVEDKEY*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE SetPreservedKeyDescription(REFGUID, const WCHAR*, ULONG) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetPreservedKeyDescription(REFGUID, BSTR*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE SimulatePreservedKey(ITfContext*, REFGUID, BOOL*) override { return E_NOTIMPL; }

 private:
  ULONG refs_ = 1;
  ComPtr<ITfThreadMgr> native_;
};

bool Check(bool passed, const char* test) {
  std::printf("%s %s\n", passed ? "PASS" : "FAIL", test);
  return passed;
}

bool CheckResult(HRESULT result, const char* operation) {
  if (SUCCEEDED(result)) return true;
  std::printf("%s HRESULT=0x%08lx\n", operation, static_cast<unsigned long>(result));
  return false;
}

bool TestTransport() {
  using namespace hanlin;
  const auto module = GetModuleHandleW(nullptr);
  WNDCLASSW type{};
  type.hInstance = module;
  type.lpfnWndProc = DefWindowProcW;
  type.lpszClassName = kClientWindowClass;
  if (!RegisterClassW(&type)) return false;
  const auto window = CreateWindowW(kClientWindowClass, nullptr, 0, 0, 0, 0, 0,
                                    HWND_MESSAGE, nullptr, module, nullptr);
  if (!window) return false;
  Client client;
  State state;
  const auto handle = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(window));
  Request open;
  open.args = {static_cast<int32_t>(handle), static_cast<int32_t>(handle >> 32)};
  bool initial = false;
  const auto deadline = GetTickCount64() + 5000;
  do {
    initial = client.Call(open, state) && state.session;
    if (!initial) Sleep(50);
  } while (!initial && GetTickCount64() < deadline);
  const auto old = state.session;
  const bool replaced = initial && client.Call(open, state) && state.session && state.session != old;
  const auto current = state.session;
  const bool recovered = replaced && client.Call({kProtocolVersion, Command::Focus, old}, state) && !state.session;

  bool translated = recovered;
  uint32_t old_menu = 0;
  for (char key : std::string_view("nihao")) {
    if (!translated) break;
    translated = client.Call({kProtocolVersion, Command::Key, current, {key}}, state) &&
        state.session == current && state.handled;
    if (!old_menu) old_menu = state.menu_revision;
  }
  const auto visible_menu = state.menu_revision;
  const auto choices = state.candidates.size();
  const bool stale = translated && old_menu && visible_menu && visible_menu != old_menu &&
      client.Call({kProtocolVersion, Command::Select, current, {0, static_cast<int32_t>(old_menu)}}, state) &&
      state.session == current && !state.handled && state.commit.empty();
  const bool highlighted = choices < 2 ||
      (client.Call({kProtocolVersion, Command::Highlight, current, {1, static_cast<int32_t>(visible_menu)}}, state) &&
       state.handled && state.selection == 1 && state.menu_revision == visible_menu);
  const bool selected = stale && highlighted &&
      client.Call({kProtocolVersion, Command::Select, current, {0, static_cast<int32_t>(visible_menu)}}, state) &&
      state.handled && state.commit == L"你好";
  if (!selected) std::printf("menu translated=%d stale=%d highlight=%d selection=%d count=%zu\n",
                            translated, stale, highlighted, selected, choices);
  const std::array<int32_t, 6> position{200, 140, 201, 160, 1};
  const bool placement = selected &&
      client.Call({kProtocolVersion, Command::Place, current, position}, state) && state.has_caret &&
      client.Call({kProtocolVersion, Command::Place, current}, state) && state.session == current && !state.has_caret &&
      client.Call({kProtocolVersion, Command::Place, current, position}, state) && state.has_caret;
  if (replaced) client.Call({kProtocolVersion, Command::Close, current}, state);
  DestroyWindow(window);
  UnregisterClassW(kClientWindowClass, module);
  return Check(recovered, "same-window reconnect invalidates abandoned session") &
      Check(stale && selected, "stale selection is ignored and current selection still works") &
      Check(placement, "placement can clear and recover on the same session");
}

bool TestTextService(HMODULE library, bool tray, bool menu_request) {
  using namespace hanlin;
  using GetFactory = HRESULT(WINAPI*)(REFCLSID, REFIID, void**);
  const auto factory_entry = reinterpret_cast<GetFactory>(GetProcAddress(library, "DllGetClassObject"));
  ComPtr<IClassFactory> factory;
  ComPtr<ITfTextInputProcessorEx> service;
  ComPtr<ITfKeyEventSink> keys;
  if (!factory_entry || !CheckResult(factory_entry(kTip, IID_PPV_ARGS(&factory)), "TIP factory") ||
      !CheckResult(factory->CreateInstance(nullptr, IID_PPV_ARGS(&service)), "TIP instance") ||
      !CheckResult(service.As(&keys), "key interface")) return false;

  ComPtr<ITfThreadMgrEx> manager;
  TfClientId client = TF_CLIENTID_NULL;
  ComPtr<ITfDocumentMgr> document;
  ComPtr<ITfContext> context;
  ComPtr<Store> store;
  store.Attach(new Store());
  TfEditCookie initial = 0;
  if (!CheckResult(CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&manager)), "thread manager") ||
      !CheckResult(manager->ActivateEx(&client, TF_TMAE_UIELEMENTENABLEDONLY), "activate test thread") ||
      !CheckResult(manager->CreateDocumentMgr(&document), "document manager") ||
      !CheckResult(document->CreateContext(client, 0, static_cast<ITextStoreACP*>(store.Get()), &context, &initial), "context") ||
      !CheckResult(document->Push(context.Get()), "push context") ||
      !CheckResult(manager->SetFocus(document.Get()), "focus test document")) return false;

  ComPtr<ITfThreadMgr> test_manager;
  test_manager.Attach(new ThreadManager(manager.Get()));
  if (!CheckResult(service->ActivateEx(test_manager.Get(), client,
      tray && !menu_request ? 0 : TF_TMF_UIELEMENTENABLEDONLY), "activate test TIP")) return false;

  if (tray) {
    ComPtr<ITfLangBarItemMgr> bar;
    ComPtr<ITfLangBarItem> item;
    ComPtr<ITfLangBarItemButton> button;
    TF_LANGBARITEMINFO info{};
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const bool opened =
        CheckResult(manager.As(&bar), "language bar manager") &&
        CheckResult(bar->GetItem(GUID_LBI_INPUTMODE, &item), "language bar mode item") &&
        CheckResult(item->GetInfo(&info), "language bar mode identity") &&
        IsEqualCLSID(info.clsidService, kTip) &&
        (info.dwStyle & TF_LBI_STYLE_BTN_MENU) != 0 &&
        CheckResult(item.As(&button), "language bar button") &&
        (menu_request ? button->InitMenu(nullptr) :
         button->OnClick(TF_LBI_CLK_RIGHT, {work.right - 30, work.bottom}, &work)) == S_OK;
    service->Deactivate();
    document->Pop(TF_POPF_ALL);
    manager->Deactivate();
    return Check(opened, menu_request ?
        "HanLin TSF menu request reaches the desktop menu in a UI-element host" :
        "HanLin TSF mode button right-click reaches the desktop menu");
  }

  struct KeyResult { bool succeeded; bool tested; bool eaten; };
  auto send_key = [&](UINT vk, std::initializer_list<UINT> held, bool release = false,
                      LPARAM flags = 0, bool preview_only = false) {
    BYTE original[256]{};
    GetKeyboardState(original);
    BYTE keyboard[256]{};
    for (UINT modifier : held) keyboard[modifier] = 0x80;
    SetKeyboardState(keyboard);
    const auto scan = static_cast<LPARAM>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC) & 0xff);
    const auto param = (scan << 16) | 1 | flags | (release ? (3ULL << 30) : 0);
    BOOL tested = FALSE;
    BOOL eaten = FALSE;
    const auto test = release ? keys->OnTestKeyUp(context.Get(), vk, static_cast<LPARAM>(param), &tested) :
        keys->OnTestKeyDown(context.Get(), vk, static_cast<LPARAM>(param), &tested);
    const auto result = preview_only ? S_OK :
        release ? keys->OnKeyUp(context.Get(), vk, static_cast<LPARAM>(param), &eaten) :
            keys->OnKeyDown(context.Get(), vk, static_cast<LPARAM>(param), &eaten);
    SetKeyboardState(original);
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
    return KeyResult{SUCCEEDED(test) && SUCCEEDED(result), tested != FALSE, eaten != FALSE};
  };
  auto key = [&](UINT vk, bool shift = false, bool expect_eaten = true) {
    const auto result = send_key(vk, shift ? std::initializer_list<UINT>{VK_SHIFT, VK_LSHIFT} :
                                            std::initializer_list<UINT>{});
    const bool passed = result.succeeded && result.eaten == expect_eaten &&
        (!expect_eaten || result.tested);
    if (!passed) std::printf("key 0x%x tested=%d eaten=%d expected=%d\n",
                            vk, result.tested, result.eaten, expect_eaten);
    return passed;
  };
  auto keys_for = [&](std::string_view spelling) {
    for (const unsigned char vk : spelling)
      if (!key(vk)) return false;
    return true;
  };
  auto host_key = [&](UINT vk, std::initializer_list<UINT> held = {}, bool release = false,
                      LPARAM flags = 0) {
    const auto result = send_key(vk, held, release, flags);
    const bool passed = result.succeeded && !result.tested && !result.eaten;
    if (!passed) std::printf("host key 0x%x release=%d tested=%d eaten=%d\n",
                            vk, release, result.tested, result.eaten);
    return passed;
  };
  auto shift_press = [&] {
    const auto result = send_key(VK_LSHIFT, {VK_SHIFT, VK_LSHIFT}, false, 0, true);
    return result.succeeded && !result.tested;
  };

  bool passed = true;
  const bool clipboard = host_key('C', {VK_LCONTROL}) & host_key('V', {VK_RCONTROL}) &
      host_key('X', {VK_CONTROL}) & host_key(VK_INSERT, {VK_SHIFT}) &
      host_key(VK_INSERT, {VK_CONTROL});
  const bool navigation = host_key('L', {VK_CONTROL}) & host_key(VK_LEFT, {}, false, 1L << 29) &
      host_key('E', {VK_LWIN}) & host_key(VK_F2) & host_key(VK_F10, {VK_SHIFT}) &
      host_key(VK_HOME, {VK_SHIFT}) & host_key(VK_BACK, {VK_CONTROL}) &
      host_key('3', {VK_CONTROL, VK_SHIFT});
  passed &= Check(clipboard && navigation && store->text.empty(),
                  "host shortcuts pass both TSF preview and delivery");

  const bool shift_chord = shift_press() && host_key(VK_CONTROL, {VK_SHIFT}) &&
      host_key('V', {VK_SHIFT, VK_CONTROL}) && host_key(VK_CONTROL, {VK_SHIFT}, true) &&
      host_key(VK_LSHIFT, {}, true);
  passed &= Check(shift_chord && key('N') && key(VK_ESCAPE) && store->text.empty(),
                  "a shortcut chord leaves Chinese mode unchanged");
  const bool shift_to_ascii = shift_press() && send_key(VK_LSHIFT, {}, true).tested;
  const bool ascii_passthrough = shift_to_ascii && key('N', false, false) && store->text.empty();
  const bool shift_to_chinese = shift_press() && send_key(VK_LSHIFT, {}, true).tested;
  passed &= Check(ascii_passthrough && shift_to_chinese && key('N') && key(VK_ESCAPE),
                  "standalone Shift taps still toggle both input modes");

  const bool composed = keys_for("NIHAO");
  const auto preedit = store->text;
  const bool composed_clipboard = host_key('C', {VK_CONTROL}) && host_key('C', {}, true) &&
      host_key('V', {VK_CONTROL}) && host_key('V', {}, true) && host_key(VK_INSERT, {VK_SHIFT}) &&
      host_key(VK_INSERT, {}, true);
  passed &= Check(composed && composed_clipboard && store->text == preedit,
                  "clipboard shortcuts and their releases pass during composition");
  store->reject_write_lock = true;
  const bool passed_through = composed && key('1', false, false) && store->text == preedit;
  store->reject_write_lock = false;
  passed &= Check(passed_through && key('1') && store->text == L"你好", "denied edit lock leaves candidate available");

  const bool pending = keys_for("NIHAO");
  const auto before_rejected_commit = store->text;
  store->reject_next_commit = true;
  const bool rejected = pending && key('1') && !store->reject_next_commit && store->text == before_rejected_commit;
  passed &= Check(rejected && key('N') && key(VK_ESCAPE) && store->text == L"你好你好",
                  "transient write failure retries the text once");

  const bool composing_back = keys_for("NIHAO");
  const auto backspace = send_key(VK_BACK, {VK_CONTROL});
  send_key(VK_ESCAPE, {});  // Cleanup if Rime kept any syllables.
  passed &= Check(composing_back && backspace.succeeded && backspace.tested && backspace.eaten &&
                      store->text == L"你好你好",
                  "Ctrl+Backspace still edits the active IME composition");

  const bool unfinished = keys_for("NIHAO");
  store->reject_next_commit = true;
  const bool focus_changed = unfinished && key('1') && !store->reject_next_commit &&
      SUCCEEDED(keys->OnSetFocus(FALSE)) && SUCCEEDED(keys->OnSetFocus(TRUE));
  passed &= Check(focus_changed && key('N') && key(VK_ESCAPE) && store->text == L"你好你好",
                  "focus change invalidates an unsubmitted commit");

  store->SetReadOnly(true);
  passed &= Check(key('N', false, false) && store->text == L"你好你好", "readonly input passes its key to the host");
  store->SetReadOnly(false);
  passed &= Check(keys_for("KEAI") && key(VK_OEM_COMMA) && keys_for("SUNXIAOCHUAN") &&
                      key(VK_NUMPAD2) && key(VK_NUMPAD5) && key(VK_NUMPAD8) && key(VK_OEM_PERIOD) &&
                      keys_for("NIHAO1") && store->text == L"你好你好可爱，孙笑川258。你好",
                  "mixed sentence typed and submitted in TSF");
  passed &= Check(key('C') && key('C', true) && key(VK_NUMPAD2) && key(VK_ADD) &&
                      key(VK_NUMPAD3) && key('1') && store->text == L"你好你好可爱，孙笑川258。你好5",
                  "cC calculator submitted in TSF");
  passed &= Check(store->starts == store->ends, "composition balanced after commit and cancel");
  service->Deactivate();
  document->Pop(TF_POPF_ALL);
  manager->Deactivate();
  return passed;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  const std::wstring_view option = argc == 3 ? argv[2] : L"";
  const bool menu_request = option == L"--tray-init";
  const bool tray = option == L"--tray" || menu_request;
  if (argc != 2 && !tray) {
    std::fputs("Usage: HanLinFrontendRegression.exe <HanLinTip.dll> [--tray | --tray-init]\n"
               "Start HanLinHost with an isolated --user-data before running this probe.\n", stderr);
    return 2;
  }
  if (!CheckResult(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), "COM apartment")) return 2;
  const auto library = LoadLibraryExW(argv[1], nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
  const bool transport = tray || TestTransport();
  const bool text = library && TestTextService(library, tray, menu_request);
  if (library) FreeLibrary(library);
  CoUninitialize();
  return transport && text ? 0 : 1;
}

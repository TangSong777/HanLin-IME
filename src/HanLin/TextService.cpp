#include "TsfObjects.h"
#include "../include/InputHostCompatibility.h"

#include <X11/keysym.h>

#include <algorithm>
#include <bitset>
#include <functional>
#include <mutex>
#include <new>
#include <optional>

namespace hanlin {
namespace {

constexpr int kShift = 1 << 0;
constexpr int kCaps = 1 << 1;
constexpr int kControl = 1 << 2;
constexpr int kAlt = 1 << 3;
constexpr int kSuper = 1 << 26;
constexpr int kRelease = 1 << 30;
constexpr LPARAM kAltContext = 1L << 29;
constexpr LPARAM kPreviousKeyState = 1L << 30;
constexpr ULONGLONG kShiftTapTimeout = 500;
std::mutex notify_class;

enum class CallResult { Success, SessionRejected, Unavailable };

class Edit final : public SingleInterface<ITfEditSession> {
 public:
  explicit Edit(std::function<HRESULT(TfEditCookie)> action) : action_(std::move(action)) {}
  HRESULT STDMETHODCALLTYPE DoEditSession(TfEditCookie cookie) override { return action_(cookie); }

 private:
  std::function<HRESULT(TfEditCookie)> action_;
};

HRESULT RequestEdit(ITfContext* context, TfClientId client, DWORD flags,
                    std::function<HRESULT(TfEditCookie)> action) {
  if (!context) return E_INVALIDARG;
  ComPtr<ITfEditSession> session;
  session.Attach(new (std::nothrow) Edit(std::move(action)));
  if (!session) return E_OUTOFMEMORY;
  HRESULT applied = E_FAIL;
  const auto requested = context->RequestEditSession(client, session.Get(), flags, &applied);
  return SUCCEEDED(requested) ? applied : requested;
}

DWORD Advise(IUnknown* owner, REFIID iid, IUnknown* sink) {
  ComPtr<ITfSource> source;
  DWORD cookie = kNoCookie;
  if (owner && SUCCEEDED(owner->QueryInterface(IID_PPV_ARGS(&source))))
    source->AdviseSink(iid, sink, &cookie);
  return cookie;
}

void Unadvise(IUnknown* owner, DWORD& cookie) {
  if (cookie == kNoCookie) return;
  ComPtr<ITfSource> source;
  if (owner && SUCCEEDED(owner->QueryInterface(IID_PPV_ARGS(&source))))
    source->UnadviseSink(cookie);
  cookie = kNoCookie;
}

ComPtr<ITfCompartment> GetCompartment(IUnknown* owner, REFGUID id) {
  ComPtr<ITfCompartmentMgr> manager;
  ComPtr<ITfCompartment> result;
  if (owner && SUCCEEDED(owner->QueryInterface(IID_PPV_ARGS(&manager))))
    manager->GetCompartment(id, &result);
  return result;
}

bool ReadInteger(ITfCompartment* compartment, LONG& value) {
  VARIANT data{};
  if (!compartment || FAILED(compartment->GetValue(&data))) return false;
  const bool integer = data.vt == VT_I4;
  if (integer) value = data.lVal;
  VariantClear(&data);
  return integer;
}

bool ContextDisabled(ITfContext* context) {
  if (!context) return true;
  TF_STATUS status{};
  if (FAILED(context->GetStatus(&status)) || (status.dwDynamicFlags & TF_SD_READONLY)) return true;
  for (auto* id : {&GUID_COMPARTMENT_KEYBOARD_DISABLED, &GUID_COMPARTMENT_EMPTYCONTEXT}) {
    LONG value = 0;
    const auto compartment = GetCompartment(context, *id);
    if (ReadInteger(compartment.Get(), value) && value) return true;
  }
  return false;
}

bool IsShiftKey(UINT key) {
  return key == VK_SHIFT || key == VK_LSHIFT || key == VK_RSHIFT;
}

bool PassToHost(UINT key, int modifiers, bool composing) {
  switch (key) {
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
    case VK_MENU: case VK_LMENU: case VK_RMENU:
    case VK_LWIN: case VK_RWIN:
    case VK_INSERT:
      return true;
  }
  if ((modifiers & (kAlt | kSuper)) || (key >= VK_F1 && key <= VK_F24))
    return true;
  // Ctrl+Backspace is an IME editing key only during an active composition.
  if (modifiers & kControl)
    return key != VK_BACK || (modifiers & kShift) || !composing;
  if ((modifiers & kShift) && (key == VK_DELETE || key == VK_SPACE)) return true;
  switch (key) {
    case VK_BACK: case VK_TAB: case VK_RETURN: case VK_ESCAPE: case VK_SPACE:
    case VK_DELETE: case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
    case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
      return !composing;
  }
  return false;
}

int KeySymbol(UINT key, LPARAM data, BYTE* keyboard) {
  const bool extended = (data & (1 << 24)) != 0;
  const UINT scan = static_cast<UINT>((data >> 16) & 0xff);
  if (key >= VK_NUMPAD0 && key <= VK_NUMPAD9) return XK_KP_0 + key - VK_NUMPAD0;
  if (key >= VK_F1 && key <= VK_F24) return XK_F1 + key - VK_F1;
  switch (key) {
    case VK_SHIFT: return MapVirtualKeyW(scan, MAPVK_VSC_TO_VK_EX) == VK_RSHIFT ? XK_Shift_R : XK_Shift_L;
    case VK_LSHIFT: return XK_Shift_L;
    case VK_RSHIFT: return XK_Shift_R;
    case VK_CAPITAL: return XK_Caps_Lock;
    case VK_ESCAPE: return XK_Escape;
    case VK_RETURN: return extended ? XK_KP_Enter : XK_Return;
    case VK_BACK: return XK_BackSpace;
    case VK_TAB: return XK_Tab;
    case VK_SPACE: return XK_space;
    case VK_LEFT: return extended ? XK_Left : XK_KP_Left;
    case VK_UP: return extended ? XK_Up : XK_KP_Up;
    case VK_RIGHT: return extended ? XK_Right : XK_KP_Right;
    case VK_DOWN: return extended ? XK_Down : XK_KP_Down;
    case VK_PRIOR: return extended ? XK_Page_Up : XK_KP_Page_Up;
    case VK_NEXT: return extended ? XK_Page_Down : XK_KP_Page_Down;
    case VK_HOME: return extended ? XK_Home : XK_KP_Home;
    case VK_END: return extended ? XK_End : XK_KP_End;
    case VK_DELETE: return extended ? XK_Delete : XK_KP_Delete;
    case VK_CLEAR: return XK_KP_Begin;
    case VK_ADD: return XK_KP_Add;
    case VK_SUBTRACT: return XK_KP_Subtract;
    case VK_MULTIPLY: return XK_KP_Multiply;
    case VK_DIVIDE: return XK_KP_Divide;
    case VK_DECIMAL: return XK_KP_Decimal;
    case VK_SEPARATOR: return XK_KP_Separator;
  }
  wchar_t translated[4]{};
  const auto count = ToUnicodeEx(key, scan, keyboard, translated, 4, 4, GetKeyboardLayout(0));
  if (count != 1 || translated[0] < L' ' || translated[0] == 0x7f) return 0;
  if (translated[0] <= 0xff) return translated[0];
  return 0x01000000 | translated[0];
}

struct Key { int symbol = 0; int modifiers = 0; };

int KeyModifiers(const BYTE* keyboard, LPARAM data, bool release) {
  int modifiers = release ? kRelease : 0;
  if ((keyboard[VK_SHIFT] | keyboard[VK_LSHIFT] | keyboard[VK_RSHIFT]) & 0x80)
    modifiers |= kShift;
  if ((keyboard[VK_CONTROL] | keyboard[VK_LCONTROL] | keyboard[VK_RCONTROL]) & 0x80)
    modifiers |= kControl;
  if (((keyboard[VK_MENU] | keyboard[VK_LMENU] | keyboard[VK_RMENU]) & 0x80) ||
      (data & kAltContext))
    modifiers |= kAlt;
  if ((keyboard[VK_LWIN] | keyboard[VK_RWIN]) & 0x80) modifiers |= kSuper;
  if (keyboard[VK_CAPITAL] & 1) modifiers |= kCaps;
  return modifiers;
}

void MoveSelection(ITfContext* context, ITfRange* range, TfEditCookie cookie,
                   LONG offset, bool at_end) {
  ComPtr<ITfRange> caret;
  if (FAILED(range->Clone(&caret))) return;
  if (FAILED(caret->Collapse(cookie, at_end ? TF_ANCHOR_END : TF_ANCHOR_START))) return;
  if (!at_end && offset > 0) {
    LONG moved = 0;
    if (FAILED(caret->ShiftEnd(cookie, offset, &moved, nullptr))) return;
    caret->Collapse(cookie, TF_ANCHOR_END);
  }
  const TF_SELECTION selection{caret.Get(), {TF_AE_NONE, FALSE}};
  context->SetSelection(cookie, 1, &selection);
}

HRESULT Insert(ITfContext* context, TfEditCookie cookie, const std::wstring& text) {
  ComPtr<ITfInsertAtSelection> insert;
  HRESULT result = context->QueryInterface(IID_PPV_ARGS(&insert));
  if (FAILED(result)) return result;
  ComPtr<ITfRange> range;
  result = insert->InsertTextAtSelection(cookie, 0, text.c_str(), static_cast<LONG>(text.size()), &range);
  if (SUCCEEDED(result) && range) MoveSelection(context, range.Get(), cookie, 0, true);
  return result;
}

class TextService final : public ITfTextInputProcessorEx,
                          public ITfThreadMgrEventSink,
                          public ITfThreadFocusSink,
                          public ITfKeyEventSink,
                          public ITfCompositionSink,
                          public ITfDisplayAttributeProvider,
                          public ITfTextEditSink,
                          public ITfTextLayoutSink,
                          public ITfCompartmentEventSink {
 public:
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (iid == IID_IUnknown || iid == IID_ITfTextInputProcessor || iid == IID_ITfTextInputProcessorEx)
      *out = static_cast<ITfTextInputProcessorEx*>(this);
    else if (iid == IID_ITfThreadMgrEventSink) *out = static_cast<ITfThreadMgrEventSink*>(this);
    else if (iid == IID_ITfThreadFocusSink) *out = static_cast<ITfThreadFocusSink*>(this);
    else if (iid == IID_ITfKeyEventSink) *out = static_cast<ITfKeyEventSink*>(this);
    else if (iid == IID_ITfCompositionSink) *out = static_cast<ITfCompositionSink*>(this);
    else if (iid == IID_ITfDisplayAttributeProvider) *out = static_cast<ITfDisplayAttributeProvider*>(this);
    else if (iid == IID_ITfTextEditSink) *out = static_cast<ITfTextEditSink*>(this);
    else if (iid == IID_ITfTextLayoutSink) *out = static_cast<ITfTextLayoutSink*>(this);
    else if (iid == IID_ITfCompartmentEventSink) *out = static_cast<ITfCompartmentEventSink*>(this);
    else return E_NOINTERFACE;
    AddRef();
    return S_OK;
  }

  ULONG STDMETHODCALLTYPE AddRef() override { return lifetime_.Retain(); }
  ULONG STDMETHODCALLTYPE Release() override { return lifetime_.Release(this); }

  HRESULT STDMETHODCALLTYPE Activate(ITfThreadMgr* manager, TfClientId client) override {
    return ActivateEx(manager, client, 0);
  }

  HRESULT STDMETHODCALLTYPE ActivateEx(ITfThreadMgr* manager, TfClientId client, DWORD flags) override {
    if (!manager || client == TF_CLIENTID_NULL) return E_INVALIDARG;
    manager_ = manager;
    client_ = client;
    flags_ = flags;
    if (flags_ & TF_TMF_SECUREMODE) return S_OK;
    auto result = manager_.As(&keys_);
    if (SUCCEEDED(result)) result = keys_->AdviseKeyEventSink(client, this, TRUE);
    if (FAILED(result)) {
      Deactivate();
      return result;
    }
    advised_keys_ = true;
    manager_cookie_ = Advise(manager, IID_ITfThreadMgrEventSink, static_cast<ITfThreadMgrEventSink*>(this));
    focus_cookie_ = Advise(manager, IID_ITfThreadFocusSink, static_cast<ITfThreadFocusSink*>(this));
    manager_.As(&ui_manager_);
    conversion_ = GetCompartment(manager, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION);
    conversion_cookie_ = Advise(conversion_.Get(), IID_ITfCompartmentEventSink, static_cast<ITfCompartmentEventSink*>(this));
    open_ = GetCompartment(manager, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
    SetCompartment(open_.Get(), 1);
    open_cookie_ = Advise(open_.Get(), IID_ITfCompartmentEventSink, static_cast<ITfCompartmentEventSink*>(this));
    ComPtr<ITfCategoryMgr> categories;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&categories))))
      categories->RegisterGUID(kPreedit, &attribute_);

    CreateNotifyWindow();
    manager_.As(&bar_manager_);
    if (bar_manager_) {
      button_.Attach(new (std::nothrow) ModeButton(
          [this] { SetMode(!ascii_); }, [this](POINT point) { return OpenTrayMenu(point); }));
      if (button_ && FAILED(bar_manager_->AddItem(button_.Get()))) button_.Reset();
    }
    ComPtr<ITfDocumentMgr> focused;
    manager_->GetFocus(&focused);
    FocusDocument(focused.Get());
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE Deactivate() override {
    FocusDocument(nullptr);
    Send(Command::Close);
    token_ = 0;
    if (notify_) {
      std::lock_guard<std::mutex> lock(notify_class);
      DestroyWindow(notify_);
      notify_ = nullptr;
      UnregisterClassW(kClientWindowClass, module_instance);
    }
    if (button_) {
      button_->Detach();
      bar_manager_->RemoveItem(button_.Get());
      button_.Reset();
    }
    bar_manager_.Reset();
    Unadvise(conversion_.Get(), conversion_cookie_);
    Unadvise(open_.Get(), open_cookie_);
    conversion_.Reset();
    open_.Reset();
    Unadvise(manager_.Get(), manager_cookie_);
    Unadvise(manager_.Get(), focus_cookie_);
    if (advised_keys_) keys_->UnadviseKeyEventSink(client_);
    advised_keys_ = false;
    keys_.Reset();
    manager_.Reset();
    ui_manager_.Reset();
    client_ = TF_CLIENTID_NULL;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE OnInitDocumentMgr(ITfDocumentMgr*) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE OnUninitDocumentMgr(ITfDocumentMgr*) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE OnSetFocus(ITfDocumentMgr* focus, ITfDocumentMgr*) override {
    FocusDocument(focus);
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE OnPushContext(ITfContext*) override { return RefreshFocusedDocument(); }
  HRESULT STDMETHODCALLTYPE OnPopContext(ITfContext*) override { return RefreshFocusedDocument(); }
  HRESULT STDMETHODCALLTYPE OnSetThreadFocus() override { return RefreshFocusedDocument(); }
  HRESULT STDMETHODCALLTYPE OnKillThreadFocus() override { FocusDocument(nullptr); return S_OK; }
  HRESULT STDMETHODCALLTYPE OnSetFocus(BOOL focus) override {
    return focus ? RefreshFocusedDocument() : OnKillThreadFocus();
  }

  HRESULT STDMETHODCALLTYPE OnTestKeyDown(ITfContext* context, WPARAM key, LPARAM param, BOOL* eaten) override {
    return TestKey(context, key, param, false, eaten);
  }
  HRESULT STDMETHODCALLTYPE OnTestKeyUp(ITfContext* context, WPARAM key, LPARAM param, BOOL* eaten) override {
    return TestKey(context, key, param, true, eaten);
  }
  HRESULT STDMETHODCALLTYPE OnKeyDown(ITfContext* context, WPARAM key, LPARAM param, BOOL* eaten) override {
    return HandleKey(context, key, param, false, eaten);
  }
  HRESULT STDMETHODCALLTYPE OnKeyUp(ITfContext* context, WPARAM key, LPARAM param, BOOL* eaten) override {
    return HandleKey(context, key, param, true, eaten);
  }
  HRESULT STDMETHODCALLTYPE OnPreservedKey(ITfContext*, REFGUID, BOOL* eaten) override {
    if (!eaten) return E_POINTER;
    *eaten = FALSE;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE OnCompositionTerminated(TfEditCookie, ITfComposition* composition) override {
    if (composition == composition_.Get()) {
      composition_.Reset();
      Send(Command::Clear);
      ResetInput();
    }
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** out) override {
    return EnumInputAttributes(out);
  }
  HRESULT STDMETHODCALLTYPE GetDisplayAttributeInfo(REFGUID id, ITfDisplayAttributeInfo** out) override {
    return InputAttribute(id, out);
  }

  HRESULT STDMETHODCALLTYPE OnEndEdit(ITfContext* context, TfEditCookie cookie, ITfEditRecord* changes) override {
    BOOL changed = FALSE;
    if (context != context_.Get() || !changes ||
        FAILED(changes->GetSelectionStatus(&changed)) || !changed) return S_OK;
    if (!composition_) {
      pending_commit_.clear();
      return S_OK;
    }
    TF_SELECTION selected{};
    ULONG fetched = 0;
    if (FAILED(context->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selected, &fetched)) || !fetched) return S_OK;
    ComPtr<ITfRange> selection;
    selection.Attach(selected.range);
    ComPtr<ITfRange> range;
    if (FAILED(composition_->GetRange(&range))) return S_OK;
    LONG left = 0;
    LONG right = 0;
    if (SUCCEEDED(selection->CompareStart(cookie, range.Get(), TF_ANCHOR_START, &left)) &&
        SUCCEEDED(selection->CompareEnd(cookie, range.Get(), TF_ANCHOR_END, &right)) &&
        (left < 0 || right > 0)) {
      Send(Command::Clear);
      ResetInput();
    }
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE OnLayoutChange(ITfContext* context, TfLayoutCode code, ITfContextView*) override {
    if (!composition_ || context != context_.Get()) return S_OK;
    if (code == TF_LC_DESTROY) SendPlacement({});
    else if (code == TF_LC_CHANGE || code == TF_LC_CREATE)
      RequestEdit(context, client_, TF_ES_ASYNCDONTCARE | TF_ES_READ, [this, keep = ComPtr<ITfTextInputProcessorEx>(this),
                   current = ComPtr<ITfContext>(context)](TfEditCookie cookie) {
        if (current.Get() == context_.Get()) Place(current.Get(), cookie);
        return S_OK;
      });
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE OnChange(REFGUID id) override {
    if (changing_mode_) return S_OK;
    LONG value = 0;
    if (id == GUID_COMPARTMENT_KEYBOARD_OPENCLOSE && ReadInteger(open_.Get(), value) && !value)
      SetCompartment(open_.Get(), 1);
    else if (id == GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION && ReadInteger(conversion_.Get(), value))
      SetMode((value & TF_CONVERSIONMODE_NATIVE) == 0);
    return S_OK;
  }

 private:
  bool TestContext(ITfContext* context) const {
    return manager_ && !(flags_ & TF_TMF_SECUREMODE) && !ContextDisabled(context);
  }

  Key RouteKey(ITfContext* context, WPARAM word, LPARAM data, bool release, bool testing) {
    const auto vk = static_cast<UINT>(word);
    const bool shift_key = IsShiftKey(vk);
    const bool composing = composing_ && context == context_.Get();
    if (!shift_key && !release) shift_tap_.reset();
    // ASCII mode is native keyboard input. In particular, do not claim the
    // preview and then try to give the key back after an edit-session request.
    if (ascii_ && !shift_key) return {};
    if (word >= 256 || (!shift_key && release && !composing)) return {};

    BYTE keyboard[256]{};
    if (!GetKeyboardState(keyboard)) {
      shift_tap_.reset();
      return {};
    }
    const int modifiers = KeyModifiers(keyboard, data, release);
    if (shift_key) {
      const int symbol = KeySymbol(vk, data, keyboard);
      const auto opposite = symbol == XK_Shift_L ? VK_RSHIFT : VK_LSHIFT;
      const bool chord = (modifiers & (kControl | kAlt | kSuper)) ||
          (keyboard[opposite] & 0x80);
      if (!release) {
        // TSF may only deliver the preview for a key we leave to the app.
        // Record the press here and send Shift to Rime only for a real tap.
        if (chord) shift_tap_.reset();
        else if (!(data & kPreviousKeyState))
          shift_tap_ = ShiftTap{context, symbol, GetTickCount64()};
        return {};
      }
      const bool tap = !chord && shift_tap_ && shift_tap_->context == context &&
          shift_tap_->symbol == symbol && GetTickCount64() - shift_tap_->started < kShiftTapTimeout;
      if (!testing || !tap) shift_tap_.reset();
      return tap && TestContext(context) ? Key{symbol, modifiers} : Key{};
    }

    // Leave key-up with the app even if the shortcut's modifier was released first.
    const bool host_key = PassToHost(vk, modifiers, composing) ||
        (shortcut_keys_[vk] && (release || (data & kPreviousKeyState)));
    if (!release) shortcut_keys_.set(vk, host_key);
    if (host_key || !TestContext(context)) return {};
    return {KeySymbol(vk, data, keyboard), modifiers};
  }

  HRESULT TestKey(ITfContext* context, WPARAM key, LPARAM param, bool release, BOOL* eaten) {
    if (!eaten) return E_POINTER;
    *eaten = RouteKey(context, key, param, release, true).symbol != 0;
    return S_OK;
  }

  HRESULT HandleKey(ITfContext* context, WPARAM word, LPARAM data, bool release, BOOL* eaten) {
    if (!eaten) return E_POINTER;
    *eaten = FALSE;
    const auto key = RouteKey(context, word, data, release, false);
    if (!key.symbol) return S_OK;
    if (context != context_.Get()) AttachContext(context);
    if (!EnsureSession() && pending_commit_.empty()) return S_OK;
    // Only consume the key if TSF grants a synchronous write session.
    RequestEdit(context, client_, TF_ES_SYNC | TF_ES_READWRITE,
        [this, context, key, eaten, generation = generation_](TfEditCookie cookie) {
          if (context != context_.Get() || generation != generation_) return S_OK;
          const auto written = FlushCommit(context, cookie);
          if (FAILED(written)) return written;
          State state;
          const bool shift = key.symbol == XK_Shift_L || key.symbol == XK_Shift_R;
          if (shift && !SendInput(Command::Key, {key.symbol, (key.modifiers & ~kRelease) | kShift}, state, cookie))
            return S_OK;
          if (!SendInput(Command::Key, {key.symbol, key.modifiers}, state, cookie)) return S_OK;
          const bool mode_tip = shift && ascii_ != state.ascii && !state.composing;
          return Accept(context, cookie, std::move(state), eaten, mode_tip);
        });
    return S_OK;
  }

  HRESULT Accept(ITfContext* context, TfEditCookie cookie, State state, BOOL* eaten = nullptr,
                 bool mode_tip = false) {
    if (eaten) *eaten = state.handled;
    const bool committed = !state.commit.empty();
    if (committed) {
      pending_commit_ = std::move(state.commit);
      const auto written = FlushCommit(context, cookie);
      if (FAILED(written)) {
        EndElements();
        SendPlacement({});
        return written;
      }
    }

    const auto applied = ApplyComposition(context, cookie, state);
    if (FAILED(applied)) {
      if (eaten && !committed) *eaten = FALSE;
      Send(Command::Clear);
      ResetInput(cookie);
      return applied;
    }
    composing_ = state.composing;
    UpdateMode(state.ascii);
    UpdateElements(context, state);
    if ((composition_ || mode_tip) && context == context_.Get()) Place(context, cookie, mode_tip);
    return S_OK;
  }

  HRESULT FlushCommit(ITfContext* context, TfEditCookie cookie) {
    if (pending_commit_.empty()) return S_OK;
    auto text = std::move(pending_commit_);
    pending_commit_.clear();
    const auto generation = generation_;
    const auto written = composition_ ? EndComposition(context, cookie, text) : Insert(context, cookie, text);
    if (FAILED(written) && generation == generation_) pending_commit_ = std::move(text);
    return written;
  }

  HRESULT ApplyComposition(ITfContext* context, TfEditCookie cookie, const State& state) {
    if (!state.composing) return composition_ ? EndComposition(context, cookie, L"") : S_OK;
    if (!composition_) {
      const auto result = BeginComposition(context, cookie);
      if (FAILED(result)) return result;
    }
    const bool inline_text = state.inline_preedit || (flags_ & TF_TMF_UIELEMENTENABLEDONLY);
    const auto text = inline_text ? state.preedit.c_str() : L"";
    const LONG size = inline_text ? static_cast<LONG>(state.preedit.size()) : 0;
    ComPtr<ITfRange> range;
    HRESULT result = composition_->GetRange(&range);
    if (FAILED(result)) return result;
    result = range->SetText(cookie, 0, text, size);
    if (FAILED(result)) return result;
    if (inline_text && attribute_ != TF_INVALID_GUIDATOM) {
      ComPtr<ITfProperty> attr;
      if (SUCCEEDED(context->GetProperty(GUID_PROP_ATTRIBUTE, &attr))) {
        VARIANT value{};
        value.vt = VT_I4;
        value.lVal = static_cast<LONG>(attribute_);
        attr->SetValue(cookie, range.Get(), &value);
      }
    }
    MoveSelection(context, range.Get(), cookie, inline_text ? static_cast<LONG>(state.caret) : 0, false);
    return S_OK;
  }

  HRESULT BeginComposition(ITfContext* context, TfEditCookie cookie) {
    ComPtr<ITfInsertAtSelection> insert;
    ComPtr<ITfContextComposition> owner;
    ComPtr<ITfRange> range;
    HRESULT result = context->QueryInterface(IID_PPV_ARGS(&insert));
    if (SUCCEEDED(result)) result = context->QueryInterface(IID_PPV_ARGS(&owner));
    if (SUCCEEDED(result)) result = insert->InsertTextAtSelection(cookie, TF_IAS_QUERYONLY, nullptr, 0, &range);
    if (SUCCEEDED(result)) result = owner->StartComposition(cookie, range.Get(), this, &composition_);
    return SUCCEEDED(result) && !composition_ ? E_FAIL : result;
  }

  HRESULT EndComposition(ITfContext* context, TfEditCookie cookie, const std::wstring& text) {
    auto composition = composition_;
    ComPtr<ITfRange> range;
    auto written = composition->GetRange(&range);
    if (SUCCEEDED(written)) written = range->SetText(cookie, 0, text.c_str(), static_cast<LONG>(text.size()));
    if (FAILED(written)) return written;

    composition_.Reset();
    ComPtr<ITfProperty> attr;
    if (SUCCEEDED(context->GetProperty(GUID_PROP_ATTRIBUTE, &attr))) attr->Clear(cookie, range.Get());
    MoveSelection(context, range.Get(), cookie, 0, true);
    // At this point the text is already submitted, even if ending the composition fails.
    composition->EndComposition(cookie);
    return S_OK;
  }

  void CancelComposition(std::optional<TfEditCookie> cookie) {
    if (!composition_ || !context_) return;
    auto cancel = [composition = std::move(composition_)](TfEditCookie edit) {
      ComPtr<ITfRange> range;
      if (SUCCEEDED(composition->GetRange(&range))) range->SetText(edit, 0, L"", 0);
      return composition->EndComposition(edit);
    };
    if (cookie) cancel(*cookie);
    else RequestEdit(context_.Get(), client_, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, std::move(cancel));
  }

  void Place(ITfContext* context, TfEditCookie cookie, bool mode_tip = false) {
    if ((!composition_ && !mode_tip) || !show_popup_ || (flags_ & TF_TMF_UIELEMENTENABLEDONLY)) {
      SendPlacement({});
      return;
    }
    ComPtr<ITfContextView> view;
    if (FAILED(context->GetActiveView(&view)) || !view) {
      SendPlacement({});
      return;
    }
    HWND view_window = nullptr;
    view->GetWnd(&view_window);
    const auto root = view_window ? GetAncestor(view_window, GA_ROOT) : nullptr;
    if (root && IsIconic(root)) {
      SendPlacement({});
      return;
    }
    RECT view_bounds{};
    if (FAILED(view->GetScreenExt(&view_bounds)) || IsRectEmpty(&view_bounds)) {
      view_bounds = {};
      if (root) GetWindowRect(root, &view_bounds);
    }

    TF_SELECTION selected{};
    ULONG fetched = 0;
    RECT bounds{};
    if (SUCCEEDED(context->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selected, &fetched)) && fetched) {
      ComPtr<ITfRange> caret;
      caret.Attach(selected.range);
      BOOL clipped = FALSE;
      if (SUCCEEDED(view->GetTextExt(cookie, caret.Get(), &bounds, &clipped)) && !clipped &&
          input_host::RectTouchesMonitor(bounds) && !input_host::PlaceholderExtent(bounds, view_bounds)) {
        SendPlacement(bounds);
        return;
      }
    }

    const auto foreground = GetForegroundWindow();
    const bool owns_foreground = foreground && (root ? root == GetAncestor(foreground, GA_ROOT) :
        GetWindowThreadProcessId(foreground, nullptr) == GetCurrentThreadId());
    if (owns_foreground) {
      if (input_host::GetForegroundCaretRect(foreground, 20, bounds) &&
          !input_host::PlaceholderExtent(bounds, view_bounds)) {
        SendPlacement(bounds);
        return;
      }
      if (view_bounds.right - view_bounds.left > 200 && view_bounds.bottom - view_bounds.top > 100) {
        const auto anchor = input_host::FallbackAnchor(view_bounds);
        if (input_host::RectTouchesMonitor(anchor)) {
          SendPlacement(anchor);
          return;
        }
      }
    }
    SendPlacement({});
  }

  void SendPlacement(const std::optional<RECT>& bounds) {
    if (!bounds && !last_placement_) return;
    std::array<int32_t, 6> placement{};
    if (bounds) placement = {bounds->left, bounds->top, bounds->right, bounds->bottom, 1};
    if (last_placement_ && *last_placement_ == placement) return;
    State state;
    if (Call(Command::Place, placement, state) == CallResult::Success) last_placement_ = placement;
  }

  void UpdateElements(ITfContext* context, const State& state) {
    if (!ui_manager_ || state.candidates.empty()) { EndElements(); return; }
    if (!element_) {
      element_.Attach(new (std::nothrow) CandidateElement([this](Command command, int value, uint32_t revision) {
        CandidateAction(command, value, revision);
      }));
      if (!element_) return;
      element_->Update(context, state);
      BOOL popup = TRUE;
      if (SUCCEEDED(ui_manager_->BeginUIElement(element_.Get(), &popup, &element_id_))) {
        show_popup_ = popup != FALSE;
        element_->Show(popup);
      } else EndElements();
    } else {
      if (element_->Update(context, state)) ui_manager_->UpdateUIElement(element_id_);
      show_popup_ = element_->Visible();
    }
  }

  void EndElements() {
    if (element_) element_->Detach();
    if (ui_manager_ && element_id_ != kNoElement) ui_manager_->EndUIElement(element_id_);
    element_id_ = kNoElement;
    element_.Reset();
    show_popup_ = true;
  }

  void CandidateAction(Command command, int value, uint32_t revision) {
    if (!revision || !token_ || !pending_commit_.empty() || !TestContext(context_.Get())) return;
    RequestEdit(context_.Get(), client_, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE,
        [this, keep = ComPtr<ITfTextInputProcessorEx>(this), current = context_,
         generation = generation_, token = token_, command, value, revision](TfEditCookie cookie) {
          if (generation != generation_ || token != token_ || current.Get() != context_.Get()) return S_OK;
          State state;
          if (Call(command, {value, static_cast<int32_t>(revision)}, state) != CallResult::Success) {
            ResetInput(cookie);
            return S_OK;
          }
          return state.handled ? Accept(current.Get(), cookie, std::move(state)) : S_OK;
        });
  }

  void FocusDocument(ITfDocumentMgr* document) {
    ComPtr<ITfContext> context;
    if (document) document->GetTop(&context);
    AttachContext(context.Get());
  }

  HRESULT RefreshFocusedDocument() {
    ComPtr<ITfDocumentMgr> document;
    if (manager_) manager_->GetFocus(&document);
    FocusDocument(document.Get());
    return S_OK;
  }

  void AttachContext(ITfContext* context) {
    if (context == context_.Get()) return;
    ResetInput();
    Unadvise(context_.Get(), edit_cookie_);
    Unadvise(context_.Get(), layout_cookie_);
    Send(Command::Clear);
    Send(Command::Blur);
    context_ = context;
    if (TestContext(context)) {
      edit_cookie_ = Advise(context, IID_ITfTextEditSink, static_cast<ITfTextEditSink*>(this));
      layout_cookie_ = Advise(context, IID_ITfTextLayoutSink, static_cast<ITfTextLayoutSink*>(this));
      if (!EnsureSession(false)) transport_.WakeIfMissing();
      State state;
      if (Call(Command::Focus, {}, state) == CallResult::Success) UpdateMode(state.ascii);
    }
  }

  void SetMode(bool ascii) {
    State state;
    if (!SendInput(Command::SetAscii, {ascii ? 1 : 0}, state)) return;
    if (state.ascii && !ascii_) ResetInput();
    UpdateMode(state.ascii);
  }

  void ResetInput(std::optional<TfEditCookie> cookie = {}) {
    ++generation_;
    shift_tap_.reset();
    composing_ = false;
    pending_commit_.clear();
    last_placement_.reset();
    CancelComposition(cookie);
    EndElements();
  }

  void UpdateMode(bool ascii) {
    if (ascii_ == ascii && mode_initialized_) return;
    ascii_ = ascii;
    mode_initialized_ = true;
    if (button_) button_->Update(ascii);
    LONG conversion = 0;
    ReadInteger(conversion_.Get(), conversion);
    SetCompartment(conversion_.Get(), ascii ? (conversion & ~TF_CONVERSIONMODE_NATIVE) :
                                             (conversion | TF_CONVERSIONMODE_NATIVE));
    SetCompartment(open_.Get(), 1);
  }

  void SetCompartment(ITfCompartment* owner, LONG value) {
    if (!owner) return;
    VARIANT data{};
    data.vt = VT_I4;
    data.lVal = value;
    changing_mode_ = true;
    owner->SetValue(client_, &data);
    changing_mode_ = false;
  }

  bool OpenTrayMenu(POINT point) {
    const uint64_t window = reinterpret_cast<uintptr_t>(notify_);
    Request request{kProtocolVersion, Command::OpenTrayMenu, 0,
        {point.x, point.y, static_cast<int32_t>(window), static_cast<int32_t>(window >> 32)}};
    State reply;
    return window && transport_.Call(request, reply, true) && reply.handled;
  }

  bool EnsureSession(bool wake_host = true) {
    if (token_) return true;
    if (GetTickCount64() < retry_after_) return false;
    Request request;
    const uint64_t window = reinterpret_cast<uintptr_t>(notify_);
    request.args[0] = static_cast<int32_t>(window);
    request.args[1] = static_cast<int32_t>(window >> 32);
    State state;
    if (!transport_.Call(request, state, wake_host) || !state.session) {
      if (wake_host) retry_after_ = GetTickCount64() + 400;
      return false;
    }
    token_ = state.session;
    retry_after_ = 0;
    UpdateMode(state.ascii);
    return true;
  }

  CallResult Call(Command command, std::array<int32_t, 6> args, State& state) {
    if (!token_) return CallResult::Unavailable;
    Request request{kProtocolVersion, command, token_, args};
    const bool replied = transport_.Call(request, state,
        command == Command::Key || command == Command::SetAscii);
    if (replied && (state.session == token_ || command == Command::Close)) {
      if (!state.has_caret) last_placement_.reset();
      return CallResult::Success;
    }
    last_placement_.reset();
    if (command != Command::Place) token_ = 0;
    return replied && !state.session ? CallResult::SessionRejected : CallResult::Unavailable;
  }

  bool SendInput(Command command, std::array<int32_t, 6> args, State& state,
                 std::optional<TfEditCookie> cookie = {}) {
    if (!EnsureSession()) return false;
    const auto result = Call(command, args, state);
    if (result == CallResult::Success) return true;
    ResetInput(cookie);
    // An empty session reply rejects the request before input is processed.
    // A failed round trip can mean that only the reply was lost.
    return result == CallResult::SessionRejected && EnsureSession() &&
        Call(command, args, state) == CallResult::Success;
  }

  void Send(Command command, std::array<int32_t, 6> args = {}) {
    State ignored;
    Call(command, args, ignored);
  }

  static LRESULT CALLBACK Notify(HWND window, UINT message, WPARAM word, LPARAM param) {
    if (message == WM_NCCREATE) {
      const auto* create = reinterpret_cast<const CREATESTRUCTW*>(param);
      SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    } else if (message == kCandidateAction) {
      const auto service = reinterpret_cast<TextService*>(GetWindowLongPtrW(window, GWLP_USERDATA));
      const auto command = static_cast<Command>(LOWORD(param));
      if (service && (command == Command::Select || command == Command::Highlight || command == Command::Page))
        service->CandidateAction(command, HIWORD(param), static_cast<uint32_t>(word));
      return 0;
    }
    return DefWindowProcW(window, message, word, param);
  }

  void CreateNotifyWindow() {
    std::lock_guard<std::mutex> lock(notify_class);
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = Notify;
    window_class.hInstance = module_instance;
    window_class.lpszClassName = kClientWindowClass;
    RegisterClassW(&window_class);
    notify_ = CreateWindowW(kClientWindowClass, nullptr, 0, 0, 0, 0, 0,
                            HWND_MESSAGE, nullptr, module_instance, this);
    if (!notify_) UnregisterClassW(kClientWindowClass, module_instance);
  }

  struct ShiftTap {
    ITfContext* context;
    int symbol;
    ULONGLONG started;
  };
  ComLifetime lifetime_;
  Client transport_;
  std::optional<ShiftTap> shift_tap_;
  std::bitset<256> shortcut_keys_;
  std::optional<std::array<int32_t, 6>> last_placement_;
  std::wstring pending_commit_;
  uint64_t token_ = 0;
  ULONGLONG retry_after_ = 0;
  HWND notify_ = nullptr;
  TfClientId client_ = TF_CLIENTID_NULL;
  TfGuidAtom attribute_ = TF_INVALID_GUIDATOM;
  DWORD flags_ = 0;
  DWORD manager_cookie_ = kNoCookie;
  DWORD focus_cookie_ = kNoCookie;
  DWORD edit_cookie_ = kNoCookie;
  DWORD layout_cookie_ = kNoCookie;
  DWORD conversion_cookie_ = kNoCookie;
  DWORD open_cookie_ = kNoCookie;
  DWORD element_id_ = kNoElement;
  unsigned generation_ = 0;
  bool advised_keys_ = false;
  bool composing_ = false;
  bool ascii_ = false;
  bool mode_initialized_ = false;
  bool changing_mode_ = false;
  bool show_popup_ = true;
  ComPtr<ITfThreadMgr> manager_;
  ComPtr<ITfKeystrokeMgr> keys_;
  ComPtr<ITfContext> context_;
  ComPtr<ITfComposition> composition_;
  ComPtr<ITfUIElementMgr> ui_manager_;
  ComPtr<CandidateElement> element_;
  ComPtr<ITfLangBarItemMgr> bar_manager_;
  ComPtr<ModeButton> button_;
  ComPtr<ITfCompartment> conversion_;
  ComPtr<ITfCompartment> open_;
};

}  // namespace

HRESULT NewService(REFIID iid, void** out) {
  if (!out) return E_POINTER;
  *out = nullptr;
  auto* service = new (std::nothrow) TextService;
  if (!service) return E_OUTOFMEMORY;
  const auto result = service->QueryInterface(iid, out);
  service->Release();
  return result;
}

}  // namespace hanlin

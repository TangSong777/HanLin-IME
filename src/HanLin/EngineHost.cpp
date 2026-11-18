#include "TsfObjects.h"

#include <WeaselUI.h>
#include <rime_api.h>
#include <X11/keysym.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <filesystem>
#include <map>
#include <string_view>

namespace hanlin {
namespace {

constexpr wchar_t kHostWindowClass[] = L"HanLinEngineHost";
constexpr UINT kPresent = WM_APP + 1;
constexpr char kSchema[] = "rime_ice";

std::wstring Wide(std::string_view text) {
  const int size = static_cast<int>(text.size());
  if (!size) return {};
  const int units = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), size, nullptr, 0);
  std::wstring wide(units, L'\0');
  if (units) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), size, wide.data(), units);
  return wide;
}

std::wstring Wide(const char* text) { return text ? Wide(std::string_view(text)) : std::wstring(); }

std::string Utf8(const std::wstring& text) {
  const int size = static_cast<int>(text.size());
  if (!size) return {};
  const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), size, nullptr, 0, nullptr, nullptr);
  std::string utf8(bytes, '\0');
  if (bytes) WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), size, utf8.data(), bytes, nullptr, nullptr);
  return utf8;
}

std::wstring RegistryPath(const wchar_t* key, const wchar_t* value) {
  DWORD size = 0;
  if (RegGetValueW(HKEY_CURRENT_USER, key, value, RRF_RT_REG_SZ, nullptr, nullptr, &size)) return {};
  std::wstring text(size / sizeof(wchar_t), L'\0');
  if (RegGetValueW(HKEY_CURRENT_USER, key, value, RRF_RT_REG_SZ, nullptr, text.data(), &size)) return {};
  if (!text.empty() && text.back() == L'\0') text.pop_back();
  return text;
}

std::filesystem::path KnownDirectory(REFKNOWNFOLDERID folder) {
  PWSTR app_data = nullptr;
  std::filesystem::path directory;
  if (SUCCEEDED(SHGetKnownFolderPath(folder, KF_FLAG_DEFAULT, nullptr, &app_data))) directory = app_data;
  CoTaskMemFree(app_data);
  return directory;
}

std::wstring DefaultUserDataDirectory() {
  auto directory = RegistryPath(L"Software\\HanLin", L"UserDataDir");
  if (!directory.empty()) return directory;
  const auto app_data = KnownDirectory(FOLDERID_RoamingAppData);
  return app_data.empty() ? std::wstring{} : (app_data / L"HanLin").wstring();
}

bool PrepareUserData(const std::filesystem::path& shared, const std::filesystem::path& personal,
                     bool deploy) {
  namespace fs = std::filesystem;
  const auto preset = shared / L"preset";
  std::error_code error;
  const bool has_preset = fs::is_directory(preset, error);
  error.clear();
  if (deploy || has_preset) fs::create_directories(personal, error);
  if (error || !fs::is_directory(personal, error)) return false;
  if (has_preset)
    fs::copy(preset, personal, fs::copy_options::recursive | fs::copy_options::skip_existing, error);
  return !error;
}

int EnableUserProfile() {
  using InstallLayoutOrTip = BOOL(WINAPI*)(LPCWSTR, DWORD);
  const auto input = LoadLibraryExW(L"input.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (!input) return 1;
  const auto install = reinterpret_cast<InstallLayoutOrTip>(GetProcAddress(input, "InstallLayoutOrTip"));
  wchar_t tip[40]{}, profile[40]{}, language[8]{};
  StringFromGUID2(kTip, tip, static_cast<int>(std::size(tip)));
  StringFromGUID2(kProfile, profile, static_cast<int>(std::size(profile)));
  swprintf_s(language, L"0x%04x:", static_cast<unsigned int>(kLanguage));
  const auto name = std::wstring(language) + tip + profile;
  const bool enabled = install && install(name.c_str(), 0);
  FreeLibrary(input);
  return enabled ? 0 : 1;
}

int StopRunningHost() {
  const auto window = FindWindowExW(HWND_MESSAGE, nullptr, kHostWindowClass, nullptr);
  if (!window) return 0;
  DWORD process_id = 0;
  GetWindowThreadProcessId(window, &process_id);
  const auto process = OpenProcess(SYNCHRONIZE, FALSE, process_id);
  if (!process) return 1;
  PostMessageW(window, WM_CLOSE, 0, 0);
  const auto wait = WaitForSingleObject(process, 5000);
  CloseHandle(process);
  return wait == WAIT_OBJECT_0 ? 0 : 1;
}

struct Session {
  RimeSessionId rime;
  DWORD process;
  HWND notify_window;
  bool show_popup;
};

class EngineHost {
 public:
  ~EngineHost() {
    if (api_) api_->finalize();
    if (module_) FreeLibrary(module_);
  }

  bool Initialize(const std::filesystem::path& engine, const std::wstring& shared,
                  const std::wstring& personal, bool deploy) {
    module_ = LoadLibraryExW(engine.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module_) return false;
    const auto get_api = reinterpret_cast<decltype(&rime_get_api)>(GetProcAddress(module_, "rime_get_api"));
    if (!get_api) return false;
    api_ = get_api();
    if (!api_) return false;
    const auto shared_utf8 = Utf8(shared);
    const auto personal_utf8 = Utf8(personal);
    RIME_STRUCT(RimeTraits, traits);
    traits.shared_data_dir = shared_utf8.c_str();
    traits.user_data_dir = personal_utf8.c_str();
    traits.app_name = "rime.hanlin";
    traits.distribution_name = "HanLin";
    traits.distribution_code_name = "HanLin";
    traits.min_log_level = 2;
    api_->setup(&traits);
    if (deploy) api_->deployer_initialize(&traits);
    else api_->initialize(&traits);
    return true;
  }

  bool Deploy() {
    if (!api_->deploy()) return false;
    RimeConfig schema{};
    if (!api_->schema_open(kSchema, &schema)) return false;
    const char* id = api_->config_get_cstring(&schema, "schema/schema_id");
    const bool available = id && std::string_view(id) == kSchema;
    api_->config_close(&schema);
    return available && api_->deploy_config_file("weasel.yaml", "config_version");
  }

  void AttachWindow(HWND window, const std::wstring& personal) {
    window_ = window;
    ReadStyle(personal);
    ui_.InServer() = true;
    ui_.SetUICallBack([this](size_t* select, size_t* highlight, bool* backward, bool* scroll) {
      const auto session = sessions_.find(active_);
      if (session == sessions_.end() || !session->second.notify_window) return;
      const auto window = session->second.notify_window;
      if (select) PostMessageW(window, kCandidateAction, static_cast<WPARAM>(Command::Select), *select);
      else if (highlight) PostMessageW(window, kCandidateAction, static_cast<WPARAM>(Command::Highlight), *highlight);
      else if (backward || scroll) PostMessageW(window, kCandidateAction, static_cast<WPARAM>(Command::Page), backward ? *backward : *scroll);
    });
  }

  State Dispatch(const Request& request, DWORD process) {
    if (request.command == Command::Open) return Open(request, process);
    const auto it = sessions_.find(request.session);
    if (it == sessions_.end() || it->second.process != process) return {};
    auto& session = it->second;
    const auto id = session.rime;
    bool handled = true;
    switch (request.command) {
      case Command::Close:
        if (active_ == request.session) Blur();
        api_->destroy_session(id);
        sessions_.erase(it);
        return {};
      case Command::Blur:
        if (active_ == request.session) Blur();
        return Snapshot(request.session, handled);
      case Command::Focus:
        SetActive(request.session);
        if (global_ascii_) api_->set_option(id, "ascii_mode", ascii_);
        break;
      case Command::Place:
        return Place(request, session);
      case Command::Clear:
        api_->clear_composition(id);
        break;
      case Command::SetAscii:
        api_->set_option(id, "ascii_mode", request.args[0] != 0);
        if (global_ascii_) ascii_ = request.args[0] != 0;
        break;
      case Command::Key:
        SetActive(request.session);
        if (global_ascii_ && !!api_->get_option(id, "ascii_mode") != ascii_)
          api_->set_option(id, "ascii_mode", ascii_);
        handled = api_->process_key(id, request.args[0], request.args[1]) != 0;
        if (global_ascii_) {
          const bool mode = api_->get_option(id, "ascii_mode") != 0;
          const bool shift = request.args[0] == XK_Shift_L || request.args[0] == XK_Shift_R;
          if (ui_.style().ssf_enabled && !shift && mode != ascii_)
            api_->set_option(id, "ascii_mode", ascii_);
          else
            ascii_ = mode;
        }
        break;
      case Command::Select:
        if (request.args[0] < 0 || request.args[0] >= static_cast<int32_t>(kMaxCandidates)) return {};
        handled = Select(id, request.args[0]);
        break;
      case Command::Highlight:
        if (request.args[0] < 0 || request.args[0] >= static_cast<int32_t>(kMaxCandidates)) return {};
        handled = api_->highlight_candidate_on_current_page(id, request.args[0]) != 0;
        break;
      case Command::Page:
        handled = api_->process_key(id, request.args[0] ? XK_Page_Up : XK_Page_Down, 0) != 0;
        break;
      default:
        return {};
    }
    auto state = Snapshot(request.session, handled);
    if (active_ == request.session) {
      frame_ = state;
      frame_.commit.clear();
      frame_changed_ = true;
      SchedulePresent();
    }
    return state;
  }

  void PresentPending() {
    present_scheduled_ = false;
    const auto session = sessions_.find(active_);
    if (session != sessions_.end() && active_ == frame_.session)
      Present(session->second, frame_);
  }

 private:
  State Open(const Request& request, DWORD process) {
    for (auto it = sessions_.begin(); it != sessions_.end();) {
      if (!it->second.notify_window || IsWindow(it->second.notify_window)) { ++it; continue; }
      if (it->first == active_) Blur();
      api_->destroy_session(it->second.rime);
      it = sessions_.erase(it);
    }
    if (sessions_.size() >= 512) return {};
    const auto native_window = static_cast<uint64_t>(static_cast<uint32_t>(request.args[0])) |
        (static_cast<uint64_t>(static_cast<uint32_t>(request.args[1])) << 32);
    const auto notify = reinterpret_cast<HWND>(static_cast<uintptr_t>(native_window));
    if (notify) {
      wchar_t name[64]{};
      DWORD owner = 0;
      if (!GetWindowThreadProcessId(notify, &owner) || owner != process ||
          !GetClassNameW(notify, name, 64) || std::wstring_view(name) != kClientWindowClass) return {};
    }
    uint64_t token = 0;
    if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&token), sizeof(token), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0 ||
        !token || sessions_.count(token)) return {};
    const auto id = api_->create_session();
    if (!id) return {};
    char schema[128]{};
    const bool already_selected = api_->get_current_schema(id, schema, sizeof(schema) - 1) &&
        std::string_view(schema) == kSchema;
    if (!already_selected && !api_->select_schema(id, kSchema)) {
      api_->destroy_session(id);
      return {};
    }
    api_->set_option(id, "ascii_mode", global_ascii_ && ascii_);
    api_->set_option(id, "_inline_preedit", ui_.style().inline_preedit);
    sessions_.emplace(token, Session{id, process, notify, true});
    return Snapshot(token, true);
  }

  std::string Property(RimeSessionId session, const char* name) {
    char buffer[4096];
    if (!api_->get_property(session, name, buffer, sizeof(buffer) - 1)) return {};
    buffer[sizeof(buffer) - 1] = '\0';
    return buffer;
  }

  bool Select(RimeSessionId session, int index) {
    if (Property(session, "weasel_keypad_prefix").empty() &&
        Property(session, "weasel_keypad_suffix").empty())
      return api_->select_candidate_on_current_page(session, index) != 0;
    RIME_STRUCT(RimeContext, context);
    if (!api_->get_context(session, &context)) return false;
    const bool visible = index < context.menu.num_candidates;
    api_->free_context(&context);
    if (!visible) return false;
    api_->highlight_candidate_on_current_page(session, index);
    return api_->process_key(session, XK_space, 0) != 0;
  }

  State Snapshot(uint64_t token, bool handled) {
    const auto id = sessions_.at(token).rime;
    State state;
    state.session = token;
    state.handled = handled;
    state.ascii = api_->get_option(id, "ascii_mode") != 0;
    state.inline_preedit = ui_.style().inline_preedit;
    state.has_caret = active_ == token && has_caret_;
    RIME_STRUCT(RimeCommit, commit);
    if (api_->get_commit(id, &commit)) {
      state.commit = Wide(commit.text);
      api_->free_commit(&commit);
    }
    RIME_STRUCT(RimeContext, context);
    if (!api_->get_context(id, &context)) return state;
    const std::string_view preedit = context.composition.preedit ? context.composition.preedit : "";
    const auto prefix = Property(id, "weasel_keypad_prefix");
    const auto suffix = Property(id, "weasel_keypad_suffix");
    const auto prefix_cursor = Property(id, "weasel_keypad_prefix_cursor");
    state.preedit = Wide(std::string_view(prefix));
    const auto prefix_size = state.preedit.size();
    const auto body = Wide(preedit);
    state.preedit += body;
    const auto suffix_cursor = suffix.empty() || !prefix_cursor.empty()
        ? std::string() : Property(id, "weasel_keypad_cursor");
    if (!prefix_cursor.empty()) {
      state.caret = static_cast<uint32_t>(Wide(std::string_view(prefix).substr(0, std::strtoul(prefix_cursor.c_str(), nullptr, 10))).size());
    } else if (!suffix.empty() && suffix_cursor != "-1") {
      const auto position = std::strtoul(suffix_cursor.c_str(), nullptr, 10);
      state.caret = static_cast<uint32_t>(state.preedit.size() + Wide(std::string_view(suffix).substr(0, position)).size());
    } else {
      const auto position = static_cast<size_t>(context.composition.cursor_pos);
      const auto body_caret = position >= preedit.size() ? body.size() : Wide(preedit.substr(0, position)).size();
      state.caret = static_cast<uint32_t>(prefix_size + body_caret);
    }
    state.preedit += Wide(std::string_view(suffix));
    state.composing = !state.preedit.empty() || context.menu.num_candidates > 0;
    state.last_page = context.menu.is_last_page != 0;
    state.page = static_cast<uint32_t>(context.menu.page_no);
    state.selection = static_cast<uint32_t>(context.menu.highlighted_candidate_index);
    const auto count = (std::min)(static_cast<uint32_t>(context.menu.num_candidates), kMaxCandidates);
    const std::string_view keys = context.menu.select_keys ? context.menu.select_keys : "";
    state.candidates.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
      const auto& candidate = context.menu.candidates[i];
      std::wstring label;
      if (context.select_labels && context.select_labels[i]) label = Wide(context.select_labels[i]);
      else if (i < keys.size()) label = Wide(keys.substr(i, 1));
      else label = std::to_wstring(i + 1);
      state.candidates.push_back({Wide(candidate.text), Wide(candidate.comment), std::move(label)});
    }
    api_->free_context(&context);
    return state;
  }

  void Blur() {
    active_ = 0;
    has_caret_ = false;
    frame_ = {};
    ui_.Hide();
  }

  void SetActive(uint64_t token) {
    if (active_ == token) return;
    Blur();
    active_ = token;
  }

  void SchedulePresent() {
    if (!present_scheduled_)
      present_scheduled_ = PostMessageW(window_, kPresent, 0, 0) != FALSE;
  }

  State Place(const Request& request, Session& session) {
    if (active_ != request.session) return {};
    const RECT caret{request.args[0], request.args[1], request.args[2], request.args[3]};
    const bool show_popup = request.args[4] != 0;
    const bool changed = !has_caret_ || !EqualRect(&caret_, &caret) || session.show_popup != show_popup;
    caret_ = caret;
    has_caret_ = true;
    session.show_popup = show_popup;
    if (changed || (show_popup && frame_.composing && !ui_.panel_hwnd())) SchedulePresent();
    State acknowledgement;
    acknowledgement.session = request.session;
    acknowledgement.handled = true;
    acknowledgement.has_caret = true;
    return acknowledgement;
  }

  void Present(const Session& session, const State& state) {
    if (!state.composing || !session.show_popup || !has_caret_) {
      ui_.Hide();
      return;
    }
    const bool needs_window = !ui_.panel_hwnd();
    if (needs_window && !ui_.Create(nullptr)) return;
    if (frame_changed_ || needs_window || !ui_.IsShown()) {
      weasel::Context context;
      if (!state.inline_preedit) context.preedit.str = state.preedit;
      context.cinfo.currentPage = static_cast<int>(state.page);
      context.cinfo.is_last_page = state.last_page;
      context.cinfo.highlighted = static_cast<int>(state.selection);
      for (const auto& item : state.candidates) {
        context.cinfo.candies.emplace_back().str = item.text;
        context.cinfo.comments.emplace_back().str = item.comment;
        context.cinfo.labels.emplace_back().str = item.label;
      }
      weasel::Status status;
      status.composing = state.composing;
      status.ascii_mode = state.ascii;
      ui_.Update(context, status);
      frame_changed_ = false;
    }
    ui_.UpdateInputPosition(caret_);
    ui_.Show();
  }

  void ReadStyle(const std::wstring& personal) {
    auto& style = ui_.style();
    style.font_face = L"Microsoft YaHei UI";
    style.font_point = style.comment_font_point = style.label_font_point = 16;
    style.layout_type = weasel::UIStyle::LAYOUT_HORIZONTAL;
    style.label_text_format = L"%s.";
    style.margin_x = 8;
    style.margin_y = 5;
    style.candidate_spacing = 12;
    style.back_color = static_cast<int>(0xfff8f8f8u);
    style.text_color = style.candidate_text_color = style.comment_text_color = static_cast<int>(0xff303030u);
    style.label_text_color = style.candidate_text_color;
    style.hilited_candidate_back_color = static_cast<int>(0xfff1e3d7u);
    style.hilited_candidate_text_color = style.hilited_label_text_color = style.hilited_comment_text_color = style.text_color;
    RimeConfig config{};
    if (!api_->config_open("weasel", &config)) return;
    auto get = [&](const char* key) { return Wide(api_->config_get_cstring(&config, key)); };
    auto flag = [&](const char* key, bool& value) {
      Bool configured = value;
      if (api_->config_get_bool(&config, key, &configured)) value = configured != 0;
    };
    flag("global_ascii", global_ascii_);
    flag("style/inline_preedit", style.inline_preedit);
    flag("style/ssf_enabled", style.ssf_enabled);
    flag("style/ssf_status_bar", style.ssf_status_bar);
    auto horizontal = true;
    flag("style/horizontal", horizontal);
    if (!horizontal) style.layout_type = weasel::UIStyle::LAYOUT_VERTICAL;
    style.ssf_skin = get("style/ssf_skin");
    if (style.ssf_skin.empty() && style.ssf_enabled) {
      const auto program_data = KnownDirectory(FOLDERID_ProgramData);
      if (!program_data.empty()) style.ssf_skin = (program_data / L"HanLin" / L"Skin").wstring();
    } else if (!style.ssf_skin.empty() && std::filesystem::path(style.ssf_skin).is_relative()) {
      style.ssf_skin = (std::filesystem::path(personal) / style.ssf_skin).wstring();
    }
    if (style.ssf_enabled) {
      std::error_code error;
      style.ssf_enabled = !style.ssf_skin.empty() &&
          std::filesystem::is_regular_file(std::filesystem::path(style.ssf_skin) / L"skin.ini", error);
    }
    for (const auto& setting : {std::pair{"style/font_face", &style.font_face},
         {"style/label_font_face", &style.label_font_face}, {"style/comment_font_face", &style.comment_font_face},
         {"style/label_format", &style.label_text_format}}) {
      auto value = get(setting.first);
      if (!value.empty()) *setting.second = std::move(value);
    }
    for (const auto& setting : {std::pair{"style/font_point", &style.font_point},
         {"style/label_font_point", &style.label_font_point}, {"style/comment_font_point", &style.comment_font_point},
         {"style/layout/margin_x", &style.margin_x}, {"style/layout/margin_y", &style.margin_y},
         {"style/layout/candidate_spacing", &style.candidate_spacing}, {"style/layout/hilite_padding_x", &style.hilite_padding_x},
         {"style/layout/hilite_padding_y", &style.hilite_padding_y}})
      api_->config_get_int(&config, setting.first, setting.second);
    api_->config_close(&config);
  }

  HMODULE module_ = nullptr;
  RimeApi* api_ = nullptr;
  HWND window_ = nullptr;
  weasel::UI ui_;
  std::map<uint64_t, Session> sessions_;
  State frame_;
  uint64_t active_ = 0;
  RECT caret_{};
  bool has_caret_ = false;
  bool frame_changed_ = false;
  bool present_scheduled_ = false;
  bool global_ascii_ = false;
  bool ascii_ = false;
};

LRESULT CALLBACK HostWindow(HWND window, UINT message, WPARAM word, LPARAM data) {
  if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
  if (message == kPresent) {
    const auto engine = reinterpret_cast<EngineHost*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (engine) engine->PresentPending();
    return 0;
  }
  return DefWindowProcW(window, message, word, data);
}

int RunWindow(HINSTANCE image, EngineHost& engine, const std::wstring& personal) {
  WNDCLASSW window_class{};
  window_class.lpfnWndProc = HostWindow;
  window_class.hInstance = image;
  window_class.lpszClassName = kHostWindowClass;
  RegisterClassW(&window_class);
  const auto window = CreateWindowW(kHostWindowClass, kName, 0, 0, 0, 0, 0,
                                    HWND_MESSAGE, nullptr, image, nullptr);
  if (!window) return 1;
  SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&engine));
  engine.AttachWindow(window, personal);
  const auto result = RunTransport([&](const Request& request, DWORD process) {
    return engine.Dispatch(request, process);
  });
  if (IsWindow(window)) DestroyWindow(window);
  return result;
}

int RunHost(HINSTANCE image) {
  wchar_t filename[32768]{};
  const DWORD length = GetModuleFileNameW(image, filename, static_cast<DWORD>(std::size(filename)));
  if (!length || length >= std::size(filename)) return 1;
  const auto directory = std::filesystem::path(filename).parent_path();
  auto shared = (directory / L"data").wstring();
  std::wstring personal;

  int count = 0;
  const auto args = CommandLineToArgvW(GetCommandLineW(), &count);
  if (!args) return 1;
  if (count == 2 && std::wstring_view(args[1]) == L"--enable-profile") {
    LocalFree(args);
    return EnableUserProfile();
  }
  bool stop = false;
  bool deploy = false;
  bool valid = true;
  for (int i = 1; i < count; ++i) {
    const std::wstring_view key(args[i]);
    if (key == L"--stop") stop = true;
    else if (key == L"--deploy") deploy = true;
    else if (key == L"--shared-data" && i + 1 < count) shared = args[++i];
    else if (key == L"--user-data" && i + 1 < count) personal = args[++i];
    else valid = false;
  }
  LocalFree(args);
  if (!valid || (stop && deploy)) return 2;
  if (stop) return StopRunningHost();

  if (personal.empty()) personal = DefaultUserDataDirectory();
  std::error_code error;
  if (personal.empty() || !std::filesystem::is_directory(shared, error)) return 1;

  const auto name = L"Local\\HanLinEngineHost." + UserSid();
  const HANDLE mutex = CreateMutexW(nullptr, FALSE, name.c_str());
  if (!mutex) return 1;
  if (GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(mutex); return deploy ? 3 : 0; }
  const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  int result = 1;
  if (PrepareUserData(shared, personal, deploy)) {
    EngineHost engine;
    if (engine.Initialize(directory / L"rime.dll", shared, personal, deploy)) {
      if (deploy) result = engine.Deploy() ? 0 : 1;
      else result = RunWindow(image, engine, personal);
    }
  }
  if (SUCCEEDED(initialized)) CoUninitialize();
  CloseHandle(mutex);
  return result;
}

}  // namespace
}  // namespace hanlin

int WINAPI wWinMain(HINSTANCE image, HINSTANCE, PWSTR, int) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  return hanlin::RunHost(image);
}

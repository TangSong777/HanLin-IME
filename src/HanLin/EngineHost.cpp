#include "TsfObjects.h"
#include "TrayIcon.h"

#include <WeaselUI.h>
#include <rime_api.h>
#include <X11/keysym.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string_view>
#include <vector>

namespace hanlin {
namespace {

constexpr wchar_t kHostWindowClass[] = L"HanLinEngineHost";
constexpr UINT kPresent = WM_APP + 1;
constexpr UINT kPauseForSync = WM_APP + 2;
constexpr UINT_PTR kSweepSessions = 1;
constexpr UINT_PTR kCheckAutoSync = 2;
constexpr UINT kSessionSweepInterval = 60000;
constexpr UINT kAutoSyncCheckInterval = 120000;
constexpr char kSchema[] = "rime_ice";
constexpr wchar_t kDataRevision[] = L"hanlin-data.revision";
constexpr ULONGLONG kSessionIdleTime = 5 * 60 * 1000;

namespace fs = std::filesystem;

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
  const auto terminator = text.find(L'\0');
  if (terminator != std::wstring::npos) text.resize(terminator);
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
  const auto preset = shared / L"preset";
  std::error_code error;
  const bool has_preset = fs::is_directory(preset, error);
  error.clear();
  if (deploy || has_preset) fs::create_directories(personal, error);
  if (error || !fs::is_directory(personal, error)) return false;
  const auto style = personal / L"hanlin.custom.yaml";
  const auto previous_style = personal / L"weasel.custom.yaml";
  if (!fs::exists(style, error) && !error && fs::exists(previous_style, error) && !error)
    fs::copy_file(previous_style, style, fs::copy_options::skip_existing, error);
  if (error) return false;
  if (has_preset)
    fs::copy(preset, personal, fs::copy_options::recursive | fs::copy_options::skip_existing, error);
  return !error;
}

std::string ReadDataRevision(const fs::path& path) {
  char digest[65];
  std::ifstream file(path, std::ios::binary);
  file.read(digest, sizeof(digest));
  if (file.gcount() != 64 || !std::all_of(digest, digest + 64, [](char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F');
      })) return {};
  return std::string(digest, 64);
}

bool RemoveGeneratedCache(const fs::path& path) {
  const auto attributes = GetFileAttributesW(path.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) return GetLastError() == ERROR_FILE_NOT_FOUND;
  if (!(attributes & FILE_ATTRIBUTE_DIRECTORY) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
    return false;
  std::error_code error;
  fs::remove_all(path, error);
  return !error;
}

bool RecoverGeneratedCache(const fs::path& build, const fs::path& previous) {
  std::error_code error;
  if (fs::exists(build, error)) {
    RemoveGeneratedCache(previous);
    return true;
  }
  if (error) return false;
  const auto attributes = GetFileAttributesW(previous.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) return GetLastError() == ERROR_FILE_NOT_FOUND;
  if (!(attributes & FILE_ATTRIBUTE_DIRECTORY) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
    return false;
  fs::rename(previous, build, error);
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

bool InputIsIdle() {
  LASTINPUTINFO input{sizeof(input)};
  return GetLastInputInfo(&input) && GetTickCount() - input.dwTime >= kAutoSyncCheckInterval;
}

int StopRunningHost(bool only_if_idle = false) {
  auto window = FindWindowW(kHostWindowClass, nullptr);
  if (!window) window = FindWindowExW(HWND_MESSAGE, nullptr, kHostWindowClass, nullptr);
  if (!window) return 0;
  DWORD process_id = 0;
  GetWindowThreadProcessId(window, &process_id);
  const auto process = OpenProcess(SYNCHRONIZE, FALSE, process_id);
  if (!process) return 1;
  if (only_if_idle) {
    DWORD_PTR allowed = 0;
    if (!SendMessageTimeoutW(window, kPauseForSync, 0, 0, SMTO_ABORTIFHUNG, 1000, &allowed) || !allowed) {
      CloseHandle(process);
      return 4;
    }
  } else {
    PostMessageW(window, WM_CLOSE, 0, 0);
  }
  const auto wait = WaitForSingleObject(process, only_if_idle ? 15000 : 5000);
  CloseHandle(process);
  return wait == WAIT_OBJECT_0 ? 0 : 1;
}

struct Session {
  RimeSessionId rime;
  DWORD process;
  HWND notify_window;
  ULONGLONG last_activity;
  uint32_t menu_revision = 0;
};

HWND ClientWindow(int32_t low, int32_t high) {
  const auto id = static_cast<uint64_t>(static_cast<uint32_t>(low)) |
      (static_cast<uint64_t>(static_cast<uint32_t>(high)) << 32);
  return reinterpret_cast<HWND>(static_cast<uintptr_t>(id));
}

bool IsOwnedClientWindow(HWND window, DWORD process) {
  wchar_t name[64]{};
  DWORD owner = 0;
  return GetWindowThreadProcessId(window, &owner) && owner == process &&
      GetClassNameW(window, name, 64) && std::wstring_view(name) == kClientWindowClass;
}

class EngineHost {
 public:
  ~EngineHost() {
    Finalize();
    if (module_) FreeLibrary(module_);
  }

  bool Load(const fs::path& engine) {
    module_ = LoadLibraryExW(engine.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module_) return false;
    const auto get_api = reinterpret_cast<decltype(&rime_get_api)>(GetProcAddress(module_, "rime_get_api"));
    if (!get_api) return false;
    api_ = get_api();
    return api_ != nullptr;
  }

  void Initialize(const std::wstring& shared, const std::wstring& personal, bool deploy,
                  const fs::path& staging = {}) {
    personal_ = personal;
    const auto shared_utf8 = Utf8(shared);
    const auto personal_utf8 = Utf8(personal);
    const auto staging_utf8 = Utf8(staging.wstring());
    RIME_STRUCT(RimeTraits, traits);
    traits.shared_data_dir = shared_utf8.c_str();
    traits.user_data_dir = personal_utf8.c_str();
    if (!staging_utf8.empty()) traits.staging_dir = staging_utf8.c_str();
    traits.app_name = "rime.hanlin";
    traits.distribution_name = "HanLin";
    traits.distribution_code_name = "HanLin";
    traits.min_log_level = 2;
    if (!setup_) {
      api_->setup(&traits);
      setup_ = true;
    }
    if (deploy) api_->deployer_initialize(&traits);
    else api_->initialize(&traits);
    initialized_ = true;
  }

  void Finalize() {
    if (initialized_) api_->finalize();
    initialized_ = false;
  }

  bool Deploy(bool full = true) {
    if (full) {
      if (!api_->deploy()) return false;
    } else if (!RIME_API_AVAILABLE(api_, run_task) || !api_->run_task("workspace_update")) {
      return false;
    }
    RimeConfig schema{};
    if (!api_->schema_open(kSchema, &schema)) return false;
    const char* id = api_->config_get_cstring(&schema, "schema/schema_id");
    const bool available = id && std::string_view(id) == kSchema;
    api_->config_close(&schema);
    return available && api_->deploy_config_file("hanlin.yaml", "config_version");
  }

  // One-shot user dictionary sync for the standalone --sync-user-data mode.
  // librime imports every *.userdb.txt under the profile's sync directory and
  // exports the local user dictionary back, so both sides converge in one run.
  bool SyncUserData() {
    if (!RIME_API_AVAILABLE(api_, sync_user_data)) return false;
    if (api_->sync_user_data() == 0) return false;
    // The request schedules the import/export on the maintenance thread; joining
    // keeps the one-shot mode from returning before the export is written.
    if (RIME_API_AVAILABLE(api_, join_maintenance_thread))
      api_->join_maintenance_thread();
    return true;
  }

  void AttachWindow(HWND window, HINSTANCE image, const fs::path& directory,
                    const std::wstring& shared, const std::wstring& personal, bool enable_tray) {
    window_ = window;
    install_directory_ = directory;
    shared_directory_ = shared;
    show_tray_ = enable_tray;
    SetTimer(window, kCheckAutoSync, kAutoSyncCheckInterval, nullptr);
    ReadStyle();
    ui_.InServer() = true;
    ui_.SetUICallBack([this](size_t* select, size_t* highlight, bool* backward, bool* scroll) {
      if (!rendered_menu_revision_) return;
      const auto session = sessions_.find(active_);
      if (session == sessions_.end() || !session->second.notify_window) return;
      const auto window = session->second.notify_window;
      const auto post = [&](Command command, size_t value) {
        PostMessageW(window, kCandidateAction, rendered_menu_revision_,
                     MAKELPARAM(static_cast<WORD>(command), static_cast<WORD>(value)));
      };
      if (select) post(Command::Select, *select);
      else if (highlight) post(Command::Highlight, *highlight);
      else if (backward || scroll) post(Command::Page, backward ? *backward : *scroll);
    });
    tray_ = std::make_unique<TrayIcon>(window, image, directory, shared, personal, enable_tray);
  }

  bool HandleTrayMessage(UINT message, WPARAM word, LPARAM data) {
    return tray_ && tray_->Handle(message, word, data);
  }

  void CloseTray() { tray_.reset(); }

  bool PauseForSync() {
    if (!InputIsIdle() || frame_.composing) return false;
    return PostMessageW(window_, WM_CLOSE, 0, 0) != FALSE;
  }

  void CheckAutoSync() {
    if (!InputIsIdle() || frame_.composing) return;
    std::error_code error;
    if (!fs::is_regular_file(personal_ / L".hanlin-sync.json", error) ||
        fs::exists(personal_ / L".hanlin-auto-sync-off", error) || error)
      return;
    SYSTEMTIME today{};
    GetLocalTime(&today);
    char day[12]{};
    sprintf_s(day, "%04u-%02u-%02u", static_cast<unsigned>(today.wYear),
              static_cast<unsigned>(today.wMonth), static_cast<unsigned>(today.wDay));
    std::ifstream stamp(personal_ / L".hanlin-auto-sync-day");
    std::string last;
    if (std::getline(stamp, last) && last == day) return;
    stamp.close();
    const auto attempted = fs::last_write_time(personal_ / L".hanlin-auto-sync-attempt", error);
    if (!error && fs::file_time_type::clock::now() - attempted < std::chrono::minutes(10)) return;
    const auto script = install_directory_ / L"Sync-HanLin.ps1";
    if (!fs::is_regular_file(script, error)) return;
    wchar_t programs[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILES, nullptr, SHGFP_TYPE_CURRENT, programs)))
      return;
    auto pwsh = fs::path(programs) / L"PowerShell" / L"7" / L"pwsh.exe";
    if (!fs::is_regular_file(pwsh, error)) {
      wchar_t local[MAX_PATH]{};
      if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, local)))
        return;
      pwsh = fs::path(local) / L"Microsoft" / L"WindowsApps" / L"pwsh.exe";
      if (GetFileAttributesW(pwsh.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    }
    const auto quote = [](const std::wstring& arg) { return L"\"" + arg + L"\""; };
    auto arguments = quote(pwsh.wstring()) + L" -NoProfile -NonInteractive -File " +
        quote(script.wstring()) + L" sync -Automatic -UserData " + quote(personal_.wstring()) +
        L" -HostPath " + quote((install_directory_ / L"HanLinHost.exe").wstring()) +
        L" -SharedData " + quote(shared_directory_);
    if (show_tray_) arguments += L" -ResumeTray";
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (CreateProcessW(pwsh.c_str(), arguments.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                       nullptr, install_directory_.c_str(), &startup, &process)) {
      CloseHandle(process.hThread);
      CloseHandle(process.hProcess);
    }
  }

  State& Dispatch(const Request& request, DWORD process, State& reply) {
    if (request.command == Command::Health) {
      reply.handled = true;
      return reply;
    }
    if (request.command == Command::OpenTrayMenu) {
      const auto window = ClientWindow(request.args[2], request.args[3]);
      reply.handled = tray_ && IsOwnedClientWindow(window, process) &&
          tray_->RequestMenu({request.args[0], request.args[1]});
      return reply;
    }
    if (request.command == Command::Open) return reply = Open(request, process);
    const auto it = sessions_.find(request.session);
    if (it == sessions_.end() || it->second.process != process) return reply;
    auto& session = it->second;
    session.last_activity = GetTickCount64();
    const bool menu_action = request.command == Command::Select || request.command == Command::Highlight ||
        request.command == Command::Page || (request.command == Command::Clear && request.args[1]);
    if (menu_action && (active_ != request.session || !session.menu_revision ||
                        session.menu_revision != static_cast<uint32_t>(request.args[1]))) {
      reply.session = request.session;
      reply.has_caret = active_ == request.session && has_caret_;
      return reply;
    }
    const auto id = session.rime;
    bool handled = true;
    bool show_mode_tip = false;
    switch (request.command) {
      case Command::Close:
        RemoveSession(it);
        return reply;
      case Command::Blur:
        if (active_ == request.session) Blur();
        session.menu_revision = 0;
        return reply = Snapshot(request.session, handled);
      case Command::Focus:
        SetActive(request.session);
        if (global_ascii_) api_->set_option(id, "ascii_mode", ascii_);
        break;
      case Command::Place:
        return reply = Place(request);
      case Command::Clear:
        api_->clear_composition(id);
        break;
      case Command::SetAscii: {
        const bool next = request.args[0] != 0;
        if (next && !api_->get_option(id, "ascii_mode")) api_->clear_composition(id);
        api_->set_option(id, "ascii_mode", next);
        if (global_ascii_) ascii_ = next;
        break;
      }
      case Command::Key: {
        SetActive(request.session);
        if (global_ascii_ && !!api_->get_option(id, "ascii_mode") != ascii_)
          api_->set_option(id, "ascii_mode", ascii_);
        const bool shift = request.args[0] == XK_Shift_L || request.args[0] == XK_Shift_R;
        const bool before = api_->get_option(id, "ascii_mode") != 0;
        if (before && !shift) {
          handled = false;
          break;
        }
        handled = api_->process_key(id, request.args[0], request.args[1]) != 0;
        if (shift || global_ascii_) {
          const bool mode = api_->get_option(id, "ascii_mode") != 0;
          show_mode_tip = shift && mode != before;
          if (global_ascii_) {
            if (ui_.style().ssf_enabled && !shift && mode != ascii_)
              api_->set_option(id, "ascii_mode", ascii_);
            else
              ascii_ = mode;
          }
        }
        break;
      }
      case Command::Select:
        if (request.args[0] < 0 || request.args[0] >= static_cast<int32_t>(kMaxCandidates)) return reply;
        handled = Select(id, request.args[0]);
        break;
      case Command::Highlight:
        if (request.args[0] < 0 || request.args[0] >= static_cast<int32_t>(kMaxCandidates)) return reply;
        handled = api_->highlight_candidate_on_current_page(id, request.args[0]) != 0;
        break;
      case Command::Page:
        handled = api_->process_key(id, request.args[0] ? XK_Page_Up : XK_Page_Down, 0) != 0;
        break;
      default:
        return reply;
    }
    auto& state = active_ == request.session ? frame_ : reply;
    state = Snapshot(request.session, handled);
    if (state.candidates.empty()) {
      session.menu_revision = 0;
    } else if (!session.menu_revision || request.command != Command::Highlight) {
      if (!++next_menu_revision_) ++next_menu_revision_;
      session.menu_revision = next_menu_revision_;
    }
    state.menu_revision = session.menu_revision;
    if (active_ == request.session) {
      mode_tip_pending_ = show_mode_tip && !state.composing;
      if (mode_tip_pending_) has_caret_ = state.has_caret = false;
      frame_changed_ = true;
      SchedulePresent();
    }
    return state;
  }

  void PresentPending() {
    present_scheduled_ = false;
    if (active_ && active_ == frame_.session) {
      if (tray_) tray_->UpdateMode(frame_.ascii);
      Present(frame_);
    }
  }

  void SweepSessions(HWND notify = nullptr, DWORD process = 0) {
    const auto now = GetTickCount64();
    for (auto it = sessions_.begin(); it != sessions_.end();) {
      const auto& session = it->second;
      DWORD owner = 0;
      if (session.notify_window) GetWindowThreadProcessId(session.notify_window, &owner);
      const bool abandoned = session.notify_window && owner != session.process;
      const bool replaced = notify && session.notify_window == notify && session.process == process;
      const bool idle = it->first != active_ && now - session.last_activity >= kSessionIdleTime;
      if (abandoned || replaced || idle) it = RemoveSession(it);
      else ++it;
    }
  }

 private:
  using Sessions = std::map<uint64_t, Session>;

  Sessions::iterator RemoveSession(Sessions::iterator it) {
    if (it->first == active_) Blur();
    api_->destroy_session(it->second.rime);
    const auto next = sessions_.erase(it);
    if (sessions_.empty()) KillTimer(window_, kSweepSessions);
    return next;
  }

  State Open(const Request& request, DWORD process) {
    const auto notify = ClientWindow(request.args[0], request.args[1]);
    if (notify && !IsOwnedClientWindow(notify, process)) return {};
    SweepSessions(notify, process);
    if (sessions_.size() >= 512) return {};
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
    if (sessions_.empty()) SetTimer(window_, kSweepSessions, kSessionSweepInterval, nullptr);
    sessions_.emplace(token, Session{id, process, notify, GetTickCount64()});
    if (!next_menu_revision_) next_menu_revision_ = static_cast<uint32_t>(token);
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
    mode_tip_pending_ = false;
    rendered_menu_revision_ = 0;
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

  State Place(const Request& request) {
    if (active_ != request.session) return {};
    const RECT caret{request.args[0], request.args[1], request.args[2], request.args[3]};
    const bool show_popup = request.args[4] != 0 && caret.bottom > caret.top;
    const bool changed = has_caret_ != show_popup || !EqualRect(&caret_, &caret);
    caret_ = caret;
    has_caret_ = show_popup;
    if (!show_popup) rendered_menu_revision_ = 0;
    if (changed || (show_popup && (frame_.composing || mode_tip_pending_) && !ui_.panel_hwnd()))
      SchedulePresent();
    State acknowledgement;
    acknowledgement.session = request.session;
    acknowledgement.handled = true;
    acknowledgement.has_caret = has_caret_;
    return acknowledgement;
  }

  void Present(const State& state) {
    if (!has_caret_ || (!state.composing && !mode_tip_pending_)) {
      rendered_menu_revision_ = 0;
      ui_.Hide();
      return;
    }
    const bool needs_window = !ui_.panel_hwnd();
    if (needs_window && !ui_.Create(nullptr)) return;
    if (mode_tip_pending_) {
      weasel::Status status;
      status.ascii_mode = state.ascii;
      status.show_mode_tip = true;
      ui_.Update({}, status);
      ui_.UpdateInputPosition(caret_);
      ui_.ShowWithTimeout(3000);
      mode_tip_pending_ = false;
      rendered_menu_revision_ = 0;
      return;
    }
    if (frame_changed_ || needs_window || !ui_.IsShown()) {
      weasel::Context context;
      if (!state.inline_preedit && !state.preedit.empty()) {
        context.preedit.str = state.preedit;
        weasel::TextAttribute cursor;
        cursor.range.cursor = static_cast<int>((std::min)(static_cast<size_t>(state.caret), state.preedit.size()));
        context.preedit.attributes.push_back(cursor);
      }
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
    rendered_menu_revision_ = state.menu_revision;
  }

  void ReadStyle() {
    auto& style = ui_.style();
    const auto color = [](BYTE r, BYTE g, BYTE b) {
      return static_cast<int>(0xff000000u | RGB(r, g, b));
    };
    style.font_face = style.label_font_face = style.comment_font_face = L"Arial,SimSun";
    // The native renderer takes points. At 96 DPI, 12 pt draws at 16 px.
    style.font_point = style.comment_font_point = style.label_font_point = 12;
    style.layout_type = weasel::UIStyle::LAYOUT_HORIZONTAL;
    style.label_text_format = L"%s.";
    style.margin_x = 10;
    style.margin_y = 8;
    style.spacing = 7;
    style.candidate_spacing = 16;
    style.hilite_spacing = 4;
    style.hilite_padding_x = 5;
    style.hilite_padding_y = 3;
    style.border = 1;
    style.round_corner = 5;
    style.round_corner_ex = 8;
    style.back_color = color(255, 252, 249);
    style.border_color = color(226, 223, 221);
    style.text_color = style.candidate_text_color = color(60, 60, 60);
    style.label_text_color = style.comment_text_color = color(115, 100, 96);
    style.hilited_back_color = style.hilited_candidate_back_color = color(252, 238, 235);
    style.hilited_text_color = style.hilited_candidate_text_color = color(147, 68, 77);
    style.hilited_label_text_color = style.hilited_comment_text_color = style.hilited_text_color;
    style.hover_type = weasel::UIStyle::HoverType::SEMI_HILITE;
    style.prevpage_color = style.nextpage_color = style.label_text_color;

    auto skin = personal_ / L"Skin";
    std::error_code error;
    if (!fs::is_regular_file(skin / L"skin.ini", error)) {
      const auto program_data = KnownDirectory(FOLDERID_ProgramData);
      skin = program_data.empty() ? fs::path{} : program_data / L"HanLin" / L"Skin";
    }
    if (!skin.empty() && fs::is_regular_file(skin / L"skin.ini", error)) {
      style.ssf_enabled = true;
      style.ssf_skin = skin.wstring();
    }

    RimeConfig config{};
    if (!api_->config_open("hanlin", &config) && !api_->config_open("weasel", &config)) return;
    Bool global_ascii = global_ascii_;
    if (api_->config_get_bool(&config, "global_ascii", &global_ascii))
      global_ascii_ = global_ascii != 0;
    api_->config_close(&config);
  }

  fs::path personal_;
  fs::path install_directory_;
  std::wstring shared_directory_;
  bool show_tray_ = false;
  HMODULE module_ = nullptr;
  RimeApi* api_ = nullptr;
  bool setup_ = false;
  bool initialized_ = false;
  HWND window_ = nullptr;
  std::unique_ptr<TrayIcon> tray_;
  weasel::UI ui_;
  Sessions sessions_;
  State frame_;
  uint64_t active_ = 0;
  uint32_t next_menu_revision_ = 0;
  uint32_t rendered_menu_revision_ = 0;
  RECT caret_{};
  bool has_caret_ = false;
  bool mode_tip_pending_ = false;
  bool frame_changed_ = false;
  bool present_scheduled_ = false;
  bool global_ascii_ = false;
  bool ascii_ = false;
};

LRESULT CALLBACK HostWindow(HWND window, UINT message, WPARAM word, LPARAM data) {
  const auto engine = reinterpret_cast<EngineHost*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_DESTROY) {
    if (engine) engine->CloseTray();
    PostQuitMessage(0);
    return 0;
  }
  if (engine && engine->HandleTrayMessage(message, word, data)) return 0;
  if (message == kPauseForSync) return engine && engine->PauseForSync();
  if (message == WM_TIMER && word == kCheckAutoSync) {
    if (engine) engine->CheckAutoSync();
    return 0;
  }
  if (message == kPresent || (message == WM_TIMER && word == kSweepSessions)) {
    if (engine) {
      if (message == kPresent) engine->PresentPending();
      else engine->SweepSessions();
    }
    return 0;
  }
  return DefWindowProcW(window, message, word, data);
}

int RunWindow(HINSTANCE image, EngineHost& engine, const fs::path& directory,
              const std::wstring& shared, const std::wstring& personal, bool enable_tray) {
  WNDCLASSW window_class{};
  window_class.lpfnWndProc = HostWindow;
  window_class.hInstance = image;
  window_class.lpszClassName = kHostWindowClass;
  RegisterClassW(&window_class);
  const auto window = CreateWindowExW(WS_EX_TOOLWINDOW, kHostWindowClass, kName, WS_POPUP,
                                     0, 0, 0, 0, nullptr, nullptr, image, nullptr);
  if (!window) return 1;
  SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&engine));
  engine.AttachWindow(window, image, directory, shared, personal, enable_tray);
  const auto result = RunTransport([&](const Request& request, DWORD process, State& reply) -> State& {
    return engine.Dispatch(request, process, reply);
  });
  if (IsWindow(window)) DestroyWindow(window);
  return result;
}

bool RefreshGeneratedCache(EngineHost& engine, const std::wstring& shared,
                           const std::wstring& personal, const std::string& revision,
                           const fs::path& build, const fs::path& previous, bool deploy) {
  const fs::path staging = fs::path(personal) / L".hanlin-build-staging";
  std::error_code error;
  if (!RemoveGeneratedCache(previous) || !RemoveGeneratedCache(staging) ||
      !fs::create_directory(staging, error)) return false;

  engine.Initialize(shared, personal, true, staging);
  const bool compiled = engine.Deploy(deploy);
  engine.Finalize();
  if (!compiled) {
    RemoveGeneratedCache(staging);
    return false;
  }

  std::ofstream stamp(staging / kDataRevision, std::ios::binary);
  stamp << revision;
  stamp.close();
  if (!stamp) return false;

  // Keep the previous cache until the new directory has taken its place.
  const auto attributes = GetFileAttributesW(build.c_str());
  if (attributes != INVALID_FILE_ATTRIBUTES) {
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
      return false;
    fs::rename(build, previous, error);
    if (error) return false;
  } else if (GetLastError() != ERROR_FILE_NOT_FOUND) {
    return false;
  }
  fs::rename(staging, build, error);
  if (error) return false;
  RemoveGeneratedCache(previous);
  return true;
}

int RunEngine(HINSTANCE image, const fs::path& directory, const std::wstring& shared,
              const std::wstring& personal, bool deploy, bool enable_tray) {
  if (!PrepareUserData(shared, personal, deploy)) return 1;
  const fs::path build = fs::path(personal) / L"build";
  const fs::path previous = fs::path(personal) / L".hanlin-build-previous";
  if (!RecoverGeneratedCache(build, previous)) return 1;

  EngineHost engine;
  if (!engine.Load(directory / L"rime.dll")) return 1;
  const auto revision = ReadDataRevision(fs::path(shared) / kDataRevision);
  std::error_code error;
  const bool refresh = !revision.empty() &&
      (!fs::is_directory(build, error) ||
       ReadDataRevision(build / kDataRevision) != revision);
  if (deploy || refresh) {
    const bool updated = RefreshGeneratedCache(engine, shared, personal, revision, build, previous, deploy);
    if (!updated) {
      OutputDebugStringW(L"HanLin could not refresh the compiled Rime cache.\n");
      if (!RecoverGeneratedCache(build, previous)) return 1;
    }
    if (deploy) return updated ? 0 : 1;
  }

  engine.Initialize(shared, personal, false);
  return RunWindow(image, engine, directory, shared, personal, enable_tray);
}

std::string ReadInstallationId(const fs::path& personal) {
  std::ifstream file(personal / L"installation.yaml", std::ios::binary);
  if (!file) return {};
  constexpr std::string_view key = "installation_id:";
  std::string line;
  while (std::getline(file, line)) {
    if (line.rfind(key, 0) != 0) continue;
    std::string value = line.substr(key.size());
    const auto first = value.find_first_not_of(" \t\r\"'");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\"'");
    return value.substr(first, last - first + 1);
  }
  return {};
}

std::string JsonEscape(std::string_view text) {
  std::string escaped;
  escaped.reserve(text.size() + 8);
  for (const char ch : text) {
    switch (ch) {
      case '\\': escaped += "\\\\"; break;
      case '"': escaped += "\\\""; break;
      case '\n': escaped += "\\n"; break;
      case '\r': escaped += "\\r"; break;
      case '\t': escaped += "\\t"; break;
      default:
        if (static_cast<unsigned char>(ch) >= 0x20) escaped += ch;
    }
  }
  return escaped;
}

bool IsUserDbExport(const std::wstring& name) {
  constexpr std::wstring_view suffix = L".userdb.txt";
  return name.size() >= suffix.size() &&
         name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
}

struct SyncFileInfo {
  std::wstring name;
  uintmax_t bytes = 0;
};

bool CollectSyncFiles(const fs::path& personal, const std::string& id,
                      std::vector<SyncFileInfo>& files, fs::path& root) {
  std::error_code error;
  root = personal / L"sync" / fs::path(Wide(id));
  if (id.empty() || !fs::is_directory(root, error)) {
    // A profile without a readable installation id still reports what librime
    // exported, using the newest directory under sync/.
    root.clear();
    const fs::path parent = personal / L"sync";
    if (!fs::is_directory(parent, error)) return false;
    std::filesystem::file_time_type newest{};
    for (const auto& entry : fs::directory_iterator(parent, error)) {
      if (error) break;
      std::error_code kind;
      if (!entry.is_directory(kind)) continue;
      std::error_code stamp_error;
      const auto stamp = entry.last_write_time(stamp_error);
      if (stamp_error) continue;
      if (root.empty() || stamp > newest) {
        root = entry.path();
        newest = stamp;
      }
    }
    if (root.empty()) return false;
  }
  for (const auto& entry : fs::directory_iterator(root, error)) {
    if (error) break;
    std::error_code kind;
    if (!entry.is_regular_file(kind)) continue;
    const auto name = entry.path().filename().wstring();
    if (!IsUserDbExport(name)) continue;
    files.push_back({name, entry.file_size(kind)});
  }
  return !files.empty();
}

// Standalone user data sync: the profile is supplied explicitly, the desktop host
// mutex is never taken, no window or tray is created and the TSF service is untouched.
// The outcome is written to <profile>\sync-report.json because a windows-subsystem
// binary has no stdout for the packaging scripts to read.
int RunSyncUserData(const fs::path& directory, const std::wstring& shared,
                    const fs::path& personal) {
  const auto engine_guard = CreateFileW((personal / L".hanlin-librime.lock").c_str(),
      GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_HIDDEN, nullptr);
  if (engine_guard == INVALID_HANDLE_VALUE) return 3;
  const fs::path report_path = personal / L"sync-report.json";
  std::ofstream report(report_path, std::ios::binary | std::ios::trunc);
  if (!report) { CloseHandle(engine_guard); return 1; }

  EngineHost engine;
  std::string error;
  bool synced = false;
  std::string installation_id;
  std::vector<SyncFileInfo> files;
  fs::path sync_root;
  if (!engine.Load(directory / L"rime.dll")) {
    error = "could not load rime.dll";
  } else {
    // The deployment module group (core, dict, levers) registers the sync tasks;
    // the regular service initialization only loads core, dict and gears.
    engine.Initialize(shared, personal.wstring(), true);
    installation_id = ReadInstallationId(personal);
    synced = engine.SyncUserData();
    const bool exported =
        synced && CollectSyncFiles(personal, installation_id, files, sync_root);
    engine.Finalize();
    if (!synced)
      error = "librime rejected the user data sync";
    else if (!exported)
      error = "librime exported no *.userdb.txt";
    synced = exported;
  }

  uintmax_t total = 0;
  for (const auto& file : files) total += file.bytes;

  report << "{\n";
  report << "  \"ok\": " << (synced ? "true" : "false") << ",\n";
  report << "  \"profile\": \"" << JsonEscape(Utf8(personal.wstring())) << "\",\n";
  report << "  \"sharedData\": \"" << JsonEscape(Utf8(shared)) << "\",\n";
  report << "  \"installationId\": \"" << JsonEscape(installation_id) << "\",\n";
  report << "  \"syncDir\": \"" << JsonEscape(Utf8(sync_root.wstring())) << "\",\n";
  report << "  \"totalBytes\": " << total << ",\n";
  report << "  \"files\": [";
  for (size_t index = 0; index < files.size(); ++index) {
    report << (index ? ",\n" : "\n") << "    {\"name\": \""
           << JsonEscape(Utf8(files[index].name)) << "\", \"bytes\": "
           << files[index].bytes << "}";
  }
  report << (files.empty() ? "]" : "\n  ]") << ",\n";
  report << "  \"error\": \"" << JsonEscape(error) << "\"\n";
  report << "}\n";
  report.close();
  CloseHandle(engine_guard);
  return synced ? 0 : 1;
}

int RunHost(HINSTANCE image) {
  wchar_t filename[32768]{};
  const DWORD length = GetModuleFileNameW(image, filename, static_cast<DWORD>(std::size(filename)));
  if (!length || length >= std::size(filename)) return 1;
  const auto directory = std::filesystem::path(filename).parent_path();
  const auto runtime_data = directory / L"RuntimeData";
  std::error_code runtime_error;
  auto shared = (fs::is_regular_file(runtime_data / L"default.yaml", runtime_error)
      ? runtime_data : directory / L"data").wstring();
  std::wstring personal;
  std::wstring sync_user;

  int count = 0;
  const auto args = CommandLineToArgvW(GetCommandLineW(), &count);
  if (!args) return 1;
  if (count == 2 && std::wstring_view(args[1]) == L"--enable-profile") {
    LocalFree(args);
    return EnableUserProfile();
  }
  if (count == 2 && std::wstring_view(args[1]) == L"--health") {
    LocalFree(args);
    State state;
    return Client().Call({kProtocolVersion, Command::Health, 0, {}}, state) && state.handled ? 0 : 1;
  }
  if (count == 3 && std::wstring_view(args[1]) == L"--compile-package-data") {
    const fs::path profile(args[2]);
    LocalFree(args);
    // Package compilation has its own empty profile and never enters the desktop host or its mutex.
    std::error_code error;
    if (!profile.is_absolute() || !fs::is_directory(shared, error) ||
        !fs::create_directory(profile, error))
      return 2;
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const auto result = RunEngine(image, directory, shared, profile.wstring(), true, false);
    if (SUCCEEDED(initialized)) CoUninitialize();
    return result;
  }
  bool stop = false, idle_stop = false;
  bool deploy = false;
  bool desktop_update = false;
  bool enable_tray = false;
  bool valid = true;
  for (int i = 1; i < count; ++i) {
    const std::wstring_view key(args[i]);
    if (key == L"--stop") stop = true;
    else if (key == L"--stop-idle") idle_stop = true;
    else if (key == L"--deploy") deploy = true;
    else if (key == L"--activate-desktop-update") desktop_update = true;
    else if (key == L"--no-tray") enable_tray = false;
    else if (key == L"--show-tray") enable_tray = true;
    else if (key == L"--shared-data" && i + 1 < count) shared = args[++i];
    else if (key == L"--user-data" && i + 1 < count) personal = args[++i];
    else if (key == L"--sync-user-data" && i + 1 < count) sync_user = args[++i];
    else valid = false;
  }
  LocalFree(args);
  if (!valid || (stop && deploy) || (desktop_update && (stop || deploy))) return 2;
  if (stop) return StopRunningHost();
  if (idle_stop) return StopRunningHost(true);
  if (!sync_user.empty()) {
    // One-shot user dictionary sync: explicit profile, no desktop host involvement.
    const fs::path profile(sync_user);
    std::error_code error;
    if (!profile.is_absolute() || !fs::is_directory(profile, error) ||
        !fs::is_directory(shared, error))
      return 2;
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const auto result = RunSyncUserData(directory, shared, profile);
    if (SUCCEEDED(initialized)) CoUninitialize();
    return result;
  }

  if (personal.empty()) personal = DefaultUserDataDirectory();
  std::error_code error;
  if (personal.empty() || !std::filesystem::is_directory(shared, error)) return 1;

  const auto sid = UserSid();
  const auto name = L"Local\\HanLinEngineHost." + sid;
  const HANDLE mutex = CreateMutexW(nullptr, FALSE, name.c_str());
  if (!mutex) return 1;
  const bool already_running = GetLastError() == ERROR_ALREADY_EXISTS;
  if (desktop_update) {
    const auto event_name = L"Local\\HanLinDesktopUpdate." + sid;
    const auto ready = OpenEventW(SYNCHRONIZE, FALSE, event_name.c_str());
    const bool reserved = ready && WaitForSingleObject(ready, 0) == WAIT_OBJECT_0;
    if (ready) CloseHandle(ready);
    if (!already_running || !reserved) { CloseHandle(mutex); return 2; }
  } else if (already_running) {
    CloseHandle(mutex);
    return deploy ? 3 : 0;
  }
  fs::create_directories(personal, error);
  const auto profile_guard = CreateFileW((fs::path(personal) / L".hanlin-sync-operation.lock").c_str(),
      GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
      OPEN_ALWAYS, FILE_ATTRIBUTE_HIDDEN, nullptr);
  if (profile_guard == INVALID_HANDLE_VALUE) {
    CloseHandle(mutex);
    return 3;
  }
  const auto engine_guard = CreateFileW((fs::path(personal) / L".hanlin-librime.lock").c_str(),
      GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_HIDDEN, nullptr);
  if (engine_guard == INVALID_HANDLE_VALUE) {
    CloseHandle(profile_guard);
    CloseHandle(mutex);
    return 3;
  }
  const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const auto result = RunEngine(image, directory, shared, personal, deploy, enable_tray);
  CloseHandle(engine_guard);
  CloseHandle(profile_guard);
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

// HanLin sync probe -- test only, never shipped with the installer.
//
// Drives the packaged librime (src\dist\rime.dll) directly so the sync
// verification profiles contain real user dictionary entries instead of
// hand-written guesses at the *.userdb.txt format. Two modes:
//   --deploy           compile the profile once (needed before --learn)
//   --learn <input>    set the input, pick the first candidate and commit it,
//                      which records the commitment in the profile's userdb
// A JSON report is written next to the profile, because a windows-subsystem
// binary has no stdout the test scripts could read.
#include <rime_api.h>

#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Options {
  fs::path dll;
  fs::path shared;
  fs::path profile;
  fs::path report;
  std::vector<std::string> learn;
  bool deploy = false;
  bool sync = false;
};

std::string Utf8(const std::wstring& text) {
  if (text.empty()) return {};
  const int units = static_cast<int>(text.size());
  const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.data(), units, nullptr, 0, nullptr, nullptr);
  if (bytes <= 0) return {};
  std::string utf8(static_cast<size_t>(bytes), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), units, utf8.data(), bytes, nullptr, nullptr);
  return utf8;
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

bool Parse(int argc, wchar_t** argv, Options& options) {
  for (int index = 1; index < argc; ++index) {
    const std::wstring_view key(argv[index]);
    const bool has_value = index + 1 < argc;
    if (key == L"--deploy") {
      options.deploy = true;
    } else if (key == L"--sync") {
      options.sync = true;
    } else if (!has_value) {
      return false;
    } else if (key == L"--rime-dll") {
      options.dll = fs::path(argv[++index]);
    } else if (key == L"--shared") {
      options.shared = fs::path(argv[++index]);
    } else if (key == L"--profile") {
      options.profile = fs::path(argv[++index]);
    } else if (key == L"--report") {
      options.report = fs::path(argv[++index]);
    } else if (key == L"--learn") {
      options.learn.push_back(Utf8(argv[++index]));
    } else {
      return false;
    }
  }
  if (options.dll.empty() || options.shared.empty() || options.profile.empty()) return false;
  if (options.report.empty()) options.report = options.profile / L"probe-report.json";
  return true;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  Options options;
  if (!Parse(argc, argv, options)) {
    return 2;
  }
  std::vector<std::string> errors;
  std::vector<std::string> committed;
  bool initialized = false;

  const HMODULE module = LoadLibraryExW(options.dll.c_str(), nullptr,
      LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
  const auto get_api = module
      ? reinterpret_cast<decltype(&rime_get_api)>(GetProcAddress(module, "rime_get_api"))
      : nullptr;
  RimeApi* api = get_api ? get_api() : nullptr;
  if (!api) errors.push_back("could not load rime.dll");

  const std::string shared = Utf8(options.shared.wstring());
  const std::string profile = Utf8(options.profile.wstring());
  if (api) {
    RIME_STRUCT(RimeTraits, traits);
    traits.shared_data_dir = shared.c_str();
    traits.user_data_dir = profile.c_str();
    traits.app_name = "rime.hanlin";
    traits.distribution_name = "HanLin";
    traits.distribution_code_name = "HanLin";
    traits.min_log_level = 3;
    traits.log_dir = profile.c_str();

    if (options.deploy) {
      api->setup(&traits);
      api->deployer_initialize(&traits);
      if (!api->deploy()) errors.push_back("deploy failed");
      api->finalize();
    }

    if (options.sync) {
      std::vector<std::string> problems;
      std::string sync_dir_text;
      std::string user_sync_dir_text;
      std::string user_id_text;
      api->setup(&traits);
      api->deployer_initialize(&traits);
      if (api->get_sync_dir) {
        const char* value = api->get_sync_dir();
        if (value) sync_dir_text = value;
      }
      if (api->get_user_data_sync_dir) {
        char buffer[1024] = {0};
        api->get_user_data_sync_dir(buffer, sizeof(buffer) - 1);
        user_sync_dir_text = buffer;
      }
      if (api->get_user_id) {
        const char* value = api->get_user_id();
        if (value) user_id_text = value;
      }
      const bool synced = api->sync_user_data && api->sync_user_data() != 0;
      if (!synced) problems.push_back("sync_user_data rejected");
      if (api->join_maintenance_thread) api->join_maintenance_thread();
      api->finalize();
      std::ofstream file(options.report, std::ios::binary | std::ios::trunc);
      if (!file) return 1;
      file << "{\n  \"ok\": " << (problems.empty() ? "true" : "false") << ",\n";
      file << "  \"profile\": \"" << JsonEscape(profile) << "\",\n";
      file << "  \"syncDir\": \"" << JsonEscape(sync_dir_text) << "\",\n";
      file << "  \"userDataSyncDir\": \"" << JsonEscape(user_sync_dir_text) << "\",\n";
      file << "  \"userId\": \"" << JsonEscape(user_id_text) << "\",\n";
      file << "  \"synced\": " << (synced ? "true" : "false") << ",\n";
      file << "  \"errors\": [";
      for (size_t index = 0; index < problems.size(); ++index) {
        file << (index ? ", " : "") << "\"" << JsonEscape(problems[index]) << "\"";
      }
      file << "]\n}\n";
      file.close();
      return problems.empty() ? 0 : 1;
    }

    api->setup(&traits);
    api->initialize(&traits);
    initialized = true;

    const RimeSessionId session = api->create_session();
    if (!session) {
      errors.push_back("create_session failed");
    } else if (!api->select_schema(session, "rime_ice")) {
      errors.push_back("select_schema rime_ice failed");
    } else {
      for (const auto& input : options.learn) {
        if (!api->set_input(session, input.c_str())) {
          errors.push_back("set_input failed: " + input);
          continue;
        }
        RIME_STRUCT(RimeContext, context);
        const bool has_context = api->get_context(session, &context);
        if (has_context) api->free_context(&context);
        if (!has_context || !api->select_candidate(session, 0)) {
          errors.push_back("select_candidate failed: " + input);
          continue;
        }
        api->commit_composition(session);
        RIME_STRUCT(RimeCommit, commit);
        if (api->get_commit(session, &commit) && commit.text) {
          committed.push_back(commit.text);
          api->free_commit(&commit);
        } else {
          errors.push_back("get_commit failed: " + input);
        }
      }
      api->destroy_session(session);
      api->cleanup_all_sessions();
    }
    api->finalize();
    initialized = false;
  }

  std::ofstream report(options.report, std::ios::binary | std::ios::trunc);
  if (!report) return 1;
  report << "{\n";
  report << "  \"ok\": " << (errors.empty() ? "true" : "false") << ",\n";
  report << "  \"profile\": \"" << JsonEscape(profile) << "\",\n";
  report << "  \"sharedData\": \"" << JsonEscape(shared) << "\",\n";
  report << "  \"deployed\": " << (options.deploy ? "true" : "false") << ",\n";
  report << "  \"initialized\": " << (initialized ? "true" : "false") << ",\n";
  report << "  \"committed\": [";
  for (size_t index = 0; index < committed.size(); ++index) {
    report << (index ? ", " : "") << "\"" << JsonEscape(committed[index]) << "\"";
  }
  report << "],\n  \"errors\": [";
  for (size_t index = 0; index < errors.size(); ++index) {
    report << (index ? ", " : "") << "\"" << JsonEscape(errors[index]) << "\"";
  }
  report << "]\n}\n";
  report.close();
  return errors.empty() ? 0 : 1;
}

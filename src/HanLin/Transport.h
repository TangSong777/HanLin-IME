#pragma once

#include <windows.h>

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace hanlin {

enum class Command : uint32_t {
  Open,
  Close,
  Key,
  Select,
  Highlight,
  Page,
  SetAscii,
  Place,
  Clear,
  Blur,
  Focus,
};

constexpr uint32_t kProtocolVersion = 2;
constexpr uint32_t kMaxCandidates = 64;
constexpr UINT kCandidateAction = WM_APP + 24;
constexpr wchar_t kClientWindowClass[] = L"HanLinTipEvents";

struct Request {
  uint32_t version = kProtocolVersion;
  Command command = Command::Open;
  uint64_t session = 0;
  std::array<int32_t, 6> args{};
};

struct Candidate {
  std::wstring text;
  std::wstring comment;
  std::wstring label;
};

struct State {
  uint64_t session = 0;
  bool handled = false;
  bool ascii = false;
  bool composing = false;
  bool last_page = false;
  bool inline_preedit = false;
  bool has_caret = false;
  uint32_t caret = 0;
  uint32_t page = 0;
  uint32_t selection = 0;
  uint32_t menu_revision = 0;
  std::wstring preedit;
  std::wstring commit;
  std::vector<Candidate> candidates;
};

class Client {
 public:
  Client();
  bool Call(const Request& request, State& result) const;

 private:
  std::wstring endpoint_;
};

// Runs the host's message loop and serializes all engine access on its UI thread.
int RunTransport(const std::function<State(const Request&, DWORD)>& dispatch);
std::wstring UserSid();

}  // namespace hanlin

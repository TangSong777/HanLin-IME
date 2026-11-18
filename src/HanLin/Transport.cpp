#include "Transport.h"

#include <sddl.h>

#include <algorithm>
#include <cstring>
#include <type_traits>
#include <utility>

namespace hanlin {
namespace {

constexpr size_t kMaxPacket = 64 * 1024;
constexpr DWORD kReceiveTimeout = 200;
constexpr DWORD kKeyTimeout = 125;
constexpr DWORD kOpenTimeout = 1500;
constexpr DWORD kClientAccess = FILE_GENERIC_READ | FILE_WRITE_DATA | FILE_WRITE_ATTRIBUTES;

class Handle {
 public:
  explicit Handle(HANDLE handle) : value_(handle) {}
  ~Handle() { if (get()) CloseHandle(value_); }
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  HANDLE get() const { return value_ == INVALID_HANDLE_VALUE ? nullptr : value_; }

 private:
  HANDLE value_;
};

std::wstring Endpoint(const std::wstring& sid) {
  DWORD session = 0;
  if (sid.empty() || !ProcessIdToSessionId(GetCurrentProcessId(), &session))
    return {};
  return L"\\\\.\\pipe\\HanLinIme.1." + std::to_wstring(session) + L"." + sid;
}

struct ResponseHeader {
  uint32_t version;
  uint32_t flags;
  uint64_t session;
  uint32_t caret;
  uint32_t page;
  uint32_t selection;
  uint32_t candidate_count;
};

static_assert(sizeof(Request) == 40 && sizeof(ResponseHeader) == 32);
static_assert(sizeof(wchar_t) == 2);

enum StateFlag : uint32_t {
  Handled = 1 << 0,
  Ascii = 1 << 1,
  Composing = 1 << 2,
  LastPage = 1 << 3,
  InlinePreedit = 1 << 4,
  HasCaret = 1 << 5,
};

template <typename T>
void Append(std::vector<uint8_t>& packet, const T& value) {
  static_assert(std::is_trivially_copyable_v<T>);
  const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
  packet.insert(packet.end(), bytes, bytes + sizeof(value));
}

bool AppendString(std::vector<uint8_t>& packet, const std::wstring& value) {
  if (packet.size() + sizeof(uint32_t) + value.size() * sizeof(wchar_t) > kMaxPacket)
    return false;
  Append(packet, static_cast<uint32_t>(value.size()));
  const auto* bytes = reinterpret_cast<const uint8_t*>(value.data());
  packet.insert(packet.end(), bytes, bytes + value.size() * sizeof(wchar_t));
  return true;
}

bool WriteState(const State& state, std::vector<uint8_t>& packet) {
  uint32_t flags = 0;
  if (state.handled) flags |= Handled;
  if (state.ascii) flags |= Ascii;
  if (state.composing) flags |= Composing;
  if (state.last_page) flags |= LastPage;
  if (state.inline_preedit) flags |= InlinePreedit;
  if (state.has_caret) flags |= HasCaret;
  packet.clear();
  Append(packet, ResponseHeader{kProtocolVersion, flags, state.session, state.caret,
                                state.page, state.selection,
                                static_cast<uint32_t>(state.candidates.size())});
  if (!AppendString(packet, state.preedit) || !AppendString(packet, state.commit))
    return false;
  for (const auto& candidate : state.candidates)
    if (!AppendString(packet, candidate.text) || !AppendString(packet, candidate.comment) ||
        !AppendString(packet, candidate.label))
      return false;
  return true;
}

class PacketReader {
 public:
  PacketReader(const uint8_t* packet, size_t size) : packet_(packet), size_(size) {}

  template <typename T>
  bool Read(T& result) {
    if (size_ < sizeof(T)) return false;
    std::memcpy(&result, packet_, sizeof(T));
    Advance(sizeof(T));
    return true;
  }

  bool Read(std::wstring& value) {
    uint32_t length = 0;
    if (!Read(length) || length > size_ / sizeof(wchar_t)) return false;
    value.resize(length);
    auto count = length * sizeof(wchar_t);
    std::memcpy(value.data(), packet_, count);
    Advance(count);
    return true;
  }

  bool Done() const { return size_ == 0; }

 private:
  void Advance(size_t count) { packet_ += count; size_ -= count; }
  const uint8_t* packet_;
  size_t size_;
};

bool ReadState(const uint8_t* packet, size_t size, State& result) {
  PacketReader reader(packet, size);
  ResponseHeader header{};
  State state;
  if (!reader.Read(header) || header.version != kProtocolVersion ||
      header.candidate_count > kMaxCandidates || !reader.Read(state.preedit) ||
      !reader.Read(state.commit)) return false;
  state.session = header.session;
  state.handled = (header.flags & Handled) != 0;
  state.ascii = (header.flags & Ascii) != 0;
  state.composing = (header.flags & Composing) != 0;
  state.last_page = (header.flags & LastPage) != 0;
  state.inline_preedit = (header.flags & InlinePreedit) != 0;
  state.has_caret = (header.flags & HasCaret) != 0;
  state.caret = (std::min)(header.caret, static_cast<uint32_t>(state.preedit.size()));
  state.page = header.page;
  state.selection = header.selection;
  state.candidates.resize(header.candidate_count);
  for (auto& candidate : state.candidates)
    if (!reader.Read(candidate.text) || !reader.Read(candidate.comment) ||
        !reader.Read(candidate.label))
      return false;
  if (!reader.Done()) return false;
  result = std::move(state);
  return true;
}

void CancelIo(HANDLE pipe, OVERLAPPED& operation) {
  CancelIoEx(pipe, &operation);
  DWORD ignored = 0;
  GetOverlappedResult(pipe, &operation, &ignored, TRUE);
}

bool AwaitOnHost(HANDLE pipe, OVERLAPPED& operation, DWORD& size,
                 DWORD timeout, bool& stopped) {
  const auto deadline = GetTickCount64() + timeout;
  for (;;) {
    const auto now = GetTickCount64();
    const DWORD remaining = timeout == INFINITE ? INFINITE :
        (now < deadline ? static_cast<DWORD>(deadline - now) : 0);
    const auto wait = MsgWaitForMultipleObjectsEx(1, &operation.hEvent, remaining,
                                                  QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    if (wait == WAIT_OBJECT_0)
      return GetOverlappedResult(pipe, &operation, &size, FALSE) != FALSE;
    if (wait != WAIT_OBJECT_0 + 1) break;
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) { stopped = true; break; }
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    if (stopped || (timeout != INFINITE && GetTickCount64() >= deadline)) break;
  }
  CancelIo(pipe, operation);
  return false;
}

}  // namespace

std::wstring UserSid() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return {};
  Handle token_guard(token);
  DWORD bytes = 0;
  GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
  if (!bytes) return {};
  std::vector<uint8_t> buffer(bytes);
  if (!GetTokenInformation(token, TokenUser, buffer.data(), bytes, &bytes)) return {};
  const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());
  PWSTR sid = nullptr;
  if (!ConvertSidToStringSidW(user->User.Sid, &sid)) return {};
  std::wstring result(sid);
  LocalFree(sid);
  return result;
}

Client::Client() : endpoint_(Endpoint(UserSid())) {}

bool Client::Call(const Request& request, State& result) const {
  if (endpoint_.empty()) return false;
  const auto timeout = request.command == Command::Open ? kOpenTimeout : kKeyTimeout;
  HANDLE pipe = CreateFileW(endpoint_.c_str(), kClientAccess, 0, nullptr, OPEN_EXISTING,
                           FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
  if (pipe == INVALID_HANDLE_VALUE && GetLastError() == ERROR_PIPE_BUSY &&
      WaitNamedPipeW(endpoint_.c_str(), timeout))
    pipe = CreateFileW(endpoint_.c_str(), kClientAccess, 0, nullptr, OPEN_EXISTING,
                       FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
  Handle pipe_guard(pipe);
  if (!pipe_guard.get()) return false;
  DWORD mode = PIPE_READMODE_MESSAGE;
  if (!SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr)) return false;
  Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
  if (!event.get()) return false;
  OVERLAPPED operation{};
  operation.hEvent = event.get();
  auto outbound = request;
  std::array<uint8_t, kMaxPacket> response;
  DWORD size = 0;
  if (!TransactNamedPipe(pipe, &outbound, sizeof(outbound), response.data(),
                         static_cast<DWORD>(response.size()), &size, &operation)) {
    if (GetLastError() != ERROR_IO_PENDING) return false;
    if (WaitForSingleObject(event.get(), timeout) != WAIT_OBJECT_0) {
      CancelIo(pipe, operation);
      return false;
    }
    if (!GetOverlappedResult(pipe, &operation, &size, FALSE)) return false;
  }
  return ReadState(response.data(), size, result);
}

int RunTransport(const std::function<State(const Request&, DWORD)>& dispatch) {
  const auto sid = UserSid();
  const auto endpoint = Endpoint(sid);
  if (endpoint.empty()) return 1;
  // A TSF instance inside an app container opens the user's local endpoint.
  const auto descriptor = L"D:P(A;;GA;;;SY)(A;;GA;;;" + sid +
      L")(A;;0x12018b;;;AC)(A;;0x12018b;;;S-1-15-2-2)S:(ML;;NW;;;LW)";
  PSECURITY_DESCRIPTOR security = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(descriptor.c_str(), SDDL_REVISION_1,
                                                            &security, nullptr)) return 1;
  SECURITY_ATTRIBUTES attributes{sizeof(attributes), security, FALSE};
  Handle pipe(CreateNamedPipeW(endpoint.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED |
      FILE_FLAG_FIRST_PIPE_INSTANCE, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_REJECT_REMOTE_CLIENTS,
      1, static_cast<DWORD>(kMaxPacket), sizeof(Request), 0, &attributes));
  LocalFree(security);
  Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
  if (!pipe.get() || !event.get()) return 1;

  std::vector<uint8_t> reply;
  bool stopped = false;
  while (!stopped) {
    OVERLAPPED operation{};
    operation.hEvent = event.get();
    ResetEvent(event.get());
    DWORD size = 0;
    bool connected = ConnectNamedPipe(pipe.get(), &operation) != FALSE;
    if (!connected) {
      const auto error = GetLastError();
      if (error == ERROR_PIPE_CONNECTED) connected = true;
      else if (error == ERROR_IO_PENDING)
        connected = AwaitOnHost(pipe.get(), operation, size, INFINITE, stopped);
    }
    if (!connected) break;

    ResetEvent(event.get());
    Request request;
    const bool read = ReadFile(pipe.get(), &request, sizeof(request), &size, &operation) ||
        (GetLastError() == ERROR_IO_PENDING && AwaitOnHost(pipe.get(), operation, size, kReceiveTimeout, stopped));
    ULONG process_id = 0;
    if (read && size == sizeof(request) && request.version == kProtocolVersion &&
        GetNamedPipeClientProcessId(pipe.get(), &process_id)) {
      if (WriteState(dispatch(request, process_id), reply)) {
        ResetEvent(event.get());
        const bool written = WriteFile(pipe.get(), reply.data(), static_cast<DWORD>(reply.size()), &size, &operation) ||
            (GetLastError() == ERROR_IO_PENDING && AwaitOnHost(pipe.get(), operation, size, kReceiveTimeout, stopped));
        if (written && !stopped) {
          // The client closes its handle once the response is read.
          ResetEvent(event.get());
          uint8_t extra = 0;
          if (!ReadFile(pipe.get(), &extra, sizeof(extra), &size, &operation) && GetLastError() == ERROR_IO_PENDING)
            AwaitOnHost(pipe.get(), operation, size, kReceiveTimeout, stopped);
        }
      }
    }
    DisconnectNamedPipe(pipe.get());
  }
  return stopped ? 0 : 1;
}

}  // namespace hanlin

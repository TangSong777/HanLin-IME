#include <rime/algo/strings.h>
#include <utf8.h>

namespace rime {
namespace strings {

vector<string> split(const string& str,
                     const string& delim,
                     SplitBehavior behavior) {
  vector<string> strings;
  size_t lastPos, pos;
  if (behavior == SplitBehavior::SkipToken) {
    lastPos = str.find_first_not_of(delim, 0);
  } else {
    lastPos = 0;
  }
  pos = str.find_first_of(delim, lastPos);

  while (std::string::npos != pos || std::string::npos != lastPos) {
    strings.emplace_back(str.substr(lastPos, pos - lastPos));
    if (behavior == SplitBehavior::SkipToken) {
      lastPos = str.find_first_not_of(delim, pos);
    } else {
      if (pos == std::string::npos) {
        break;
      }
      lastPos = pos + 1;
    }
    pos = str.find_first_of(delim, lastPos);
  }
  return strings;
};

vector<string> split(const string& str, const string& delim) {
  return split(str, delim, SplitBehavior::KeepToken);
};

size_t literal_symbol_length(std::string_view input) {
  if (input.empty())
    return 0;
  const auto ch = static_cast<unsigned char>(input.front());
  if (ch < 0x80) {
    // Space and apostrophe are spelling delimiters in the pinyin scheme.
    const bool is_symbol = (ch >= '!' && ch <= '/') ||
                           (ch >= ':' && ch <= '@') ||
                           (ch >= '[' && ch <= '`') ||
                           (ch >= '{' && ch <= '~');
    return is_symbol && ch != '\'' ? 1 : 0;
  }
  // Preserve each UTF-8 code point emitted by the input's symbol mapping.
  auto end = input.begin();
  return utf8::internal::validate_next(end, input.end()) ==
                 utf8::internal::UTF8_OK
             ? end - input.begin()
             : 0;
}

}  // namespace strings
}  // namespace rime

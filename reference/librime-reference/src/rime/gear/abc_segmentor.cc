//
// Copyright RIME Developers
// Distributed under the BSD License
//
// 2011-05-15 GONG Chen <chen.sst@gmail.com>
//
#include <rime/common.h>
#include <rime/config.h>
#include <rime/schema.h>
#include <rime/segmentation.h>
#include <rime/algo/strings.h>
#include <rime/gear/abc_segmentor.h>

static const char kRimeAlphabet[] = "zyxwvutsrqponmlkjihgfedcba";

namespace rime {

AbcSegmentor::AbcSegmentor(const Ticket& ticket)
    : Segmentor(ticket), alphabet_(kRimeAlphabet) {
  if (!ticket.schema)
    return;
  if (Config* config = ticket.schema->config()) {
    config->GetString("speller/alphabet", &alphabet_);
    config->GetString("speller/delimiter", &delimiter_);
    config->GetString("speller/initials", &initials_);
    config->GetString("speller/finals", &finals_);
    config->GetBool("speller/enable_dynamic_symbols", &enable_dynamic_symbols_);
    if (auto extra_tags = config->GetList("abc_segmentor/extra_tags")) {
      for (size_t i = 0; i < extra_tags->size(); ++i) {
        if (auto value = As<ConfigValue>(extra_tags->GetAt(i))) {
          extra_tags_.insert(value->str());
        }
      }
    }
  }
  if (initials_.empty()) {
    initials_ = alphabet_;
  }
}

bool AbcSegmentor::Proceed(Segmentation* segmentation) {
  const string& input(segmentation->input());
  DLOG(INFO) << "abc_segmentor: " << input;
  size_t j = segmentation->GetCurrentStartPosition();
  size_t k = j;
  bool expecting_an_initial = true;
  const bool allow_symbols =
      enable_dynamic_symbols_ &&
      (segmentation->empty() || !segmentation->back().HasTag("calculator"));
  const std::string_view view(input);
  for (; k < input.length(); ++k) {
    bool is_letter = alphabet_.find(input[k]) != string::npos;
    bool is_delimiter = (k != 0) && (delimiter_.find(input[k]) != string::npos);
    if (allow_symbols && k != j && !is_delimiter) {
      if (auto length = strings::literal_symbol_length(view.substr(k))) {
        k += length - 1;
        expecting_an_initial = true;
        continue;
      }
    }
    if (!is_letter && !is_delimiter)
      break;
    bool is_initial = initials_.find(input[k]) != string::npos;
    bool is_final = finals_.find(input[k]) != string::npos;
    if (expecting_an_initial && !is_initial && !is_delimiter) {
      break;  // not a valid spelling.
    }
    // for the next character.
    expecting_an_initial = is_final || is_delimiter;
  }
  DLOG(INFO) << "[" << j << ", " << k << ")";
  if (j < k) {
    Segment segment(j, k);
    segment.tags.insert("abc");
    for (const string& tag : extra_tags_) {
      segment.tags.insert(tag);
    }
    segmentation->AddSegment(segment);
  }
  // continue this round
  return true;
}

}  // namespace rime

//
// Copyright RIME Developers
// Distributed under the BSD License
//
// 2011-7-12 Zou xu <zouivex@gmail.com>
//

#ifndef RIME_SYLLABIFIER_H_
#define RIME_SYLLABIFIER_H_

#include <stdint.h>
#include <rime_api.h>
#include <rime/common.h>
#include "spelling.h"

namespace rime {

class Prism;
class Corrector;

using SyllableId = int32_t;

struct EdgeProperties : SpellingProperties {
  EdgeProperties(SpellingProperties sup) : SpellingProperties(sup) {};
  EdgeProperties() = default;
  // 切分歧義編碼段的起始位置
  set<size_t> ambiguous_source_positions;
};

using SpellingMap = map<SyllableId, EdgeProperties>;
using VertexMap = map<size_t, SpellingType>;
using EndVertexMap = map<size_t, SpellingMap>;
using EdgeMap = map<size_t, EndVertexMap>;

using SpellingPropertiesList = vector<const EdgeProperties*>;
using SpellingIndex = map<SyllableId, SpellingPropertiesList>;
using SpellingIndices = map<size_t, SpellingIndex>;

struct SyllableGraph {
  size_t input_length = 0;
  size_t interpreted_length = 0;
  VertexMap vertices;
  EdgeMap edges;
  SpellingIndices indices;
  // Negative syllable IDs refer to literal text owned by this graph.
  vector<string> literal_syllables;

  const string* GetLiteralSyllable(SyllableId id) const {
    if (id >= 0)
      return nullptr;
    const auto index = static_cast<size_t>(-1 - id);
    return index < literal_syllables.size() ? &literal_syllables[index]
                                            : nullptr;
  }
};

class Syllabifier {
 public:
  Syllabifier() = default;
  explicit Syllabifier(const string& delimiters,
                       bool enable_completion = false,
                       bool strict_spelling = false)
      : delimiters_(delimiters),
        enable_completion_(enable_completion),
        strict_spelling_(strict_spelling) {}

  RIME_DLL int BuildSyllableGraph(const string& input,
                                  Prism& prism,
                                  SyllableGraph* graph);
  RIME_DLL void EnableCorrection(Corrector* corrector);
  void EnableDynamicNumeric() { enable_dynamic_numeric_ = true; }
  void EnableDynamicSymbols() { enable_dynamic_symbols_ = true; }
  void SetDynamicPunctuation(const vector<string>* spellings) {
    dynamic_punctuation_ = spellings;
  }

 protected:
  void CheckOverlappedSpellings(SyllableGraph* graph, size_t start, size_t end);
  void Transpose(SyllableGraph* graph);
  size_t MatchLiteral(const string& input) const;

  string delimiters_;
  bool enable_completion_ = false;
  bool strict_spelling_ = false;
  bool enable_dynamic_numeric_ = false;
  bool enable_dynamic_symbols_ = false;
  const vector<string>* dynamic_punctuation_ = nullptr;
  Corrector* corrector_ = nullptr;
};

}  // namespace rime

#endif  // RIME_SYLLABIFIER_H_

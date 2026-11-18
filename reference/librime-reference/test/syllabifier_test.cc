//
// Copyright RIME Developers
// Distributed under the BSD License
//
// 2011-07-05 GONG Chen <chen.sst@gmail.com>
//
#include <algorithm>
#include <utility>
#include <gtest/gtest.h>
#include <rime/dict/prism.h>
#include <rime/algo/syllabifier.h>

class RimeSyllabifierTest : public ::testing::Test {
 public:
  virtual void SetUp() {
    rime::vector<rime::string> syllables;
    syllables.push_back("a");      // 0 == id
    syllables.push_back("an");     // 1
    syllables.push_back("cha");    // 2
    syllables.push_back("chan");   // 3
    syllables.push_back("chang");  // 4
    syllables.push_back("gan");    // 5
    syllables.push_back("han");    // 6
    syllables.push_back("hang");   // 7
    syllables.push_back("na");     // 8
    syllables.push_back("tu");     // 9
    syllables.push_back("tuan");   // 10
    std::sort(syllables.begin(), syllables.end());
    for (size_t i = 0; i < syllables.size(); ++i) {
      syllable_id_[syllables[i]] = i;
    }

    rime::path file_path("syllabifier_test.bin");
    prism_.reset(new rime::Prism(file_path));
    rime::set<rime::string> keyset;
    std::copy(syllables.begin(), syllables.end(),
              std::inserter(keyset, keyset.begin()));
    prism_->Build(keyset);
  }

  virtual void TearDown() {}

 protected:
  rime::map<rime::string, rime::SyllableId> syllable_id_;
  rime::the<rime::Prism> prism_;
};

TEST_F(RimeSyllabifierTest, CaseAlpha) {
  rime::Syllabifier s;
  rime::SyllableGraph g;
  const rime::string input("a");
  s.BuildSyllableGraph(input, *prism_, &g);
  EXPECT_EQ(input.length(), g.input_length);
  EXPECT_EQ(input.length(), g.interpreted_length);
  EXPECT_EQ(2, g.vertices.size());
  ASSERT_FALSE(g.vertices.end() == g.vertices.find(1));
  EXPECT_EQ(rime::kNormalSpelling, g.vertices[1]);
  rime::SpellingMap& sp(g.edges[0][1]);
  EXPECT_EQ(1, sp.size());
  ASSERT_FALSE(sp.end() == sp.find(syllable_id_["a"]));
  EXPECT_EQ(rime::kNormalSpelling, sp[0].type);
  EXPECT_EQ(0.0, sp[0].credibility);
}

TEST_F(RimeSyllabifierTest, CaseFailure) {
  rime::Syllabifier s;
  rime::SyllableGraph g;
  const rime::string input("ang");
  s.BuildSyllableGraph(input, *prism_, &g);
  EXPECT_EQ(input.length(), g.input_length);
  EXPECT_EQ(input.length() - 1, g.interpreted_length);
  EXPECT_EQ(2, g.vertices.size());
  ASSERT_TRUE(g.vertices.end() == g.vertices.find(1));
  ASSERT_FALSE(g.vertices.end() == g.vertices.find(2));
  EXPECT_EQ(rime::kNormalSpelling, g.vertices[2]);
  rime::SpellingMap& sp(g.edges[0][2]);
  EXPECT_EQ(1, sp.size());
  ASSERT_FALSE(sp.end() == sp.find(syllable_id_["an"]));
}

TEST_F(RimeSyllabifierTest, DynamicNumeric) {
  rime::Syllabifier s("'");
  const rime::string input("an001234567890'tu42");

  rime::SyllableGraph disabled;
  EXPECT_EQ(2, s.BuildSyllableGraph(input, *prism_, &disabled));
  EXPECT_TRUE(disabled.literal_syllables.empty());

  s.EnableDynamicNumeric();
  rime::SyllableGraph enabled;
  EXPECT_EQ(input.size(), s.BuildSyllableGraph(input, *prism_, &enabled));
  EXPECT_EQ((rime::vector<rime::string>{"001234567890", "42"}),
            enabled.literal_syllables);
  ASSERT_EQ(1, enabled.indices.at(2).size());
  const auto& [id, properties] = *enabled.indices.at(2).begin();
  ASSERT_NE(nullptr, enabled.GetLiteralSyllable(id));
  EXPECT_EQ("001234567890", *enabled.GetLiteralSyllable(id));
  EXPECT_EQ(input.find("tu"), properties.front()->end_pos);
}

TEST_F(RimeSyllabifierTest, DynamicPunctuation) {
  const rime::string input = u8"an\uFF0Ctu-42\u3002na";
  const rime::vector<rime::string> punctuation = {u8"\uFF0C", "-", u8"\u3002"};
  rime::Syllabifier s;
  s.EnableDynamicNumeric();
  s.SetDynamicPunctuation(&punctuation);

  rime::SyllableGraph graph;
  EXPECT_EQ(input.size(), s.BuildSyllableGraph(input, *prism_, &graph));
  EXPECT_EQ((rime::vector<rime::string>{u8"\uFF0C", "-", "42", u8"\u3002"}),
            graph.literal_syllables);
  const auto& [id, properties] = *graph.indices.at(2).begin();
  ASSERT_NE(nullptr, graph.GetLiteralSyllable(id));
  EXPECT_EQ(u8"\uFF0C", *graph.GetLiteralSyllable(id));
  EXPECT_EQ(input.find("tu"), properties.front()->end_pos);
}

TEST_F(RimeSyllabifierTest, TrimDelimitersAroundDynamicNumber) {
  rime::Syllabifier s("'");
  s.EnableDynamicNumeric();
  rime::SyllableGraph graph;
  const rime::string input("''42'tu");
  EXPECT_EQ(input.size(), s.BuildSyllableGraph(input, *prism_, &graph));
  const auto& [id, properties] = *graph.indices.at(0).begin();
  ASSERT_NE(nullptr, graph.GetLiteralSyllable(id));
  EXPECT_EQ("42", *graph.GetLiteralSyllable(id));
  EXPECT_EQ(input.find("tu"), properties.front()->end_pos);
}

TEST_F(RimeSyllabifierTest, DynamicSymbols) {
  const rime::string input = u8"an_\u2026\u2026\u00A5\U0001F4B0'42/tu+";
  rime::Syllabifier s("'");
  s.EnableDynamicNumeric();
  s.EnableDynamicSymbols();

  rime::SyllableGraph graph;
  EXPECT_EQ(input.size(), s.BuildSyllableGraph(input, *prism_, &graph));
  EXPECT_EQ((rime::vector<rime::string>{"_", u8"\u2026", u8"\u2026", u8"\u00A5",
                                       u8"\U0001F4B0", "42", "/", "+"}),
            graph.literal_syllables);
}

TEST_F(RimeSyllabifierTest, CaseChangan) {
  rime::Syllabifier s;
  rime::SyllableGraph g;
  const rime::string input("changan");
  s.BuildSyllableGraph(input, *prism_, &g);
  EXPECT_EQ(input.length(), g.input_length);
  EXPECT_EQ(input.length(), g.interpreted_length);
  EXPECT_EQ(4, g.vertices.size());
  // not c'han'gan or c'hang'an
  EXPECT_TRUE(g.vertices.end() == g.vertices.find(1));
  ASSERT_FALSE(g.vertices.end() == g.vertices.find(4));
  ASSERT_FALSE(g.vertices.end() == g.vertices.find(5));
  EXPECT_EQ(rime::kNormalSpelling, g.vertices[4]);
  EXPECT_EQ(rime::kNormalSpelling, g.vertices[5]);
  // chan, chang but not cha
  rime::EndVertexMap& e0(g.edges[0]);
  EXPECT_EQ(2, e0.size());
  ASSERT_FALSE(e0.end() == e0.find(4));
  ASSERT_FALSE(e0.end() == e0.find(5));
  EXPECT_FALSE(e0[4].end() == e0[4].find(syllable_id_["chan"]));
  EXPECT_FALSE(e0[5].end() == e0[5].find(syllable_id_["chang"]));
  // gan$
  rime::EndVertexMap& e4(g.edges[4]);
  EXPECT_EQ(1, e4.size());
  ASSERT_FALSE(e4.end() == e4.find(7));
  EXPECT_FALSE(e4[7].end() == e4[7].find(syllable_id_["gan"]));
  // an$
  rime::EndVertexMap& e5(g.edges[5]);
  EXPECT_EQ(1, e5.size());
  ASSERT_FALSE(e5.end() == e5.find(7));
  EXPECT_FALSE(e5[7].end() == e5[7].find(syllable_id_["an"]));
}

TEST_F(RimeSyllabifierTest, CaseTuan) {
  rime::Syllabifier s;
  rime::SyllableGraph g;
  const rime::string input("tuan");
  s.BuildSyllableGraph(input, *prism_, &g);
  EXPECT_EQ(input.length(), g.input_length);
  EXPECT_EQ(input.length(), g.interpreted_length);
  EXPECT_EQ(3, g.vertices.size());
  // both tu'an and tuan
  ASSERT_FALSE(g.vertices.end() == g.vertices.find(2));
  ASSERT_FALSE(g.vertices.end() == g.vertices.find(4));
  EXPECT_EQ(rime::kAmbiguousSpelling, g.vertices[2]);
  EXPECT_EQ(rime::kNormalSpelling, g.vertices[4]);
  rime::EndVertexMap& e0(g.edges[0]);
  EXPECT_EQ(2, e0.size());
  ASSERT_FALSE(e0.end() == e0.find(2));
  ASSERT_FALSE(e0.end() == e0.find(4));
  EXPECT_FALSE(e0[2].end() == e0[2].find(syllable_id_["tu"]));
  EXPECT_FALSE(e0[4].end() == e0[4].find(syllable_id_["tuan"]));
  // an$
  rime::EndVertexMap& e2(g.edges[2]);
  EXPECT_EQ(1, e2.size());
  ASSERT_FALSE(e2.end() == e2.find(4));
  EXPECT_FALSE(e2[4].end() == e2[4].find(syllable_id_["an"]));
}

TEST_F(RimeSyllabifierTest, CaseChainingAmbiguity) {
  rime::Syllabifier s;
  rime::SyllableGraph g;
  const rime::string input("anana");
  s.BuildSyllableGraph(input, *prism_, &g);
  EXPECT_EQ(input.length(), g.input_length);
  EXPECT_EQ(input.length(), g.interpreted_length);
  EXPECT_EQ(input.length() + 1, g.vertices.size());
}

TEST_F(RimeSyllabifierTest, TransposedSyllableGraph) {
  rime::Syllabifier s;
  rime::SyllableGraph g;
  const rime::string input("changan");
  s.BuildSyllableGraph(input, *prism_, &g);
  ASSERT_FALSE(g.indices.end() == g.indices.find(0));
  EXPECT_EQ(2, g.indices[0].size());
  EXPECT_FALSE(g.indices[0].end() == g.indices[0].find(syllable_id_["chan"]));
  EXPECT_FALSE(g.indices[0].end() == g.indices[0].find(syllable_id_["chang"]));
  ASSERT_EQ(1, g.indices[0][syllable_id_["chan"]].size());
  ASSERT_FALSE(NULL == g.indices[0][syllable_id_["chan"]][0]);
  EXPECT_EQ(4, g.indices[0][syllable_id_["chan"]][0]->end_pos);
}

TEST_F(RimeSyllabifierTest, TrimLeadingDelimiters) {
  rime::Syllabifier s(" '");
  rime::SyllableGraph g;
  const rime::string input("''a");
  s.BuildSyllableGraph(input, *prism_, &g);
  EXPECT_EQ(input.length(), g.input_length);
  EXPECT_EQ(input.length(), g.interpreted_length);
  EXPECT_EQ(2, g.vertices.size());
  ASSERT_FALSE(g.vertices.end() == g.vertices.find(3));
  EXPECT_EQ(rime::kNormalSpelling, g.vertices[1]);
  rime::SpellingMap& sp(g.edges[0][3]);
  EXPECT_EQ(1, sp.size());
  ASSERT_FALSE(sp.end() == sp.find(syllable_id_["a"]));
  EXPECT_EQ(rime::kNormalSpelling, sp[0].type);
  EXPECT_EQ(0.0, sp[0].credibility);
}

TEST_F(RimeSyllabifierTest, TrimTrailingDelimiters) {
  rime::Syllabifier s(" '");
  rime::SyllableGraph g;
  const rime::string input("a''");
  s.BuildSyllableGraph(input, *prism_, &g);
  EXPECT_EQ(input.length(), g.input_length);
  EXPECT_EQ(input.length(), g.interpreted_length);
  EXPECT_EQ(2, g.vertices.size());
  ASSERT_FALSE(g.vertices.end() == g.vertices.find(3));
  EXPECT_EQ(rime::kNormalSpelling, g.vertices[1]);
  rime::SpellingMap& sp(g.edges[0][3]);
  EXPECT_EQ(1, sp.size());
  ASSERT_FALSE(sp.end() == sp.find(syllable_id_["a"]));
  EXPECT_EQ(rime::kNormalSpelling, sp[0].type);
  EXPECT_EQ(0.0, sp[0].credibility);
}

TEST_F(RimeSyllabifierTest, TrimBothLeadingAndTrailingDelimiters) {
  rime::Syllabifier s(" '");
  rime::SyllableGraph g;
  const rime::string input("''a''");
  s.BuildSyllableGraph(input, *prism_, &g);
  EXPECT_EQ(input.length(), g.input_length);
  EXPECT_EQ(input.length(), g.interpreted_length);
  EXPECT_EQ(2, g.vertices.size());
  ASSERT_FALSE(g.vertices.end() == g.vertices.find(5));
  EXPECT_EQ(rime::kNormalSpelling, g.vertices[1]);
  rime::SpellingMap& sp(g.edges[0][5]);
  EXPECT_EQ(1, sp.size());
  ASSERT_FALSE(sp.end() == sp.find(syllable_id_["a"]));
  EXPECT_EQ(rime::kNormalSpelling, sp[0].type);
  EXPECT_EQ(0.0, sp[0].credibility);
}

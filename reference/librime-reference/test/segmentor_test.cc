//
// Copyright RIME Developers
// Distributed under the BSD License
//
// 2011-05-20 GONG Chen <chen.sst@gmail.com>
//

#include <gtest/gtest.h>
#include <rime/common.h>
#include <rime/config.h>
#include <rime/engine.h>
#include <rime/schema.h>
#include <rime/segmentation.h>
#include <rime/segmentor.h>

using namespace rime;

TEST(AbcSegmentorTest, NoMatch) {
  Segmentor::Component* component = Segmentor::Require("abc_segmentor");
  ASSERT_TRUE(component != NULL);
  the<Engine> engine(Engine::Create());
  the<Segmentor> segmentor(component->Create(engine.get()));
  ASSERT_TRUE(bool(segmentor));
  Segmentation segmentation;
  segmentation.Reset("3.1415926");
  bool goon = segmentor->Proceed(&segmentation);
  EXPECT_TRUE(goon);
  EXPECT_FALSE(segmentation.HasFinishedSegmentation());
  ASSERT_EQ(0, segmentation.size());
}

TEST(AbcSegmentorTest, FullMatch) {
  Segmentor::Component* component = Segmentor::Require("abc_segmentor");
  ASSERT_TRUE(component != NULL);
  the<Engine> engine(Engine::Create());
  the<Segmentor> segmentor(component->Create(engine.get()));
  ASSERT_TRUE(bool(segmentor));
  Segmentation segmentation;
  segmentation.Reset("zyxwvutsrqponmlkjihgfedcba");
  bool goon = segmentor->Proceed(&segmentation);
  EXPECT_TRUE(goon);
  EXPECT_TRUE(segmentation.HasFinishedSegmentation());
  ASSERT_EQ(1, segmentation.size());
  EXPECT_EQ(0, segmentation[0].start);
  EXPECT_EQ(26, segmentation[0].end);
  EXPECT_GE(1U, segmentation[0].tags.size());
}

TEST(AbcSegmentorTest, PrefixMatch) {
  Segmentor::Component* component = Segmentor::Require("abc_segmentor");
  ASSERT_TRUE(component != NULL);
  the<Engine> engine(Engine::Create());
  the<Segmentor> segmentor(component->Create(engine.get()));
  ASSERT_TRUE(bool(segmentor));
  Segmentation segmentation;
  segmentation.Reset("abcdefg.1415926");
  bool goon = segmentor->Proceed(&segmentation);
  EXPECT_TRUE(goon);
  EXPECT_FALSE(segmentation.HasFinishedSegmentation());
  ASSERT_EQ(1, segmentation.size());
  EXPECT_EQ(0, segmentation[0].start);
  EXPECT_EQ(7, segmentation[0].end);
  EXPECT_GE(1U, segmentation[0].tags.size());
}

TEST(AbcSegmentorTest, DynamicSymbols) {
  auto* component = Segmentor::Require("abc_segmentor");
  ASSERT_NE(nullptr, component);
  the<Engine> engine(Engine::Create());
  ASSERT_TRUE(engine->schema()->config()->SetItem("speller/enable_dynamic_symbols",
                                                  New<ConfigValue>(true)));
  the<Segmentor> segmentor(component->Create(engine.get()));
  ASSERT_TRUE(segmentor);

  Segmentation phrase;
  phrase.Reset(u8"ni\u3010hao\u3011/ma");
  EXPECT_TRUE(segmentor->Proceed(&phrase));
  EXPECT_TRUE(phrase.HasFinishedSegmentation());
  ASSERT_EQ(1, phrase.size());
  EXPECT_TRUE(phrase.back().HasTag("abc"));

  Segmentation calculator;
  calculator.Reset("cc!ni");
  Segment segment(0, calculator.input().size());
  segment.tags.insert("calculator");
  calculator.AddSegment(segment);
  EXPECT_TRUE(segmentor->Proceed(&calculator));
  EXPECT_TRUE(calculator.back().HasTag("calculator"));
  EXPECT_FALSE(calculator.back().HasTag("abc"));
}

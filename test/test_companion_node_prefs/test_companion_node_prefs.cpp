#include <gtest/gtest.h>

#include <cstring>

#include "../../examples/companion_radio/NodePrefs.h"

// These target the pure, hardware-independent helpers declared alongside
// NodePrefs (free functions and NodePrefs:: static lookup tables). They build
// and run on the `native` host env with no board/filesystem mocking, unlike
// DataStore::savePrefs()/loadPrefsInt() (the actual on-disk read/write path),
// which pulls in FILESYSTEM/File (Adafruit_LittleFS or fs::FS, depending on
// platform) plus IdentityStore/ContactInfo/ChannelDetails/target.h — real
// coverage of that path needs a filesystem mock this test file doesn't have.

TEST(DefaultRepeaterFreqForBand, PicksTheBandBelowTheCompanionFrequency) {
  EXPECT_FLOAT_EQ(433.000f, defaultRepeaterFreqForBand(490.0f));  // < 500
  EXPECT_FLOAT_EQ(869.495f, defaultRepeaterFreqForBand(500.0f));  // 500 boundary
  EXPECT_FLOAT_EQ(869.495f, defaultRepeaterFreqForBand(868.0f));  // 500..890
  EXPECT_FLOAT_EQ(918.000f, defaultRepeaterFreqForBand(890.0f));  // 890 boundary
  EXPECT_FLOAT_EQ(918.000f, defaultRepeaterFreqForBand(915.0f));  // >= 890
}

TEST(IsValidRepeaterProfile, AcceptsAProfileWithinAllBounds) {
  EXPECT_TRUE(isValidRepeaterProfile(868.0f, 250.0f, 10, 5, 850.0f, 930.0f));
}

TEST(IsValidRepeaterProfile, RejectsFrequencyOutsideTheChipRange) {
  EXPECT_FALSE(isValidRepeaterProfile(800.0f, 250.0f, 10, 5, 850.0f, 930.0f));
  EXPECT_FALSE(isValidRepeaterProfile(950.0f, 250.0f, 10, 5, 850.0f, 930.0f));
}

TEST(IsValidRepeaterProfile, RejectsSpreadingFactorOutsideFiveToTwelve) {
  EXPECT_FALSE(isValidRepeaterProfile(868.0f, 250.0f, 4, 5, 850.0f, 930.0f));
  EXPECT_FALSE(isValidRepeaterProfile(868.0f, 250.0f, 13, 5, 850.0f, 930.0f));
}

TEST(IsValidRepeaterProfile, RejectsCodingRateOutsideFiveToEight) {
  EXPECT_FALSE(isValidRepeaterProfile(868.0f, 250.0f, 10, 4, 850.0f, 930.0f));
  EXPECT_FALSE(isValidRepeaterProfile(868.0f, 250.0f, 10, 9, 850.0f, 930.0f));
}

TEST(IsValidRepeaterProfile, RejectsBandwidthOutsideSevenTo510) {
  EXPECT_FALSE(isValidRepeaterProfile(868.0f, 6.9f, 10, 5, 850.0f, 930.0f));
  EXPECT_FALSE(isValidRepeaterProfile(868.0f, 510.1f, 10, 5, 850.0f, 930.0f));
}

TEST(SeedDefaultRepeaterProfile, SeedsFromTheCompanionFrequencyBand) {
  NodePrefs prefs{};
  prefs.freq = 915.0f;
  seedDefaultRepeaterProfile(prefs);
  EXPECT_EQ(1, prefs.repeater_use_profile);
  EXPECT_FLOAT_EQ(918.000f, prefs.repeater_freq);
  EXPECT_FLOAT_EQ((float)LORA_BW, prefs.repeater_bw);
  EXPECT_EQ((uint8_t)LORA_SF, prefs.repeater_sf);
  EXPECT_EQ((uint8_t)LORA_CR, prefs.repeater_cr);
}

TEST(AlarmRepeatRoundTrip, EveryPresetIndexRoundTripsThroughItsMask) {
  for (uint8_t idx = 0; idx < NodePrefs::ALARM_REPEAT_COUNT; idx++) {
    uint8_t mask = NodePrefs::alarmRepeatMaskForIdx(idx);
    EXPECT_EQ(idx, NodePrefs::alarmRepeatIdxForMask(mask)) << "idx=" << (int)idx;
  }
}

TEST(AlarmRepeatRoundTrip, OutOfRangeIndexClampsToNone) {
  // Cast to a prvalue: EXPECT_EQ binds its arguments by const T&, which would
  // otherwise ODR-use this in-class-initialized static const and need an
  // out-of-line definition that doesn't exist.
  EXPECT_EQ((uint8_t)NodePrefs::ALARM_REPEAT_NONE, NodePrefs::alarmRepeatMaskForIdx(99));
}

TEST(AlarmRepeatRoundTrip, AnArbitraryNonPresetMaskReadsAsOff) {
  // Documents the reverse-lookup's documented behaviour: a mask that doesn't
  // match a preset (e.g. a future custom-day picker's value) reads as index 0
  // ("OFF") even though it isn't actually the all-zero NONE mask.
  EXPECT_EQ(0, NodePrefs::alarmRepeatIdxForMask(0x15));
}

TEST(AlarmRepeatLabel, MatchesEachPresetIndex) {
  EXPECT_STREQ("OFF", NodePrefs::alarmRepeatLabel(0));
  EXPECT_STREQ("Daily", NodePrefs::alarmRepeatLabel(1));
  EXPECT_STREQ("Weekdays", NodePrefs::alarmRepeatLabel(2));
  EXPECT_STREQ("Weekends", NodePrefs::alarmRepeatLabel(3));
}

TEST(AlarmRepeatLabel, OutOfRangeIndexClampsToOff) {
  EXPECT_STREQ("OFF", NodePrefs::alarmRepeatLabel(99));
}

TEST(KeyboardAlphabetLabel, MatchesEachAlphabetIndex) {
  EXPECT_STREQ("Latin", NodePrefs::keyboardAlphabetLabel(NodePrefs::KB_ALPHABET_LATIN_ONLY));
  EXPECT_STREQ("Cyrillic", NodePrefs::keyboardAlphabetLabel(NodePrefs::KB_ALPHABET_CYRILLIC));
  EXPECT_STREQ("Greek", NodePrefs::keyboardAlphabetLabel(NodePrefs::KB_ALPHABET_GREEK));
}

TEST(KeyboardAlphabetLabel, OutOfRangeIndexClampsToLatin) {
  EXPECT_STREQ("Latin", NodePrefs::keyboardAlphabetLabel(99));
}

TEST(HomePageLabel, MatchesEveryBitIndexInOrder) {
  EXPECT_STREQ("Clock", NodePrefs::homePageLabel(NodePrefs::HPB_CLOCK));
  EXPECT_STREQ("Tools", NodePrefs::homePageLabel(NodePrefs::HPB_TOOLS));
  EXPECT_STREQ("Messages", NodePrefs::homePageLabel(NodePrefs::HPB_QUICK_MSG));
  EXPECT_STREQ("Favourites", NodePrefs::homePageLabel(NodePrefs::HPB_FAVOURITES));
  EXPECT_STREQ("Map", NodePrefs::homePageLabel(NodePrefs::HPB_MAP));
}

TEST(HomePageLabel, OutOfRangeBitReturnsEmptyString) {
  EXPECT_STREQ("", NodePrefs::homePageLabel(NodePrefs::HPB_COUNT));
}

TEST(OptionTables, InRangeIndicesReturnTheDocumentedValues) {
  EXPECT_EQ(250, NodePrefs::locShareMoveMeters(2));
  EXPECT_EQ(120, NodePrefs::locShareIntervalSecs(2));
  EXPECT_EQ(900, NodePrefs::locShareHeartbeatSecs(2));
  EXPECT_EQ(500, NodePrefs::locatorRadiusMeters(3));
  EXPECT_STREQ("Both", NodePrefs::locatorModeLabel(2));
  EXPECT_EQ(10, NodePrefs::gpsAvgSecs(2));
  EXPECT_STREQ("10s", NodePrefs::gpsAvgLabel(2));
  EXPECT_EQ(120, NodePrefs::trailAutoPauseSecs(2));
  EXPECT_STREQ("2m", NodePrefs::trailAutoPauseLabel(2));
}

// Each option table clamps an out-of-range index independently, and they
// don't all agree on which in-range index to fall back to (1 for the
// loc-share/locator tables, 0 for the GPS-averaging/trail-autopause ones) —
// this pins down that existing, slightly inconsistent behaviour so a future
// change to any one table doesn't silently change another's fallback.
TEST(OptionTables, OutOfRangeIndexFallsBackPerTable) {
  EXPECT_EQ(NodePrefs::locShareMoveMeters(1), NodePrefs::locShareMoveMeters(99));
  EXPECT_EQ(NodePrefs::locShareIntervalSecs(1), NodePrefs::locShareIntervalSecs(99));
  EXPECT_EQ(NodePrefs::locShareHeartbeatSecs(0), NodePrefs::locShareHeartbeatSecs(99));
  EXPECT_EQ(NodePrefs::locatorRadiusMeters(1), NodePrefs::locatorRadiusMeters(99));
  EXPECT_STREQ("Arrive", NodePrefs::locatorModeLabel(99));
  EXPECT_EQ(NodePrefs::gpsAvgSecs(0), NodePrefs::gpsAvgSecs(99));
  EXPECT_EQ(NodePrefs::trailAutoPauseSecs(0), NodePrefs::trailAutoPauseSecs(99));
}

TEST(BuildRTTTLString, EmptyWhenLengthIsZero) {
  char buf[64] = "unchanged";
  NodePrefs::buildRTTTLString(nullptr, 0, 0, buf, sizeof(buf));
  EXPECT_STREQ("", buf);
}

TEST(BuildRTTTLString, EncodesANoteAndARestWithTheChosenTempo) {
  // pitch=2('d'), octave offset=1 (-> octave 5), dur_idx=1 (-> 8th note)
  const uint8_t note = 2 | (1 << 3) | (1 << 5);
  // pitch=0 (rest), dur_idx=0 (-> 4th note)
  const uint8_t rest = 0;
  const uint8_t notes[2] = { note, rest };

  char buf[64];
  NodePrefs::buildRTTTLString(notes, 2, /*bpm_idx=*/1, buf, sizeof(buf));
  EXPECT_STREQ("Ring:d=8,o=5,b=90:8d5,4p", buf);
}

TEST(BuildRTTTLString, OutOfRangeBpmIndexClampsToTheMiddlePreset) {
  const uint8_t note = 0;  // a single rest is enough to isolate the tempo
  char buf[64];
  NodePrefs::buildRTTTLString(&note, 1, /*bpm_idx=*/9, buf, sizeof(buf));
  EXPECT_STREQ("Ring:d=8,o=5,b=120:4p", buf);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

// Host-side unit tests for the cross-process settings contract (SettingsContract.h).
// Verifies explicit string option IDs, case-insensitive parsing, legacy integer
// migration compatibility, and value clamping shared with Settings.cpp.
#include "check.h"

#include "SettingsContract.h"

#include <cstring>

using namespace settings_contract;

static void testSampVersionContract()
{
	auto v0 = parseSampVersion("0.3.7");
	CHECK(std::strcmp(v0.id, "0.3.7") == 0);
	CHECK_EQ(v0.netgameVersion, 0xFD9);

	auto v1 = parseSampVersion("0.3.7-R4");
	CHECK(std::strcmp(v1.id, "0.3.7-R4") == 0);
	CHECK_EQ(v1.netgameVersion, 0xFD9);

	auto v2 = parseSampVersion("0.3.7-r5"); // case-insensitive
	CHECK(std::strcmp(v2.id, "0.3.7-R5") == 0);
	CHECK_EQ(v2.netgameVersion, 0xFD9);

	auto v3 = parseSampVersion("0.3.dl-r1"); // case-insensitive DL
	CHECK(std::strcmp(v3.id, "0.3.DL-R1") == 0);
	CHECK_EQ(v3.netgameVersion, 0xFDE);

	// Unknown / null safely falls back to 0.3.7 (0xFD9).
	auto vBad = parseSampVersion("invalid-version");
	CHECK(std::strcmp(vBad.id, "0.3.7") == 0);
	CHECK_EQ(vBad.netgameVersion, 0xFD9);

	auto vNull = parseSampVersion(nullptr);
	CHECK(std::strcmp(vNull.id, "0.3.7") == 0);
	CHECK_EQ(vNull.netgameVersion, 0xFD9);

	// Legacy integer compatibility (pre-v2 settings.json)
	CHECK(std::strcmp(parseSampVersionLegacyInt(0).id, "0.3.7") == 0);
	CHECK(std::strcmp(parseSampVersionLegacyInt(1).id, "0.3.7-R4") == 0);
	CHECK(std::strcmp(parseSampVersionLegacyInt(2).id, "0.3.7-R5") == 0);
	CHECK(std::strcmp(parseSampVersionLegacyInt(3).id, "0.3.DL-R1") == 0);
	CHECK_EQ(parseSampVersionLegacyInt(3).netgameVersion, 0xFDE);
	CHECK(std::strcmp(parseSampVersionLegacyInt(-1).id, "0.3.7") == 0);
	CHECK(std::strcmp(parseSampVersionLegacyInt(99).id, "0.3.7") == 0);
}

static void testEnumContractsAndLegacyMigration()
{
	// Aspect ratio
	CHECK_EQ(parseAspectRatio("auto"), 0);
	CHECK_EQ(parseAspectRatio("16:9"), 1);
	CHECK_EQ(parseAspectRatio("20:9"), 2);
	CHECK_EQ(parseAspectRatio("21:9"), 3);
	CHECK_EQ(parseAspectRatio("16:10"), 4);
	CHECK_EQ(parseAspectRatio("4:3"), 5);
	CHECK_EQ(parseAspectRatio("AUTO"), 0);
	CHECK_EQ(parseAspectRatio("unknown"), 0);
	CHECK_EQ(parseAspectRatioLegacyInt(3), 3);
	CHECK_EQ(parseAspectRatioLegacyInt(42), 0);

	// Crosshair style
	CHECK_EQ(parseCrosshairStyle("default"), 0);
	CHECK_EQ(parseCrosshairStyle("cross"), 1);
	CHECK_EQ(parseCrosshairStyle("dot"), 2);
	CHECK_EQ(parseCrosshairStyle("cross_dot"), 3);
	CHECK_EQ(parseCrosshairStyle("hidden"), 4);
	CHECK_EQ(parseCrosshairStyle("CROSS_DOT"), 3);
	CHECK_EQ(parseCrosshairStyle(nullptr), 0);
	CHECK_EQ(parseCrosshairStyleLegacyInt(4), 4);
	CHECK_EQ(parseCrosshairStyleLegacyInt(-5), 0);

	// Font family
	CHECK_EQ(parseFontFamily("chinese"), 0);
	CHECK_EQ(parseFontFamily("arial_bold"), 1);
	CHECK_EQ(parseFontFamily("custom"), 2);
	CHECK_EQ(parseFontFamily("ARIAL_BOLD"), 1);
	CHECK_EQ(parseFontFamilyLegacyInt(2), 2);
	CHECK_EQ(parseFontFamilyLegacyInt(9), 0);

	// Screenshot mode
	CHECK_EQ(parseScreenshotMode("hud"), 0);
	CHECK_EQ(parseScreenshotMode("game"), 1);
	CHECK_EQ(parseScreenshotMode("GAME"), 1);
	CHECK_EQ(parseScreenshotModeLegacyInt(1), 1);
	CHECK_EQ(parseScreenshotModeLegacyInt(7), 0);

	// Voice mic mode
	CHECK_EQ(parseVoiceMicMode("ptt"), 0);
	CHECK_EQ(parseVoiceMicMode("toggle"), 1);
	CHECK_EQ(parseVoiceMicMode("TOGGLE"), 1);
	CHECK_EQ(parseVoiceMicModeLegacyInt(1), 1);
	CHECK_EQ(parseVoiceMicModeLegacyInt(-1), 0);

	// Reverse ID lookup round-trip
	CHECK(std::strcmp(enumIdForValue(kCrosshairStyles, 3, "default"), "cross_dot") == 0);
	CHECK(std::strcmp(enumIdForValue(kCrosshairStyles, 99, "default"), "default") == 0);
	CHECK(std::strcmp(enumIdForValue(kVoiceMicModes, 1, "ptt"), "toggle") == 0);
}

static void testClampHelpers()
{
	CHECK_EQ(clampInt(-10, 0, 100), 0);
	CHECK_EQ(clampInt(50, 0, 100), 50);
	CHECK_EQ(clampInt(250, 0, 100), 100);

	CHECK(clampFloat(-1.0f, 0.0f, 1.0f) == 0.0f);
	CHECK(clampFloat(0.5f, 0.0f, 1.0f) == 0.5f);
	CHECK(clampFloat(2.5f, 0.0f, 1.0f) == 1.0f);
}

int main()
{
	RUN(testSampVersionContract);
	RUN(testEnumContractsAndLegacyMigration);
	RUN(testClampHelpers);
	return check::finish();
}

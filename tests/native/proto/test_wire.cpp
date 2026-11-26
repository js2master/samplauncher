// Unit tests for Net/Proto/Wire.h (Reader, Writer, sticky failure, primitives).
#include "check.h"

#include "Net/Proto/Version.h"
#include "Net/Proto/Wire.h"

#include <cstdint>
#include <cstring>
#include <string>

static void versionHelpers()
{
	CHECK(proto::versionFromNetgame(0xFD9) == proto::ProtoVersion::V037);
	CHECK(proto::versionFromNetgame(0xFDE) == proto::ProtoVersion::V03DL);
	CHECK(proto::versionFromNetgame(0) == proto::ProtoVersion::V037);
	CHECK(proto::versionMatches(proto::kVerAll, proto::ProtoVersion::V037));
	CHECK(proto::versionMatches(proto::kVerAll, proto::ProtoVersion::V03DL));
	CHECK(proto::versionMatches(proto::kVer037, proto::ProtoVersion::V037));
	CHECK(!proto::versionMatches(proto::kVer037, proto::ProtoVersion::V03DL));
	CHECK(!proto::versionMatches(proto::kVer03DL, proto::ProtoVersion::V037));
	CHECK(proto::versionMatches(proto::kVer03DL, proto::ProtoVersion::V03DL));
}

static void roundTripPrimitives()
{
	RakNet::BitStream bs;
	proto::Writer w(bs);
	uint8_t outU8 = 0xAB;
	int8_t outI8 = -42;
	uint16_t outU16 = 0xBEEF;
	int16_t outI16 = -12345;
	uint32_t outU32 = 0xDEADBEEF;
	int32_t outI32 = -987654;
	float outF32 = 3.25f;
	bool outB1 = true, outB2 = false;
	proto::Vec2 outV2{1.25f, -2.5f};
	proto::Vec3 outV3{10.5f, -20.25f, 30.75f};
	uint8_t outRaw[4] = {1, 2, 3, 4};
	std::string outS8 = "hello";
	std::string outS8Empty;
	std::string outS32 = "longer string payload";
	std::string outFix = "AB";
	std::string outCmp = "Compressed text {FF0000}test\nline2";
	std::string outCmpEmpty;
	proto::Quat outQ{0.5f, -0.5f, 0.5f, 0.5f};
	proto::Vec3 outCV{0.3f, -0.4f, 1.2f};

	CHECK(w.u8(outU8) && w.i8(outI8) && w.u16(outU16) && w.i16(outI16));
	CHECK(w.u32(outU32) && w.i32(outI32) && w.f32(outF32));
	CHECK(w.bit(outB1) && w.bit(outB2));
	CHECK(w.vec2(outV2) && w.vec3(outV3));
	CHECK(w.bytes(outRaw, sizeof(outRaw)));
	CHECK(w.dynStr8(outS8) && w.dynStr8(outS8Empty));
	CHECK(w.dynStr32(outS32) && w.fixedStr(outFix, 4));
	CHECK(w.compressedStr(outCmp) && w.compressedStr(outCmpEmpty));
	CHECK(w.normQuat(outQ) && w.compressedVec(outCV));
	CHECK(w.healthArmour(100, 0) && w.healthArmour(49, 100));
	CHECK(w.ok());

	proto::Reader r(bs.GetData(), static_cast<uint32_t>(bs.GetNumberOfBitsUsed()));
	uint8_t inU8 = 0;
	int8_t inI8 = 0;
	uint16_t inU16 = 0;
	int16_t inI16 = 0;
	uint32_t inU32 = 0;
	int32_t inI32 = 0;
	float inF32 = 0;
	bool inB1 = false, inB2 = true;
	proto::Vec2 inV2{};
	proto::Vec3 inV3{};
	uint8_t inRaw[4] = {};
	std::string inS8, inS8Empty = "not-empty", inS32, inFix, inCmp, inCmpEmpty = "not-empty";
	proto::Quat inQ{};
	proto::Vec3 inCV{};
	uint8_t h1 = 0, a1 = 99, h2 = 0, a2 = 0;

	CHECK(r.u8(inU8) && r.i8(inI8) && r.u16(inU16) && r.i16(inI16));
	CHECK(r.u32(inU32) && r.i32(inI32) && r.f32(inF32));
	CHECK(r.bit(inB1) && r.bit(inB2));
	CHECK(r.vec2(inV2) && r.vec3(inV3));
	CHECK(r.bytes(inRaw, sizeof(inRaw)));
	CHECK(r.dynStr8(inS8) && r.dynStr8(inS8Empty));
	CHECK(r.dynStr32(inS32) && r.fixedStr(inFix, 4));
	CHECK(r.compressedStr(inCmp) && r.compressedStr(inCmpEmpty));
	CHECK(r.normQuat(inQ) && r.compressedVec(inCV));
	CHECK(r.healthArmour(h1, a1) && r.healthArmour(h2, a2));
	CHECK(r.ok());
	CHECK_EQ(r.unreadBits(), 0);

	CHECK_EQ(inU8, outU8);
	CHECK_EQ(inI8, outI8);
	CHECK_EQ(inU16, outU16);
	CHECK_EQ(inI16, outI16);
	CHECK_EQ(inU32, outU32);
	CHECK_EQ(inI32, outI32);
	CHECK_NEAR(inF32, outF32, 0.0);
	CHECK(inB1 && !inB2);
	CHECK_NEAR(inV2.x, outV2.x, 0.0);
	CHECK_NEAR(inV2.y, outV2.y, 0.0);
	CHECK_NEAR(inV3.x, outV3.x, 0.0);
	CHECK_NEAR(inV3.y, outV3.y, 0.0);
	CHECK_NEAR(inV3.z, outV3.z, 0.0);
	CHECK(memcmp(inRaw, outRaw, 4) == 0);
	CHECK(inS8 == outS8 && inS8Empty.empty());
	CHECK(inS32 == outS32);
	CHECK_EQ(inFix.size(), 4);
	CHECK(inFix[0] == 'A' && inFix[1] == 'B' && inFix[2] == '\0' && inFix[3] == '\0');
	CHECK(inCmp == outCmp && inCmpEmpty.empty());
	CHECK_NEAR(inQ.w, outQ.w, 1e-3);
	CHECK_NEAR(inQ.x, outQ.x, 1e-3);
	CHECK_NEAR(inQ.y, outQ.y, 1e-3);
	CHECK_NEAR(inQ.z, outQ.z, 1e-3);
	CHECK_NEAR(inCV.x, outCV.x, 1e-3);
	CHECK_NEAR(inCV.y, outCV.y, 1e-3);
	CHECK_NEAR(inCV.z, outCV.z, 1e-3);
	CHECK_EQ(h1, 100);
	CHECK_EQ(a1, 0);
	CHECK_EQ(h2, 49);
	CHECK_EQ(a2, 100);
}

static void stickyFailureLeavesFieldsUntouched()
{
	uint8_t raw[3] = {0x11, 0x22, 0x33};
	proto::Reader r(raw, 24);
	uint16_t a = 0;
	uint32_t b = 0xCAFEBABE;
	uint8_t c = 0x77;

	CHECK(r.u16(a));
	CHECK_EQ(a, 0x2211);
	CHECK(!r.u32(b));
	CHECK(!r.ok());
	CHECK_EQ(b, 0xCAFEBABE);
	// Sticky: even though 1 byte remains, subsequent reads must fail and leave c intact.
	CHECK(!r.u8(c));
	CHECK_EQ(c, 0x77);
}

static void emptyAndNullStreams()
{
	proto::Reader empty(nullptr, 0);
	CHECK(empty.ok());
	CHECK_EQ(empty.unreadBits(), 0);
	uint8_t x = 9;
	CHECK(!empty.u8(x));
	CHECK_EQ(x, 9);

	proto::Reader badNull(nullptr, 16);
	CHECK(!badNull.ok());
}

static void optionalTailWorks()
{
	RakNet::BitStream bs;
	proto::Writer w(bs);
	CHECK(w.u16(42));
	// Omit trailing uint32_t on purpose.

	proto::Reader r(bs.GetData(), static_cast<uint32_t>(bs.GetNumberOfBitsUsed()));
	uint16_t head = 0;
	uint32_t tail = 999;
	CHECK(r.u16(head));
	CHECK(r.optionalTail([&](auto& s) { return s.u32(tail); }));
	CHECK(r.ok());
	CHECK_EQ(head, 42);
	CHECK_EQ(tail, 999);
	CHECK_EQ(r.unreadBits(), 0);
}

int main()
{
	StringCompressor::AddReference();
	RUN(versionHelpers);
	RUN(roundTripPrimitives);
	RUN(stickyFailureLeavesFieldsUntouched);
	RUN(emptyAndNullStreams);
	RUN(optionalTailWorks);
	StringCompressor::RemoveReference();
	return check::finish();
}

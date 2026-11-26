#include "check.h"

#include "Net/Proto/RpcDecode.h"
#include "Net/Proto/Sync.h"

#include <RakNet/BitStream.h>

#include <cstdint>

template <typename Packet>
static Packet roundTrip(const Packet& in)
{
	RakNet::BitStream bs;
	proto::Writer w(bs);
	Packet copy = in;
	CHECK(copy.serialize(w, proto::ProtoVersion::V037));
	CHECK(w.ok());

	const int totalBits = bs.GetNumberOfBitsUsed();
	proto::Reader r(bs.GetData(), static_cast<uint32_t>(totalBits));
	Packet out{};
	CHECK(out.serialize(r, proto::ProtoVersion::V037));
	CHECK(r.ok());
	CHECK_EQ(r.unreadBits(), 0);

	const auto dec = proto::decodeInboundPacket(bs.GetData(), static_cast<uint32_t>(totalBits), proto::ProtoVersion::V037);
	CHECK(dec.status == proto::DecodeStatus::kOk);
	CHECK_EQ(dec.unreadBits, 0);
	return out;
}

template <typename Packet>
static void expectTruncatedFails(const Packet& in, int dropBits = 8)
{
	RakNet::BitStream bs;
	proto::Writer w(bs);
	Packet copy = in;
	CHECK(copy.serialize(w, proto::ProtoVersion::V037));
	const int totalBits = bs.GetNumberOfBitsUsed();
	CHECK(totalBits >= dropBits);

	proto::Reader r(bs.GetData(), static_cast<uint32_t>(totalBits - dropBits));
	Packet out{};
	CHECK(!out.serialize(r, proto::ProtoVersion::V037));
}

static void testHandshakePackets()
{
	{
		proto::AuthKeyPacket in{};
		in.key = "15121F6F18550C00AC4B4F8A167D0379BB0ACA99043";
		const auto out = roundTrip(in);
		CHECK(out.key == in.key);
		expectTruncatedFails(in);
	}
	{
		proto::ConnectionSucceededPacket in{};
		in.binaryAddress = 0x7F000001u;
		in.port = 7777;
		in.playerId = 3;
		in.challenge = 0xCAFEBABEu;
		const auto out = roundTrip(in);
		CHECK_EQ(out.binaryAddress, 0x7F000001u);
		CHECK_EQ(out.port, 7777);
		CHECK_EQ(out.playerId, 3);
		CHECK_EQ(out.challenge, 0xCAFEBABEu);
		expectTruncatedFails(in);
	}
}

static void testInboundSyncPackets()
{
	// PlayerSync (with and without timestamp, with and without optional flags)
	for (bool withTs : {false, true}) {
		proto::InboundPlayerSync in{};
		in.hasTimestamp = withTs;
		in.timestamp = withTs ? 123456u : 0u;
		in.playerId = 7;
		in.hasLR = true;
		in.lrAnalog = 128;
		in.hasUD = true;
		in.udAnalog = 65408;
		in.keys = 0x0028;
		in.position = {100.0f, -200.5f, 15.25f};
		in.quat = {0.70710678f, 0.0f, 0.0f, 0.70710678f};
		in.health = 100;
		in.armour = 49;
		in.currentWeapon = 24;
		in.specialAction = 10;
		in.moveSpeed = {0.5f, -0.25f, 0.0f};
		in.hasSurfing = true;
		in.surfInfo = 5;
		in.surfOffsets = {0.1f, 0.2f, 1.5f};
		in.hasAnim = true;
		in.animation = 0x110404A5u;

		const auto out = roundTrip(in);
		CHECK_EQ(out.hasTimestamp, withTs);
		CHECK_EQ(out.timestamp, in.timestamp);
		CHECK_EQ(out.playerId, 7);
		CHECK(out.hasLR && out.lrAnalog == 128);
		CHECK(out.hasUD && out.udAnalog == 65408);
		CHECK_EQ(out.keys, 0x0028);
		CHECK_NEAR(out.position.y, -200.5, 0.0);
		CHECK_NEAR(out.quat.w, 0.70710678, 1e-3);
		CHECK_EQ(out.health, 100);
		CHECK_EQ(out.armour, 49);
		CHECK_EQ(out.currentWeapon, 24);
		CHECK_EQ(out.specialAction, 10);
		CHECK_NEAR(out.moveSpeed.x, 0.5, 1e-2);
		CHECK(out.hasSurfing && out.surfInfo == 5);
		CHECK(out.hasAnim && out.animation == 0x110404A5u);
		expectTruncatedFails(in, 1);
	}
	{
		proto::InboundPlayerSync minimal{};
		minimal.playerId = 2;
		minimal.quat = {1.0f, 0.0f, 0.0f, 0.0f};
		const auto out = roundTrip(minimal);
		CHECK(!out.hasLR && !out.hasUD && !out.hasSurfing && !out.hasAnim);
		CHECK_EQ(out.surfInfo, 0xFFFF);
		CHECK_EQ(out.animation, 0x80000000u);
	}

	// VehicleSync
	for (bool withTs : {false, true}) {
		proto::InboundVehicleSync in{};
		in.hasTimestamp = withTs;
		in.timestamp = withTs ? 99999u : 0u;
		in.playerId = 4;
		in.vehicleId = 15;
		in.lrAnalog = 10;
		in.udAnalog = 20;
		in.keys = 0x0008;
		in.quat = {1.0f, 0.0f, 0.0f, 0.0f};
		in.position = {10.0f, 20.0f, 30.0f};
		in.moveSpeed = {0.2f, 0.3f, 0.0f};
		in.carHealth = 950;
		in.playerHealth = 70;
		in.playerArmour = 0;
		in.currentWeapon = 28;
		in.sirenOn = true;
		in.landingGearState = false;
		in.hasTrainOrHydra = true;
		in.trainOrHydra = 0x3F800000u;
		in.hasTrailer = true;
		in.trailerId = 42;

		const auto out = roundTrip(in);
		CHECK_EQ(out.hasTimestamp, withTs);
		CHECK_EQ(out.playerId, 4);
		CHECK_EQ(out.vehicleId, 15);
		CHECK_EQ(out.carHealth, 950);
		CHECK_EQ(out.playerHealth, 70);
		CHECK_EQ(out.playerArmour, 0);
		CHECK(out.sirenOn && !out.landingGearState);
		CHECK(out.hasTrainOrHydra && out.trainOrHydra == 0x3F800000u);
		CHECK(out.hasTrailer && out.trailerId == 42);
		expectTruncatedFails(in, 1);
	}

	// AimSync
	{
		proto::InboundAimSync in{};
		in.playerId = 8;
		in.camMode = 53;
		in.aimF1 = {0.0f, 1.0f, 0.0f};
		in.aimPos = {10.0f, 20.0f, 15.0f};
		in.aimZ = 0.25f;
		in.zoomWeaponState = static_cast<uint8_t>((2 << 6) | 30);
		in.aspectRatio = 85;
		const auto out = roundTrip(in);
		CHECK_EQ(out.playerId, 8);
		CHECK_EQ(out.camMode, 53);
		CHECK_NEAR(out.aimZ, 0.25, 0.0);
		CHECK_EQ(out.zoomWeaponState, in.zoomWeaponState);
		CHECK_EQ(out.aspectRatio, 85);
		expectTruncatedFails(in);
	}

	// BulletSync
	{
		proto::InboundBulletSync in{};
		in.playerId = 8;
		in.targetType = 1;
		in.targetId = 3;
		in.origin = {1.0f, 2.0f, 3.0f};
		in.pos = {4.0f, 5.0f, 6.0f};
		in.offset = {0.1f, 0.2f, 0.3f};
		in.weaponId = 24;
		const auto out = roundTrip(in);
		CHECK_EQ(out.playerId, 8);
		CHECK_EQ(out.targetType, 1);
		CHECK_EQ(out.targetId, 3);
		CHECK_EQ(out.weaponId, 24);
		expectTruncatedFails(in);
	}

	// PassengerSync
	{
		proto::InboundPassengerSync in{};
		in.playerId = 6;
		in.vehicleId = 5;
		in.seatFlags = 1;
		in.currentWeapon = 29;
		in.playerHealth = 100;
		in.playerArmour = 50;
		in.lrAnalog = 0;
		in.udAnalog = 0;
		in.keys = 0;
		in.position = {1.0f, 2.0f, 3.0f};
		const auto out = roundTrip(in);
		CHECK_EQ(out.playerId, 6);
		CHECK_EQ(out.vehicleId, 5);
		CHECK_EQ(out.seatFlags, 1);
		CHECK_EQ(out.currentWeapon, 29);
		expectTruncatedFails(in);
	}

	// MarkersSync (empty and populated)
	{
		proto::InboundMarkersSync empty{};
		const auto outEmpty = roundTrip(empty);
		CHECK_EQ(outEmpty.entries.size(), 0u);

		proto::InboundMarkersSync in{};
		in.entries.push_back({1, true, 100, -200, 15});
		in.entries.push_back({2, false, 0, 0, 0});
		const auto out = roundTrip(in);
		CHECK_EQ(out.entries.size(), 2u);
		CHECK(out.entries[0].active && out.entries[0].x == 100 && out.entries[0].y == -200 && out.entries[0].z == 15);
		CHECK(!out.entries[1].active && out.entries[1].playerId == 2);
		expectTruncatedFails(in, 1);
	}

	// TrailerSync
	{
		proto::InboundTrailerSync in{};
		in.playerId = 3;
		in.trailerId = 44;
		in.position = {10.0f, 20.0f, 30.0f};
		in.quat = {1.0f, 0.1f, 0.2f, 0.3f};
		in.moveSpeed = {0.5f, 0.0f, 0.0f};
		in.turnSpeed = {0.0f, 0.0f, 0.1f};
		const auto out = roundTrip(in);
		CHECK_EQ(out.playerId, 3);
		CHECK_EQ(out.trailerId, 44);
		CHECK_NEAR(out.quat.w, 1.0, 0.0);
		CHECK_NEAR(out.quat.z, 0.3, 1e-6);
		expectTruncatedFails(in);
	}

	// UnoccupiedSync
	{
		proto::InboundUnoccupiedSync in{};
		in.playerId = 3;
		in.vehicleId = 20;
		in.seatId = 1;
		in.roll = {1.0f, 0.0f, 0.0f};
		in.direction = {0.0f, 1.0f, 0.0f};
		in.position = {10.0f, 20.0f, 15.0f};
		in.moveSpeed = {0.1f, 0.2f, 0.3f};
		in.turnSpeed = {0.01f, 0.02f, 0.03f};
		in.carHealth = 850.0f;
		const auto out = roundTrip(in);
		CHECK_EQ(out.playerId, 3);
		CHECK_EQ(out.vehicleId, 20);
		CHECK_EQ(out.seatId, 1);
		CHECK_NEAR(out.carHealth, 850.0, 0.0);
		expectTruncatedFails(in);
	}
}

int main()
{
	RUN(testHandshakePackets);
	RUN(testInboundSyncPackets);
	return check::finish();
}

// Unit tests for F02 Batch 7: all 23 outbound RPCs + 11 outbound raw/sync packets.
#include "check.h"

#include "Net/Proto/Gpci.h"
#include "Net/Proto/RpcDecode.h"
#include "Net/Proto/RpcOutbound.h"
#include "Net/Proto/Sync.h"

#include <RakNet/BitStream.h>

#include <cstdint>
#include <cstring>

template <typename T>
static void roundTripRpc(const T& in, T& out, proto::ProtoVersion ver = proto::ProtoVersion::V037)
{
	RakNet::BitStream bs;
	proto::Writer w(bs);
	T copy = in;
	CHECK(copy.serialize(w, ver));

	proto::Reader r(bs.GetData(), static_cast<uint32_t>(bs.GetNumberOfBitsUsed()));
	CHECK(out.serialize(r, ver));
	CHECK_EQ(r.unreadBits(), 0);

	auto dec = proto::decodeOutboundRpc(T::kRpcId, bs.GetData(), static_cast<uint32_t>(bs.GetNumberOfBitsUsed()), ver);
	CHECK(dec.status == proto::DecodeStatus::kOk);
	CHECK_EQ(dec.unreadBits, 0);

	if (bs.GetNumberOfBitsUsed() > 0) {
		T trunc{};
		proto::Reader rt(bs.GetData(), static_cast<uint32_t>(bs.GetNumberOfBitsUsed() - 1));
		CHECK(!trunc.serialize(rt, ver));
	}
}

template <typename T>
static void roundTripPkt(const T& in, T& out, int expectedBytes = -1)
{
	RakNet::BitStream bs;
	proto::Writer w(bs);
	T copy = in;
	CHECK(copy.serialize(w, proto::ProtoVersion::V037));

	if (expectedBytes >= 0) {
		CHECK_EQ(bs.GetNumberOfBitsUsed(), expectedBytes * 8);
	}

	proto::Reader r(bs.GetData(), static_cast<uint32_t>(bs.GetNumberOfBitsUsed()));
	CHECK(out.serialize(r, proto::ProtoVersion::V037));
	CHECK_EQ(r.unreadBits(), 0);

	auto dec = proto::decodeOutboundPacket(bs.GetData(), static_cast<uint32_t>(bs.GetNumberOfBitsUsed()), proto::ProtoVersion::V037);
	CHECK(dec.status == proto::DecodeStatus::kOk);
	CHECK_EQ(dec.unreadBits, 0);

	if (bs.GetNumberOfBitsUsed() > 8 && expectedBytes >= 0) {
		T trunc{};
		proto::Reader rt(bs.GetData(), static_cast<uint32_t>(bs.GetNumberOfBitsUsed() - 1));
		CHECK(!trunc.serialize(rt, proto::ProtoVersion::V037));
	}
}

static void testOutboundRpcs()
{
	// ClientJoin 0.3.7 and 0.3.DL
	{
		proto::ClientJoin in{}, out{};
		in.version = 4057;
		in.mod = 1;
		in.nickname = "Tester";
		in.challengeResponse = 0x12345678u;
		in.authKey = "0123456789ABCDEF";
		in.clientVersion = "0.3.7";
		roundTripRpc(in, out, proto::ProtoVersion::V037);
		CHECK_EQ(out.version, 4057);
		CHECK(out.nickname == "Tester");
		CHECK_EQ(out.challengeResponse, 0x12345678u);
		CHECK(out.authKey == "0123456789ABCDEF");
		CHECK(out.clientVersion == "0.3.7");

		proto::ClientJoin outDl{};
		in.clientVersion = "0.3.DL-R1";
		roundTripRpc(in, outDl, proto::ProtoVersion::V03DL);
		CHECK(outDl.clientVersion == "0.3.DL-R1");
	}
	{
		proto::ClientCheckReply in{0x48, 0x12345678u, 0xAB}, out{};
		roundTripRpc(in, out);
		CHECK_EQ(out.type, 0x48);
		CHECK_EQ(out.argument, 0x12345678u);
		CHECK_EQ(out.result, 0xAB);
	}
	{
		proto::DialogResponse in{100, 1, 2, "hello"}, out{};
		roundTripRpc(in, out);
		CHECK_EQ(out.id, 100);
		CHECK_EQ(out.response, 1);
		CHECK_EQ(out.listItem, 2);
		CHECK(out.input == "hello");
	}
	{
		proto::ChatReq in{"hello world"}, out{};
		roundTripRpc(in, out);
		CHECK(out.message == "hello world");
	}
	{
		proto::ServerCommandReq in{"/smoke"}, out{};
		roundTripRpc(in, out);
		CHECK(out.command == "/smoke");
	}
	{
		proto::UpdateScoresPingsReq in{}, out{};
		roundTripRpc(in, out);
	}
	{
		proto::RequestClassReq in{3}, out{};
		roundTripRpc(in, out);
		CHECK_EQ(out.classId, 3);
	}
	{
		proto::RequestSpawnReq in{}, out{};
		roundTripRpc(in, out);
	}
	{
		proto::SpawnReq in{}, out{};
		roundTripRpc(in, out);
	}
	{
		proto::DeathReq in{54, 0xFFFF}, out{};
		roundTripRpc(in, out);
		CHECK_EQ(out.reason, 54);
		CHECK_EQ(out.killerId, 0xFFFF);
	}
	{
		proto::SetInteriorReq in{7}, out{};
		roundTripRpc(in, out);
		CHECK_EQ(out.interiorId, 7);
	}
	{
		proto::EnterVehicleReq in{42, 1}, out{};
		roundTripRpc(in, out);
		CHECK_EQ(out.vehicleId, 42);
		CHECK_EQ(out.passenger, 1);
	}
	{
		proto::ExitVehicleReq in{42}, out{};
		roundTripRpc(in, out);
		CHECK_EQ(out.vehicleId, 42);
	}
	{
		proto::DamageVehicleReq in{42, 0x11223344u, 0x55667788u, 0x0Fu, 0x05u}, out{};
		roundTripRpc(in, out);
		CHECK_EQ(out.vehicleId, 42);
		CHECK_EQ(out.panelDamageStatus, 0x11223344u);
		CHECK_EQ(out.doorDamageStatus, 0x55667788u);
		CHECK_EQ(out.lightDamageStatus, 0x0Fu);
		CHECK_EQ(out.tireDamageStatus, 0x05u);
	}
	{
		proto::VehicleDestroyedReq in{19}, out{};
		roundTripRpc(in, out);
		CHECK_EQ(out.vehicleId, 19);
	}
	{
		proto::PickedUpPickupReq in{77}, out{};
		roundTripRpc(in, out);
		CHECK_EQ(out.pickupId, 77);
	}
	{
		proto::ClickTextDrawReq in{2050}, out{};
		roundTripRpc(in, out);
		CHECK_EQ(out.textDrawId, 2050);
	}
	{
		proto::ClickPlayerReq in{5, 0}, out{};
		roundTripRpc(in, out);
		CHECK_EQ(out.playerId, 5);
		CHECK_EQ(out.source, 0);
	}
	{
		proto::MapMarkerReq in{{10.5f, -20.25f, 30.75f}}, out{};
		roundTripRpc(in, out);
		CHECK_NEAR(out.position.x, 10.5, 0.0);
		CHECK_NEAR(out.position.y, -20.25, 0.0);
		CHECK_NEAR(out.position.z, 30.75, 0.0);
	}
	{
		proto::GiveTakeDamageReq in{true, 9, 25.5f, 24, 3}, out{};
		roundTripRpc(in, out);
		CHECK(out.taking);
		CHECK_EQ(out.playerId, 9);
		CHECK_NEAR(out.damage, 25.5, 0.0);
		CHECK_EQ(out.weaponId, 24);
		CHECK_EQ(out.bodyPart, 3);
	}
	{
		proto::GiveDamageActorReq in{false, 12, 15.0f, 31, 9}, out{};
		roundTripRpc(in, out);
		CHECK(!out.unknown);
		CHECK_EQ(out.actorId, 12);
		CHECK_NEAR(out.damage, 15.0, 0.0);
		CHECK_EQ(out.weaponId, 31);
		CHECK_EQ(out.bodyPart, 9);
	}
	{
		proto::EditObjectReply in{true, 88, 1, {1.0f, 2.0f, 3.0f}, {10.0f, 20.0f, 30.0f}}, out{};
		roundTripRpc(in, out);
		CHECK(out.playerObject);
		CHECK_EQ(out.objectId, 88);
		CHECK_EQ(out.response, 1u);
		CHECK_NEAR(out.rotation.z, 30.0, 0.0);
	}
	{
		proto::EditAttachedObjectReply in{1, 2, 18912, 2, {0.1f, 0.2f, 0.3f}, {1.0f, 2.0f, 3.0f}, {1.0f, 1.0f, 1.0f}, 0xFF112233u, 0xFF445566u}, out{};
		roundTripRpc(in, out);
		CHECK_EQ(out.response, 1u);
		CHECK_EQ(out.index, 2);
		CHECK_EQ(out.modelId, 18912);
		CHECK_EQ(out.boneId, 2);
		CHECK_EQ(out.color1, 0xFF112233u);
		CHECK_EQ(out.color2, 0xFF445566u);
	}
	{
		proto::SelectObjectReply in{0, 42, 1271, {12.5f, -34.25f, 5.75f}}, out{};
		roundTripRpc(in, out);
		CHECK_EQ(out.selectType, 0u);
		CHECK_EQ(out.objectId, 42);
		CHECK_EQ(out.modelId, 1271u);
		CHECK_NEAR(out.position.x, 12.5, 0.0);
		CHECK_NEAR(out.position.y, -34.25, 0.0);
		CHECK_NEAR(out.position.z, 5.75, 0.0);
	}
	{
		proto::MenuSelectReq in{3}, out{};
		roundTripRpc(in, out);
		CHECK_EQ(out.row, 3);
	}
	{
		proto::MenuQuitReq in{}, out{};
		roundTripRpc(in, out);
	}
	{
		proto::CameraTargetReply in{12, 34, 5, 0xFFFF}, out{};
		roundTripRpc(in, out);
		CHECK_EQ(out.objectId, 12);
		CHECK_EQ(out.vehicleId, 34);
		CHECK_EQ(out.playerId, 5);
		CHECK_EQ(out.actorId, 0xFFFF);
	}
	{
		proto::PickedUpWeaponPickupReq in{17}, out{};
		roundTripRpc(in, out);
		CHECK_EQ(out.owner, 17);
	}
}

static void testOutboundPackets()
{
	{
		proto::RconCommandPacket in{"players"}, out{};
		roundTripPkt(in, out);
		CHECK(out.command == "players");
	}
	{
		proto::OutboundPlayerSync in{}, out{};
		in.lrAnalog = 128;
		in.udAnalog = 65408;
		in.keys = 0x0028;
		in.position = {100.5f, -200.25f, 15.75f};
		in.quat = {1.0f, 0.0f, 0.0f, 0.0f};
		in.health = 100;
		in.armour = 50;
		in.currentWeapon = 24;
		in.specialAction = 10;
		in.moveSpeed = {0.1f, 0.2f, 0.3f};
		in.surfOffsets = {0.0f, 0.0f, 1.5f};
		in.surfInfo = 12;
		in.animation = 0x110404A5u;
		roundTripPkt(in, out, 69);
		CHECK_EQ(out.health, 100);
		CHECK_EQ(out.animation, 0x110404A5u);
	}
	{
		proto::OutboundVehicleSync in{}, out{};
		in.vehicleId = 7;
		in.lrAnalog = 10;
		in.udAnalog = 20;
		in.keys = 8;
		in.quat = {1.0f, 0.0f, 0.0f, 0.0f};
		in.position = {1.0f, 2.0f, 3.0f};
		in.moveSpeed = {0.25f, 0.0f, 0.0f};
		in.carHealth = 950.0f;
		in.playerHealth = 70;
		in.playerArmour = 20;
		in.currentWeapon = 28;
		in.sirenOn = 1;
		in.landingGearState = 0;
		in.trailerId = 9;
		in.trainOrHydra = 5000u;
		roundTripPkt(in, out, 64);
		CHECK_EQ(out.vehicleId, 7);
		CHECK_EQ(out.trainOrHydra, 5000u);
	}
	{
		proto::OutboundPassengerSync in{}, out{};
		in.vehicleId = 5;
		in.seatFlags = 0x42;
		in.currentWeapon = 29;
		in.playerHealth = 100;
		in.playerArmour = 50;
		in.lrAnalog = 1;
		in.udAnalog = 2;
		in.keys = 3;
		in.position = {10.0f, 20.0f, 30.0f};
		roundTripPkt(in, out, 25);
		CHECK_EQ(out.seatFlags, 0x42);
	}
	{
		proto::OutboundUnoccupiedSync in{}, out{};
		in.vehicleId = 11;
		in.seatId = 1;
		in.roll = {1.0f, 0.0f, 0.0f};
		in.direction = {0.0f, 1.0f, 0.0f};
		in.position = {10.0f, 20.0f, 15.0f};
		in.moveSpeed = {0.1f, 0.2f, 0.3f};
		in.turnSpeed = {0.01f, 0.02f, 0.03f};
		in.carHealth = 800.0f;
		roundTripPkt(in, out, 68);
		CHECK_EQ(out.vehicleId, 11);
	}
	{
		proto::OutboundTrailerSync in{}, out{};
		in.trailerId = 44;
		in.position = {1.0f, 2.0f, 3.0f};
		in.quat = {1.0f, 0.1f, 0.2f, 0.3f};
		in.moveSpeed = {0.5f, 0.0f, 0.0f};
		in.turnSpeed = {0.0f, 0.0f, 0.1f};
		roundTripPkt(in, out, 55);
		CHECK_EQ(out.trailerId, 44);
	}
	{
		proto::OutboundAimSync in{}, out{};
		in.camMode = 53;
		in.aimF1 = {0.0f, 1.0f, 0.0f};
		in.aimPos = {10.0f, 20.0f, 30.0f};
		in.aimZ = -0.125f;
		in.zoomWeaponState = static_cast<uint8_t>((2 << 6) | 15);
		in.aspectRatio = 85;
		roundTripPkt(in, out, 32);
		CHECK_EQ(out.zoomWeaponState, static_cast<uint8_t>((2 << 6) | 15));
	}
	{
		proto::OutboundBulletSync in{}, out{};
		in.targetType = 1;
		in.targetId = 9;
		in.origin = {1.0f, 2.0f, 3.0f};
		in.pos = {4.0f, 5.0f, 6.0f};
		in.offset = {0.1f, 0.2f, 0.3f};
		in.weaponId = 31;
		roundTripPkt(in, out, 41);
		CHECK_EQ(out.weaponId, 31);
	}
	{
		proto::OutboundSpectatorSync in{}, out{};
		in.lrAnalog = 128;
		in.udAnalog = 0;
		in.keys = 4;
		in.position = {50.0f, 60.0f, 70.0f};
		roundTripPkt(in, out, 19);
		CHECK_EQ(out.keys, 4);
	}
	{
		proto::OutboundStatsUpdate in{12345, 2000}, out{};
		roundTripPkt(in, out, 9);
		CHECK_EQ(out.money, 12345);
		CHECK_EQ(out.drunkLevel, 2000);
	}
	{
		proto::OutboundWeaponsUpdate in{}, out{};
		in.targetPlayerId = 0xFFFF;
		in.targetActorId = 0xFFFF;
		in.slots = {{2, 24, 70}, {5, 31, 150}};
		roundTripPkt(in, out, 13);
		CHECK_EQ(out.slots.size(), 2u);
		CHECK_EQ(out.slots[0].slot, 2);
		CHECK_EQ(out.slots[0].weapon, 24);
		CHECK_EQ(out.slots[0].ammo, 70);
		CHECK_EQ(out.slots[1].slot, 5);
		CHECK_EQ(out.slots[1].weapon, 31);
		CHECK_EQ(out.slots[1].ammo, 150);
	}
}

static void testGpci()
{
	// Legacy constant validates against open.mp divisibility-by-1001 rule
	std::string legacySerial;
	CHECK(proto::validateGpci("39FB2DEEDB49ACFB8D4EECE6953D2507988CCCF4410", &legacySerial));
	CHECK(legacySerial == "ED40ED0E8089CC44C08EE9580F4C8C44EE8EE990");

	// Per-device seeds yield deterministic, distinct, open.mp-valid GPCIs
	const std::string g1 = proto::generateGpci("device-seed-alpha-0001");
	const std::string g1Again = proto::generateGpci("device-seed-alpha-0001");
	const std::string g2 = proto::generateGpci("device-seed-beta-0002");
	CHECK(g1 == g1Again);
	CHECK(g1 != g2);
	CHECK(g1.size() <= 43u);
	CHECK(g2.size() <= 43u);

	std::string s1, s2;
	CHECK(proto::validateGpci(g1, &s1));
	CHECK(proto::validateGpci(g2, &s2));
	CHECK(s1 != s2);
	CHECK(s1.size() > 0u && s1.size() < 50u);
	CHECK(s2.size() > 0u && s2.size() < 50u);

	// Tampering any hex digit breaks divisibility by 1001
	std::string bad = g1;
	bad.back() = (bad.back() == '0') ? '1' : '0';
	CHECK(!proto::validateGpci(bad));
}

int main()
{
	RUN(testOutboundRpcs);
	RUN(testOutboundPackets);
	RUN(testGpci);
	return check::finish();
}

#include "check.h"

#include "Net/Proto/RpcClass.h"
#include "Net/Proto/RpcCore.h"
#include "Net/Proto/RpcDecode.h"

#include <RakNet/BitStream.h>

#include <cstdint>

template <typename Packet>
static Packet roundTrip(const Packet& in, proto::ProtoVersion v = proto::ProtoVersion::V037)
{
	RakNet::BitStream bs;
	proto::Writer w(bs);
	Packet copy = in;
	CHECK(copy.serialize(w, v));
	CHECK(w.ok());

	const int totalBits = bs.GetNumberOfBitsUsed();
	proto::Reader r(bs.GetData(), static_cast<uint32_t>(totalBits));
	Packet out{};
	CHECK(out.serialize(r, v));
	CHECK(r.ok());
	CHECK_EQ(r.unreadBits(), 0);

	const auto dec = proto::decodeInboundRpc(Packet::kRpcId, bs.GetData(), static_cast<uint32_t>(totalBits), v);
	CHECK(dec.status == proto::DecodeStatus::kOk);
	CHECK_EQ(dec.unreadBits, 0);
	return out;
}

template <typename Packet>
static void expectTruncatedFails(const Packet& in, int dropBits, proto::ProtoVersion v = proto::ProtoVersion::V037)
{
	RakNet::BitStream bs;
	proto::Writer w(bs);
	Packet copy = in;
	CHECK(copy.serialize(w, v));
	const int totalBits = bs.GetNumberOfBitsUsed();
	CHECK(totalBits >= dropBits);

	proto::Reader r(bs.GetData(), static_cast<uint32_t>(totalBits - dropBits));
	Packet out{};
	CHECK(!out.serialize(r, v));
	CHECK(!r.ok());
}

static void testWorldPlayer()
{
	{
		proto::WorldPlayerAdd in{};
		in.playerId = 17;
		in.team = 2;
		in.skin = 294;
		in.customSkin = 20001;
		in.position = {123.5f, -456.25f, 18.75f};
		in.rotation = 90.5f;
		in.color = 0x112233FFu;
		in.fightingStyle = 6;
		for (uint16_t i = 0; i < 11; ++i) in.skillLevels[i] = static_cast<uint16_t>(100 + i * 50);

		const auto out37 = roundTrip(in, proto::ProtoVersion::V037);
		CHECK_EQ(out37.playerId, 17);
		CHECK_EQ(out37.team, 2);
		CHECK_EQ(out37.skin, 294);
		CHECK_EQ(out37.customSkin, 0);
		CHECK_NEAR(out37.position.x, 123.5, 0.0);
		CHECK_NEAR(out37.position.y, -456.25, 0.0);
		CHECK_NEAR(out37.position.z, 18.75, 0.0);
		CHECK_NEAR(out37.rotation, 90.5, 0.0);
		CHECK_EQ(out37.color, 0x112233FFu);
		CHECK_EQ(out37.fightingStyle, 6);
		for (size_t i = 0; i < 11; ++i) CHECK_EQ(out37.skillLevels[i], in.skillLevels[i]);

		const auto outDL = roundTrip(in, proto::ProtoVersion::V03DL);
		CHECK_EQ(outDL.customSkin, 20001);
		for (size_t i = 0; i < 11; ++i) CHECK_EQ(outDL.skillLevels[i], in.skillLevels[i]);

		// Short packet without optional 11 skill levels (22 bytes = 176 bits).
		RakNet::BitStream bs;
		proto::Writer w(bs);
		CHECK(w.u16(17) && w.u8(2) && w.i32(294) && w.vec3(in.position) &&
		      w.f32(90.5f) && w.u32(0x112233FFu) && w.u8(6));
		proto::Reader r(bs.GetData(), static_cast<uint32_t>(bs.GetNumberOfBitsUsed()));
		proto::WorldPlayerAdd shortOut{};
		CHECK(shortOut.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		for (auto lvl : shortOut.skillLevels) CHECK_EQ(lvl, 0);

		// Truncating inside mandatory header (drop 176 skill bits + 8 bits) must fail.
		expectTruncatedFails(in, 184, proto::ProtoVersion::V037);
	}
	{
		proto::WorldPlayerDeath in{};
		in.playerId = 42;
		const auto out = roundTrip(in);
		CHECK_EQ(out.playerId, 42);
		expectTruncatedFails(in, 8);
	}
	{
		proto::WorldPlayerRemove in{};
		in.playerId = 43;
		const auto out = roundTrip(in);
		CHECK_EQ(out.playerId, 43);
		expectTruncatedFails(in, 8);
	}
}

static void testAnimations()
{
	{
		proto::ApplyPlayerAnimation in{};
		in.playerId = 9;
		in.animLib = "PED";
		in.animName = "KD_left";
		in.delta = 4.1f;
		in.loop = true;
		in.lockX = false;
		in.lockY = true;
		in.freeze = true;
		in.time = 2500;
		const auto out = roundTrip(in);
		CHECK_EQ(out.playerId, 9);
		CHECK(out.animLib == "PED");
		CHECK(out.animName == "KD_left");
		CHECK_NEAR(out.delta, 4.1, 1e-6);
		CHECK(out.loop && !out.lockX && out.lockY && out.freeze);
		CHECK_EQ(out.time, 2500);
		expectTruncatedFails(in, 1);
	}
	{
		proto::ClearPlayerAnimations in{};
		in.playerId = 9;
		const auto out = roundTrip(in);
		CHECK_EQ(out.playerId, 9);
		expectTruncatedFails(in, 8);
	}
}

static void testCameraSpectateWidescreen()
{
	{
		proto::SetCameraPos in{};
		in.position = {10.0f, 20.0f, 30.0f};
		const auto out = roundTrip(in);
		CHECK_NEAR(out.position.x, 10.0, 0.0);
		CHECK_NEAR(out.position.y, 20.0, 0.0);
		CHECK_NEAR(out.position.z, 30.0, 0.0);
		expectTruncatedFails(in, 8);
	}
	{
		proto::SetCameraLookAt in{};
		in.position = {-1.5f, 2.5f, 3.5f};
		in.cutType = 2;
		const auto out = roundTrip(in);
		CHECK_NEAR(out.position.x, -1.5, 0.0);
		CHECK_EQ(out.cutType, 2);
		expectTruncatedFails(in, 8);
	}
	{
		proto::SetCameraBehindPlayer in{};
		roundTrip(in);
	}
	{
		proto::InterpolateCamera in{};
		in.setPos = true;
		in.from = {1.0f, 2.0f, 3.0f};
		in.to = {4.0f, 5.0f, 6.0f};
		in.time = 1500;
		in.cutType = 1;
		const auto out = roundTrip(in);
		CHECK(out.setPos);
		CHECK_NEAR(out.from.z, 3.0, 0.0);
		CHECK_NEAR(out.to.z, 6.0, 0.0);
		CHECK_EQ(out.time, 1500);
		CHECK_EQ(out.cutType, 1);
		expectTruncatedFails(in, 1);
	}
	{
		proto::AttachCameraToObject in{};
		in.objectId = 321;
		const auto out = roundTrip(in);
		CHECK_EQ(out.objectId, 321);
		expectTruncatedFails(in, 8);
	}
	{
		proto::TogglePlayerSpectating in{};
		in.toggle = 1;
		const auto out = roundTrip(in);
		CHECK_EQ(out.toggle, 1u);
		expectTruncatedFails(in, 8);
	}
	{
		proto::SpectatePlayer in{};
		in.playerId = 7;
		in.mode = 2;
		const auto out = roundTrip(in);
		CHECK_EQ(out.playerId, 7);
		CHECK_EQ(out.mode, 2);
		expectTruncatedFails(in, 8);
	}
	{
		proto::SpectateVehicle in{};
		in.vehicleId = 55;
		in.mode = 3;
		const auto out = roundTrip(in);
		CHECK_EQ(out.vehicleId, 55);
		CHECK_EQ(out.mode, 3);
		expectTruncatedFails(in, 8);
	}
	{
		proto::ToggleWidescreen in{};
		in.toggle = 1;
		const auto out = roundTrip(in);
		CHECK_EQ(out.toggle, 1);
		expectTruncatedFails(in, 8);
	}
}

static void testClassAndSpawn()
{
	proto::SpawnInfo sp{};
	sp.team = 3;
	sp.skin = 23;
	sp.customSkin = 25000;
	sp.unk = 7;
	sp.position = {1958.3783f, 1343.1572f, 15.3746f};
	sp.rotation = 270.0f;
	sp.spawnWeapons = {24, 31, 0};
	sp.spawnWeaponsAmmo = {70, 200, 0};

	{
		proto::RequestClassReply in{};
		in.selectable = 1;
		in.spawnInfo = sp;
		const auto out37 = roundTrip(in, proto::ProtoVersion::V037);
		CHECK_EQ(out37.selectable, 1);
		CHECK_EQ(out37.spawnInfo.team, 3);
		CHECK_EQ(out37.spawnInfo.skin, 23);
		CHECK_EQ(out37.spawnInfo.customSkin, 0);
		CHECK_EQ(out37.spawnInfo.unk, 7);
		CHECK_NEAR(out37.spawnInfo.position.x, 1958.3783, 1e-4);
		CHECK_NEAR(out37.spawnInfo.rotation, 270.0, 0.0);
		CHECK_EQ(out37.spawnInfo.spawnWeapons[0], 24);
		CHECK_EQ(out37.spawnInfo.spawnWeaponsAmmo[1], 200);

		const auto outDL = roundTrip(in, proto::ProtoVersion::V03DL);
		CHECK_EQ(outDL.spawnInfo.customSkin, 25000);
		CHECK_EQ(outDL.spawnInfo.spawnWeapons[1], 31);
		expectTruncatedFails(in, 8, proto::ProtoVersion::V037);
		expectTruncatedFails(in, 8, proto::ProtoVersion::V03DL);
	}
	{
		proto::SetSpawnInfo in{};
		in.spawnInfo = sp;
		const auto out37 = roundTrip(in, proto::ProtoVersion::V037);
		CHECK_EQ(out37.spawnInfo.skin, 23);
		CHECK_EQ(out37.spawnInfo.customSkin, 0);
		const auto outDL = roundTrip(in, proto::ProtoVersion::V03DL);
		CHECK_EQ(outDL.spawnInfo.customSkin, 25000);
		expectTruncatedFails(in, 8, proto::ProtoVersion::V037);
		expectTruncatedFails(in, 8, proto::ProtoVersion::V03DL);
	}
	{
		proto::RequestSpawnReply in{};
		in.outcome = 2;
		const auto out = roundTrip(in);
		CHECK_EQ(out.outcome, 2);

		// Legacy 1-byte SA-MP server payload must also decode with 0 unread bits.
		RakNet::BitStream bs1;
		uint8_t oneByte = 1;
		bs1.Write(oneByte);
		proto::Reader r1(bs1.GetData(), 8);
		proto::RequestSpawnReply out1{};
		CHECK(out1.serialize(r1, proto::ProtoVersion::V037));
		CHECK_EQ(r1.unreadBits(), 0);
		CHECK_EQ(out1.outcome, 1);

		// Empty payload must fail.
		proto::Reader r0(nullptr, 0);
		proto::RequestSpawnReply out0{};
		CHECK(!out0.serialize(r0, proto::ProtoVersion::V037));
	}
	{
		// ForceClassSelection (RPC 74, T10): empty payload
		CHECK(proto::decodeInboundRpc(proto::ForceClassSelection::kRpcId, nullptr, 0, proto::ProtoVersion::V037).status ==
			proto::DecodeStatus::kOk);
	}
	{
		// EnableCameraTarget (RPC 170, T16): 1-bit bool
		proto::EnableCameraTarget in{true};
		CHECK(roundTrip(in).enable);
		proto::EnableCameraTarget inOff{false};
		CHECK(!roundTrip(inOff).enable);
		proto::Reader r0(nullptr, 0);
		proto::EnableCameraTarget out0{};
		CHECK(!out0.serialize(r0, proto::ProtoVersion::V037));
	}
	{
		// SetPlayerShopName (RPC 33, T25): fixed 32-byte string
		proto::SetPlayerShopName in{"FDPIZA"};
		const auto out = roundTrip(in);
		CHECK(std::string(out.shopName.c_str()) == "FDPIZA");
		expectTruncatedFails(in, 8);
	}
	{
		// PlayCrimeReport (RPC 112, T25)
		proto::PlayCrimeReport in{7, 1, 411, 3, 16, {100.5f, -200.25f, 15.0f}};
		const auto out = roundTrip(in);
		CHECK_EQ(out.suspectId, 7);
		CHECK_EQ(out.inVehicle, 1u);
		CHECK_EQ(out.vehicleModel, 411u);
		CHECK_EQ(out.vehicleColor, 3u);
		CHECK_EQ(out.crimeId, 16u);
		CHECK_NEAR(out.position.x, 100.5, 0.0);
		CHECK_NEAR(out.position.y, -200.25, 0.0);
		CHECK_NEAR(out.position.z, 15.0, 0.0);
		expectTruncatedFails(in, 8);
	}
}

int main()
{
	RUN(testWorldPlayer);
	RUN(testAnimations);
	RUN(testCameraSpectateWidescreen);
	RUN(testClassAndSpawn);
	return check::finish();
}

// Oracle tests for F02 Batch 3: official open.mp write() -> client proto::* serialize().
#include "check.h"

#include <sdk.hpp>
#include <bitstream.hpp>
#include <netcode.hpp>

#include "Net/Proto/RpcClass.h"
#include "Net/Proto/RpcCore.h"

#include <cstdint>

static proto::Reader makeReader(NetworkBitStream& bs)
{
	return proto::Reader(bs.GetData(), static_cast<uint32_t>(bs.GetNumberOfBitsUsed()));
}

static void oracleWorldPlayer()
{
	for (bool isDL : {false, true}) {
		const auto ver = isDL ? proto::ProtoVersion::V03DL : proto::ProtoVersion::V037;
		NetCode::RPC::PlayerStreamIn omp(isDL);
		omp.PlayerID = 12;
		omp.Team = 4;
		omp.Skin = 299;
		omp.CustomSkin = 20055;
		omp.Pos = Vector3(10.5f, -20.25f, 30.75f);
		omp.Angle = 135.0f;
		omp.Col = Colour(0xAA, 0xBB, 0xCC, 0xFF);
		omp.FightingStyle = 5;
		for (size_t i = 0; i < omp.SkillLevel.size(); ++i) {
			omp.SkillLevel[i] = static_cast<uint16_t>(200 + i * 70);
		}
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::WorldPlayerAdd p{};
		CHECK(p.serialize(r, ver));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.playerId, 12);
		CHECK_EQ(p.team, 4);
		CHECK_EQ(p.skin, 299);
		if (isDL) CHECK_EQ(p.customSkin, 20055);
		CHECK_NEAR(p.position.x, 10.5, 0.0);
		CHECK_NEAR(p.position.y, -20.25, 0.0);
		CHECK_NEAR(p.position.z, 30.75, 0.0);
		CHECK_NEAR(p.rotation, 135.0, 0.0);
		CHECK_EQ(p.color, omp.Col.RGBA());
		CHECK_EQ(p.fightingStyle, 5);
		for (size_t i = 0; i < 11; ++i) CHECK_EQ(p.skillLevels[i], omp.SkillLevel[i]);
	}
	{
		NetCode::RPC::PlayerDeath omp;
		omp.PlayerID = 12;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::WorldPlayerDeath p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.playerId, 12);
	}
	{
		NetCode::RPC::PlayerStreamOut omp;
		omp.PlayerID = 13;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::WorldPlayerRemove p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.playerId, 13);
	}
}

static void oracleAnimations()
{
	{
		AnimationData anim(4.1f, true, false, true, true, 1200, "PED", "KD_left");
		NetCode::RPC::ApplyPlayerAnimation omp(anim);
		omp.PlayerID = 3;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::ApplyPlayerAnimation p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.playerId, 3);
		CHECK(p.animLib == "PED");
		CHECK(p.animName == "KD_left");
		CHECK_NEAR(p.delta, 4.1, 1e-6);
		CHECK(p.loop && !p.lockX && p.lockY && p.freeze);
		CHECK_EQ(p.time, 1200);
	}
	{
		NetCode::RPC::ClearPlayerTasks omp;
		omp.PlayerID = 3;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::ClearPlayerAnimations p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.playerId, 3);
	}
}

static void oracleCameraSpectateWidescreen()
{
	{
		NetCode::RPC::SetPlayerCameraPosition omp;
		omp.Pos = Vector3(1.0f, 2.0f, 3.0f);
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::SetCameraPos p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_NEAR(p.position.x, 1.0, 0.0);
		CHECK_NEAR(p.position.y, 2.0, 0.0);
		CHECK_NEAR(p.position.z, 3.0, 0.0);
	}
	{
		NetCode::RPC::SetPlayerCameraLookAt omp;
		omp.Pos = Vector3(4.0f, 5.0f, 6.0f);
		omp.CutType = 1;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::SetCameraLookAt p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_NEAR(p.position.z, 6.0, 0.0);
		CHECK_EQ(p.cutType, 1);
	}
	{
		NetCode::RPC::SetPlayerCameraBehindPlayer omp;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::SetCameraBehindPlayer p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
	}
	{
		NetCode::RPC::InterpolateCamera omp;
		omp.PosSet = true;
		omp.From = Vector3(10.0f, 20.0f, 30.0f);
		omp.To = Vector3(40.0f, 50.0f, 60.0f);
		omp.Time = 2000;
		omp.Cut = 1;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::InterpolateCamera p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK(p.setPos);
		CHECK_NEAR(p.from.x, 10.0, 0.0);
		CHECK_NEAR(p.to.z, 60.0, 0.0);
		CHECK_EQ(p.time, 2000);
		CHECK_EQ(p.cutType, 1);
	}
	{
		NetCode::RPC::AttachCameraToObject omp;
		omp.ObjectID = 99;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::AttachCameraToObject p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.objectId, 99);
	}
	{
		NetCode::RPC::TogglePlayerSpectating omp;
		omp.Enable = true;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::TogglePlayerSpectating p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.toggle, 1u);
	}
	{
		NetCode::RPC::PlayerSpectatePlayer omp;
		omp.PlayerID = 7;
		omp.SpecCamMode = PlayerSpectateMode_Fixed;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::SpectatePlayer p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.playerId, 7);
		CHECK_EQ(p.mode, static_cast<uint8_t>(PlayerSpectateMode_Fixed));
	}
	{
		NetCode::RPC::PlayerSpectateVehicle omp;
		omp.VehicleID = 44;
		omp.SpecCamMode = PlayerSpectateMode_Side;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::SpectateVehicle p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.vehicleId, 44);
		CHECK_EQ(p.mode, static_cast<uint8_t>(PlayerSpectateMode_Side));
	}
	{
		NetCode::RPC::ToggleWidescreen omp;
		omp.enable = true;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::ToggleWidescreen p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.toggle, 1);
	}
}

static void oracleClassAndSpawn()
{
	for (bool isDL : {false, true}) {
		const auto ver = isDL ? proto::ProtoVersion::V03DL : proto::ProtoVersion::V037;
		{
			NetCode::RPC::PlayerRequestClassResponse omp;
			omp.Selectable = 1;
			omp.TeamID = 2;
			omp.ModelID = 23;
			omp.CustomModelID = 25001;
			omp.Unknown1 = 0;
			omp.Spawn = Vector3(1958.5f, 1343.25f, 15.5f);
			omp.ZAngle = 270.0f;
			omp.Weapons = {24, 31, 0};
			omp.Ammos = {70, 150, 0};
			omp.IsDL = isDL;
			NetworkBitStream w;
			omp.write(w);
			auto r = makeReader(w);
			proto::RequestClassReply p{};
			CHECK(p.serialize(r, ver));
			CHECK_EQ(r.unreadBits(), 0);
			CHECK_EQ(p.selectable, 1);
			CHECK_EQ(p.spawnInfo.team, 2);
			CHECK_EQ(p.spawnInfo.skin, 23);
			if (isDL) CHECK_EQ(p.spawnInfo.customSkin, 25001);
			CHECK_NEAR(p.spawnInfo.position.x, 1958.5, 0.0);
			CHECK_NEAR(p.spawnInfo.rotation, 270.0, 0.0);
			CHECK_EQ(p.spawnInfo.spawnWeapons[0], 24);
			CHECK_EQ(p.spawnInfo.spawnWeaponsAmmo[1], 150);
		}
		{
			NetCode::RPC::SetSpawnInfo omp(isDL);
			omp.TeamID = 5;
			omp.ModelID = 280;
			omp.CustomModelID = 25002;
			omp.Unknown1 = 1;
			omp.Spawn = Vector3(-100.0f, 200.0f, 12.0f);
			omp.ZAngle = 90.0f;
			omp.Weapons = {22, 0, 0};
			omp.Ammos = {50, 0, 0};
			NetworkBitStream w;
			omp.write(w);
			auto r = makeReader(w);
			proto::SetSpawnInfo p{};
			CHECK(p.serialize(r, ver));
			CHECK_EQ(r.unreadBits(), 0);
			CHECK_EQ(p.spawnInfo.team, 5);
			CHECK_EQ(p.spawnInfo.skin, 280);
			if (isDL) CHECK_EQ(p.spawnInfo.customSkin, 25002);
			CHECK_EQ(p.spawnInfo.unk, 1);
			CHECK_NEAR(p.spawnInfo.position.x, -100.0, 0.0);
			CHECK_EQ(p.spawnInfo.spawnWeapons[0], 22);
			CHECK_EQ(p.spawnInfo.spawnWeaponsAmmo[0], 50);
		}
	}
	{
		NetCode::RPC::PlayerRequestSpawnResponse omp;
		omp.Allow = 1;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::RequestSpawnReply p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.outcome, 1);
	}
	{
		NetCode::RPC::ImmediatelySpawnPlayer omp;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::RequestSpawnReply p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.outcome, 2);
	}
	{
		NetCode::RPC::SetPlayerCameraTargeting omp;
		omp.Enabled = true;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::EnableCameraTarget p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK(p.enable);
	}
	{
		NetCode::RPC::SetPlayerShopName omp;
		omp.Name = "FDBURG";
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::SetPlayerShopName p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK(std::string(p.shopName.c_str()) == "FDBURG");
	}
	{
		NetCode::RPC::PlayCrimeReport omp;
		omp.Suspect = 42;
		omp.InVehicle = 1;
		omp.VehicleModel = 522;
		omp.VehicleColour = 6;
		omp.CrimeID = 14;
		omp.Position = Vector3(123.5f, -456.25f, 18.75f);
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::PlayCrimeReport p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.suspectId, 42);
		CHECK_EQ(p.inVehicle, 1u);
		CHECK_EQ(p.vehicleModel, 522u);
		CHECK_EQ(p.vehicleColor, 6u);
		CHECK_EQ(p.crimeId, 14u);
		CHECK_NEAR(p.position.x, 123.5, 0.0);
		CHECK_NEAR(p.position.y, -456.25, 0.0);
		CHECK_NEAR(p.position.z, 18.75, 0.0);
	}
}

int main()
{
	RUN(oracleWorldPlayer);
	RUN(oracleAnimations);
	RUN(oracleCameraSpectateWidescreen);
	RUN(oracleClassAndSpawn);
	return check::finish();
}

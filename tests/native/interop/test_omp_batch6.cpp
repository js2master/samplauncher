// Oracle tests for F02 Batch 6: official open.mp sync packet write() -> client proto::Inbound*Sync serialize().
#include "check.h"

#include <sdk.hpp>
#include <bitstream.hpp>
#include <netcode.hpp>

#include "Net/Proto/Sync.h"

#include <cstdint>

static proto::Reader makeReader(NetworkBitStream& bs)
{
	return proto::Reader(bs.GetData(), static_cast<uint32_t>(bs.GetNumberOfBitsUsed()));
}

static void oraclePlayerAndVehicleSync()
{
	{
		NetCode::Packet::PlayerFootSync omp{};
		omp.PlayerID = 5;
		omp.LeftRight = 128;
		omp.UpDown = 65408;
		omp.Keys = 0x0028;
		omp.Position = Vector3(100.5f, -200.25f, 15.75f);
		omp.Rotation = GTAQuat(1.0f, 0.0f, 0.0f, 0.0f);
		omp.HealthArmour = Vector2(100.0f, 49.0f);
		omp.WeaponAdditionalKey = 24;
		omp.SpecialAction = 10;
		omp.Velocity = Vector3(0.5f, -0.25f, 0.0f);
		omp.SurfingData.type = PlayerSurfingData::Type::Vehicle;
		omp.SurfingData.ID = 12;
		omp.SurfingData.offset = Vector3(0.1f, 0.2f, 1.5f);
		omp.AnimationID = 1189;
		omp.AnimationFlags = 0x1104;

		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::InboundPlayerSync p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.playerId, 5);
		CHECK(p.hasLR && p.lrAnalog == 128);
		CHECK(p.hasUD && p.udAnalog == 65408);
		CHECK_EQ(p.keys, 0x0028);
		CHECK_NEAR(p.position.x, 100.5, 0.0);
		CHECK_EQ(p.health, 100);
		CHECK_EQ(p.armour, 49);
		CHECK_EQ(p.currentWeapon, 24);
		CHECK_EQ(p.specialAction, 10);
		CHECK_NEAR(p.moveSpeed.x, 0.5, 1e-2);
		CHECK(p.hasSurfing && p.surfInfo == 12);
		CHECK(p.hasAnim);
		CHECK_EQ(p.animation & 0xFFFFu, 1189u);
		CHECK_EQ((p.animation >> 16) & 0xFFFFu, 0x1104u);
	}
	{
		NetCode::Packet::PlayerVehicleSync omp{};
		omp.PlayerID = 3;
		omp.VehicleID = 7;
		omp.LeftRight = 10;
		omp.UpDown = 20;
		omp.Keys = 8;
		omp.Rotation = GTAQuat(1.0f, 0.0f, 0.0f, 0.0f);
		omp.Position = Vector3(1.0f, 2.0f, 3.0f);
		omp.Velocity = Vector3(0.25f, 0.0f, 0.0f);
		omp.Health = 950.0f;
		omp.PlayerHealthArmour = Vector2(70.0f, 0.0f);
		omp.AdditionalKeyWeapon = 28;
		omp.Siren = 1;
		omp.LandingGear = 1;
		omp.HydraThrustAngle = 5000;
		omp.HasTrailer = true;
		omp.TrailerID = 9;

		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::InboundVehicleSync p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.playerId, 3);
		CHECK_EQ(p.vehicleId, 7);
		CHECK_EQ(p.carHealth, 950);
		CHECK_EQ(p.playerHealth, 70);
		CHECK_EQ(p.playerArmour, 0);
		CHECK(p.sirenOn && p.landingGearState);
		CHECK(p.hasTrainOrHydra && p.trainOrHydra == 5000u);
		CHECK(p.hasTrailer && p.trailerId == 9);
	}
}

static void oracleOtherSyncPackets()
{
	{
		NetCode::Packet::PlayerAimSync omp{};
		omp.PlayerID = 4;
		omp.CamMode = 53;
		omp.CamFrontVector = Vector3(0.0f, 1.0f, 0.0f);
		omp.CamPos = Vector3(10.0f, 20.0f, 30.0f);
		omp.AimZ = -0.125f;
		omp.ZoomWepState = static_cast<uint8_t>((2 << 6) | 15);
		omp.AspectRatio = 85;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::InboundAimSync p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.playerId, 4);
		CHECK_EQ(p.camMode, 53);
		CHECK_NEAR(p.aimZ, -0.125, 0.0);
		CHECK_EQ(p.zoomWeaponState, omp.ZoomWepState);
		CHECK_EQ(p.aspectRatio, 85);
	}
	{
		NetCode::Packet::PlayerBulletSync omp{};
		omp.PlayerID = 4;
		omp.HitType = 1;
		omp.HitID = 9;
		omp.Origin = Vector3(1.0f, 2.0f, 3.0f);
		omp.HitPos = Vector3(4.0f, 5.0f, 6.0f);
		omp.Offset = Vector3(0.1f, 0.2f, 0.3f);
		omp.WeaponID = 31;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::InboundBulletSync p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.playerId, 4);
		CHECK_EQ(p.targetType, 1);
		CHECK_EQ(p.targetId, 9);
		CHECK_EQ(p.weaponId, 31);
	}
	{
		NetCode::Packet::PlayerPassengerSync omp{};
		omp.PlayerID = 6;
		omp.VehicleID = 5;
		omp.SeatID = 2;
		omp.DriveBy = 1;
		omp.WeaponID = 29;
		omp.HealthArmour = Vector2(100.0f, 50.0f);
		omp.LeftRight = 1;
		omp.UpDown = 2;
		omp.Keys = 3;
		omp.Position = Vector3(10.0f, 20.0f, 30.0f);
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::InboundPassengerSync p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.playerId, 6);
		CHECK_EQ(p.vehicleId, 5);
		CHECK_EQ(p.playerHealth, 100);
		CHECK_EQ(p.playerArmour, 50);
		CHECK_EQ(p.keys, 3);
	}
	{
		NetCode::Packet::PlayerTrailerSync omp{};
		omp.PlayerID = 2;
		omp.VehicleID = 44;
		omp.Position = Vector3(1.0f, 2.0f, 3.0f);
		omp.Quat = Vector4(1.0f, 0.1f, 0.2f, 0.3f);
		omp.Velocity = Vector3(0.5f, 0.0f, 0.0f);
		omp.TurnVelocity = Vector3(0.0f, 0.0f, 0.1f);
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::InboundTrailerSync p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.playerId, 2);
		CHECK_EQ(p.trailerId, 44);
		CHECK_NEAR(p.quat.w, 1.0, 0.0);
		CHECK_NEAR(p.quat.z, 0.3, 1e-6);
	}
	{
		NetCode::Packet::PlayerUnoccupiedSync omp{};
		omp.PlayerID = 2;
		omp.VehicleID = 11;
		omp.SeatID = 1;
		omp.Roll = Vector3(1.0f, 0.0f, 0.0f);
		omp.Rotation = Vector3(0.0f, 1.0f, 0.0f);
		omp.Position = Vector3(10.0f, 20.0f, 15.0f);
		omp.Velocity = Vector3(0.1f, 0.2f, 0.3f);
		omp.AngularVelocity = Vector3(0.01f, 0.02f, 0.03f);
		omp.Health = 800.0f;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::InboundUnoccupiedSync p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.playerId, 2);
		CHECK_EQ(p.vehicleId, 11);
		CHECK_EQ(p.seatId, 1);
		CHECK_NEAR(p.carHealth, 800.0, 0.0);
	}
}

int main()
{
	RUN(oraclePlayerAndVehicleSync);
	RUN(oracleOtherSyncPackets);
	return check::finish();
}

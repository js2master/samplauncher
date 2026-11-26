// Oracle tests for F02 Batch 7: client proto::* Writer -> official open.mp read().
#include "check.h"

#include <sdk.hpp>
#include <bitstream.hpp>
#include <netcode.hpp>

#include "Net/Proto/RpcOutbound.h"
#include "Net/Proto/Sync.h"

#include <RakNet/BitStream.h>

#include <cstdint>
#include <string>

template <typename ClientMsg>
static NetworkBitStream writeClientRpc(const ClientMsg& msg, proto::ProtoVersion ver = proto::ProtoVersion::V037)
{
	RakNet::BitStream bs;
	proto::Writer w(bs);
	ClientMsg copy = msg;
	CHECK(copy.serialize(w, ver));
	NetworkBitStream out(bs.GetData(), bs.GetNumberOfBytesUsed(), true);
	out.SetWriteOffset(bs.GetNumberOfBitsUsed());
	return out;
}

template <typename ClientPkt>
static NetworkBitStream writeClientPktSkipId(const ClientPkt& pkt, uint8_t expectedId)
{
	RakNet::BitStream bs;
	proto::Writer w(bs);
	ClientPkt copy = pkt;
	CHECK(copy.serialize(w, proto::ProtoVersion::V037));
	NetworkBitStream out(bs.GetData(), bs.GetNumberOfBytesUsed(), true);
	out.SetWriteOffset(bs.GetNumberOfBitsUsed());
	uint8_t id = 0;
	CHECK(out.readUINT8(id));
	CHECK_EQ(id, expectedId);
	return out;
}

static void oracleOutboundCoreRpcs()
{
	// ClientJoin 0.3.7 and 0.3.DL
	{
		proto::ClientJoin req{};
		req.version = 4057;
		req.mod = 1;
		req.nickname = "Tester";
		req.challengeResponse = 0xCAFEBABEu;
		req.authKey = "0123456789ABCDEF";
		req.clientVersion = "0.3.7";
		auto bs = writeClientRpc(req, proto::ProtoVersion::V037);
		NetCode::RPC::PlayerConnect omp{};
		CHECK(omp.read(bs));
		CHECK_EQ(omp.VersionNumber, 4057u);
		CHECK_EQ(omp.Modded, 1);
		CHECK(std::string(omp.Name.data(), omp.Name.length()) == "Tester");
		CHECK_EQ(omp.ChallengeResponse, 0xCAFEBABEu);
		CHECK(std::string(omp.Key.data(), omp.Key.length()) == "0123456789ABCDEF");
		CHECK(std::string(omp.VersionString.data(), omp.VersionString.length()) == "0.3.7");
		CHECK(!omp.IsUsingOfficialClient);
		CHECK_EQ(bs.GetNumberOfUnreadBits(), 0);
	}
	{
		proto::ClientJoin req{};
		req.version = 4062;
		req.mod = 1;
		req.nickname = "TesterDL";
		req.challengeResponse = 0x11223344u;
		req.authKey = "FEDCBA9876543210";
		req.clientVersion = "0.3.DL-R1";
		auto bs = writeClientRpc(req, proto::ProtoVersion::V03DL);
		NetCode::RPC::PlayerConnect omp{};
		CHECK(omp.read(bs));
		CHECK(omp.IsUsingOfficialClient);
		CHECK_EQ(omp.OmpVersion, 0x11223344u);
		CHECK_EQ(bs.GetNumberOfUnreadBits(), 0);
	}
	{
		proto::ClientCheckReply req{0x48, 0x12345678u, 0xAB};
		auto bs = writeClientRpc(req);
		NetCode::RPC::ClientCheck omp{};
		CHECK(omp.read(bs));
		CHECK_EQ(omp.Type, 0x48);
		CHECK_EQ(omp.Address, 0x12345678);
		CHECK_EQ(omp.Results, 0xAB);
		CHECK_EQ(bs.GetNumberOfUnreadBits(), 0);
	}
	{
		proto::DialogResponse req{100, 1, 2, "myinput"};
		auto bs = writeClientRpc(req);
		NetCode::RPC::OnPlayerDialogResponse omp{};
		CHECK(omp.read(bs));
		CHECK_EQ(omp.ID, 100);
		CHECK_EQ(omp.Response, 1);
		CHECK_EQ(omp.ListItem, 2);
		CHECK(std::string(omp.Text.data(), omp.Text.length()) == "myinput");
		CHECK_EQ(bs.GetNumberOfUnreadBits(), 0);
	}
	{
		proto::ChatReq req{"hello omp"};
		auto bs = writeClientRpc(req);
		NetCode::RPC::PlayerRequestChatMessage omp{};
		CHECK(omp.read(bs));
		CHECK(std::string(omp.message.data(), omp.message.length()) == "hello omp");
		CHECK_EQ(bs.GetNumberOfUnreadBits(), 0);
	}
	{
		proto::ServerCommandReq req{"/smoke"};
		auto bs = writeClientRpc(req);
		NetCode::RPC::PlayerRequestCommandMessage omp{};
		CHECK(omp.read(bs));
		CHECK(std::string(omp.message.data(), omp.message.length()) == "/smoke");
		CHECK_EQ(bs.GetNumberOfUnreadBits(), 0);
	}
	{
		proto::RequestClassReq req{5};
		auto bs = writeClientRpc(req);
		NetCode::RPC::PlayerRequestClass omp{};
		CHECK(omp.read(bs));
		CHECK_EQ(omp.Classid, 5);
	}
	{
		proto::RequestSpawnReq req{};
		auto bs = writeClientRpc(req);
		NetCode::RPC::PlayerRequestSpawn omp{};
		CHECK(omp.read(bs));
		CHECK_EQ(bs.GetNumberOfUnreadBits(), 0);
	}
	{
		proto::SpawnReq req{};
		auto bs = writeClientRpc(req);
		NetCode::RPC::PlayerSpawn omp{};
		CHECK(omp.read(bs));
		CHECK_EQ(bs.GetNumberOfUnreadBits(), 0);
	}
	{
		proto::DeathReq req{54, 7};
		auto bs = writeClientRpc(req);
		NetCode::RPC::OnPlayerDeath omp{};
		CHECK(omp.read(bs));
		CHECK_EQ(omp.Reason, 54);
		CHECK_EQ(omp.KillerID, 7);
		CHECK_EQ(bs.GetNumberOfUnreadBits(), 0);
	}
	{
		proto::SetInteriorReq req{3};
		auto bs = writeClientRpc(req);
		NetCode::RPC::OnPlayerInteriorChange omp{};
		CHECK(omp.read(bs));
		CHECK_EQ(omp.Interior, 3u);
		CHECK_EQ(bs.GetNumberOfUnreadBits(), 0);
	}
}

static void oracleOutboundWorldRpcs()
{
	{
		proto::EnterVehicleReq req{42, 1};
		auto bs = writeClientRpc(req);
		NetCode::RPC::OnPlayerEnterVehicle omp{};
		CHECK(omp.read(bs));
		CHECK_EQ(omp.VehicleID, 42);
		CHECK_EQ(omp.Passenger, 1);
		CHECK_EQ(bs.GetNumberOfUnreadBits(), 0);
	}
	{
		proto::ExitVehicleReq req{42};
		auto bs = writeClientRpc(req);
		NetCode::RPC::OnPlayerExitVehicle omp{};
		CHECK(omp.read(bs));
		CHECK_EQ(omp.VehicleID, 42);
		CHECK_EQ(bs.GetNumberOfUnreadBits(), 0);
	}
	{
		proto::DamageVehicleReq req{42, 0x11223344u, 0x55667788u, 0x0Fu, 0x05u};
		auto bs = writeClientRpc(req);
		NetCode::RPC::SetVehicleDamageStatus omp{};
		CHECK(omp.read(bs));
		CHECK_EQ(omp.VehicleID, 42);
		CHECK_EQ(omp.PanelStatus, 0x11223344u);
		CHECK_EQ(omp.DoorStatus, 0x55667788u);
		CHECK_EQ(omp.LightStatus, 0x0Fu);
		CHECK_EQ(omp.TyreStatus, 0x05u);
		CHECK_EQ(bs.GetNumberOfUnreadBits(), 0);
	}
	{
		proto::VehicleDestroyedReq req{19};
		auto bs = writeClientRpc(req);
		NetCode::RPC::VehicleDeath omp{};
		CHECK(omp.read(bs));
		CHECK_EQ(omp.VehicleID, 19);
		CHECK_EQ(bs.GetNumberOfUnreadBits(), 0);
	}
	{
		proto::PickedUpPickupReq req{77};
		auto bs = writeClientRpc(req);
		NetCode::RPC::OnPlayerPickUpPickup omp{};
		CHECK(omp.read(bs));
		CHECK_EQ(omp.PickupID, 77);
		CHECK_EQ(bs.GetNumberOfUnreadBits(), 0);
	}
	{
		proto::ClickTextDrawReq req{2050};
		auto bs = writeClientRpc(req);
		NetCode::RPC::OnPlayerSelectTextDraw omp{};
		CHECK(omp.read(bs));
		CHECK(!omp.Invalid);
		CHECK(omp.PlayerTextDraw);
		CHECK_EQ(omp.TextDrawID, 2050 - GLOBAL_TEXTDRAW_POOL_SIZE);
		CHECK_EQ(bs.GetNumberOfUnreadBits(), 0);
	}
	{
		proto::ClickPlayerReq req{5, 0};
		auto bs = writeClientRpc(req);
		NetCode::RPC::OnPlayerClickPlayer omp{};
		CHECK(omp.read(bs));
		CHECK_EQ(omp.PlayerID, 5);
		CHECK_EQ(omp.Source, 0);
		CHECK_EQ(bs.GetNumberOfUnreadBits(), 0);
	}
	{
		proto::MapMarkerReq req{{10.5f, -20.25f, 30.75f}};
		auto bs = writeClientRpc(req);
		NetCode::RPC::OnPlayerClickMap omp{};
		CHECK(omp.read(bs));
		CHECK_NEAR(omp.Pos.x, 10.5, 0.0);
		CHECK_NEAR(omp.Pos.y, -20.25, 0.0);
		CHECK_NEAR(omp.Pos.z, 30.75, 0.0);
		CHECK_EQ(bs.GetNumberOfUnreadBits(), 0);
	}
	{
		proto::GiveTakeDamageReq req{true, 9, 25.5f, 24, 3};
		auto bs = writeClientRpc(req);
		NetCode::RPC::OnPlayerGiveTakeDamage omp{};
		CHECK(omp.read(bs));
		CHECK(omp.Taking);
		CHECK_EQ(omp.PlayerID, 9);
		CHECK_NEAR(omp.Damage, 25.5, 0.0);
		CHECK_EQ(omp.WeaponID, 24u);
		CHECK_EQ(omp.Bodypart, 3u);
		CHECK_EQ(bs.GetNumberOfUnreadBits(), 0);
	}
	{
		proto::GiveDamageActorReq req{false, 12, 15.0f, 31, 9};
		auto bs = writeClientRpc(req);
		NetCode::RPC::OnPlayerDamageActor omp{};
		CHECK(omp.read(bs));
		CHECK(!omp.Unknown);
		CHECK_EQ(omp.ActorID, 12);
		CHECK_NEAR(omp.Damage, 15.0, 0.0);
		CHECK_EQ(omp.WeaponID, 31u);
		CHECK_EQ(omp.Bodypart, 9u);
		CHECK_EQ(bs.GetNumberOfUnreadBits(), 0);
	}
	{
		proto::EditObjectReply req{true, 88, 1, {1.0f, 2.0f, 3.0f}, {10.0f, 20.0f, 30.0f}};
		auto bs = writeClientRpc(req);
		NetCode::RPC::OnPlayerEditObject omp{};
		CHECK(omp.read(bs));
		CHECK(omp.PlayerObject);
		CHECK_EQ(omp.ObjectID, 88);
		CHECK_EQ(omp.Response, 1);
		CHECK_NEAR(omp.Offset.x, 1.0, 0.0);
		CHECK_NEAR(omp.Rotation.z, 30.0, 0.0);
		CHECK_EQ(bs.GetNumberOfUnreadBits(), 0);
	}
	{
		proto::EditAttachedObjectReply req{1, 2, 18912, 2, {0.1f, 0.2f, 0.3f}, {1.0f, 2.0f, 3.0f}, {1.0f, 1.0f, 1.0f}, 0xFF112233u, 0xFF445566u};
		auto bs = writeClientRpc(req);
		NetCode::RPC::OnPlayerEditAttachedObject omp{};
		CHECK(omp.read(bs));
		CHECK_EQ(omp.Response, 1u);
		CHECK_EQ(omp.Index, 2u);
		CHECK_EQ(omp.AttachmentData.model, 18912);
		CHECK_EQ(omp.AttachmentData.bone, 2);
		CHECK_EQ(omp.AttachmentData.colour1.ARGB(), 0xFF112233u);
		CHECK_EQ(omp.AttachmentData.colour2.ARGB(), 0xFF445566u);
		CHECK_EQ(bs.GetNumberOfUnreadBits(), 0);
	}
	{
		proto::SelectObjectReply req{0, 42, 1271, {12.5f, -34.25f, 5.75f}};
		auto bs = writeClientRpc(req);
		NetCode::RPC::OnPlayerSelectObject omp{};
		CHECK(omp.read(bs));
		CHECK_EQ(omp.SelectType, 0);
		CHECK_EQ(omp.ObjectID, 42);
		CHECK_EQ(omp.Model, 1271);
		CHECK_NEAR(omp.Position.x, 12.5, 0.0);
		CHECK_NEAR(omp.Position.y, -34.25, 0.0);
		CHECK_NEAR(omp.Position.z, 5.75, 0.0);
		CHECK_EQ(bs.GetNumberOfUnreadBits(), 0);
	}
	{
		proto::MenuSelectReq req{3};
		auto bs = writeClientRpc(req);
		NetCode::RPC::OnPlayerSelectedMenuRow omp{};
		CHECK(omp.read(bs));
		CHECK_EQ(omp.MenuRow, 3);
		CHECK_EQ(bs.GetNumberOfUnreadBits(), 0);
	}
	{
		proto::MenuQuitReq req{};
		auto bs = writeClientRpc(req);
		NetCode::RPC::OnPlayerExitedMenu omp{};
		CHECK(omp.read(bs));
		CHECK_EQ(bs.GetNumberOfUnreadBits(), 0);
	}
	{
		proto::CameraTargetReply req{12, 34, 5, 7};
		auto bs = writeClientRpc(req);
		NetCode::RPC::OnPlayerCameraTarget omp{};
		CHECK(omp.read(bs));
		CHECK_EQ(omp.TargetObjectID, 12);
		CHECK_EQ(omp.TargetVehicleID, 34);
		CHECK_EQ(omp.TargetPlayerID, 5);
		CHECK_EQ(omp.TargetActorID, 7);
		CHECK_EQ(bs.GetNumberOfUnreadBits(), 0);
	}
}

static void oracleOutboundSyncPackets()
{
	{
		proto::RconCommandPacket pkt{"players"};
		auto bs = writeClientPktSkipId(pkt, ID_RCON_COMMAND);
		NetCode::Packet::PlayerRconCommand omp{};
		CHECK(omp.read(bs));
		CHECK(std::string(omp.cmd.data(), omp.cmd.length()) == "players");
		CHECK_EQ(bs.GetNumberOfUnreadBits(), 0);
	}
	{
		proto::OutboundPlayerSync pkt{};
		pkt.lrAnalog = 128;
		pkt.udAnalog = 65408;
		pkt.keys = 0x0028;
		pkt.position = {100.5f, -200.25f, 15.75f};
		pkt.quat = {1.0f, 0.0f, 0.0f, 0.0f};
		pkt.health = 100;
		pkt.armour = 49;
		pkt.currentWeapon = 24;
		pkt.specialAction = 10;
		pkt.moveSpeed = {0.5f, -0.25f, 0.0f};
		pkt.surfOffsets = {0.1f, 0.2f, 1.5f};
		pkt.surfInfo = 12;
		pkt.animation = (0x1104u << 16) | 1189u;
		auto bs = writeClientPktSkipId(pkt, ID_PLAYER_SYNC);
		NetCode::Packet::PlayerFootSync omp{};
		CHECK(omp.read(bs));
		CHECK_EQ(omp.LeftRight, 128);
		CHECK_EQ(omp.UpDown, 65408);
		CHECK_EQ(omp.Keys, 0x0028);
		CHECK_NEAR(omp.Position.x, 100.5, 0.0);
		CHECK_NEAR(omp.HealthArmour.x, 100.0, 0.0);
		CHECK_NEAR(omp.HealthArmour.y, 49.0, 0.0);
		CHECK_EQ(omp.WeaponAdditionalKey, 24);
		CHECK_EQ(omp.SpecialAction, 10);
		CHECK(omp.SurfingData.type == PlayerSurfingData::Type::Vehicle);
		CHECK_EQ(omp.SurfingData.ID, 12);
		CHECK_EQ(omp.AnimationID, 1189);
		CHECK_EQ(omp.AnimationFlags, 0x1104);
		CHECK_EQ(bs.GetNumberOfUnreadBits(), 0);
	}
	{
		proto::OutboundVehicleSync pkt{};
		pkt.vehicleId = 7;
		pkt.lrAnalog = 10;
		pkt.udAnalog = 20;
		pkt.keys = 8;
		pkt.quat = {1.0f, 0.0f, 0.0f, 0.0f};
		pkt.position = {1.0f, 2.0f, 3.0f};
		pkt.moveSpeed = {0.25f, 0.0f, 0.0f};
		pkt.carHealth = 950.0f;
		pkt.playerHealth = 70;
		pkt.playerArmour = 20;
		pkt.currentWeapon = 28;
		pkt.sirenOn = 1;
		pkt.landingGearState = 1;
		pkt.trailerId = 9;
		pkt.trainOrHydra = 5000u;
		auto bs = writeClientPktSkipId(pkt, ID_VEHICLE_SYNC);
		NetCode::Packet::PlayerVehicleSync omp{};
		CHECK(omp.read(bs));
		CHECK_EQ(omp.VehicleID, 7);
		CHECK_NEAR(omp.Health, 950.0, 0.0);
		CHECK_NEAR(omp.PlayerHealthArmour.x, 70.0, 0.0);
		CHECK_NEAR(omp.PlayerHealthArmour.y, 20.0, 0.0);
		CHECK_EQ(omp.Siren, 1);
		CHECK_EQ(omp.LandingGear, 1);
		CHECK_EQ(omp.TrailerID, 9);
		CHECK_EQ(omp.HydraThrustAngle, 5000u);
		CHECK_EQ(bs.GetNumberOfUnreadBits(), 0);
	}
	{
		proto::OutboundPassengerSync pkt{};
		pkt.vehicleId = 5;
		pkt.seatFlags = 0x42;
		pkt.currentWeapon = 29;
		pkt.playerHealth = 100;
		pkt.playerArmour = 50;
		pkt.lrAnalog = 1;
		pkt.udAnalog = 2;
		pkt.keys = 3;
		pkt.position = {10.0f, 20.0f, 30.0f};
		auto bs = writeClientPktSkipId(pkt, ID_PASSENGER_SYNC);
		NetCode::Packet::PlayerPassengerSync omp{};
		CHECK(omp.read(bs));
		CHECK_EQ(omp.VehicleID, 5);
		CHECK_EQ(omp.SeatID, 2);
		CHECK_EQ(omp.DriveBy, 1);
		CHECK_EQ(omp.WeaponID, 29);
		CHECK_NEAR(omp.HealthArmour.x, 100.0, 0.0);
		CHECK_NEAR(omp.HealthArmour.y, 50.0, 0.0);
		CHECK_EQ(omp.Keys, 3);
		CHECK_EQ(bs.GetNumberOfUnreadBits(), 0);
	}
	{
		proto::OutboundUnoccupiedSync pkt{};
		pkt.vehicleId = 11;
		pkt.seatId = 1;
		pkt.roll = {1.0f, 0.0f, 0.0f};
		pkt.direction = {0.0f, 1.0f, 0.0f};
		pkt.position = {10.0f, 20.0f, 15.0f};
		pkt.moveSpeed = {0.1f, 0.2f, 0.3f};
		pkt.turnSpeed = {0.01f, 0.02f, 0.03f};
		pkt.carHealth = 800.0f;
		auto bs = writeClientPktSkipId(pkt, ID_UNOCCUPIED_SYNC);
		NetCode::Packet::PlayerUnoccupiedSync omp{};
		CHECK(omp.read(bs));
		CHECK_EQ(omp.VehicleID, 11);
		CHECK_EQ(omp.SeatID, 1);
		CHECK_NEAR(omp.Health, 800.0, 0.0);
		CHECK_EQ(bs.GetNumberOfUnreadBits(), 0);
	}
	{
		proto::OutboundTrailerSync pkt{};
		pkt.trailerId = 44;
		pkt.position = {1.0f, 2.0f, 3.0f};
		pkt.quat = {1.0f, 0.1f, 0.2f, 0.3f};
		pkt.moveSpeed = {0.5f, 0.0f, 0.0f};
		pkt.turnSpeed = {0.0f, 0.0f, 0.1f};
		auto bs = writeClientPktSkipId(pkt, ID_TRAILER_SYNC);
		NetCode::Packet::PlayerTrailerSync omp{};
		CHECK(omp.read(bs));
		CHECK_EQ(omp.VehicleID, 44);
		CHECK_NEAR(omp.Quat.x, 1.0, 0.0); // glm::vec4 memory order is x,y,z,w -> matches wire float[4]
		CHECK_EQ(bs.GetNumberOfUnreadBits(), 0);
	}
	{
		proto::OutboundAimSync pkt{};
		pkt.camMode = 53;
		pkt.aimF1 = {0.0f, 1.0f, 0.0f};
		pkt.aimPos = {10.0f, 20.0f, 30.0f};
		pkt.aimZ = -0.125f;
		pkt.zoomWeaponState = static_cast<uint8_t>((2 << 6) | 15);
		pkt.aspectRatio = 85;
		auto bs = writeClientPktSkipId(pkt, ID_AIM_SYNC);
		NetCode::Packet::PlayerAimSync omp{};
		CHECK(omp.read(bs));
		CHECK_EQ(omp.CamMode, 53);
		CHECK_NEAR(omp.AimZ, -0.125, 0.0);
		CHECK_EQ(omp.CamZoom, 15);
		CHECK_EQ(omp.WeaponState, 2);
		CHECK_EQ(omp.AspectRatio, 85);
		CHECK_EQ(bs.GetNumberOfUnreadBits(), 0);
	}
	{
		proto::OutboundBulletSync pkt{};
		pkt.targetType = 1;
		pkt.targetId = 9;
		pkt.origin = {1.0f, 2.0f, 3.0f};
		pkt.pos = {4.0f, 5.0f, 6.0f};
		pkt.offset = {0.1f, 0.2f, 0.3f};
		pkt.weaponId = 31;
		auto bs = writeClientPktSkipId(pkt, ID_BULLET_SYNC);
		NetCode::Packet::PlayerBulletSync omp{};
		CHECK(omp.read(bs));
		CHECK_EQ(omp.HitType, 1);
		CHECK_EQ(omp.HitID, 9);
		CHECK_EQ(omp.WeaponID, 31);
		CHECK_EQ(bs.GetNumberOfUnreadBits(), 0);
	}
	{
		proto::OutboundSpectatorSync pkt{};
		pkt.lrAnalog = 128;
		pkt.udAnalog = 0;
		pkt.keys = 4;
		pkt.position = {50.0f, 60.0f, 70.0f};
		auto bs = writeClientPktSkipId(pkt, ID_SPECTATOR_SYNC);
		NetCode::Packet::PlayerSpectatorSync omp{};
		CHECK(omp.read(bs));
		CHECK_EQ(omp.LeftRight, 128);
		CHECK_EQ(omp.Keys, 4);
		CHECK_NEAR(omp.Position.z, 70.0, 0.0);
		CHECK_EQ(bs.GetNumberOfUnreadBits(), 0);
	}
	{
		proto::OutboundStatsUpdate pkt{12345, 2000};
		auto bs = writeClientPktSkipId(pkt, ID_STATS_UPDATE);
		NetCode::Packet::PlayerStatsSync omp{};
		CHECK(omp.read(bs));
		CHECK_EQ(omp.Money, 12345);
		CHECK_EQ(omp.DrunkLevel, 2000);
		CHECK_EQ(bs.GetNumberOfUnreadBits(), 0);
	}
	{
		proto::OutboundWeaponsUpdate pkt{};
		pkt.targetPlayerId = 0xFFFF;
		pkt.targetActorId = 0xFFFF;
		pkt.slots = {{2, 24, 70}, {5, 31, 150}};
		auto bs = writeClientPktSkipId(pkt, ID_WEAPONS_UPDATE);
		NetCode::Packet::PlayerWeaponsUpdate omp{};
		CHECK(omp.read(bs));
		CHECK_EQ(omp.WeaponDataCount, 2);
		CHECK_EQ(omp.WeaponData[0].first, 2);
		CHECK_EQ(omp.WeaponData[0].second.id, 24);
		CHECK_EQ(omp.WeaponData[0].second.ammo, 70);
		CHECK_EQ(omp.WeaponData[1].first, 5);
		CHECK_EQ(omp.WeaponData[1].second.id, 31);
		CHECK_EQ(omp.WeaponData[1].second.ammo, 150);
		CHECK_EQ(bs.GetNumberOfUnreadBits(), 0);
	}
}

int main()
{
	RUN(oracleOutboundCoreRpcs);
	RUN(oracleOutboundWorldRpcs);
	RUN(oracleOutboundSyncPackets);
	return check::finish();
}

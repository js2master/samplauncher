// Oracle tests for F02 Batch 2: official open.mp write() -> client proto::* serialize().
#include "check.h"

#include <sdk.hpp>
#include <bitstream.hpp>
#include <netcode.hpp>
#include <custommodels.hpp>

#include "Net/Proto/RpcCore.h"
#include "Net/Proto/RpcOutbound.h"

#include <cstdint>
#include <string>

static proto::Reader makeReader(NetworkBitStream& bs)
{
	return proto::Reader(bs.GetData(), static_cast<uint32_t>(bs.GetNumberOfBitsUsed()));
}

static void oracleInitGame()
{
	NetCode::RPC::PlayerInit omp;
	omp.EnableZoneNames = true;
	omp.UsePlayerPedAnims = false;
	omp.AllowInteriorWeapons = true;
	omp.UseLimitGlobalChatRadius = false;
	omp.LimitGlobalChatRadius = 200.0f;
	omp.EnableStuntBonus = true;
	omp.SetNameTagDrawDistance = 70.0f;
	omp.DisableInteriorEnterExits = false;
	omp.DisableNameTagLOS = true;
	omp.ManualVehicleEngineAndLights = false;
	omp.SetSpawnInfoCount = 2;
	omp.PlayerID = 5;
	omp.ShowNameTags = true;
	omp.ShowPlayerMarkers = 1;
	omp.SetWorldTime = 12;
	omp.SetWeather = 10;
	omp.SetGravity = 0.008f;
	omp.LanMode = false;
	omp.SetDeathDropAmount = 0;
	omp.Instagib = false;
	omp.OnFootRate = 30;
	omp.InCarRate = 31;
	omp.WeaponRate = 32;
	omp.Multiplier = 10;
	omp.LagCompensation = 1;
	omp.ServerName = StringView("parity server");
	omp.VehicleModels.fill(0);
	omp.VehicleModels[0] = 1;
	omp.VehicleModels[211] = 9;
	omp.EnableVehicleFriendlyFire = true;

	NetworkBitStream w;
	omp.write(w);
	auto r = makeReader(w);
	proto::InitGame p{};
	CHECK(p.serialize(r, proto::ProtoVersion::V037));
	CHECK_EQ(r.unreadBits(), 0);
	CHECK(p.zoneNames && !p.useCJWalk && p.allowWeapons && !p.limitGlobalChatRadius && p.stuntBonus);
	CHECK_NEAR(p.globalChatRadius, 200.0, 0.0);
	CHECK_NEAR(p.nameTagDrawDistance, 70.0, 0.0);
	CHECK(!p.disableInteriorEnterExits && p.nameTagLOS && !p.manualVehicleEngineAndLight);
	CHECK_EQ(p.spawnsAvailable, 2);
	CHECK_EQ(p.myPlayerId, 5);
	CHECK(p.showNameTags && p.showPlayerMarkers == 1 && p.worldTimeHour == 12 && p.weather == 10);
	CHECK_NEAR(p.gravity, 0.008, 1e-7);
	CHECK(!p.lanMode && p.deathDropMoney == 0 && !p.instagib);
	CHECK_EQ(p.onFootSendRate, 30);
	CHECK_EQ(p.inCarSendRate, 31);
	CHECK_EQ(p.firingSendRate, 32);
	CHECK_EQ(p.sendMultiplier, 10);
	CHECK_EQ(p.lagCompensation, 1);
	CHECK(p.hostName == "parity server");
	CHECK_EQ(p.vehicleModels[0], 1);
	CHECK_EQ(p.vehicleModels[211], 9);
	CHECK_EQ(p.vehicleFriendlyFire, 1);
}

static void oracleSessionAndChat()
{
	{
		NetCode::RPC::PlayerJoin omp;
		omp.PlayerID = 4;
		omp.Col = Colour(0x11, 0x22, 0x33, 0xFF);
		omp.IsNPC = true;
		omp.Name = StringView("ParityNPC");
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::ServerJoin p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.playerId, 4);
		CHECK_EQ(p.markerColor, omp.Col.RGBA());
		CHECK_EQ(p.isNpc, 1);
		CHECK(p.name == "ParityNPC");
	}
	{
		NetCode::RPC::PlayerQuit omp;
		omp.PlayerID = 4;
		omp.Reason = 2;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::ServerQuit p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.playerId, 4);
		CHECK_EQ(p.reason, 2);
	}
	{
		NetCode::RPC::PlayerClose omp;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::GameModeRestart p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
	}
	{
		NetCode::RPC::PlayerChatMessage omp;
		omp.PlayerID = 2;
		omp.message = StringView("hi everyone");
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::ChatMessage p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.playerId, 2);
		CHECK(p.text == "hi everyone");
	}
	{
		NetCode::RPC::SendClientMessage omp;
		omp.Col = Colour(0xFF, 0xFF, 0xFF, 0xFF);
		omp.Message = StringView("[SMOKE] hello");
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::ClientMessage p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.color, omp.Col.RGBA());
		CHECK(p.message == "[SMOKE] hello");
	}
	{
		NetCode::RPC::SetPlayerChatBubble omp;
		omp.PlayerID = 3;
		omp.Col = Colour(0xAA, 0xBB, 0xCC, 0xFF);
		omp.DrawDistance = 25.0f;
		omp.ExpireTime = 4000;
		omp.Text = StringView("chat bubble");
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::ChatBubble p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.playerId, 3);
		CHECK_EQ(p.color, omp.Col.RGBA());
		CHECK_NEAR(p.drawDistance, 25.0, 0.0);
		CHECK_EQ(p.duration, 4000u);
		CHECK(p.text == "chat bubble");
	}
	{
		NetCode::RPC::SetPlayerWorldTime omp;
		omp.Time = Hours(17);
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::WorldTime p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.hour, 17);
	}
	{
		NetCode::RPC::SetPlayerWeather omp;
		omp.WeatherID = 8;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::Weather p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.weatherId, 8);
	}
	{
		NetCode::RPC::SetPlayerTime omp;
		omp.Hour = 22;
		omp.Minute = 15;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::SetTimeEx p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.hour, 22);
		CHECK_EQ(p.minute, 15);
	}
	{
		NetCode::RPC::TogglePlayerClock omp;
		omp.Toggle = true;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::ToggleClock p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.toggle, 1);
	}
	{
		NetCode::RPC::SendGameTimeUpdate omp;
		omp.Time = 666959421;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::TimerUpdate p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.time, 666959421u);
	}
	{
		NetCode::RPC::ClientCheck omp;
		omp.Type = 0x48;
		omp.Address = 0x10203040;
		omp.Offset = 4;
		omp.Count = 16;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::ClientCheckRequest p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V03DL));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.type, 0x48);
		CHECK_EQ(p.argument, 0x10203040u);
		CHECK_EQ(p.offset, 4);
		CHECK_EQ(p.length, 16);
	}
}

static void oracleSetPlayerSkinBothModes()
{
	NetCode::RPC::SetPlayerSkin omp;
	omp.PlayerID = 7;
	omp.Skin = 123;
	omp.CustomSkin = 20001;

	omp.isDL = false;
	NetworkBitStream w037;
	omp.write(w037);
	{
		auto r = makeReader(w037);
		proto::SetPlayerSkin p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.playerId, 7u);
		CHECK_EQ(p.skinId, 123u);
	}

	omp.isDL = true;
	NetworkBitStream wDL;
	omp.write(wDL);
	{
		auto r = makeReader(wDL);
		proto::SetPlayerSkin p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V03DL));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.playerId, 7u);
		CHECK_EQ(p.skinId, 123u);
		CHECK_EQ(p.customSkinId, 20001u);
	}
}

static void oraclePlayerProperties()
{
	{
		NetCode::RPC::SetPlayerSkillLevel omp;
		omp.PlayerID = 5;
		omp.SkillType = 2;
		omp.SkillLevel = 500;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::SetPlayerSkillLevel p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.playerId, 5);
		CHECK_EQ(p.skillType, 2u);
		CHECK_EQ(p.skillLevel, 500);
	}
	{
		NetCode::RPC::SetPlayerGravity omp;
		omp.Gravity = 0.004f;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::SetGravity p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_NEAR(p.gravity, 0.004, 1e-7);
	}
	{
		NetCode::RPC::SetPlayerPosition omp;
		omp.Pos = Vector3(385.0f, 2502.0f, 16.5f);
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::SetPlayerPos p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_NEAR(p.position.x, 385.0, 0.0);
		CHECK_NEAR(p.position.y, 2502.0, 0.0);
		CHECK_NEAR(p.position.z, 16.5, 0.0);
	}
	{
		NetCode::RPC::SetPlayerPositionFindZ omp;
		omp.Pos = Vector3(1.0f, 2.0f, 3.0f);
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::SetPlayerPosFindZ p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_NEAR(p.position.z, 3.0, 0.0);
	}
	{
		NetCode::RPC::SetPlayerFacingAngle omp;
		omp.Angle = 180.0f;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::SetPlayerFacingAngle p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_NEAR(p.angle, 180.0, 0.0);
	}
	{
		NetCode::RPC::SetPlayerHealth omp;
		omp.Health = 80.0f;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::SetPlayerHealth p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_NEAR(p.health, 80.0, 0.0);
	}
	{
		NetCode::RPC::SetPlayerArmour omp;
		omp.Armour = 50.0f;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::SetPlayerArmour p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_NEAR(p.armour, 50.0, 0.0);
	}
	{
		NetCode::RPC::SetPlayerColor omp;
		omp.PlayerID = 0;
		omp.Col = Colour(0x00, 0xFF, 0x00, 0xFF);
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::SetPlayerColor p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.playerId, 0);
		CHECK_EQ(p.color, omp.Col.RGBA());
	}
	{
		NetCode::RPC::SetPlayerTeam omp;
		omp.PlayerID = 2;
		omp.Team = 5;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::SetPlayerTeam p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.playerId, 2);
		CHECK_EQ(p.team, 5);
	}
	{
		NetCode::RPC::SetPlayerName omp;
		omp.PlayerID = 2;
		omp.Name = StringView("Renamed");
		omp.Success = 1;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::SetPlayerName p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.playerId, 2);
		CHECK(p.name == "Renamed");
		CHECK_EQ(p.success, 1);
	}
	{
		NetCode::RPC::SetPlayerInterior omp;
		omp.Interior = 3;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::SetPlayerInterior p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.interiorId, 3);
	}
	{
		NetCode::RPC::TogglePlayerControllable omp;
		omp.Enable = false;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::TogglePlayerControllable p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.controllable, 0);
	}
	{
		NetCode::RPC::SetWorldBounds omp;
		omp.coords = Vector4(1000.0f, -1000.0f, 2000.0f, -2000.0f);
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::SetWorldBounds p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_NEAR(p.bounds[0], 1000.0, 0.0);
		CHECK_NEAR(p.bounds[3], -2000.0, 0.0);
	}
	{
		NetCode::RPC::GivePlayerMoney omp;
		omp.Money = -250;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::GivePlayerMoney p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.money, -250);
	}
	{
		NetCode::RPC::ResetPlayerMoney omp;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::ResetPlayerMoney p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
	}
	{
		NetCode::RPC::SetPlayerWantedLevel omp;
		omp.Level = 3;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::SetPlayerWantedLevel p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.level, 3);
	}
	{
		NetCode::RPC::SetPlayerFightingStyle omp;
		omp.PlayerID = 0;
		omp.Style = 5;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::SetPlayerFightingStyle p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.playerId, 0);
		CHECK_EQ(p.style, 5);
	}
	{
		NetCode::RPC::SetPlayerVelocity omp;
		omp.Velocity = Vector3(0.1f, 0.2f, 0.3f);
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::SetPlayerVelocity p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_NEAR(p.velocity.z, 0.3, 1e-6);
	}
	{
		NetCode::RPC::SetPlayerSpecialAction omp;
		omp.Action = 10;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::SetPlayerSpecialAction p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.action, 10);
	}
	{
		NetCode::RPC::SetPlayerDrunkLevel omp;
		omp.Level = 5000;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::SetPlayerDrunkLevel p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.level, 5000u);
	}
	{
		NetCode::RPC::ResetPlayerWeapons omp;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::ResetPlayerWeapons p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
	}
	{
		NetCode::RPC::GivePlayerWeapon omp;
		omp.Weapon = 24;
		omp.Ammo = 70;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::GivePlayerWeapon p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.weapon, 24);
		CHECK_EQ(p.ammo, 70);
	}
	{
		NetCode::RPC::SetPlayerAmmo omp;
		omp.Weapon = 24;
		omp.Ammo = 35;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::SetPlayerAmmo p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.weapon, 24);
		CHECK_EQ(p.ammo, 35);
	}
	{
		NetCode::RPC::SetPlayerArmedWeapon omp;
		omp.Weapon = 24;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::SetArmedWeapon p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.weapon, 24u);
	}
	{
		NetCode::RPC::SendGameText omp;
		omp.Style = 3;
		omp.Time = 2000;
		omp.Text = StringView("~g~SMOKE");
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::ShowGameText p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.style, 3);
		CHECK_EQ(p.time, 2000);
		CHECK(p.text == "~g~SMOKE");
	}
	{
		NetCode::RPC::SendDeathMessage omp;
		omp.HasKiller = true;
		omp.KillerID = 1;
		omp.PlayerID = 0;
		omp.reason = 24;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::DeathMessage p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.killerId, 1);
		CHECK_EQ(p.playerId, 0);
		CHECK_EQ(p.reason, 24);
	}
	{
		NetCode::RPC::ShowPlayerNameTagForPlayer omp;
		omp.PlayerID = 3;
		omp.Show = false;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::ShowNameTag p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.playerId, 3);
		CHECK_EQ(p.show, 0);
	}
	{
		NetCode::RPC::SetPlayerMapIcon omp;
		omp.IconID = 1;
		omp.Pos = Vector3(385.0f, 2502.0f, 16.5f);
		omp.Type = 52;
		omp.Col = Colour(0xFF, 0x00, 0x00, 0xFF);
		omp.Style = 0;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::SetPlayerMapIcon p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.iconId, 1);
		CHECK_EQ(p.type, 52);
		CHECK_EQ(p.color, omp.Col.RGBA());
		CHECK_EQ(p.style, 0);
	}
	{
		NetCode::RPC::RemovePlayerMapIcon omp;
		omp.IconID = 1;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::RemovePlayerMapIcon p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.iconId, 1);
	}
	{
		NetCode::RPC::CreateExplosion omp;
		omp.vec = Vector3(360.0f, 2502.0f, 16.5f);
		omp.type = 12;
		omp.radius = 2.5f;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::CreateExplosion p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.type, 12u);
		CHECK_NEAR(p.radius, 2.5, 0.0);
	}
	{
		NetCode::RPC::PlayerPlaySound omp;
		omp.SoundID = 1057;
		omp.Position = Vector3(0.0f, 0.0f, 0.0f);
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::PlayerPlaySound p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.soundId, 1057);
	}
	{
		NetCode::RPC::EnableStuntBonusForPlayer omp;
		omp.Enable = true;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::EnableStuntBonus p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK(p.enable);
	}
	{
		NetCode::RPC::PlayAudioStreamForPlayer omp;
		omp.URL = StringView("http://example.com/stream.mp3");
		omp.Position = Vector3(1.0f, 2.0f, 3.0f);
		omp.Distance = 50.0f;
		omp.Usepos = true;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::PlayAudioStream p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK(p.url == "http://example.com/stream.mp3");
		CHECK_NEAR(p.radius, 50.0, 0.0);
		CHECK_EQ(p.usePos, 1);
	}
	{
		NetCode::RPC::StopAudioStreamForPlayer omp;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::StopAudioStream p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
	}
	{
		NetCode::RPC::RemoveBuildingForPlayer omp;
		omp.ModelID = 1271;
		omp.Position = Vector3(10.0f, 20.0f, 30.0f);
		omp.Radius = 5.0f;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::RemoveBuilding p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.modelId, 1271);
		CHECK_NEAR(p.radius, 5.0, 0.0);
	}
	{
		NetCode::RPC::DisableRemoteVehicleCollisions omp;
		omp.Disable = true;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::DisableVehicleCollisions p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK(p.disable);
	}
}

static void oracleDlArtworkAndSvrStats()
{
	{
		NetCode::RPC::SetPlayerVirtualWorld omp;
		omp.worldId = 42;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::SetPlayerVirtualWorld p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V03DL));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.virtualWorld, 42);
	}
	{
		NetCode::RPC::ModelRequest omp(3, 10);
		omp.type = 2;
		omp.virtualWorld = -1;
		omp.baseId = 19374;
		omp.newId = -2001;
		omp.dffChecksum = 0x11223344u;
		omp.txdChecksum = 0x55667788u;
		omp.dffSize = 4096u;
		omp.txdSize = 8192u;
		omp.timeOn = 6;
		omp.timeOff = 18;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::DlModelRequest p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V03DL));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.poolId, 3u);
		CHECK_EQ(p.count, 10);
		CHECK_EQ(p.type, 2u);
		CHECK_EQ(p.virtualWorld, -1);
		CHECK_EQ(p.baseId, 19374);
		CHECK_EQ(p.newId, -2001);
		CHECK_EQ(p.dffChecksum, 0x11223344u);
		CHECK_EQ(p.txdChecksum, 0x55667788u);
		CHECK_EQ(p.dffSize, 4096u);
		CHECK_EQ(p.txdSize, 8192u);
		CHECK_EQ(p.timeOn, 6u);
		CHECK_EQ(p.timeOff, 18u);
	}
	{
		NetCode::RPC::ModelUrl omp("http://example.com/m.dff", 1, 0xAABBCCDDu);
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::DlModelUrl p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V03DL));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.kind, 6u);
		CHECK_EQ(p.fileType, 1u);
		CHECK_EQ(p.fileChecksum, 0xAABBCCDDu);
		CHECK(p.downloadLink == "http://example.com/m.dff");
	}
	{
		NetCode::RPC::DownloadCompleted omp;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::DlDownloadCompleted p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V03DL));
		CHECK_EQ(r.unreadBits(), 0);
	}
	{
		RakNet::BitStream bs;
		proto::Writer w(bs);
		proto::DlFinishDownloadReq req{};
		CHECK(req.serialize(w, proto::ProtoVersion::V03DL));
		NetworkBitStream ompBs(bs.GetData(), bs.GetNumberOfBytesUsed(), false);
		NetCode::RPC::FinishDownload omp;
		CHECK(omp.read(ompBs));
	}
	{
		proto::SvrStats src{};
		for (size_t i = 0; i < src.raw.size(); ++i) src.raw[i] = static_cast<uint8_t>(i & 0xFF);
		RakNet::BitStream bs;
		proto::Writer w(bs);
		CHECK(src.serialize(w, proto::ProtoVersion::V037));
		proto::Reader r(bs.GetData(), static_cast<uint32_t>(bs.GetNumberOfBitsUsed()));
		proto::SvrStats dst{};
		CHECK(dst.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK(dst.raw == src.raw);
	}
}

int main()
{
	StringCompressor::AddReference();
	RUN(oracleInitGame);
	RUN(oracleSessionAndChat);
	RUN(oracleSetPlayerSkinBothModes);
	RUN(oraclePlayerProperties);
	RUN(oracleDlArtworkAndSvrStats);
	StringCompressor::RemoveReference();
	return check::finish();
}

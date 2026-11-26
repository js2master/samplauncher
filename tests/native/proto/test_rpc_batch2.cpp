// Unit tests for F02 Batch 2 declarative RPC structs (RpcCore.h):
// Round-trip serialization, optional trailing fields, version branches, and truncation.
#include "check.h"

#include "Net/Proto/RpcDecode.h"

#include <cstdint>
#include <string>

template <typename Packet>
static void checkTruncationFails(const Packet& original, proto::ProtoVersion ver = proto::ProtoVersion::V037)
{
	RakNet::BitStream bs;
	proto::Writer w(bs);
	Packet copy = original;
	CHECK(copy.serialize(w, ver));
	const int totalBits = bs.GetNumberOfBitsUsed();
	CHECK(totalBits > 0);

	{
		proto::Reader r(bs.GetData(), static_cast<uint32_t>(totalBits));
		Packet decoded{};
		CHECK(decoded.serialize(r, ver));
		CHECK_EQ(r.unreadBits(), 0);
	}

	for (int bits = 0; bits < totalBits; ++bits) {
		proto::Reader r(bs.GetData(), static_cast<uint32_t>(bits));
		Packet decoded{};
		CHECK(!decoded.serialize(r, ver));
		CHECK(!r.ok());
	}
}

static void initGameAndOptionalTail()
{
	proto::InitGame ig{};
	ig.zoneNames = true;
	ig.useCJWalk = true;
	ig.allowWeapons = true;
	ig.limitGlobalChatRadius = false;
	ig.globalChatRadius = 200.0f;
	ig.stuntBonus = true;
	ig.nameTagDrawDistance = 70.0f;
	ig.disableInteriorEnterExits = false;
	ig.nameTagLOS = true;
	ig.manualVehicleEngineAndLight = false;
	ig.spawnsAvailable = 3;
	ig.myPlayerId = 7;
	ig.showNameTags = true;
	ig.showPlayerMarkers = 1;
	ig.worldTimeHour = 12;
	ig.weather = 8;
	ig.gravity = 0.008f;
	ig.lanMode = false;
	ig.deathDropMoney = 100;
	ig.instagib = false;
	ig.onFootSendRate = 40;
	ig.inCarSendRate = 40;
	ig.firingSendRate = 40;
	ig.sendMultiplier = 10;
	ig.lagCompensation = 1;
	ig.hostName = "parity server";
	ig.vehicleModels[0] = 1;
	ig.vehicleModels[211] = 5;
	ig.vehicleFriendlyFire = 1;

	RakNet::BitStream bs;
	proto::Writer w(bs);
	CHECK(ig.serialize(w, proto::ProtoVersion::V037));
	const int fullBits = bs.GetNumberOfBitsUsed();

	// Full packet succeeds.
	{
		proto::Reader r(bs.GetData(), static_cast<uint32_t>(fullBits));
		proto::InitGame out{};
		CHECK(out.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(out.myPlayerId, 7);
		CHECK(out.hostName == "parity server");
		CHECK_EQ(out.vehicleModels[0], 1);
		CHECK_EQ(out.vehicleModels[211], 5);
		CHECK_EQ(out.vehicleFriendlyFire, 1);
	}

	// Without optional trailing vehicleFriendlyFire (32 bits shorter): still succeeds with default 0.
	{
		proto::Reader r(bs.GetData(), static_cast<uint32_t>(fullBits - 32));
		proto::InitGame out{};
		CHECK(out.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(out.vehicleFriendlyFire, 0);
		CHECK(out.hostName == "parity server");
	}

	// Truncating before the optional tail must fail.
	for (int bits : {0, 1, 35, 100, 395, fullBits - 33}) {
		proto::Reader r(bs.GetData(), static_cast<uint32_t>(bits));
		proto::InitGame out{};
		CHECK(!out.serialize(r, proto::ProtoVersion::V037));
	}
}

static void sessionAndChatPackets()
{
	proto::ServerJoin join{};
	join.playerId = 5;
	join.markerColor = 0xFF00FF00u;
	join.isNpc = 1;
	join.name = "Bot_One";
	checkTruncationFails(join);

	proto::ServerQuit quit{};
	quit.playerId = 5;
	quit.reason = 1;
	checkTruncationFails(quit);

	auto gmr = proto::decodeInboundRpc(proto::GameModeRestart::kRpcId, nullptr, 0, proto::ProtoVersion::V037);
	CHECK(gmr.status == proto::DecodeStatus::kOk);

	proto::ConnectionRejected rej{};
	rej.reason = 2;
	checkTruncationFails(rej);

	proto::ChatMessage chat{};
	chat.playerId = 3;
	chat.text = "hello world";
	checkTruncationFails(chat);

	proto::ClientMessage cm{};
	cm.color = 0xFFFFFFFFu;
	cm.message = "[SMOKE] message";
	checkTruncationFails(cm);

	proto::ChatBubble cb{};
	cb.playerId = 4;
	cb.color = 0x66CCFFFFu;
	cb.drawDistance = 30.0f;
	cb.duration = 5000;
	cb.text = "bubble";
	checkTruncationFails(cb);

	proto::WorldTime wt{14};
	checkTruncationFails(wt);

	proto::Weather we{8};
	checkTruncationFails(we);

	proto::SetTimeEx ste{22, 30};
	checkTruncationFails(ste);

	proto::ToggleClock tc{1};
	checkTruncationFails(tc);

	proto::TimerUpdate tu{12345678u};
	checkTruncationFails(tu);

	// UpdateScoresPingsIPs: 0 entries, 2 entries, and truncated partial entry.
	{
		proto::UpdateScoresPingsIPs empty{};
		auto res = proto::decodeInboundRpc(proto::UpdateScoresPingsIPs::kRpcId, nullptr, 0, proto::ProtoVersion::V037);
		CHECK(res.status == proto::DecodeStatus::kOk);
		(void)empty;

		proto::UpdateScoresPingsIPs list{};
		list.entries.push_back({0, 42, 15});
		list.entries.push_back({3, -10, 99});
		RakNet::BitStream bs;
		proto::Writer w(bs);
		CHECK(list.serialize(w, proto::ProtoVersion::V037));
		CHECK_EQ(bs.GetNumberOfBitsUsed(), 160);

		proto::Reader r(bs.GetData(), 160);
		proto::UpdateScoresPingsIPs out{};
		CHECK(out.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(out.entries.size(), 2);
		CHECK_EQ(out.entries[0].playerId, 0);
		CHECK_EQ(out.entries[0].score, 42);
		CHECK_EQ(out.entries[0].ping, 15);
		CHECK_EQ(out.entries[1].playerId, 3);
		CHECK_EQ(out.entries[1].score, -10);
		CHECK_EQ(out.entries[1].ping, 99);

		// Partial trailing entry (88 bits = 11 bytes) must fail.
		proto::Reader trunc(bs.GetData(), 88);
		proto::UpdateScoresPingsIPs bad{};
		CHECK(!bad.serialize(trunc, proto::ProtoVersion::V037));
	}

	// ClientCheck (103): enabled in both 0.3.7 and 0.3.DL (T05).
	{
		proto::ClientCheckRequest cc{};
		cc.type = 0x48;
		cc.argument = 0x12345678u;
		cc.offset = 0;
		cc.length = 4;
		checkTruncationFails(cc, proto::ProtoVersion::V037);
		checkTruncationFails(cc, proto::ProtoVersion::V03DL);

		RakNet::BitStream bs;
		proto::Writer w(bs);
		CHECK(cc.serialize(w, proto::ProtoVersion::V037));
		auto r037 = proto::decodeInboundRpc(proto::ClientCheckRequest::kRpcId, bs.GetData(),
			static_cast<uint32_t>(bs.GetNumberOfBitsUsed()), proto::ProtoVersion::V037);
		CHECK(r037.status == proto::DecodeStatus::kOk);
		auto rDL = proto::decodeInboundRpc(proto::ClientCheckRequest::kRpcId, bs.GetData(),
			static_cast<uint32_t>(bs.GetNumberOfBitsUsed()), proto::ProtoVersion::V03DL);
		CHECK(rDL.status == proto::DecodeStatus::kOk);
	}
}

static void playerPropertyPackets()
{
	checkTruncationFails(proto::SetGravity{0.008f});
	checkTruncationFails(proto::SetPlayerPos{{1.0f, 2.0f, 3.0f}});
	checkTruncationFails(proto::SetPlayerPosFindZ{{4.0f, 5.0f, 6.0f}});
	checkTruncationFails(proto::SetPlayerFacingAngle{90.0f});
	checkTruncationFails(proto::SetPlayerHealth{80.0f});
	checkTruncationFails(proto::SetPlayerArmour{50.0f});
	checkTruncationFails(proto::SetPlayerColor{0, 0x00FF00FFu});
	checkTruncationFails(proto::SetPlayerTeam{0, 2});
	checkTruncationFails(proto::SetPlayerName{0, "NewName", 1});
	checkTruncationFails(proto::SetPlayerSkillLevel{3, 0, 999});

	// SetPlayerSkin (V037 u32+u32 & V03DL u16+u32+u32, T03)
	checkTruncationFails(proto::SetPlayerSkin{7, 123, 0}, proto::ProtoVersion::V037);
	checkTruncationFails(proto::SetPlayerSkin{7, 123, 20001}, proto::ProtoVersion::V03DL);

	checkTruncationFails(proto::SetPlayerInterior{3});
	checkTruncationFails(proto::TogglePlayerControllable{1});
	checkTruncationFails(proto::SetWorldBounds{{2000.0f, -2000.0f, 2000.0f, -2000.0f}});
	checkTruncationFails(proto::GivePlayerMoney{1500});
	CHECK(proto::decodeInboundRpc(proto::ResetPlayerMoney::kRpcId, nullptr, 0, proto::ProtoVersion::V037).status ==
		proto::DecodeStatus::kOk);
	checkTruncationFails(proto::SetPlayerWantedLevel{3});
	checkTruncationFails(proto::SetPlayerFightingStyle{0, 5});
	checkTruncationFails(proto::SetPlayerVelocity{{0.1f, -0.2f, 0.3f}});
	checkTruncationFails(proto::SetPlayerSpecialAction{10});
	checkTruncationFails(proto::SetPlayerDrunkLevel{2500u});
	checkTruncationFails(proto::SetPlayerDrunkVisuals{100u});
	checkTruncationFails(proto::SetPlayerDrunkHandling{50u});
	CHECK(proto::decodeInboundRpc(proto::ResetPlayerWeapons::kRpcId, nullptr, 0, proto::ProtoVersion::V037).status ==
		proto::DecodeStatus::kOk);
	checkTruncationFails(proto::GivePlayerWeapon{24, 70});
	checkTruncationFails(proto::SetPlayerAmmo{24, 70});
	checkTruncationFails(proto::SetArmedWeapon{24u});
	checkTruncationFails(proto::ShowGameText{3, 2000, "~g~PARITY"});
	checkTruncationFails(proto::DeathMessage{1, 0, 24});
	checkTruncationFails(proto::ShowNameTag{1, 1});
	checkTruncationFails(proto::SetPlayerMapIcon{0, {385.0f, 2502.0f, 16.5f}, 52, 0xFF0000FFu, 0});
	checkTruncationFails(proto::RemovePlayerMapIcon{0});
	checkTruncationFails(proto::CreateExplosion{{360.0f, 2502.0f, 16.5f}, 12u, 3.0f});
	checkTruncationFails(proto::PlayerPlaySound{1057, {0.0f, 0.0f, 0.0f}});
	checkTruncationFails(proto::EnableStuntBonus{true});
	checkTruncationFails(proto::PlayAudioStream{"http://example.com/a.mp3", {1.0f, 2.0f, 3.0f}, 30.0f, 1});
	CHECK(proto::decodeInboundRpc(proto::StopAudioStream::kRpcId, nullptr, 0, proto::ProtoVersion::V037).status ==
		proto::DecodeStatus::kOk);
	checkTruncationFails(proto::RemoveBuilding{1271, {10.0f, 20.0f, 30.0f}, 5.0f});
	checkTruncationFails(proto::DisableVehicleCollisions{true});
}

int main()
{
	StringCompressor::AddReference();
	RUN(initGameAndOptionalTail);
	RUN(sessionAndChatPackets);
	RUN(playerPropertyPackets);
	StringCompressor::RemoveReference();
	return check::finish();
}

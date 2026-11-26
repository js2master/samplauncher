// Unit tests for F02 Batch 1 declarative RPC structs:
// Dialog, Checkpoint, GangZone, Pickup, TextLabel.
// Covers round-trip serialization, exhaustive bit-by-bit truncation, and RpcDecode.
#include "check.h"

#include "Net/Proto/RpcDecode.h"

#include <cstdint>
#include <string>

template <typename Packet>
static void checkTruncationFails(const Packet& original, proto::ProtoVersion ver)
{
	RakNet::BitStream bs;
	proto::Writer w(bs);
	Packet copy = original;
	CHECK(copy.serialize(w, ver));
	const int totalBits = bs.GetNumberOfBitsUsed();
	CHECK(totalBits > 0);

	// Full bit length must succeed with 0 unread bits.
	{
		proto::Reader r(bs.GetData(), static_cast<uint32_t>(totalBits));
		Packet decoded{};
		CHECK(decoded.serialize(r, ver));
		CHECK_EQ(r.unreadBits(), 0);
	}

	// Every prefix strictly shorter than totalBits must fail cleanly.
	for (int bits = 0; bits < totalBits; ++bits) {
		proto::Reader r(bs.GetData(), static_cast<uint32_t>(bits));
		Packet decoded{};
		CHECK(!decoded.serialize(r, ver));
		CHECK(!r.ok());
	}
}

static void dialogPackets()
{
	proto::ShowDialog d{};
	d.id = 7002;
	d.style = 2;
	d.title = "Smoke finished";
	d.button1 = "OK";
	d.button2 = "Cancel";
	d.body = "Row 1\nRow 2";

	RakNet::BitStream bs;
	proto::Writer w(bs);
	CHECK(d.serialize(w, proto::ProtoVersion::V037));

	proto::Reader r(bs.GetData(), static_cast<uint32_t>(bs.GetNumberOfBitsUsed()));
	proto::ShowDialog out{};
	CHECK(out.serialize(r, proto::ProtoVersion::V037));
	CHECK_EQ(r.unreadBits(), 0);
	CHECK_EQ(out.id, d.id);
	CHECK_EQ(out.style, d.style);
	CHECK(out.title == d.title);
	CHECK(out.button1 == d.button1);
	CHECK(out.button2 == d.button2);
	CHECK(out.body == d.body);

	checkTruncationFails(d, proto::ProtoVersion::V037);

	// Empty strings / negative ID (HidePlayerDialog wire form)
	proto::ShowDialog hide{};
	hide.id = -1;
	hide.style = 0;
	checkTruncationFails(hide, proto::ProtoVersion::V037);

	// Outbound DialogResponse
	proto::DialogResponse resp{};
	resp.id = 7002;
	resp.response = 1;
	resp.listItem = 3;
	resp.input = "hello";
	checkTruncationFails(resp, proto::ProtoVersion::V037);
}

static void checkpointPackets()
{
	proto::SetCheckpoint cp{};
	cp.position = {385.0f, 2502.0f, 16.5f};
	cp.size = 4.5f;
	checkTruncationFails(cp, proto::ProtoVersion::V037);

	proto::SetRaceCheckpoint rcp{};
	rcp.type = 2;
	rcp.position = {1.0f, 2.0f, 3.0f};
	rcp.nextPosition = {4.0f, 5.0f, 6.0f};
	rcp.radius = 7.5f;
	checkTruncationFails(rcp, proto::ProtoVersion::V037);

	// Empty-payload RPCs must succeed on 0-bit stream even with nullptr data.
	auto d1 = proto::decodeInboundRpc(proto::DisableCheckpoint::kRpcId, nullptr, 0, proto::ProtoVersion::V037);
	CHECK(d1.status == proto::DecodeStatus::kOk);
	CHECK_EQ(d1.unreadBits, 0);

	auto d2 = proto::decodeInboundRpc(proto::DisableRaceCheckpoint::kRpcId, nullptr, 0, proto::ProtoVersion::V037);
	CHECK(d2.status == proto::DecodeStatus::kOk);
	CHECK_EQ(d2.unreadBits, 0);
}

static void gangZonePackets()
{
	proto::ShowGangZone show{};
	show.zoneId = 12;
	show.min = {100.0f, 200.0f};
	show.max = {300.0f, 400.0f};
	show.color = 0xAA0000FFu;
	checkTruncationFails(show, proto::ProtoVersion::V037);

	proto::HideGangZone hide{};
	hide.zoneId = 12;
	checkTruncationFails(hide, proto::ProtoVersion::V037);

	proto::FlashGangZone flash{};
	flash.zoneId = 12;
	flash.color = 0xAAFF0000u;
	checkTruncationFails(flash, proto::ProtoVersion::V037);

	proto::StopFlashGangZone stop{};
	stop.zoneId = 12;
	checkTruncationFails(stop, proto::ProtoVersion::V037);
}

static void pickupPackets()
{
	proto::CreatePickup cp{};
	cp.pickupId = 5;
	cp.model = 1240;
	cp.type = 1;
	cp.position = {380.0f, 2500.0f, 16.5f};
	checkTruncationFails(cp, proto::ProtoVersion::V037);

	proto::DestroyPickup dp{};
	dp.pickupId = 5;
	checkTruncationFails(dp, proto::ProtoVersion::V037);

	proto::DestroyWeaponPickup dwp{};
	dwp.owner = 7;
	checkTruncationFails(dwp, proto::ProtoVersion::V03DL);

	// DestroyWeaponPickup (151) is 0.3.DL only.
	uint8_t ownerByte = 7;
	auto r037 = proto::decodeInboundRpc(proto::DestroyWeaponPickup::kRpcId, &ownerByte, 8, proto::ProtoVersion::V037);
	CHECK(r037.status == proto::DecodeStatus::kVersionMismatch);
	auto rDL = proto::decodeInboundRpc(proto::DestroyWeaponPickup::kRpcId, &ownerByte, 8, proto::ProtoVersion::V03DL);
	CHECK(rDL.status == proto::DecodeStatus::kOk);
	CHECK_EQ(rDL.unreadBits, 0);

	proto::PickedUpPickup picked{};
	picked.pickupId = 5;
	checkTruncationFails(picked, proto::ProtoVersion::V037);
}

static void textLabelPackets()
{
	proto::Create3DTextLabel lbl{};
	lbl.labelId = 1025;
	lbl.color = 0xFFFFFFFFu;
	lbl.position = {381.0f, 2505.0f, 17.0f};
	lbl.drawDistance = 40.0f;
	lbl.testLOS = 1;
	lbl.attachedPlayerId = 0xFFFF;
	lbl.attachedVehicleId = 5;
	lbl.text = "PARITY label";
	checkTruncationFails(lbl, proto::ProtoVersion::V037);

	proto::Delete3DTextLabel del{};
	del.labelId = 1025;
	checkTruncationFails(del, proto::ProtoVersion::V037);
}

int main()
{
	StringCompressor::AddReference();
	RUN(dialogPackets);
	RUN(checkpointPackets);
	RUN(gangZonePackets);
	RUN(pickupPackets);
	RUN(textLabelPackets);
	StringCompressor::RemoveReference();
	return check::finish();
}

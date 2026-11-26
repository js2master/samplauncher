// Oracle tests for F02 Batch 1: official open.mp write() -> client proto::* serialize().
// For outbound RPCs in Batch 1, client proto::* serialize() -> open.mp read().
#include "check.h"

#include <sdk.hpp>
#include <bitstream.hpp>
#include <netcode.hpp>

#include "Net/Proto/RpcCheckpoint.h"
#include "Net/Proto/RpcDialog.h"
#include "Net/Proto/RpcGangZone.h"
#include "Net/Proto/RpcPickup.h"
#include "Net/Proto/RpcTextLabel.h"

#include <cstdint>
#include <string>

static proto::Reader makeReader(NetworkBitStream& bs)
{
	return proto::Reader(bs.GetData(), static_cast<uint32_t>(bs.GetNumberOfBitsUsed()));
}

static void oracleShowDialog()
{
	NetCode::RPC::ShowDialog omp;
	omp.ID = 7001;
	omp.Style = 2;
	omp.Title = StringView("Title");
	omp.FirstButton = StringView("OK");
	omp.SecondButton = StringView("Cancel");
	omp.Body = StringView("Line 1\n{FF0000}Line 2");

	NetworkBitStream w;
	omp.write(w);
	auto r = makeReader(w);
	proto::ShowDialog p{};
	CHECK(p.serialize(r, proto::ProtoVersion::V037));
	CHECK_EQ(r.unreadBits(), 0);
	CHECK_EQ(p.id, 7001);
	CHECK_EQ(p.style, 2);
	CHECK(p.title == "Title");
	CHECK(p.button1 == "OK");
	CHECK(p.button2 == "Cancel");
	CHECK(p.body == "Line 1\n{FF0000}Line 2");

	// HidePlayerDialog (-1 with empty strings)
	NetCode::RPC::ShowDialog hide;
	hide.ID = -1;
	hide.Style = 0;
	NetworkBitStream w2;
	hide.write(w2);
	auto r2 = makeReader(w2);
	proto::ShowDialog p2{};
	CHECK(p2.serialize(r2, proto::ProtoVersion::V037));
	CHECK_EQ(r2.unreadBits(), 0);
	CHECK_EQ(p2.id, -1);
	CHECK(p2.title.empty() && p2.button1.empty() && p2.button2.empty() && p2.body.empty());
}

static void oracleDialogResponse()
{
	proto::DialogResponse p{};
	p.id = 7002;
	p.response = 1;
	p.listItem = 4;
	p.input = "Something looked right";

	RakNet::BitStream bs;
	proto::Writer w(bs);
	CHECK(p.serialize(w, proto::ProtoVersion::V037));

	NetworkBitStream r(bs.GetData(), static_cast<unsigned int>(bs.GetNumberOfBytesUsed()), true);
	NetCode::RPC::OnPlayerDialogResponse omp;
	CHECK(omp.read(r));
	CHECK_EQ(omp.ID, 7002);
	CHECK_EQ(omp.Response, 1);
	CHECK_EQ(omp.ListItem, 4);
	CHECK(StringView(omp.Text) == StringView("Something looked right"));
}

static void oracleCheckpoints()
{
	{
		NetCode::RPC::SetCheckpoint omp;
		omp.position = Vector3(385.0f, 2502.0f, 16.5f);
		omp.size = 5.0f;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::SetCheckpoint p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_NEAR(p.position.x, 385.0, 0.0);
		CHECK_NEAR(p.position.y, 2502.0, 0.0);
		CHECK_NEAR(p.position.z, 16.5, 0.0);
		CHECK_NEAR(p.size, 5.0, 0.0);
	}
	{
		NetCode::RPC::DisableCheckpoint omp;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::DisableCheckpoint p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
	}
	{
		NetCode::RPC::SetRaceCheckpoint omp;
		omp.type = 1;
		omp.position = Vector3(10.0f, 20.0f, 30.0f);
		omp.nextPosition = Vector3(40.0f, 50.0f, 60.0f);
		omp.size = 8.0f;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::SetRaceCheckpoint p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.type, 1);
		CHECK_NEAR(p.position.x, 10.0, 0.0);
		CHECK_NEAR(p.position.y, 20.0, 0.0);
		CHECK_NEAR(p.position.z, 30.0, 0.0);
		CHECK_NEAR(p.nextPosition.x, 40.0, 0.0);
		CHECK_NEAR(p.nextPosition.y, 50.0, 0.0);
		CHECK_NEAR(p.nextPosition.z, 60.0, 0.0);
		CHECK_NEAR(p.radius, 8.0, 0.0);
	}
	{
		NetCode::RPC::DisableRaceCheckpoint omp;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::DisableRaceCheckpoint p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
	}
}

static void oracleGangZones()
{
	{
		NetCode::RPC::ShowGangZone omp;
		omp.ID = 7;
		omp.Min = Vector2(300.0f, 2400.0f);
		omp.Max = Vector2(450.0f, 2600.0f);
		omp.Col = Colour(0xFF, 0x11, 0x22, 0x80);
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::ShowGangZone p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.zoneId, 7);
		CHECK_NEAR(p.min.x, 300.0, 0.0);
		CHECK_NEAR(p.min.y, 2400.0, 0.0);
		CHECK_NEAR(p.max.x, 450.0, 0.0);
		CHECK_NEAR(p.max.y, 2600.0, 0.0);
		CHECK_EQ(p.color, omp.Col.ABGR());
	}
	{
		NetCode::RPC::HideGangZone omp;
		omp.ID = 7;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::HideGangZone p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.zoneId, 7);
	}
	{
		NetCode::RPC::FlashGangZone omp;
		omp.ID = 7;
		omp.Col = Colour(0x00, 0x00, 0xFF, 0xAA);
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::FlashGangZone p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.zoneId, 7);
		CHECK_EQ(p.color, omp.Col.ABGR());
	}
	{
		NetCode::RPC::StopFlashGangZone omp;
		omp.ID = 7;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::StopFlashGangZone p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.zoneId, 7);
	}
}

static void oraclePickups()
{
	{
		NetCode::RPC::PlayerCreatePickup omp;
		omp.PickupID = 15;
		omp.Model = 1240;
		omp.Type = 1;
		omp.Position = Vector3(382.5f, 2500.25f, 16.5f);
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::CreatePickup p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.pickupId, 15);
		CHECK_EQ(p.model, 1240);
		CHECK_EQ(p.type, 1);
		CHECK_NEAR(p.position.x, 382.5, 0.0);
		CHECK_NEAR(p.position.y, 2500.25, 0.0);
		CHECK_NEAR(p.position.z, 16.5, 0.0);
	}
	{
		NetCode::RPC::PlayerDestroyPickup omp;
		omp.PickupID = 15;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::DestroyPickup p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.pickupId, 15);
	}
	{
		proto::PickedUpPickup p{};
		p.pickupId = 15;
		RakNet::BitStream bs;
		proto::Writer w(bs);
		CHECK(p.serialize(w, proto::ProtoVersion::V037));
		NetworkBitStream r(bs.GetData(), static_cast<unsigned int>(bs.GetNumberOfBytesUsed()), true);
		NetCode::RPC::OnPlayerPickUpPickup omp;
		CHECK(omp.read(r));
		CHECK_EQ(omp.PickupID, 15);
	}
}

static void oracleTextLabels()
{
	{
		NetCode::RPC::PlayerShowTextLabel omp;
		omp.PlayerTextLabel = false;
		omp.TextLabelID = 42;
		omp.Col = Colour(0x66, 0xCC, 0xFF, 0xFF);
		omp.Position = Vector3(380.0f, 2505.0f, 17.5f);
		omp.DrawDistance = 50.0f;
		omp.LOS = true;
		omp.PlayerAttachID = INVALID_PLAYER_ID;
		omp.VehicleAttachID = 5;
		omp.Text = StringView("SMOKE 3D label");
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::Create3DTextLabel p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.labelId, 42);
		CHECK_EQ(p.color, omp.Col.RGBA());
		CHECK_NEAR(p.position.x, 380.0, 0.0);
		CHECK_NEAR(p.position.y, 2505.0, 0.0);
		CHECK_NEAR(p.position.z, 17.5, 0.0);
		CHECK_NEAR(p.drawDistance, 50.0, 0.0);
		CHECK_EQ(p.testLOS, 1);
		CHECK_EQ(p.attachedPlayerId, 0xFFFF);
		CHECK_EQ(p.attachedVehicleId, 5);
		CHECK(p.text == "SMOKE 3D label");
	}
	{
		NetCode::RPC::PlayerShowTextLabel omp;
		omp.PlayerTextLabel = true;
		omp.TextLabelID = 3;
		omp.Col = Colour(0xFF, 0xFF, 0xFF, 0xFF);
		omp.Position = Vector3(0.0f, 0.0f, 0.0f);
		omp.DrawDistance = 20.0f;
		omp.LOS = false;
		omp.PlayerAttachID = 0;
		omp.VehicleAttachID = INVALID_VEHICLE_ID;
		omp.Text = StringView("");
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::Create3DTextLabel p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.labelId, TEXT_LABEL_POOL_SIZE + 3);
		CHECK_EQ(p.testLOS, 0);
		CHECK(p.text.empty());
	}
	{
		NetCode::RPC::PlayerHideTextLabel omp;
		omp.PlayerTextLabel = false;
		omp.TextLabelID = 42;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::Delete3DTextLabel p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.labelId, 42);
	}
}

int main()
{
	StringCompressor::AddReference();
	RUN(oracleShowDialog);
	RUN(oracleDialogResponse);
	RUN(oracleCheckpoints);
	RUN(oracleGangZones);
	RUN(oraclePickups);
	RUN(oracleTextLabels);
	StringCompressor::RemoveReference();
	return check::finish();
}

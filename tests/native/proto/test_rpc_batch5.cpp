#include "check.h"

#include "Net/Proto/RpcActor.h"
#include "Net/Proto/RpcDecode.h"
#include "Net/Proto/RpcMenu.h"
#include "Net/Proto/RpcObject.h"
#include "Net/Proto/RpcTextDraw.h"

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
static void expectTruncatedFails(const Packet& in, int dropBits = 8, proto::ProtoVersion v = proto::ProtoVersion::V037)
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

static void testObjects()
{
	// CreateObject: plain, attached, and with both texture & text materials.
	{
		proto::CreateObject in{};
		in.objectId = 1;
		in.modelId = 1221;
		in.position = {10.0f, 20.0f, 30.0f};
		in.rotation = {1.0f, 2.0f, 3.0f};
		in.drawDistance = 300.0f;
		in.noCameraCol = 1;
		in.attachedVehicleId = 5;
		in.attachedObjectId = 0xFFFF;
		in.attachOffset = {0.5f, -0.5f, 1.0f};
		in.attachRotation = {0.0f, 0.0f, 90.0f};
		in.syncRotation = 1;

		proto::ObjectMaterial mTex{};
		mTex.materialType = proto::kObjectMaterialTexture;
		mTex.materialIndex = 0;
		mTex.modelId = 19377;
		mTex.txdName = "all_walls";
		mTex.textureName = "ab_kitchFloor";
		mTex.materialColor = 0xFF00AA00u;

		proto::ObjectMaterial mTxt{};
		mTxt.materialType = proto::kObjectMaterialText;
		mTxt.materialIndex = 1;
		mTxt.materialSize = 90;
		mTxt.fontName = "Arial";
		mTxt.fontSize = 24;
		mTxt.fontBold = 1;
		mTxt.fontColor = 0xFFFFFFFFu;
		mTxt.backgroundColor = 0xFF000000u;
		mTxt.alignment = 1;
		mTxt.text = "PARITY";

		in.materials = {mTex, mTxt};

		const auto out = roundTrip(in);
		CHECK_EQ(out.objectId, 1);
		CHECK_EQ(out.modelId, 1221);
		CHECK_NEAR(out.position.z, 30.0, 0.0);
		CHECK_EQ(out.noCameraCol, 1);
		CHECK_EQ(out.attachedVehicleId, 5);
		CHECK_NEAR(out.attachOffset.z, 1.0, 0.0);
		CHECK_EQ(out.syncRotation, 1);
		CHECK_EQ(out.materials.size(), 2u);
		CHECK_EQ(out.materials[0].materialType, proto::kObjectMaterialTexture);
		CHECK(out.materials[0].txdName == "all_walls");
		CHECK(out.materials[0].textureName == "ab_kitchFloor");
		CHECK_EQ(out.materials[0].materialColor, 0xFF00AA00u);
		CHECK_EQ(out.materials[1].materialType, proto::kObjectMaterialText);
		CHECK(out.materials[1].fontName == "Arial");
		CHECK(out.materials[1].text == "PARITY");
		expectTruncatedFails(in, 8);
	}
	// CreateObject without attachment and 0 materials
	{
		proto::CreateObject in{};
		in.objectId = 2;
		in.modelId = 1225;
		in.position = {1.0f, 2.0f, 3.0f};
		const auto out = roundTrip(in);
		CHECK_EQ(out.objectId, 2);
		CHECK_EQ(out.materials.size(), 0u);
		expectTruncatedFails(in, 8);
	}
	// SetObjectPos (T36): accepts both 14-byte (open.mp) and 18-byte (legacy SA-MP) payloads
	{
		proto::SetObjectPos in{};
		in.objectId = 3;
		in.position = {5.0f, 6.0f, 7.0f};
		in.unused = 0.0f;
		const auto out = roundTrip(in);
		CHECK_EQ(out.objectId, 3);
		CHECK_NEAR(out.position.y, 6.0, 0.0);

		// 14-byte open.mp payload (no trailing unused float)
		RakNet::BitStream bs14;
		proto::Writer w14(bs14);
		CHECK(w14.u16(3) && w14.vec3(in.position));
		const auto dec14 = proto::decodeInboundRpc(proto::SetObjectPos::kRpcId, bs14.GetData(),
			static_cast<uint32_t>(bs14.GetNumberOfBitsUsed()), proto::ProtoVersion::V037);
		CHECK(dec14.status == proto::DecodeStatus::kOk);
		CHECK_EQ(dec14.unreadBits, 0u);

		proto::Reader r14(bs14.GetData(), static_cast<uint32_t>(bs14.GetNumberOfBitsUsed()));
		proto::SetObjectPos p14{};
		CHECK(p14.serialize(r14, proto::ProtoVersion::V037));
		CHECK_EQ(r14.unreadBits(), 0);
		CHECK_EQ(p14.objectId, 3);
		CHECK_NEAR(p14.position.z, 7.0, 0.0);

		// Truncated below 14 bytes (112 bits) must fail
		proto::Reader trunc(bs14.GetData(), 104);
		proto::SetObjectPos bad{};
		CHECK(!bad.serialize(trunc, proto::ProtoVersion::V037));
	}
	{
		proto::SetObjectRotation in{};
		in.objectId = 3;
		in.rotation = {10.0f, 20.0f, 30.0f};
		CHECK_NEAR(roundTrip(in).rotation.z, 30.0, 0.0);
		expectTruncatedFails(in);
	}
	{
		proto::DestroyObject in{};
		in.objectId = 3;
		CHECK_EQ(roundTrip(in).objectId, 3);
		expectTruncatedFails(in);
	}
	{
		proto::MoveObject in{};
		in.objectId = 4;
		in.fromPos = {1.0f, 2.0f, 3.0f};
		in.targetPos = {4.0f, 5.0f, 6.0f};
		in.speed = 2.5f;
		in.targetRot = {0.0f, 0.0f, 180.0f};
		const auto out = roundTrip(in);
		CHECK_EQ(out.objectId, 4);
		CHECK_NEAR(out.speed, 2.5, 0.0);
		CHECK_NEAR(out.targetRot.z, 180.0, 0.0);
		expectTruncatedFails(in);
	}
	{
		proto::StopObject in{};
		in.objectId = 4;
		CHECK_EQ(roundTrip(in).objectId, 4);
		expectTruncatedFails(in);
	}
	{
		proto::AttachObjectToPlayer in{};
		in.objectId = 4;
		in.playerId = 2;
		in.offset = {0.1f, 0.2f, 0.3f};
		in.rotation = {1.0f, 2.0f, 3.0f};
		const auto out = roundTrip(in);
		CHECK_EQ(out.objectId, 4);
		CHECK_EQ(out.playerId, 2);
		CHECK_NEAR(out.offset.z, 0.3, 1e-6);
		expectTruncatedFails(in);
	}
	{
		proto::SetObjectMaterial in{};
		in.objectId = 4;
		in.material.materialType = proto::kObjectMaterialTexture;
		in.material.materialIndex = 2;
		in.material.modelId = 1000;
		in.material.txdName = "txd";
		in.material.textureName = "tex";
		in.material.materialColor = 0xAABBCCDDu;
		const auto out = roundTrip(in);
		CHECK_EQ(out.objectId, 4);
		CHECK(out.material.txdName == "txd");
		expectTruncatedFails(in);
	}
	{
		proto::StopObjectCameraCollision in{};
		in.objectId = 4;
		CHECK_EQ(roundTrip(in).objectId, 4);
		expectTruncatedFails(in);
	}
	{
		proto::SetPlayerAttachedObject inCreate{};
		inCreate.playerId = 0;
		inCreate.index = 1;
		inCreate.create = true;
		inCreate.modelId = 18939;
		inCreate.boneId = 2;
		inCreate.offset = {0.1f, 0.0f, 0.0f};
		inCreate.rotation = {0.0f, 90.0f, 0.0f};
		inCreate.scale = {1.0f, 1.0f, 1.0f};
		inCreate.materialColor1 = 0xFFFF0000u;
		inCreate.materialColor2 = 0xFF00FF00u;
		const auto outC = roundTrip(inCreate);
		CHECK(outC.create);
		CHECK_EQ(outC.modelId, 18939);
		CHECK_EQ(outC.boneId, 2);
		CHECK_EQ(outC.materialColor2, 0xFF00FF00u);
		expectTruncatedFails(inCreate, 1);

		proto::SetPlayerAttachedObject inRem{};
		inRem.playerId = 0;
		inRem.index = 1;
		inRem.create = false;
		const auto outR = roundTrip(inRem);
		CHECK(!outR.create);
		CHECK_EQ(outR.index, 1);
		expectTruncatedFails(inRem, 1);
	}
	{
		proto::EditAttachedObject in{};
		in.index = 2;
		CHECK_EQ(roundTrip(in).index, 2);
		expectTruncatedFails(in);
	}
	{
		proto::EditObject in{};
		in.playerObject = true;
		in.objectId = 77;
		const auto out = roundTrip(in);
		CHECK(out.playerObject);
		CHECK_EQ(out.objectId, 77);
		expectTruncatedFails(in, 1);
	}
	{
		proto::EnterEditObject in{};
		roundTrip(in);
	}
	{
		proto::CancelEditObject in{};
		roundTrip(in);
	}
}

static void testActors()
{
	{
		proto::ShowActor in{};
		in.actorId = 0;
		in.skin = 162;
		in.customSkin = 25005;
		in.position = {100.0f, 200.0f, 15.0f};
		in.angle = 90.0f;
		in.health = 100.0f;
		in.invulnerable = 1;

		const auto out37 = roundTrip(in, proto::ProtoVersion::V037);
		CHECK_EQ(out37.actorId, 0);
		CHECK_EQ(out37.skin, 162);
		CHECK_EQ(out37.customSkin, 0);
		CHECK_EQ(out37.invulnerable, 1);

		const auto outDL = roundTrip(in, proto::ProtoVersion::V03DL);
		CHECK_EQ(outDL.customSkin, 25005);
		expectTruncatedFails(in, 8, proto::ProtoVersion::V037);
		expectTruncatedFails(in, 8, proto::ProtoVersion::V03DL);
	}
	{
		proto::HideActor in{};
		in.actorId = 3;
		CHECK_EQ(roundTrip(in).actorId, 3);
		expectTruncatedFails(in);
	}
	{
		proto::ApplyActorAnimation in{};
		in.actorId = 0;
		in.animLib = "DANCING";
		in.animName = "dance_loop";
		in.delta = 4.1f;
		in.loop = true;
		in.lockX = false;
		in.lockY = false;
		in.freeze = false;
		in.time = 0;
		const auto out = roundTrip(in);
		CHECK_EQ(out.actorId, 0);
		CHECK(out.animLib == "DANCING");
		CHECK(out.animName == "dance_loop");
		CHECK(out.loop && !out.freeze);
		expectTruncatedFails(in, 1);
	}
	{
		proto::ClearActorAnimation in{};
		in.actorId = 0;
		CHECK_EQ(roundTrip(in).actorId, 0);
		expectTruncatedFails(in);
	}
	{
		proto::SetActorFacingAngle in{};
		in.actorId = 0;
		in.angle = 180.5f;
		CHECK_NEAR(roundTrip(in).angle, 180.5, 0.0);
		expectTruncatedFails(in);
	}
	{
		proto::SetActorPos in{};
		in.actorId = 0;
		in.position = {1.0f, 2.0f, 3.0f};
		CHECK_NEAR(roundTrip(in).position.z, 3.0, 0.0);
		expectTruncatedFails(in);
	}
	{
		proto::SetActorHealth in{};
		in.actorId = 0;
		in.health = 75.5f;
		CHECK_NEAR(roundTrip(in).health, 75.5, 0.0);
		expectTruncatedFails(in);
	}
}

static void testTextDraws()
{
	{
		proto::ShowTextDraw in{};
		in.textDrawId = 12;
		in.flags = 0x15;
		in.letterSize = {0.4f, 1.2f};
		in.letterColor = 0xFFFFFFFFu;
		in.lineSize = {320.0f, 20.0f};
		in.boxColor = 0x80000000u;
		in.shadow = 1;
		in.outline = 1;
		in.backgroundColor = 0xFF000000u;
		in.style = 1;
		in.selectable = 1;
		in.position = {320.0f, 400.0f};
		in.modelId = 411;
		in.rotation = {0.0f, 0.0f, 45.0f};
		in.zoom = 1.0f;
		in.color1 = 3;
		in.color2 = 1;
		in.text = "~g~PARITY TD";

		const auto out = roundTrip(in);
		CHECK_EQ(out.textDrawId, 12);
		CHECK_EQ(out.flags, 0x15);
		CHECK_NEAR(out.letterSize.y, 1.2, 1e-6);
		CHECK_EQ(out.modelId, 411);
		CHECK_EQ(out.color1, 3);
		CHECK(out.text == "~g~PARITY TD");
		expectTruncatedFails(in);
	}
	{
		proto::HideTextDraw in{};
		in.textDrawId = 12;
		CHECK_EQ(roundTrip(in).textDrawId, 12);
		expectTruncatedFails(in);
	}
	{
		proto::TextDrawSetString in{};
		in.textDrawId = 12;
		in.text = "Updated!";
		const auto out = roundTrip(in);
		CHECK_EQ(out.textDrawId, 12);
		CHECK(out.text == "Updated!");
		expectTruncatedFails(in);
	}
	{
		proto::SelectTextDraw in{};
		in.enable = true;
		in.hoverColor = 0xFFAA00FFu;
		const auto out = roundTrip(in);
		CHECK(out.enable);
		CHECK_EQ(out.hoverColor, 0xFFAA00FFu);
		expectTruncatedFails(in, 1);
	}
}

static void testMenus()
{
	// 2-column menu with disabled row
	{
		proto::InitMenu in{};
		in.menuId = 3;
		in.hasTwoColumns = true;
		in.title = "Parity menu";
		in.position = {200.0f, 120.0f};
		in.col1Width = 150.0f;
		in.col2Width = 140.0f;
		in.menuEnabled = true;
		in.rowEnabled.fill(true);
		in.rowEnabled[2] = false;
		in.columnHeaders[0] = "Item";
		in.columnHeaders[1] = "Price";
		in.columnItemCount[0] = 4;
		in.columnItemCount[1] = 4;
		in.items[0][0] = "Row 0";
		in.items[0][1] = "Row 1";
		in.items[0][2] = "Row 2 (disabled)";
		in.items[0][3] = "Row 3";
		in.items[1][0] = "$10";
		in.items[1][1] = "$20";
		in.items[1][2] = "$30";
		in.items[1][3] = "$40";

		const auto out = roundTrip(in);
		CHECK_EQ(out.menuId, 3);
		CHECK(out.hasTwoColumns);
		CHECK(out.title == "Parity menu");
		CHECK_NEAR(out.position.x, 200.0, 0.0);
		CHECK_NEAR(out.col1Width, 150.0, 0.0);
		CHECK_NEAR(out.col2Width, 140.0, 0.0);
		CHECK(out.menuEnabled);
		CHECK(out.rowEnabled[0] && out.rowEnabled[1] && !out.rowEnabled[2] && out.rowEnabled[3]);
		CHECK(out.columnHeaders[0] == "Item");
		CHECK(out.columnHeaders[1] == "Price");
		CHECK_EQ(out.columnItemCount[0], 4);
		CHECK_EQ(out.columnItemCount[1], 4);
		CHECK(out.items[0][2] == "Row 2 (disabled)");
		CHECK(out.items[1][3] == "$40");
		expectTruncatedFails(in, 8);
	}
	// 1-column menu (col2 omitted)
	{
		proto::InitMenu in{};
		in.menuId = 1;
		in.hasTwoColumns = false;
		in.title = "Single";
		in.position = {100.0f, 80.0f};
		in.col1Width = 180.0f;
		in.menuEnabled = true;
		in.rowEnabled.fill(true);
		in.columnHeaders[0] = "Options";
		in.columnItemCount[0] = 2;
		in.items[0][0] = "First";
		in.items[0][1] = "Second";

		const auto out = roundTrip(in);
		CHECK_EQ(out.menuId, 1);
		CHECK(!out.hasTwoColumns);
		CHECK(out.columnHeaders[0] == "Options");
		CHECK_EQ(out.columnItemCount[0], 2);
		CHECK_EQ(out.columnItemCount[1], 0);
		CHECK(out.items[0][1] == "Second");
		expectTruncatedFails(in, 8);
	}
	{
		proto::ShowMenu in{3};
		CHECK_EQ(roundTrip(in).menuId, 3);
		expectTruncatedFails(in);
	}
	{
		proto::HideMenu in{3};
		CHECK_EQ(roundTrip(in).menuId, 3);
		expectTruncatedFails(in);
	}
}

int main()
{
	StringCompressor::AddReference();
	RUN(testObjects);
	RUN(testActors);
	RUN(testTextDraws);
	RUN(testMenus);
	StringCompressor::RemoveReference();
	return check::finish();
}

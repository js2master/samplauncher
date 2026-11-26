// Oracle tests for F02 Batch 5: official open.mp write() -> client proto::* serialize().
#include "check.h"

#include <sdk.hpp>
#include <bitstream.hpp>
#include <netcode.hpp>

#include "Net/Proto/RpcActor.h"
#include "Net/Proto/RpcMenu.h"
#include "Net/Proto/RpcObject.h"
#include "Net/Proto/RpcTextDraw.h"

#include <cstdint>

static proto::Reader makeReader(NetworkBitStream& bs)
{
	return proto::Reader(bs.GetData(), static_cast<uint32_t>(bs.GetNumberOfBitsUsed()));
}

static void oracleObjects()
{
	{
		StaticArray<ObjectMaterialData, MAX_OBJECT_MATERIAL_SLOTS> mats{};
		mats[0].used = true;
		mats[0].type = ObjectMaterialData::Type::Default;
		mats[0].model = 19377;
		mats[0].textOrTXD = "all_walls";
		mats[0].fontOrTexture = "ab_kitchFloor";
		mats[0].materialColour = Colour(0x11, 0x22, 0x33, 0xFF);

		mats[2].used = true;
		mats[2].type = ObjectMaterialData::Type::Text;
		mats[2].materialSize = 90;
		mats[2].fontOrTexture = "Arial";
		mats[2].fontSize = 24;
		mats[2].bold = true;
		mats[2].fontColour = Colour(0xFF, 0xFF, 0xFF, 0xFF);
		mats[2].backgroundColour = Colour(0x00, 0x00, 0x00, 0xFF);
		mats[2].alignment = 1;
		mats[2].textOrTXD = "PARITY SIGN";

		NetCode::RPC::CreateObject omp(mats, 2, false);
		omp.ObjectID = 10;
		omp.ModelID = 1221;
		omp.Position = Vector3(1.0f, 2.0f, 3.0f);
		omp.Rotation = Vector3(4.0f, 5.0f, 6.0f);
		omp.DrawDistance = 200.0f;
		omp.CameraCollision = false;
		omp.AttachmentData.type = ObjectAttachmentData::Type::Vehicle;
		omp.AttachmentData.ID = 5;
		omp.AttachmentData.offset = Vector3(0.1f, 0.2f, 0.3f);
		omp.AttachmentData.rotation = Vector3(10.0f, 20.0f, 30.0f);
		omp.AttachmentData.syncRotation = true;

		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::CreateObject p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.objectId, 10);
		CHECK_EQ(p.modelId, 1221);
		CHECK_EQ(p.noCameraCol, 1);
		CHECK_EQ(p.attachedVehicleId, 5);
		CHECK_EQ(p.attachedObjectId, 0xFFFF);
		CHECK_EQ(p.syncRotation, 1);
		CHECK_EQ(p.materials.size(), 2u);
		CHECK_EQ(p.materials[0].materialIndex, 0);
		CHECK_EQ(p.materials[0].modelId, 19377);
		CHECK(p.materials[0].txdName == "all_walls");
		CHECK(p.materials[0].textureName == "ab_kitchFloor");
		CHECK_EQ(p.materials[0].materialColor, mats[0].materialColour.ABGR());
		CHECK_EQ(p.materials[1].materialIndex, 2);
		CHECK(p.materials[1].fontName == "Arial");
		CHECK(p.materials[1].text == "PARITY SIGN");
	}
	{
		// Spec-accurate SetObjectPos (task T36) matches open.mp 14-byte payload cleanly.
		NetCode::RPC::SetObjectPosition omp;
		omp.ObjectID = 10;
		omp.Position = Vector3(11.0f, 22.0f, 33.0f);
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::SetObjectPos p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.objectId, 10);
		CHECK_NEAR(p.position.z, 33.0, 0.0);
	}
	{
		NetCode::RPC::SetObjectRotation omp;
		omp.ObjectID = 10;
		omp.Rotation = Vector3(1.0f, 2.0f, 3.0f);
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::SetObjectRotation p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.objectId, 10);
		CHECK_NEAR(p.rotation.z, 3.0, 0.0);
	}
	{
		NetCode::RPC::DestroyObject omp;
		omp.ObjectID = 10;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::DestroyObject p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.objectId, 10);
	}
	{
		NetCode::RPC::MoveObject omp;
		omp.ObjectID = 10;
		omp.CurrentPosition = Vector3(1.0f, 2.0f, 3.0f);
		omp.MoveData.targetPos = Vector3(4.0f, 5.0f, 6.0f);
		omp.MoveData.speed = 3.5f;
		omp.MoveData.targetRot = Vector3(7.0f, 8.0f, 9.0f);
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::MoveObject p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.objectId, 10);
		CHECK_NEAR(p.speed, 3.5, 0.0);
		CHECK_NEAR(p.targetRot.z, 9.0, 0.0);
	}
	{
		NetCode::RPC::StopObject omp;
		omp.ObjectID = 10;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::StopObject p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.objectId, 10);
	}
	{
		NetCode::RPC::AttachObjectToPlayer omp;
		omp.ObjectID = 10;
		omp.PlayerID = 3;
		omp.Offset = Vector3(0.5f, 0.0f, 1.0f);
		omp.Rotation = Vector3(0.0f, 90.0f, 0.0f);
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::AttachObjectToPlayer p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.objectId, 10);
		CHECK_EQ(p.playerId, 3);
		CHECK_NEAR(p.rotation.y, 90.0, 0.0);
	}
	{
		ObjectMaterialData mat{};
		mat.type = ObjectMaterialData::Type::Text;
		mat.materialSize = 90;
		mat.fontOrTexture = "Arial";
		mat.fontSize = 24;
		mat.bold = true;
		mat.fontColour = Colour(0xFF, 0xFF, 0xFF, 0xFF);
		mat.backgroundColour = Colour(0x00, 0x00, 0x80, 0xFF);
		mat.alignment = 1;
		mat.textOrTXD = "HELLO";
		NetCode::RPC::SetPlayerObjectMaterial omp(mat);
		omp.ObjectID = 10;
		omp.MaterialID = 1;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::SetObjectMaterial p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.objectId, 10);
		CHECK_EQ(p.material.materialIndex, 1);
		CHECK(p.material.text == "HELLO");
	}
	{
		NetCode::RPC::SetPlayerAttachedObject omp;
		omp.PlayerID = 1;
		omp.Index = 2;
		omp.Create = true;
		omp.AttachmentData.model = 18939;
		omp.AttachmentData.bone = 2;
		omp.AttachmentData.offset = Vector3(0.1f, 0.2f, 0.3f);
		omp.AttachmentData.rotation = Vector3(1.0f, 2.0f, 3.0f);
		omp.AttachmentData.scale = Vector3(1.0f, 1.0f, 1.0f);
		omp.AttachmentData.colour1 = Colour(0xFF, 0x00, 0x00, 0xFF);
		omp.AttachmentData.colour2 = Colour(0x00, 0xFF, 0x00, 0xFF);
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::SetPlayerAttachedObject p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK(p.create);
		CHECK_EQ(p.modelId, 18939);
		CHECK_EQ(p.materialColor1, omp.AttachmentData.colour1.ABGR());
	}
	{
		NetCode::RPC::PlayerBeginAttachedObjectEdit omp;
		omp.Index = 3;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::EditAttachedObject p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.index, 3);
	}
	{
		NetCode::RPC::PlayerBeginObjectEdit omp;
		omp.PlayerObject = true;
		omp.ObjectID = 55;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::EditObject p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK(p.playerObject);
		CHECK_EQ(p.objectId, 55);
	}
	{
		NetCode::RPC::PlayerBeginObjectSelect omp;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::EnterEditObject p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
	}
	{
		NetCode::RPC::PlayerCancelObjectEdit omp;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::CancelEditObject p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
	}
}

static void oracleActors()
{
	for (bool isDL : {false, true}) {
		const auto ver = isDL ? proto::ProtoVersion::V03DL : proto::ProtoVersion::V037;
		NetCode::RPC::ShowActorForPlayer omp(isDL);
		omp.ActorID = 4;
		omp.SkinID = 162;
		omp.CustomSkin = 25009;
		omp.Position = Vector3(1.5f, -2.5f, 3.5f);
		omp.Angle = 45.0f;
		omp.Health = 100.0f;
		omp.Invulnerable = true;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::ShowActor p{};
		CHECK(p.serialize(r, ver));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.actorId, 4);
		CHECK_EQ(p.skin, 162);
		if (isDL) CHECK_EQ(p.customSkin, 25009);
		CHECK_NEAR(p.angle, 45.0, 0.0);
		CHECK_EQ(p.invulnerable, 1);
	}
	{
		NetCode::RPC::HideActorForPlayer omp;
		omp.ActorID = 4;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::HideActor p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.actorId, 4);
	}
	{
		AnimationData anim(4.1f, true, false, false, false, 0, "DANCING", "dance_loop");
		NetCode::RPC::ApplyActorAnimationForPlayer omp(anim);
		omp.ActorID = 4;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::ApplyActorAnimation p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.actorId, 4);
		CHECK(p.animLib == "DANCING");
		CHECK(p.animName == "dance_loop");
		CHECK(p.loop);
	}
	{
		NetCode::RPC::ClearActorAnimationsForPlayer omp;
		omp.ActorID = 4;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::ClearActorAnimation p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.actorId, 4);
	}
	{
		NetCode::RPC::SetActorFacingAngleForPlayer omp;
		omp.ActorID = 4;
		omp.Angle = 90.0f;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::SetActorFacingAngle p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.actorId, 4);
		CHECK_NEAR(p.angle, 90.0, 0.0);
	}
	{
		NetCode::RPC::SetActorPosForPlayer omp;
		omp.ActorID = 4;
		omp.Pos = Vector3(10.0f, 20.0f, 30.0f);
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::SetActorPos p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.actorId, 4);
		CHECK_NEAR(p.position.z, 30.0, 0.0);
	}
	{
		NetCode::RPC::SetActorHealthForPlayer omp;
		omp.ActorID = 4;
		omp.Health = 50.0f;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::SetActorHealth p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.actorId, 4);
		CHECK_NEAR(p.health, 50.0, 0.0);
	}
}

static void oracleTextDraws()
{
	{
		NetCode::RPC::PlayerShowTextDraw omp;
		omp.PlayerTextDraw = false;
		omp.TextDrawID = 7;
		omp.UseBox = true;
		omp.Alignment = 2;
		omp.Proportional = true;
		omp.LetterSize = Vector2(0.4f, 1.2f);
		omp.LetterColour = Colour(0xFF, 0xFF, 0xFF, 0xFF);
		omp.TextSize = Vector2(320.0f, 20.0f);
		omp.BoxColour = Colour(0x00, 0x00, 0x00, 0x80);
		omp.Shadow = 1;
		omp.Outline = 1;
		omp.BackgroundColour = Colour(0x00, 0x00, 0x00, 0xFF);
		omp.Style = 1;
		omp.Selectable = true;
		omp.Position = Vector2(320.0f, 410.0f);
		omp.Model = 0;
		omp.Rotation = Vector3(0.0f, 0.0f, 0.0f);
		omp.Zoom = 1.0f;
		omp.Color1 = 0xFFFF;
		omp.Color2 = 0xFFFF;
		omp.Text = StringView("PARITY TEST TD");

		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::ShowTextDraw p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.textDrawId, 7);
		CHECK_EQ(p.letterColor, omp.LetterColour.ABGR());
		CHECK(p.text == "PARITY TEST TD");
	}
	{
		NetCode::RPC::PlayerHideTextDraw omp;
		omp.PlayerTextDraw = false;
		omp.TextDrawID = 7;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::HideTextDraw p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.textDrawId, 7);
	}
	{
		NetCode::RPC::PlayerTextDrawSetString omp;
		omp.PlayerTextDraw = true;
		omp.TextDrawID = 3;
		omp.Text = StringView("Updated TD");
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::TextDrawSetString p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.textDrawId, GLOBAL_TEXTDRAW_POOL_SIZE + 3);
		CHECK(p.text == "Updated TD");
	}
	{
		NetCode::RPC::PlayerBeginTextDrawSelect omp;
		omp.Enable = true;
		omp.Col = Colour(0xFF, 0x88, 0x00, 0xFF);
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::SelectTextDraw p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK(p.enable);
		CHECK_EQ(p.hoverColor, omp.Col.RGBA());
	}
}

static void oracleMenus()
{
	// 2-column menu matching parity.pwn /t11
	{
		NetCode::RPC::PlayerInitMenu omp{};
		omp.MenuID = 2;
		omp.HasTwoColumns = true;
		omp.Title = StringView("Parity menu");
		omp.Position = Vector2(200.0f, 120.0f);
		omp.Col1Width = 150.0f;
		omp.Col2Width = 150.0f;
		omp.MenuEnabled = true;
		for (size_t i = 0; i < MAX_MENU_ITEMS; ++i) omp.RowEnabled[i] = true;
		omp.RowEnabled[2] = false;
		omp.ColumnHeaders[0] = StringView("Item");
		omp.ColumnHeaders[1] = StringView("Price");
		omp.ColumnItemCount[0] = 4;
		omp.ColumnItemCount[1] = 4;
		omp.MenuItems[0][0] = StringView("Row 0");
		omp.MenuItems[0][1] = StringView("Row 1");
		omp.MenuItems[0][2] = StringView("Row 2 (disabled)");
		omp.MenuItems[0][3] = StringView("Row 3");
		omp.MenuItems[1][0] = StringView("$10");
		omp.MenuItems[1][1] = StringView("$20");
		omp.MenuItems[1][2] = StringView("$30");
		omp.MenuItems[1][3] = StringView("$40");

		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::InitMenu p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.menuId, 2);
		CHECK(p.hasTwoColumns);
		CHECK(p.title == "Parity menu");
		CHECK_NEAR(p.position.x, 200.0, 0.0);
		CHECK_NEAR(p.position.y, 120.0, 0.0);
		CHECK_NEAR(p.col1Width, 150.0, 0.0);
		CHECK_NEAR(p.col2Width, 150.0, 0.0);
		CHECK(p.menuEnabled);
		CHECK(p.rowEnabled[0] && p.rowEnabled[1] && !p.rowEnabled[2] && p.rowEnabled[3]);
		CHECK(p.columnHeaders[0] == "Item");
		CHECK(p.columnHeaders[1] == "Price");
		CHECK_EQ(p.columnItemCount[0], 4);
		CHECK_EQ(p.columnItemCount[1], 4);
		CHECK(p.items[0][2] == "Row 2 (disabled)");
		CHECK(p.items[1][3] == "$40");
	}
	// 1-column menu
	{
		NetCode::RPC::PlayerInitMenu omp{};
		omp.MenuID = 5;
		omp.HasTwoColumns = false;
		omp.Title = StringView("Solo");
		omp.Position = Vector2(50.0f, 60.0f);
		omp.Col1Width = 120.0f;
		omp.MenuEnabled = true;
		for (size_t i = 0; i < MAX_MENU_ITEMS; ++i) omp.RowEnabled[i] = true;
		omp.ColumnHeaders[0] = StringView("Col0");
		omp.ColumnItemCount[0] = 2;
		omp.MenuItems[0][0] = StringView("Alpha");
		omp.MenuItems[0][1] = StringView("Beta");

		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::InitMenu p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.menuId, 5);
		CHECK(!p.hasTwoColumns);
		CHECK(p.title == "Solo");
		CHECK(p.items[0][0] == "Alpha");
		CHECK(p.items[0][1] == "Beta");
	}
	{
		NetCode::RPC::PlayerShowMenu omp{};
		omp.MenuID = 2;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::ShowMenu p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.menuId, 2);
	}
	{
		NetCode::RPC::PlayerHideMenu omp{};
		omp.MenuID = 2;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::HideMenu p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.menuId, 2);
	}
}

int main()
{
	StringCompressor::AddReference();
	RUN(oracleObjects);
	RUN(oracleActors);
	RUN(oracleTextDraws);
	RUN(oracleMenus);
	StringCompressor::RemoveReference();
	return check::finish();
}

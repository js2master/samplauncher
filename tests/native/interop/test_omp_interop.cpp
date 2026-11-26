// Oracle tests: packets are written by the official open.mp server code
// (reference/open.mp, Shared/NetCode + Shared/Network) and read back with the
// client's RakNet BitStream, field by field in the order the client reads them.
// A mismatch here means the two sides disagree about the wire format.
#include "check.h"

#include <sdk.hpp>
#include <bitstream.hpp>
#include <netcode.hpp>

#include <RakNet/BitStream.h>
#include <RakNet/StringCompressor.h>

#include <cstdint>
#include <cstring>
#include <string>

static RakNet::BitStream clientView(NetworkBitStream& bs)
{
	return RakNet::BitStream(bs.GetData(), static_cast<unsigned int>(bs.GetNumberOfBytesUsed()), true);
}

// The client stream is built from whole bytes, so up to 7 padding bits remain
// after the last field. Anything beyond that padding is data the client did not read.
static int unreadPayloadBits(RakNet::BitStream& r, NetworkBitStream& w)
{
	const int padding = w.GetNumberOfBytesUsed() * 8 - w.GetNumberOfBitsUsed();
	return r.GetNumberOfUnreadBits() - padding;
}

static bool readDynStr8(RakNet::BitStream& r, std::string& out)
{
	uint8_t len = 0;
	char buf[256] = {};
	if (!r.Read(len)) return false;
	if (len && !r.Read(buf, len)) return false;
	out.assign(buf, len);
	return true;
}

static void primitives()
{
	NetworkBitStream w;
	w.writeUINT8(0xAB);
	w.writeUINT16(0xBEEF);
	w.writeUINT32(0xDEADBEEF);
	w.writeINT32(-123456);
	w.writeFLOAT(3.25f);
	w.writeBIT(true);
	w.writeBIT(false);
	w.writeVEC3(Vector3(1.5f, -2.5f, 3.5f));
	w.writeDynStr8(StringView("hello"));

	RakNet::BitStream r = clientView(w);
	uint8_t u8 = 0;
	uint16_t u16 = 0;
	uint32_t u32 = 0;
	int32_t i32 = 0;
	float f = 0, x = 0, y = 0, z = 0;
	bool b1 = false, b2 = true;
	std::string s;
	CHECK(r.Read(u8) && r.Read(u16) && r.Read(u32) && r.Read(i32) && r.Read(f));
	CHECK(r.Read(b1) && r.Read(b2));
	CHECK(r.Read(x) && r.Read(y) && r.Read(z));
	CHECK(readDynStr8(r, s));
	CHECK_EQ(u8, 0xAB);
	CHECK_EQ(u16, 0xBEEF);
	CHECK_EQ(u32, 0xDEADBEEF);
	CHECK_EQ(i32, -123456);
	CHECK_NEAR(f, 3.25, 0.0);
	CHECK(b1 && !b2);
	CHECK_NEAR(x, 1.5, 0.0);
	CHECK_NEAR(y, -2.5, 0.0);
	CHECK_NEAR(z, 3.5, 0.0);
	CHECK(s == "hello");
	CHECK_EQ(unreadPayloadBits(r, w), 0);
}

// Sync packets: compressed quaternion and compressed velocity vector.
static void compressedQuatAndVector()
{
	NetworkBitStream w;
	w.writeGTAQuat(GTAQuat(0.5f, -0.5f, 0.5f, 0.5f));
	w.writeCompressedVEC3(Vector3(0.3f, -0.4f, 1.2f));
	w.writeCompressedVEC3(Vector3(0.0f, 0.0f, 0.0f));

	RakNet::BitStream r = clientView(w);
	float qw = 0, qx = 0, qy = 0, qz = 0;
	float vx = 9, vy = 9, vz = 9;
	float zx = 9, zy = 9, zz = 9;
	CHECK(r.ReadNormQuat(qw, qx, qy, qz));
	CHECK(r.ReadVector(vx, vy, vz));
	CHECK(r.ReadVector(zx, zy, zz));
	CHECK_NEAR(qw, 0.5, 1e-3);
	CHECK_NEAR(qx, -0.5, 1e-3);
	CHECK_NEAR(qy, 0.5, 1e-3);
	CHECK_NEAR(qz, 0.5, 1e-3);
	CHECK_NEAR(vx, 0.3, 1e-3);
	CHECK_NEAR(vy, -0.4, 1e-3);
	CHECK_NEAR(vz, 1.2, 1e-3);
	CHECK_NEAR(zx, 0.0, 1e-6);
	CHECK_NEAR(zy, 0.0, 1e-6);
	CHECK_NEAR(zz, 0.0, 1e-6);
	CHECK_EQ(unreadPayloadBits(r, w), 0);
}

// On-foot sync packs health and armour into one byte.
static void healthArmourNibbles()
{
	NetworkBitStream w;
	w.writeCompressedPercentPair(Vector2(100.0f, 0.0f));
	w.writeCompressedPercentPair(Vector2(49.0f, 100.0f));

	RakNet::BitStream r = clientView(w);
	uint8_t a = 0, b = 0;
	CHECK(r.Read(a) && r.Read(b));
	CHECK_EQ(a >> 4, 0xF);
	CHECK_EQ(a & 0xF, 0);
	CHECK_EQ(b >> 4, 7); // 49 / 7
	CHECK_EQ(b & 0xF, 0xF);
}

// RPC 61 ShowDialog, read the way A/Net/NetRPC.cpp DialogBox reads it.
static void showDialog()
{
	NetCode::RPC::ShowDialog p;
	p.ID = 7001;
	p.Style = 2;
	p.Title = StringView("Title");
	p.FirstButton = StringView("OK");
	p.SecondButton = StringView("");
	p.Body = StringView("Body with {FF0000}colour\nand a second line");
	NetworkBitStream w;
	p.write(w);

	RakNet::BitStream r = clientView(w);
	int16_t id = 0;
	uint8_t style = 0;
	std::string title, b1, b2;
	char body[4097] = {};
	CHECK(r.Read(id) && r.Read(style));
	CHECK(readDynStr8(r, title) && readDynStr8(r, b1) && readDynStr8(r, b2));
	CHECK(stringCompressor->DecodeString(body, 4096, &r));
	CHECK_EQ(id, 7001);
	CHECK_EQ(style, 2);
	CHECK(title == "Title" && b1 == "OK" && b2.empty());
	CHECK(strcmp(body, "Body with {FF0000}colour\nand a second line") == 0);

	// HidePlayerDialog is the same packet with id -1 (task T01)
	NetCode::RPC::ShowDialog hide;
	hide.ID = -1;
	hide.Style = 0;
	NetworkBitStream w2;
	hide.write(w2);
	RakNet::BitStream r2 = clientView(w2);
	int16_t hideId = 0;
	CHECK(r2.Read(hideId));
	CHECK_EQ(hideId, -1);
}

// RPC 62 dialog response, written the way NetGame::SendDialogResponse writes it,
// read by the server's own read().
static void dialogResponseToServer()
{
	RakNet::BitStream w;
	const char* text = "typed text";
	const uint8_t len = static_cast<uint8_t>(strlen(text));
	w.Write(static_cast<uint16_t>(7001));
	w.Write(static_cast<uint8_t>(1));
	w.Write(static_cast<uint16_t>(3));
	w.Write(len);
	w.Write(text, len);

	NetworkBitStream r(w.GetData(), static_cast<unsigned int>(w.GetNumberOfBytesUsed()), true);
	NetCode::RPC::OnPlayerDialogResponse p;
	CHECK(p.read(r));
	CHECK_EQ(p.ID, 7001);
	CHECK_EQ(p.Response, 1);
	CHECK_EQ(p.ListItem, 3);
	CHECK(StringView(p.Text) == StringView("typed text"));
}

// RPC 153 SetPlayerSkin: the layout differs between 0.3.7 and 0.3.DL (task T03).
// This pins what the server sends; the client's current reader is fixed in T03.
static void setPlayerSkinLayouts()
{
	NetCode::RPC::SetPlayerSkin p;
	p.PlayerID = 7;
	p.Skin = 123;
	p.CustomSkin = 0;

	p.isDL = false;
	NetworkBitStream w037;
	p.write(w037);
	CHECK_EQ(w037.GetNumberOfBitsUsed(), 64);
	RakNet::BitStream r037 = clientView(w037);
	uint32_t id32 = 0, skin = 0;
	CHECK(r037.Read(id32) && r037.Read(skin));
	CHECK_EQ(id32, 7);
	CHECK_EQ(skin, 123);

	p.isDL = true;
	NetworkBitStream wDL;
	p.write(wDL);
	CHECK_EQ(wDL.GetNumberOfBitsUsed(), 80);
	RakNet::BitStream rDL = clientView(wDL);
	uint16_t id16 = 0;
	uint32_t skinDL = 0, custom = 1;
	CHECK(rDL.Read(id16) && rDL.Read(skinDL) && rDL.Read(custom));
	CHECK_EQ(id16, 7);
	CHECK_EQ(skinDL, 123);
	CHECK_EQ(custom, 0);
}

// RPC 139 InitGame, read in the order A/Net/NetRPC.cpp InitGame reads it.
static void initGame()
{
	NetCode::RPC::PlayerInit p;
	p.EnableZoneNames = true;
	p.UsePlayerPedAnims = false;
	p.AllowInteriorWeapons = true;
	p.UseLimitGlobalChatRadius = false;
	p.LimitGlobalChatRadius = 200.0f;
	p.EnableStuntBonus = true;
	p.SetNameTagDrawDistance = 70.0f;
	p.DisableInteriorEnterExits = false;
	p.DisableNameTagLOS = true;
	p.ManualVehicleEngineAndLights = false;
	p.SetSpawnInfoCount = 2;
	p.PlayerID = 5;
	p.ShowNameTags = true;
	p.ShowPlayerMarkers = 1;
	p.SetWorldTime = 12;
	p.SetWeather = 10;
	p.SetGravity = 0.008f;
	p.LanMode = false;
	p.SetDeathDropAmount = 0;
	p.Instagib = false;
	p.OnFootRate = 30;
	p.InCarRate = 31;
	p.WeaponRate = 32;
	p.Multiplier = 10;
	p.LagCompensation = 1;
	p.ServerName = StringView("parity server");
	p.VehicleModels.fill(0);
	p.VehicleModels[0] = 1;
	p.VehicleModels[211] = 9;
	p.EnableVehicleFriendlyFire = true;
	NetworkBitStream w;
	p.write(w);

	RakNet::BitStream r = clientView(w);
	bool zoneNames = false, cjWalk = true, allowWeapons = false, limitChat = true, stunt = false;
	bool disableEnex = true, nineth = false, manualEngine = true, showTags = false, lan = true, instagib = true;
	float chatRadius = 0, tagDist = 0, gravity = 0;
	int spawns = 0, markers = 0, dropMoney = 1, onFoot = 0, inCar = 0, firing = 0, mult = 0, lag = 0;
	uint16_t myId = 0;
	uint8_t hour = 0, weather = 0;
	CHECK(r.Read(zoneNames) && r.Read(cjWalk) && r.Read(allowWeapons) && r.Read(limitChat) && r.Read(chatRadius));
	CHECK(r.Read(stunt) && r.Read(tagDist) && r.Read(disableEnex) && r.Read(nineth) && r.Read(manualEngine));
	CHECK(r.Read(spawns) && r.Read(myId) && r.Read(showTags) && r.Read(markers) && r.Read(hour) && r.Read(weather));
	CHECK(r.Read(gravity) && r.Read(lan) && r.Read(dropMoney) && r.Read(instagib));
	CHECK(r.Read(onFoot) && r.Read(inCar) && r.Read(firing) && r.Read(mult) && r.Read(lag));
	std::string host;
	CHECK(readDynStr8(r, host));
	uint8_t models[212] = {};
	CHECK(r.Read(reinterpret_cast<char*>(models), 212));
	int vehFF = 0;
	CHECK(r.Read(vehFF));
	CHECK_EQ(unreadPayloadBits(r, w), 0);

	CHECK(zoneNames && !cjWalk && allowWeapons && !limitChat && stunt);
	CHECK_NEAR(chatRadius, 200.0, 0.0);
	CHECK_NEAR(tagDist, 70.0, 0.0);
	CHECK(!disableEnex && !manualEngine && showTags && !lan && !instagib);
	// The 9th field is DisableNameTagLOS on the server side (task T35).
	CHECK(nineth == p.DisableNameTagLOS);
	CHECK_EQ(spawns, 2);
	CHECK_EQ(myId, 5);
	CHECK_EQ(markers, 1);
	CHECK_EQ(hour, 12);
	CHECK_EQ(weather, 10);
	CHECK_NEAR(gravity, 0.008, 1e-7);
	CHECK_EQ(dropMoney, 0);
	CHECK_EQ(onFoot, 30);
	CHECK_EQ(inCar, 31);
	CHECK_EQ(firing, 32);
	CHECK_EQ(mult, 10);
	CHECK_EQ(lag, 1);
	CHECK(host == "parity server");
	CHECK_EQ(models[0], 1);
	CHECK_EQ(models[211], 9);
	CHECK_EQ(vehFF, 1);
}

int main()
{
	StringCompressor::AddReference();
	RUN(primitives);
	RUN(compressedQuatAndVector);
	RUN(healthArmourNibbles);
	RUN(showDialog);
	RUN(dialogResponseToServer);
	RUN(setPlayerSkinLayouts);
	RUN(initGame);
	StringCompressor::RemoveReference();
	return check::finish();
}

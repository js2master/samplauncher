// Oracle interop tests for 0.3.DL / open.mp Artwork & Custom Models RPCs.
// Compiles directly against reference/open.mp Shared/NetCode/custommodels.hpp and core/object/actor/class RPCs.
#include "check.h"

#include <sdk.hpp>
#include <bitstream.hpp>
#include <netcode.hpp>

#include "Net/Proto/RpcActor.h"
#include "Net/Proto/RpcClass.h"
#include "Net/Proto/RpcCore.h"
#include "Net/Proto/RpcObject.h"
#include "Net/Proto/RpcOutbound.h"
#include "Net/Proto/RpcPickup.h"

#include <RakNet/BitStream.h>

#include <cstdint>
#include <string>

static proto::Reader makeReader(const NetworkBitStream& bs)
{
	return proto::Reader(bs.GetData(), static_cast<uint32_t>(bs.GetNumberOfBitsUsed()));
}

template <typename ClientMsg>
static NetworkBitStream writeClientRpc(const ClientMsg& msg, proto::ProtoVersion ver = proto::ProtoVersion::V03DL)
{
	RakNet::BitStream bs;
	proto::Writer w(bs);
	ClientMsg copy = msg;
	CHECK(copy.serialize(w, ver));
	NetworkBitStream out(bs.GetData(), bs.GetNumberOfBytesUsed(), true);
	out.SetWriteOffset(bs.GetNumberOfBitsUsed());
	return out;
}

static void testArtworkProtocolInterop()
{
	// Outbound RequestDFF (RPC 181): client Writer -> open.mp RequestDFF::read
	{
		proto::DlRequestDffReq req{0x12345678u};
		auto bs = writeClientRpc(req, proto::ProtoVersion::V03DL);
		NetCode::RPC::RequestDFF omp{};
		CHECK(omp.read(bs));
		CHECK_EQ(omp.checksum, 0x12345678u);
		CHECK_EQ(bs.GetNumberOfUnreadBits(), 0);
	}

	// Outbound RequestTXD (RPC 182): client Writer -> open.mp RequestTXD::read
	{
		proto::DlRequestTxdReq req{0x9ABCDEF0u};
		auto bs = writeClientRpc(req, proto::ProtoVersion::V03DL);
		NetCode::RPC::RequestTXD omp{};
		CHECK(omp.read(bs));
		CHECK_EQ(omp.checksum, 0x9ABCDEF0u);
		CHECK_EQ(bs.GetNumberOfUnreadBits(), 0);
	}

	// Outbound FinishDownload (RPC 184): client Writer -> open.mp FinishDownload::read
	{
		proto::DlFinishDownloadReq req{};
		auto bs = writeClientRpc(req, proto::ProtoVersion::V03DL);
		NetCode::RPC::FinishDownload omp{};
		CHECK(omp.read(bs));
	}

	// Inbound ModelRequest (RPC 179) for both Skin (20001) and Timed Object (-2005)
	{
		NetCode::RPC::ModelRequest omp(0, 2);
		omp.type = 1; // Skin
		omp.virtualWorld = -1;
		omp.baseId = 29;
		omp.newId = 20001;
		omp.dffChecksum = 0xDEADBEEFu;
		omp.txdChecksum = 0xCAFEBABEu;
		omp.dffSize = 123456;
		omp.txdSize = 654321;
		omp.timeOn = 0;
		omp.timeOff = 0;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::DlModelRequest p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V03DL));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.type, 1u);
		CHECK_EQ(p.baseId, 29);
		CHECK_EQ(p.newId, 20001);
		CHECK_EQ(p.dffChecksum, 0xDEADBEEFu);
		CHECK_EQ(p.txdChecksum, 0xCAFEBABEu);
	}

	// Inbound ModelUrl (RPC 183) URL redirect + Abort + DirectBegin variants
	{
		NetCode::RPC::ModelUrl omp("http://192.168.1.5:7777/skin_test.dff", 1, 0x11223344u);
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::DlModelUrl p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V03DL));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.kind, proto::DlModelUrl::kKindUrlRedirect);
		CHECK_EQ(p.fileType, 1u);
		CHECK_EQ(p.fileChecksum, 0x11223344u);
		CHECK(p.downloadLink == "http://192.168.1.5:7777/skin_test.dff");
	}
	{
		// Abort sub-opcode (0)
		RakNet::BitStream bs;
		proto::Writer w(bs);
		proto::DlModelUrl src{};
		src.kind = proto::DlModelUrl::kKindAbort;
		CHECK(src.serialize(w, proto::ProtoVersion::V03DL));
		proto::Reader r(bs.GetData(), static_cast<uint32_t>(bs.GetNumberOfBitsUsed()));
		proto::DlModelUrl dst{};
		CHECK(dst.serialize(r, proto::ProtoVersion::V03DL));
		CHECK_EQ(dst.kind, proto::DlModelUrl::kKindAbort);
		CHECK_EQ(r.unreadBits(), 0);
	}

	// Inbound SetPlayerSkin (RPC 153) in 0.3.DL mode (uint16 playerId + baseSkin + customSkin)
	{
		NetCode::RPC::SetPlayerSkin omp{};
		omp.isDL = true;
		omp.PlayerID = 42;
		omp.Skin = 29;
		omp.CustomSkin = 20001;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::SetPlayerSkin p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V03DL));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.playerId, 42u);
		CHECK_EQ(p.skinId, 29u);
		CHECK_EQ(p.customSkinId, 20001u);
	}

	// Inbound CreateObject (RPC 44) with negative custom model ID (-2500)
	{
		StaticArray<ObjectMaterialData, MAX_OBJECT_MATERIAL_SLOTS> mats{};
		NetCode::RPC::CreateObject omp(mats, 0, true);
		omp.ObjectID = 7;
		omp.ModelID = -2500;
		omp.Position = {100.0f, 200.0f, 15.0f};
		omp.Rotation = {0.0f, 0.0f, 90.0f};
		omp.DrawDistance = 200.0f;
		omp.CameraCollision = true;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::CreateObject p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V03DL));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.objectId, 7u);
		CHECK_EQ(p.modelId, -2500);
	}

	// Inbound CreatePickup (RPC 95) with negative custom model ID (-1500)
	{
		NetCode::RPC::PlayerCreatePickup omp;
		omp.PickupID = 3;
		omp.Model = -1500;
		omp.Type = 1;
		omp.Position = {10.0f, 20.0f, 30.0f};
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::CreatePickup p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V03DL));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.model, -1500);
	}
}

int main()
{
	testArtworkProtocolInterop();
	return check::finish();
}

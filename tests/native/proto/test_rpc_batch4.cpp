#include "check.h"

#include "Net/Proto/RpcDecode.h"
#include "Net/Proto/RpcVehicle.h"

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

static void testWorldVehicleAndDamage()
{
	{
		proto::WorldVehicleAdd in{};
		in.vehicleId = 5;
		in.modelId = 560;
		in.position = {100.25f, -200.5f, 15.75f};
		in.rotation = 180.0f;
		in.interiorColor1 = 3;
		in.interiorColor2 = 1;
		in.health = 1000.0f;
		in.interior = 0;
		in.doorDamageStatus = 0x01020304u;
		in.panelDamageStatus = 0x11223344u;
		in.lightDamageStatus = 0x05;
		in.tireDamageStatus = 0x0A;
		in.addSiren = 1;
		for (uint8_t i = 0; i < 14; ++i) in.modSlots[i] = static_cast<uint8_t>(i + 1);
		in.paintJob = 2;
		in.bodyColor1 = 6;
		in.bodyColor2 = 7;

		const auto out = roundTrip(in);
		CHECK_EQ(out.vehicleId, 5);
		CHECK_EQ(out.modelId, 560);
		CHECK_NEAR(out.position.x, 100.25, 0.0);
		CHECK_NEAR(out.position.y, -200.5, 0.0);
		CHECK_NEAR(out.position.z, 15.75, 0.0);
		CHECK_NEAR(out.rotation, 180.0, 0.0);
		CHECK_EQ(out.interiorColor1, 3);
		CHECK_EQ(out.interiorColor2, 1);
		CHECK_NEAR(out.health, 1000.0, 0.0);
		CHECK_EQ(out.doorDamageStatus, 0x01020304u);
		CHECK_EQ(out.panelDamageStatus, 0x11223344u);
		CHECK_EQ(out.lightDamageStatus, 0x05);
		CHECK_EQ(out.tireDamageStatus, 0x0A);
		CHECK_EQ(out.addSiren, 1);
		for (size_t i = 0; i < 14; ++i) CHECK_EQ(out.modSlots[i], i + 1);
		CHECK_EQ(out.paintJob, 2);
		CHECK_EQ(out.bodyColor1, 6);
		CHECK_EQ(out.bodyColor2, 7);
		expectTruncatedFails(in);
	}
	{
		proto::WorldVehicleRemove in{};
		in.vehicleId = 5;
		CHECK_EQ(roundTrip(in).vehicleId, 5);
		expectTruncatedFails(in);
	}
	{
		proto::DamageVehicle in{};
		in.vehicleId = 5;
		in.panelDamageStatus = 0xAABBCCDDu;
		in.doorDamageStatus = 0x01020304u;
		in.lightDamageStatus = 5;
		in.tireDamageStatus = 10;
		const auto out = roundTrip(in);
		CHECK_EQ(out.vehicleId, 5);
		CHECK_EQ(out.panelDamageStatus, 0xAABBCCDDu);
		CHECK_EQ(out.doorDamageStatus, 0x01020304u);
		CHECK_EQ(out.lightDamageStatus, 5);
		CHECK_EQ(out.tireDamageStatus, 10);
		expectTruncatedFails(in);
	}
	{
		proto::VehicleParamsEx in{};
		in.vehicleId = 5;
		in.engine = 1;
		in.lights = 1;
		in.alarm = 0;
		in.doors = 1;
		in.bonnet = 1;
		in.boot = 1;
		in.objective = 0;
		in.siren = 1;
		in.doorDriver = 1;
		in.doorPassenger = 0;
		in.doorBackLeft = 1;
		in.doorBackRight = 0;
		in.windowDriver = 1;
		in.windowPassenger = 1;
		in.windowBackLeft = 0;
		in.windowBackRight = 1;
		const auto out = roundTrip(in);
		CHECK_EQ(out.vehicleId, 5);
		CHECK_EQ(out.engine, 1);
		CHECK_EQ(out.bonnet, 1);
		CHECK_EQ(out.boot, 1);
		CHECK_EQ(out.siren, 1);
		CHECK_EQ(out.doorBackLeft, 1);
		CHECK_EQ(out.windowBackRight, 1);
		expectTruncatedFails(in);
	}
}

static void testVehicleEventsAndProperties()
{
	{
		proto::EnterVehicle in{};
		in.playerId = 3;
		in.vehicleId = 7;
		in.passenger = 1;
		const auto out = roundTrip(in);
		CHECK_EQ(out.playerId, 3);
		CHECK_EQ(out.vehicleId, 7);
		CHECK_EQ(out.passenger, 1);
		expectTruncatedFails(in);
	}
	{
		proto::ExitVehicle in{};
		in.playerId = 3;
		in.vehicleId = 7;
		const auto out = roundTrip(in);
		CHECK_EQ(out.playerId, 3);
		CHECK_EQ(out.vehicleId, 7);
		expectTruncatedFails(in);
	}
	{
		proto::ScmEvent in{};
		in.playerId = 1;
		in.eventType = 2;
		in.param1 = 5;
		in.param2 = 1010;
		in.param3 = 0;
		const auto out = roundTrip(in);
		CHECK_EQ(out.playerId, 1);
		CHECK_EQ(out.eventType, 2);
		CHECK_EQ(out.param1, 5u);
		CHECK_EQ(out.param2, 1010u);
		CHECK_EQ(out.param3, 0u);
		expectTruncatedFails(in);
	}
	{
		proto::SetVehiclePos in{};
		in.vehicleId = 9;
		in.position = {1.5f, 2.5f, 3.5f};
		const auto out = roundTrip(in);
		CHECK_EQ(out.vehicleId, 9);
		CHECK_NEAR(out.position.z, 3.5, 0.0);
		expectTruncatedFails(in);
	}
	{
		proto::SetVehicleZAngle in{};
		in.vehicleId = 9;
		in.angle = 270.25f;
		const auto out = roundTrip(in);
		CHECK_EQ(out.vehicleId, 9);
		CHECK_NEAR(out.angle, 270.25, 0.0);
		expectTruncatedFails(in);
	}
	{
		proto::SetVehicleParamsForPlayer in{};
		in.vehicleId = 9;
		in.objective = 1;
		in.doorsLocked = 1;
		const auto out = roundTrip(in);
		CHECK_EQ(out.vehicleId, 9);
		CHECK_EQ(out.objective, 1);
		CHECK_EQ(out.doorsLocked, 1);
		expectTruncatedFails(in);
	}
	{
		proto::SetVehicleHealth in{};
		in.vehicleId = 9;
		in.health = 600.0f;
		const auto out = roundTrip(in);
		CHECK_EQ(out.vehicleId, 9);
		CHECK_NEAR(out.health, 600.0, 0.0);
		expectTruncatedFails(in);
	}
	{
		proto::SetVehicleVelocity in{};
		in.type = 1;
		in.velocity = {0.1f, -0.2f, 0.3f};
		const auto out = roundTrip(in);
		CHECK_EQ(out.type, 1);
		CHECK_NEAR(out.velocity.y, -0.2, 1e-6);
		expectTruncatedFails(in);
	}
	{
		proto::SetVehicleTireDamageStatus in{};
		in.vehicleId = 9;
		in.tireDamageStatus = 0x0F;
		const auto out = roundTrip(in);
		CHECK_EQ(out.vehicleId, 9);
		CHECK_EQ(out.tireDamageStatus, 0x0F);
		expectTruncatedFails(in);
	}
	{
		proto::SetVehicleNumberPlate in{};
		in.vehicleId = 9;
		in.plate = "PARITY-1";
		const auto out = roundTrip(in);
		CHECK_EQ(out.vehicleId, 9);
		CHECK(out.plate == "PARITY-1");
		expectTruncatedFails(in);
	}
	{
		proto::LinkVehicleToInterior in{};
		in.vehicleId = 9;
		in.interiorId = 3;
		const auto out = roundTrip(in);
		CHECK_EQ(out.vehicleId, 9);
		CHECK_EQ(out.interiorId, 3);
		expectTruncatedFails(in);
	}
	{
		proto::AttachTrailerToVehicle in{};
		in.trailerId = 12;
		in.vehicleId = 9;
		const auto out = roundTrip(in);
		CHECK_EQ(out.trailerId, 12);
		CHECK_EQ(out.vehicleId, 9);
		expectTruncatedFails(in);
	}
	{
		proto::DetachTrailerFromVehicle in{};
		in.vehicleId = 9;
		CHECK_EQ(roundTrip(in).vehicleId, 9);
		expectTruncatedFails(in);
	}
	{
		proto::RemoveVehicleComponent in{};
		in.vehicleId = 9;
		in.componentId = 1010;
		const auto out = roundTrip(in);
		CHECK_EQ(out.vehicleId, 9);
		CHECK_EQ(out.componentId, 1010);
		expectTruncatedFails(in);
	}
	{
		proto::PutPlayerInVehicle in{};
		in.vehicleId = 9;
		in.seatId = 0;
		const auto out = roundTrip(in);
		CHECK_EQ(out.vehicleId, 9);
		CHECK_EQ(out.seatId, 0);
		expectTruncatedFails(in);
	}
	{
		proto::RemovePlayerFromVehicle in{};
		roundTrip(in);
	}
}

int main()
{
	RUN(testWorldVehicleAndDamage);
	RUN(testVehicleEventsAndProperties);
	return check::finish();
}

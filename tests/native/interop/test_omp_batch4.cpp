// Oracle tests for F02 Batch 4: official open.mp write() -> client proto::* serialize().
#include "check.h"

#include <sdk.hpp>
#include <bitstream.hpp>
#include <netcode.hpp>

#include "Net/Proto/RpcVehicle.h"

#include <cstdint>

static proto::Reader makeReader(NetworkBitStream& bs)
{
	return proto::Reader(bs.GetData(), static_cast<uint32_t>(bs.GetNumberOfBitsUsed()));
}

static void oracleStreamAndDamage()
{
	{
		NetCode::RPC::StreamInVehicle omp;
		omp.VehicleID = 5;
		omp.ModelID = 560;
		omp.Position = Vector3(12.5f, -34.25f, 56.75f);
		omp.Angle = 90.0f;
		omp.Colour1 = 3;
		omp.Colour2 = 1;
		omp.Health = 950.5f;
		omp.Interior = 2;
		omp.DoorDamage = 0x11223344u;
		omp.PanelDamage = 0x55667788u;
		omp.LightDamage = 0x03;
		omp.TyreDamage = 0x0C;
		omp.Siren = 1;
		omp.Mods.fill(0);
		omp.Mods[0] = 1010;
		omp.Mods[3] = 1025;
		omp.Paintjob = 2;
		omp.BodyColour1 = 6;
		omp.BodyColour2 = 7;

		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::WorldVehicleAdd p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.vehicleId, 5);
		CHECK_EQ(p.modelId, 560);
		CHECK_NEAR(p.position.x, 12.5, 0.0);
		CHECK_NEAR(p.position.y, -34.25, 0.0);
		CHECK_NEAR(p.position.z, 56.75, 0.0);
		CHECK_NEAR(p.rotation, 90.0, 0.0);
		CHECK_EQ(p.interiorColor1, 3);
		CHECK_EQ(p.interiorColor2, 1);
		CHECK_NEAR(p.health, 950.5, 0.0);
		CHECK_EQ(p.interior, 2);
		CHECK_EQ(p.doorDamageStatus, 0x11223344u);
		CHECK_EQ(p.panelDamageStatus, 0x55667788u);
		CHECK_EQ(p.lightDamageStatus, 0x03);
		CHECK_EQ(p.tireDamageStatus, 0x0C);
		CHECK_EQ(p.addSiren, 1);
		CHECK_EQ(p.modSlots[0], 11);
		CHECK_EQ(p.modSlots[1], 0);
		CHECK_EQ(p.modSlots[3], 26);
		CHECK_EQ(p.paintJob, 2);
		CHECK_EQ(p.bodyColor1, 6);
		CHECK_EQ(p.bodyColor2, 7);
	}
	{
		NetCode::RPC::StreamOutVehicle omp;
		omp.VehicleID = 5;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::WorldVehicleRemove p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.vehicleId, 5);
	}
	{
		NetCode::RPC::SetVehicleDamageStatus omp;
		omp.VehicleID = 5;
		omp.PanelStatus = 0x12345678u;
		omp.DoorStatus = 0x87654321u;
		omp.LightStatus = 5;
		omp.TyreStatus = 10;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::DamageVehicle p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.vehicleId, 5);
		CHECK_EQ(p.panelDamageStatus, 0x12345678u);
		CHECK_EQ(p.doorDamageStatus, 0x87654321u);
		CHECK_EQ(p.lightDamageStatus, 5);
		CHECK_EQ(p.tireDamageStatus, 10);
	}
	{
		NetCode::RPC::SetVehicleParams omp;
		omp.VehicleID = 5;
		omp.params.engine = 1;
		omp.params.lights = 1;
		omp.params.alarm = 0;
		omp.params.doors = 1;
		omp.params.bonnet = 1;
		omp.params.boot = 1;
		omp.params.objective = 0;
		omp.params.siren = 1;
		omp.params.doorDriver = 1;
		omp.params.doorPassenger = 0;
		omp.params.doorBackLeft = 1;
		omp.params.doorBackRight = 0;
		omp.params.windowDriver = 1;
		omp.params.windowPassenger = 0;
		omp.params.windowBackLeft = 1;
		omp.params.windowBackRight = 1;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::VehicleParamsEx p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.vehicleId, 5);
		CHECK_EQ(p.engine, 1);
		CHECK_EQ(p.lights, 1);
		CHECK_EQ(p.bonnet, 1);
		CHECK_EQ(p.boot, 1);
		CHECK_EQ(p.siren, 1);
		CHECK_EQ(p.doorDriver, 1);
		CHECK_EQ(p.windowBackRight, 1);
	}
}

static void oracleVehicleActions()
{
	{
		NetCode::RPC::EnterVehicle omp;
		omp.PlayerID = 2;
		omp.VehicleID = 5;
		omp.Passenger = 1;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::EnterVehicle p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.playerId, 2);
		CHECK_EQ(p.vehicleId, 5);
		CHECK_EQ(p.passenger, 1);
	}
	{
		NetCode::RPC::ExitVehicle omp;
		omp.PlayerID = 2;
		omp.VehicleID = 5;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::ExitVehicle p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.playerId, 2);
		CHECK_EQ(p.vehicleId, 5);
	}
	{
		NetCode::RPC::SCMEvent omp;
		omp.PlayerID = 0;
		omp.EventType = 2;
		omp.VehicleID = 5;
		omp.Arg1 = 1010;
		omp.Arg2 = 0;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::ScmEvent p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.playerId, 0);
		CHECK_EQ(p.eventType, 2);
		CHECK_EQ(p.param1, 5u);
		CHECK_EQ(p.param2, 1010u);
		CHECK_EQ(p.param3, 0u);
	}
	{
		NetCode::RPC::SetVehiclePosition omp;
		omp.VehicleID = 5;
		omp.position = Vector3(10.0f, 20.0f, 30.0f);
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::SetVehiclePos p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.vehicleId, 5);
		CHECK_NEAR(p.position.x, 10.0, 0.0);
		CHECK_NEAR(p.position.z, 30.0, 0.0);
	}
	{
		NetCode::RPC::SetVehicleZAngle omp;
		omp.VehicleID = 5;
		omp.angle = 180.0f;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::SetVehicleZAngle p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.vehicleId, 5);
		CHECK_NEAR(p.angle, 180.0, 0.0);
	}
	{
		NetCode::RPC::SetVehicleHealth omp;
		omp.VehicleID = 5;
		omp.health = 600.0f;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::SetVehicleHealth p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.vehicleId, 5);
		CHECK_NEAR(p.health, 600.0, 0.0);
	}
	{
		NetCode::RPC::SetVehicleVelocity omp;
		omp.Type = 0;
		omp.Velocity = Vector3(0.0f, 0.0f, 0.25f);
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::SetVehicleVelocity p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.type, 0);
		CHECK_NEAR(p.velocity.z, 0.25, 0.0);
	}
	{
		NetCode::RPC::SetVehiclePlate omp;
		omp.VehicleID = 5;
		omp.plate = StringView("SULTAN");
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::SetVehicleNumberPlate p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.vehicleId, 5);
		CHECK(p.plate == "SULTAN");
	}
	{
		NetCode::RPC::LinkVehicleToInterior omp;
		omp.VehicleID = 5;
		omp.InteriorID = 10;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::LinkVehicleToInterior p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.vehicleId, 5);
		CHECK_EQ(p.interiorId, 10);
	}
	{
		NetCode::RPC::AttachTrailer omp;
		omp.TrailerID = 12;
		omp.VehicleID = 5;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::AttachTrailerToVehicle p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.trailerId, 12);
		CHECK_EQ(p.vehicleId, 5);
	}
	{
		NetCode::RPC::DetachTrailer omp;
		omp.VehicleID = 5;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::DetachTrailerFromVehicle p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.vehicleId, 5);
	}
	{
		NetCode::RPC::RemoveVehicleComponent omp;
		omp.VehicleID = 5;
		omp.Component = 1010;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::RemoveVehicleComponent p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.vehicleId, 5);
		CHECK_EQ(p.componentId, 1010);
	}
	{
		NetCode::RPC::PutPlayerInVehicle omp;
		omp.VehicleID = 5;
		omp.SeatID = 0;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::PutPlayerInVehicle p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
		CHECK_EQ(p.vehicleId, 5);
		CHECK_EQ(p.seatId, 0);
	}
	{
		NetCode::RPC::RemovePlayerFromVehicle omp;
		NetworkBitStream w;
		omp.write(w);
		auto r = makeReader(w);
		proto::RemovePlayerFromVehicle p{};
		CHECK(p.serialize(r, proto::ProtoVersion::V037));
		CHECK_EQ(r.unreadBits(), 0);
	}
}

int main()
{
	RUN(oracleStreamAndDamage);
	RUN(oracleVehicleActions);
	return check::finish();
}

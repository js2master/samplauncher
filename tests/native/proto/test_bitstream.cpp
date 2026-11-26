// The client's own RakNet BitStream: round trips of every helper the packet
// handlers use, and the "read past the end" contract the handlers rely on.
#include "check.h"

#include <RakNet/BitStream.h>
#include <RakNet/SingleProducerConsumer.h>
#include <RakNet/StringCompressor.h>
#include <RakNet/SAMP/samp_auth.h>
#include <RakNet/SAMP/samp_netencr.h>

#include <climits>
#include <cstdint>
#include <cstring>

static void scalarsRoundTrip()
{
	RakNet::BitStream w;
	w.Write(static_cast<uint8_t>(0xAB));
	w.Write(static_cast<uint16_t>(0xBEEF));
	w.Write(static_cast<uint32_t>(0xDEADBEEF));
	w.Write(static_cast<int32_t>(-123456));
	w.Write(3.25f);
	w.Write(true);
	w.Write(false);
	w.Write(static_cast<uint8_t>(0x5A)); // not byte aligned from here on

	RakNet::BitStream r(w.GetData(), w.GetNumberOfBytesUsed(), false);
	uint8_t u8 = 0;
	uint16_t u16 = 0;
	uint32_t u32 = 0;
	int32_t i32 = 0;
	float f = 0.0f;
	bool b1 = false, b2 = true;
	uint8_t tail = 0;
	CHECK(r.Read(u8));
	CHECK(r.Read(u16));
	CHECK(r.Read(u32));
	CHECK(r.Read(i32));
	CHECK(r.Read(f));
	CHECK(r.Read(b1));
	CHECK(r.Read(b2));
	CHECK(r.Read(tail));
	CHECK_EQ(u8, 0xAB);
	CHECK_EQ(u16, 0xBEEF);
	CHECK_EQ(u32, 0xDEADBEEF);
	CHECK_EQ(i32, -123456);
	CHECK_NEAR(f, 3.25, 0.0);
	CHECK(b1 && !b2);
	CHECK_EQ(tail, 0x5A);
}

static void littleEndianOnTheWire()
{
	RakNet::BitStream w;
	w.Write(static_cast<uint16_t>(0x0102));
	w.Write(static_cast<uint32_t>(0x0A0B0C0D));
	const unsigned char expected[] = {0x02, 0x01, 0x0D, 0x0C, 0x0B, 0x0A};
	CHECK_EQ(w.GetNumberOfBitsUsed(), 48);
	CHECK(memcmp(w.GetData(), expected, sizeof(expected)) == 0);
}

// Handlers must initialise their locals and check Read(): a failed read
// returns false and leaves the output variable untouched.
static void readPastEndFailsAndKeepsOutput()
{
	RakNet::BitStream w;
	w.Write(static_cast<uint8_t>(7));

	RakNet::BitStream r(w.GetData(), w.GetNumberOfBytesUsed(), false);
	uint8_t ok = 0;
	uint32_t untouched = 0xCAFEF00D;
	float untouchedF = 42.0f;
	char buf[4] = {'x', 'x', 'x', 'x'};
	CHECK(r.Read(ok));
	CHECK_EQ(ok, 7);
	CHECK(!r.Read(untouched));
	CHECK_EQ(untouched, 0xCAFEF00D);
	CHECK(!r.Read(untouchedF));
	CHECK_NEAR(untouchedF, 42.0, 0.0);
	CHECK(!r.Read(buf, 4));
	CHECK(buf[0] == 'x' && buf[3] == 'x');
}

static void normQuatAndVector()
{
	RakNet::BitStream w;
	const float qw = 0.5f, qx = -0.5f, qy = 0.5f, qz = 0.5f;
	w.WriteNormQuat(qw, qx, qy, qz);
	w.WriteVector(0.3f, -0.4f, 1.2f);

	RakNet::BitStream r(w.GetData(), w.GetNumberOfBytesUsed(), false);
	float rw = 0, rx = 0, ry = 0, rz = 0;
	float vx = 0, vy = 0, vz = 0;
	CHECK(r.ReadNormQuat(rw, rx, ry, rz));
	CHECK(r.ReadVector(vx, vy, vz));
	CHECK_NEAR(rw, qw, 1e-3);
	CHECK_NEAR(rx, qx, 1e-3);
	CHECK_NEAR(ry, qy, 1e-3);
	CHECK_NEAR(rz, qz, 1e-3);
	CHECK_NEAR(vx, 0.3, 1e-3);
	CHECK_NEAR(vy, -0.4, 1e-3);
	CHECK_NEAR(vz, 1.2, 1e-3);
}

static void huffmanStringRoundTrip()
{
	const char* text = "Dialog body: {FF0000}red{FFFFFF} 0123456789 \\n line two";
	RakNet::BitStream w;
	stringCompressor->EncodeString(text, 256, &w);

	RakNet::BitStream r(w.GetData(), w.GetNumberOfBytesUsed(), false);
	char out[256] = {};
	CHECK(stringCompressor->DecodeString(out, sizeof(out), &r));
	CHECK(strcmp(out, text) == 0);

	// truncated input must not run past the buffer
	RakNet::BitStream cut(w.GetData(), 2, false);
	char out2[256] = {};
	stringCompressor->DecodeString(out2, sizeof(out2), &cut);
	CHECK(strlen(out2) < strlen(text));
}

static void bitstreamOverflowAndEmptyGuards()
{
	RakNet::BitStream empty;
	CHECK(!empty.ReadBit());

	RakNet::BitStream w;
	w.Write(static_cast<uint32_t>(0x11223344));
	RakNet::BitStream r(w.GetData(), w.GetNumberOfBytesUsed(), false);
	uint8_t first = 0;
	CHECK(r.Read(first));
	CHECK_EQ(first, 0x44);

	// Signed INT_MAX bit/byte counts must fail cleanly instead of wrapping readOffset + n < 0
	unsigned char scratch[8] = {};
	CHECK(!r.ReadBits(scratch, INT_MAX, true));
	CHECK(!r.ReadBits(scratch, -5, true));
	CHECK(!r.Read(reinterpret_cast<char*>(scratch), INT_MAX));
	CHECK(!r.ReadAlignedBytes(scratch, INT_MAX));
	CHECK_EQ(r.GetReadOffset(), 8);
}

static void sampAuthKeyLp64Parity()
{
	// Verify dynamic 32-bit SHA-1 gen_auth_key on LP64 matches known 0.3.7 AuthKeyTable vectors
	for (int i = 0; i < 8; ++i) {
		char computed[260] = {};
		gen_auth_key(computed, const_cast<char*>(AuthKeyTable[i][0]));
		CHECK_EQ(strlen(computed), 40u);
		CHECK(strcmp(computed, AuthKeyTable[i][1]) == 0);
	}
}

static void sampNetEncrBoundsGuard()
{
	unsigned char payload[16];
	for (int i = 0; i < 16; ++i) payload[i] = static_cast<unsigned char>(i * 7 + 3);
	unsigned char* enc = encrBuffer;
	kyretardizeDatagram(payload, sizeof(payload), 7777, 0);
	CHECK(enc[0] != 0 || enc[1] != 0);

	// Oversized length must be ignored without writing past encrBuffer[4092]
	kyretardizeDatagram(payload, 5000, 7777, 0);
}

static void spscQueueRoundTrip()
{
	DataStructures::SingleProducerConsumer<uint32_t> q;
	for (uint32_t i = 1; i <= 64; ++i) {
		uint32_t* slot = q.WriteLock();
		*slot = i * 10u;
		q.WriteUnlock();
	}
	for (uint32_t i = 1; i <= 64; ++i) {
		uint32_t* out = q.ReadLock();
		CHECK(out != nullptr);
		CHECK_EQ(*out, i * 10u);
		q.ReadUnlock();
	}
	CHECK(q.ReadLock() == nullptr);
	q.Clear();
	CHECK_EQ(q.Size(), 0);
}

int main()
{
	StringCompressor::AddReference();
	RUN(scalarsRoundTrip);
	RUN(littleEndianOnTheWire);
	RUN(readPastEndFailsAndKeepsOutput);
	RUN(normQuatAndVector);
	RUN(huffmanStringRoundTrip);
	RUN(bitstreamOverflowAndEmptyGuards);
	RUN(sampAuthKeyLp64Parity);
	RUN(sampNetEncrBoundsGuard);
	RUN(spscQueueRoundTrip);
	StringCompressor::RemoveReference();
	return check::finish();
}

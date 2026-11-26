// Packet capture file format (Net/Proto/NetCapFormat.h): what the client
// writes with /netcap must read back identically, and damaged files must be
// rejected instead of crashing the reader.
#include "check.h"

#include "Net/Proto/NetCapFormat.h"

#include <cstdint>
#include <string>

static std::string tmpPath(const char* name)
{
	return std::string("netcap_test_") + name + ".ncap";
}

static void roundTrip()
{
	const std::string path = tmpPath("roundtrip");
	const uint8_t a[] = {0x01, 0x02, 0x03};
	const uint8_t b[] = {0xFF};
	{
		FILE* f = fopen(path.c_str(), "wb");
		CHECK(f != nullptr);
		CHECK(netcap::writeHeader(f, 0xFDE, 1234567890123LL));
		CHECK(netcap::writeRecord(f, 10, netcap::kIn, netcap::kRpc, 139, a, 24));
		CHECK(netcap::writeRecord(f, 20, netcap::kOut, netcap::kPacket, 207, b, 5)); // 5 bits -> 1 byte
		CHECK(netcap::writeRecord(f, 30, netcap::kIn, netcap::kRpc, 40, nullptr, 0)); // RPC with no payload
		fclose(f);
	}
	FILE* f = fopen(path.c_str(), "rb");
	CHECK(f != nullptr);
	netcap::Header h;
	CHECK(netcap::readHeader(f, h));
	CHECK_EQ(h.formatVersion, netcap::kFormatVersion);
	CHECK_EQ(h.netgameVersion, 0xFDE);
	CHECK_EQ(h.unixTime, 1234567890123LL);

	netcap::Record r;
	CHECK(netcap::readRecord(f, r));
	CHECK_EQ(r.timeMs, 10);
	CHECK_EQ(r.dir, netcap::kIn);
	CHECK_EQ(r.kind, netcap::kRpc);
	CHECK_EQ(r.id, 139);
	CHECK_EQ(r.bitLength, 24);
	CHECK_EQ(r.data.size(), 3);
	CHECK(r.data.size() == 3 && r.data[0] == 0x01 && r.data[2] == 0x03);

	CHECK(netcap::readRecord(f, r));
	CHECK_EQ(r.dir, netcap::kOut);
	CHECK_EQ(r.kind, netcap::kPacket);
	CHECK_EQ(r.id, 207);
	CHECK_EQ(r.bitLength, 5);
	CHECK_EQ(r.data.size(), 1);

	CHECK(netcap::readRecord(f, r));
	CHECK_EQ(r.id, 40);
	CHECK_EQ(r.bitLength, 0);
	CHECK(r.data.empty());

	CHECK(!netcap::readRecord(f, r)); // clean end of file
	fclose(f);
	remove(path.c_str());
}

static void rejectsDamagedFiles()
{
	const std::string path = tmpPath("damaged");
	const uint8_t a[] = {1, 2, 3, 4};

	// wrong magic
	{
		FILE* f = fopen(path.c_str(), "wb");
		fwrite("NOTACAPTUREFILE.........", 1, 24, f);
		fclose(f);
		f = fopen(path.c_str(), "rb");
		netcap::Header h;
		CHECK(!netcap::readHeader(f, h));
		fclose(f);
	}
	// record cut in the middle of its payload (game killed while writing)
	{
		FILE* f = fopen(path.c_str(), "wb");
		netcap::writeHeader(f, 0xFD9, 1);
		netcap::writeRecord(f, 1, netcap::kIn, netcap::kRpc, 61, a, 32);
		fclose(f);
		f = fopen(path.c_str(), "rb");
		fseek(f, 0, SEEK_END);
		const long full = ftell(f);
		fclose(f);
		CHECK_EQ(full, netcap::kHeaderSize + netcap::kRecordHeaderSize + 4);

		std::string bytes(static_cast<size_t>(full), '\0');
		f = fopen(path.c_str(), "rb");
		CHECK_EQ(fread(&bytes[0], 1, bytes.size(), f), bytes.size());
		fclose(f);
		f = fopen(path.c_str(), "wb");
		fwrite(bytes.data(), 1, bytes.size() - 2, f);
		fclose(f);

		f = fopen(path.c_str(), "rb");
		netcap::Header h;
		netcap::Record r;
		CHECK(netcap::readHeader(f, h));
		CHECK(!netcap::readRecord(f, r));
		fclose(f);
	}
	// absurd length field must not allocate gigabytes
	{
		FILE* f = fopen(path.c_str(), "wb");
		netcap::writeHeader(f, 0xFD9, 1);
		uint8_t rec[netcap::kRecordHeaderSize] = {};
		netcap::putLE(rec + 8, 0xFFFFFFFFu, 4);
		fwrite(rec, 1, sizeof(rec), f);
		fclose(f);
		f = fopen(path.c_str(), "rb");
		netcap::Header h;
		netcap::Record r;
		CHECK(netcap::readHeader(f, h));
		CHECK(!netcap::readRecord(f, r));
		fclose(f);
	}
	// the writer refuses the same
	{
		FILE* f = fopen(path.c_str(), "wb");
		CHECK(!netcap::writeRecord(f, 1, netcap::kIn, netcap::kRpc, 1, a, netcap::kMaxRecordBits + 1));
		CHECK(!netcap::writeRecord(f, 1, netcap::kIn, netcap::kRpc, 1, nullptr, 8));
		fclose(f);
	}
	remove(path.c_str());
}

int main()
{
	RUN(roundTrip);
	RUN(rejectsDamagedFiles);
	return check::finish();
}

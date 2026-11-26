// Lists what is inside a packet capture made with /netcap.
//   ncapdump file.ncap            per-id summary
//   ncapdump file.ncap -v         every record with a hex preview
#include "Net/Proto/NetCapFormat.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <tuple>

int main(int argc, char** argv)
{
	if (argc < 2) {
		std::printf("usage: ncapdump <file.ncap> [-v]\n");
		return 2;
	}
	const bool verbose = argc > 2 && strcmp(argv[2], "-v") == 0;
	FILE* f = fopen(argv[1], "rb");
	if (!f) {
		std::printf("cannot open %s\n", argv[1]);
		return 1;
	}
	netcap::Header h;
	if (!netcap::readHeader(f, h)) {
		std::printf("%s is not a capture file\n", argv[1]);
		fclose(f);
		return 1;
	}
	std::printf("netgame version 0x%X (%s), unix time %lld\n", h.netgameVersion,
		h.netgameVersion == 0xFDE ? "0.3.DL" : (h.netgameVersion == 0xFD9 ? "0.3.7" : "unknown"),
		static_cast<long long>(h.unixTime));

	struct Stat {
		uint32_t count = 0;
		uint32_t minBits = 0xFFFFFFFFu;
		uint32_t maxBits = 0;
	};
	std::map<std::tuple<int, int, int>, Stat> stats;
	netcap::Record r;
	uint32_t total = 0;
	while (netcap::readRecord(f, r)) {
		++total;
		Stat& s = stats[std::make_tuple(static_cast<int>(r.dir), static_cast<int>(r.kind), static_cast<int>(r.id))];
		++s.count;
		if (r.bitLength < s.minBits) s.minBits = r.bitLength;
		if (r.bitLength > s.maxBits) s.maxBits = r.bitLength;
		if (verbose) {
			std::printf("%8u ms  %s %-6s %3u  %6u bits ", r.timeMs, r.dir == netcap::kIn ? "IN " : "OUT",
				r.kind == netcap::kRpc ? "RPC" : "PACKET", r.id, r.bitLength);
			for (size_t i = 0; i < r.data.size() && i < 24; ++i) std::printf(" %02x", r.data[i]);
			std::printf(r.data.size() > 24 ? " ...\n" : "\n");
		}
	}
	const long consumed = ftell(f);
	fseek(f, 0, SEEK_END);
	const long size = ftell(f);
	fclose(f);

	std::printf("%u records\n", total);
	std::printf("dir kind    id   count   min bits   max bits\n");
	for (const auto& kv : stats) {
		std::printf("%s %-6s %3d  %6u  %9u  %9u\n", std::get<0>(kv.first) == netcap::kIn ? "IN " : "OUT",
			std::get<1>(kv.first) == netcap::kRpc ? "RPC" : "PACKET", std::get<2>(kv.first),
			kv.second.count, kv.second.minBits, kv.second.maxBits);
	}
	if (consumed != size) {
		std::printf("warning: %ld trailing bytes ignored (capture was cut short)\n", size - consumed);
	}
	return 0;
}

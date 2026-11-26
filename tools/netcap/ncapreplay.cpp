// Replays inbound and outbound RPCs and raw/sync packets from a .ncap capture
// through the declarative protocol layer (Net/Proto/RpcDecode.h), reporting
// read failures and unread trailing bits.
//   ncapreplay <file.ncap> [-v]
#include "Net/Proto/NetCapFormat.h"
#include "Net/Proto/RpcDecode.h"

#include <RakNet/StringCompressor.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>

int main(int argc, char** argv)
{
	if (argc < 2) {
		std::printf("usage: ncapreplay <file.ncap> [-v]\n");
		return 2;
	}
	const bool verbose = argc > 2 && strcmp(argv[2], "-v") == 0;

	FILE* f = fopen(argv[1], "rb");
	if (!f) {
		std::printf("cannot open %s\n", argv[1]);
		return 1;
	}
	netcap::Header h{};
	if (!netcap::readHeader(f, h)) {
		std::printf("%s is not a valid .ncap file\n", argv[1]);
		fclose(f);
		return 1;
	}

	const proto::ProtoVersion ver = proto::versionFromNetgame(h.netgameVersion);
	std::printf("replaying %s: netgame 0x%X (%s)\n", argv[1], h.netgameVersion,
		ver == proto::ProtoVersion::V03DL ? "0.3.DL" : "0.3.7");

	StringCompressor::AddReference();

	struct Stat {
		const char* name = nullptr;
		uint32_t ok = 0;
		uint32_t fail = 0;
		uint32_t leftover = 0;
		uint32_t skipped = 0;
		int maxUnread = 0;
	};
	std::map<int, Stat> inRpcStats, inPktStats, outRpcStats, outPktStats;

	uint32_t totalInRpc = 0, inRpcOk = 0, inRpcFail = 0, inRpcLeftover = 0, inRpcUnmigrated = 0;
	uint32_t totalInPkt = 0, inPktOk = 0, inPktFail = 0, inPktLeftover = 0, inPktOther = 0;
	uint32_t totalOutRpc = 0, outRpcOk = 0, outRpcFail = 0, outRpcLeftover = 0, outRpcUnmigrated = 0;
	uint32_t totalOutPkt = 0, outPktOk = 0, outPktFail = 0, outPktLeftover = 0, outPktOther = 0;

	auto tally = [&](const char* label, const netcap::Record& rec, const proto::DecodeResult& res,
	                 std::map<int, Stat>& map, uint32_t& okCnt, uint32_t& failCnt,
	                 uint32_t& leftCnt, uint32_t& skipCnt) {
		Stat& st = map[rec.id];
		if (res.name) st.name = res.name;
		switch (res.status) {
			case proto::DecodeStatus::kOk:
				++okCnt;
				++st.ok;
				if (res.unreadBits > st.maxUnread) st.maxUnread = res.unreadBits;
				if (res.unreadBits >= 8) {
					++leftCnt;
					++st.leftover;
					std::printf("WARN  %8u ms  %s %3u (%s): %u bits, %d unread bits left\n",
						rec.timeMs, label, rec.id, res.name ? res.name : "?", rec.bitLength, res.unreadBits);
				} else if (verbose) {
					std::printf("OK    %8u ms  %s %3u (%s): %u bits (%d pad)\n",
						rec.timeMs, label, rec.id, res.name ? res.name : "?", rec.bitLength, res.unreadBits);
				}
				break;
			case proto::DecodeStatus::kReadFailed:
			case proto::DecodeStatus::kVersionMismatch:
				++failCnt;
				++st.fail;
				std::printf("FAIL  %8u ms  %s %3u (%s): decode failed (%u bits, %d unread)\n",
					rec.timeMs, label, rec.id, res.name ? res.name : "?", rec.bitLength, res.unreadBits);
				break;
			case proto::DecodeStatus::kNotMigrated:
				++skipCnt;
				++st.skipped;
				break;
		}
	};

	netcap::Record rec;
	while (netcap::readRecord(f, rec)) {
		if (rec.dir == netcap::kIn) {
			if (rec.kind == netcap::kRpc) {
				++totalInRpc;
				auto res = proto::decodeInboundRpc(rec.id, rec.data.data(), rec.bitLength, ver);
				tally("IN  RPC", rec, res, inRpcStats, inRpcOk, inRpcFail, inRpcLeftover, inRpcUnmigrated);
			}
			else if (rec.kind == netcap::kPacket) {
				++totalInPkt;
				auto res = proto::decodeInboundPacket(rec.data.data(), rec.bitLength, ver);
				tally("IN  PKT", rec, res, inPktStats, inPktOk, inPktFail, inPktLeftover, inPktOther);
			}
		}
		else if (rec.dir == netcap::kOut) {
			if (rec.kind == netcap::kRpc) {
				++totalOutRpc;
				auto res = proto::decodeOutboundRpc(rec.id, rec.data.data(), rec.bitLength, ver);
				tally("OUT RPC", rec, res, outRpcStats, outRpcOk, outRpcFail, outRpcLeftover, outRpcUnmigrated);
			}
			else if (rec.kind == netcap::kPacket) {
				++totalOutPkt;
				auto res = proto::decodeOutboundPacket(rec.data.data(), rec.bitLength, ver);
				tally("OUT PKT", rec, res, outPktStats, outPktOk, outPktFail, outPktLeftover, outPktOther);
			}
		}
	}
	fclose(f);
	StringCompressor::RemoveReference();

	std::printf("\ninbound  RPC summary: total=%u, decoded=%u, readFails=%u, leftover(>=8b)=%u, unmigrated=%u\n",
		totalInRpc, inRpcOk, inRpcFail, inRpcLeftover, inRpcUnmigrated);
	std::printf("inbound  PKT summary: total=%u, decoded=%u, readFails=%u, leftover(>=8b)=%u, nonSync=%u\n",
		totalInPkt, inPktOk, inPktFail, inPktLeftover, inPktOther);
	std::printf("outbound RPC summary: total=%u, decoded=%u, readFails=%u, leftover(>=8b)=%u, unmigrated=%u\n",
		totalOutRpc, outRpcOk, outRpcFail, outRpcLeftover, outRpcUnmigrated);
	std::printf("outbound PKT summary: total=%u, decoded=%u, readFails=%u, leftover(>=8b)=%u, nonSync=%u\n",
		totalOutPkt, outPktOk, outPktFail, outPktLeftover, outPktOther);

	for (const auto& kv : outRpcStats) {
		const auto& st = kv.second;
		std::printf("OUT RPC %3d  %-24s ok=%4u fail=%4u leftover=%4u maxUnread=%4d skipped=%4u\n",
			kv.first, st.name ? st.name : "(unmigrated)", st.ok, st.fail, st.leftover, st.maxUnread, st.skipped);
	}
	for (const auto& kv : outPktStats) {
		const auto& st = kv.second;
		std::printf("OUT PKT %3d  %-24s ok=%4u fail=%4u leftover=%4u maxUnread=%4d skipped=%4u\n",
			kv.first, st.name ? st.name : "(non-sync/internal)", st.ok, st.fail, st.leftover, st.maxUnread, st.skipped);
	}

	const uint32_t totalFails = inRpcFail + inRpcLeftover + inPktFail + inPktLeftover +
	                            outRpcFail + outRpcLeftover + outPktFail + outPktLeftover;
	return totalFails == 0 ? 0 : 1;
}

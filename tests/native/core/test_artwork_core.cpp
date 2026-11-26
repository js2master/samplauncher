// Host-side unit tests for 0.3.DL / open.mp Artwork pure logic:
// - ArtworkCore (CRC32, cache naming, URL loopback rewrite, timed visibility, manifest FSM, slot map)
// - TxdParser (D3D8/D3D9 DXT1/3/5, PAL8/4, BGRA32, 16-bit, NPOT normalization, malformed input rejection)
// - DffValidator (chunk validation, COL2/3 extraction, collision stripping & length recomputation)
#include "check.h"

#include "Game/Artwork/ArtworkCore.h"
#include "Game/Artwork/DffValidator.h"
#include "Game/Artwork/Lzma1Decoder.h"
#include "Game/Artwork/TxdParser.h"

#include <cstring>
#include <string>
#include <vector>

using namespace artwork;

namespace {

void pushLE16(std::vector<uint8_t>& buf, uint16_t v)
{
	buf.push_back(static_cast<uint8_t>(v & 0xFFu));
	buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
}

void pushLE32(std::vector<uint8_t>& buf, uint32_t v)
{
	buf.push_back(static_cast<uint8_t>(v & 0xFFu));
	buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
	buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
	buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
}

void pushF32(std::vector<uint8_t>& buf, float f)
{
	uint32_t bits = 0;
	std::memcpy(&bits, &f, sizeof(bits));
	pushLE32(buf, bits);
}

std::vector<uint8_t> wrapChunk(uint32_t type, const std::vector<uint8_t>& payload, uint32_t ver = 0x1803FFFFu)
{
	std::vector<uint8_t> out;
	out.reserve(12u + payload.size());
	pushLE32(out, type);
	pushLE32(out, static_cast<uint32_t>(payload.size()));
	pushLE32(out, ver);
	out.insert(out.end(), payload.begin(), payload.end());
	return out;
}

std::vector<uint8_t> buildSingleTextureTxd(
	const char* name, uint32_t platformId, uint32_t rasterFmt, uint32_t d3dFmtOrAlpha,
	uint16_t w, uint16_t h, uint8_t depth, uint8_t flagsOrComp,
	const std::vector<uint8_t>& palette, const std::vector<uint8_t>& mip0)
{
	std::vector<uint8_t> st;
	pushLE32(st, platformId);
	pushLE32(st, 0x00001106u); // filter + wrap flags
	// name[32]
	for (size_t i = 0; i < 32; ++i) {
		st.push_back(i < std::strlen(name) ? static_cast<uint8_t>(name[i]) : 0);
	}
	// mask[32]
	st.insert(st.end(), 32, 0);
	pushLE32(st, rasterFmt);
	pushLE32(st, d3dFmtOrAlpha);
	pushLE16(st, w);
	pushLE16(st, h);
	st.push_back(depth);
	st.push_back(1); // mipCount
	st.push_back(4); // rasterType = texture
	st.push_back(flagsOrComp);
	st.insert(st.end(), palette.begin(), palette.end());
	pushLE32(st, static_cast<uint32_t>(mip0.size()));
	st.insert(st.end(), mip0.begin(), mip0.end());

	auto native = wrapChunk(0x0015u, wrapChunk(0x0001u, st));
	std::vector<uint8_t> dictHeader;
	pushLE16(dictHeader, 1); // texture count
	pushLE16(dictHeader, 0); // device id
	std::vector<uint8_t> rootPayload = wrapChunk(0x0001u, dictHeader);
	rootPayload.insert(rootPayload.end(), native.begin(), native.end());
	return wrapChunk(0x0016u, rootPayload);
}

std::vector<uint8_t> buildValidCol3(uint32_t colFourCc = 0x334C4F43u)
{
	// 32B header (fourcc + size + 24B name/modelId) + 88B COL3 body header + 18B (3 compressed verts) + 8B (1 face)
	const uint32_t headerSize = (colFourCc == 0x324C4F43u) ? 76u : 88u;
	const uint32_t vertsRel = headerSize;
	const uint32_t facesRel = vertsRel + 18u; // 3 * 6B
	const uint32_t bodySize = facesRel + 8u;  // 1 * 8B
	const uint32_t encodedSize = 24u + bodySize;

	std::vector<uint8_t> col;
	col.reserve(8u + encodedSize);
	pushLE32(col, colFourCc);
	pushLE32(col, encodedSize);
	col.insert(col.end(), 24u, 0u); // model name + id

	// 40B bounds: min(-1,-1,-1), max(1,1,1), center(0,0,0), radius(2.0)
	pushF32(col, -1.0f); pushF32(col, -1.0f); pushF32(col, -1.0f);
	pushF32(col,  1.0f); pushF32(col,  1.0f); pushF32(col,  1.0f);
	pushF32(col,  0.0f); pushF32(col,  0.0f); pushF32(col,  0.0f);
	pushF32(col,  2.0f);

	pushLE16(col, 0u); // numSpheres
	pushLE16(col, 0u); // numBoxes
	pushLE16(col, 1u); // numFaces
	col.push_back(0u); // numLines
	col.push_back(0u); // pad
	pushLE32(col, 0x02u); // flags: has collision
	pushLE32(col, 0u); // offSpheres
	pushLE32(col, 0u); // offBoxes
	pushLE32(col, 0u); // offLines
	pushLE32(col, vertsRel + 28u); // offVerts (relative to col+4)
	pushLE32(col, facesRel + 28u); // offFaces (relative to col+4)
	pushLE32(col, 0u); // offTriPlanes
	if (headerSize >= 88u) {
		pushLE32(col, 0u); // numShadowFaces
		pushLE32(col, 0u); // offShadowVerts
		pushLE32(col, 0u); // offShadowFaces
	}
	// 3 compressed vertices (18B)
	for (int i = 0; i < 9; ++i) pushLE16(col, 0u);
	// 1 face: a=0, b=1, c=2, mat=0, light=0
	pushLE16(col, 0u);
	pushLE16(col, 1u);
	pushLE16(col, 2u);
	col.push_back(0u);
	col.push_back(0u);
	return col;
}

std::vector<uint8_t> buildMinimalDff(const char* texName, uint32_t colChunkId, uint32_t colFourCc,
	const char* nodeName = nullptr, const std::vector<uint8_t>* geomExtChild = nullptr)
{
	// Clump struct: numAtomics=1, numLights=0, numCameras=0
	std::vector<uint8_t> clumpStruct;
	pushLE32(clumpStruct, 1);
	pushLE32(clumpStruct, 0);
	pushLE32(clumpStruct, 0);

	// FrameList: 1 frame, parent = -1
	std::vector<uint8_t> flStruct;
	pushLE32(flStruct, 1);
	// Identity 3x3 + zero pos (48 bytes)
	for (int i = 0; i < 12; ++i) pushF32(flStruct, (i == 0 || i == 4 || i == 8) ? 1.0f : 0.0f);
	pushLE32(flStruct, 0xFFFFFFFFu); // parent -1
	pushLE32(flStruct, 0);           // flags
	std::vector<uint8_t> flPayload = wrapChunk(0x0001u, flStruct);
	if (nodeName) {
		std::vector<uint8_t> nnBytes(nodeName, nodeName + std::strlen(nodeName));
		auto nnChunk = wrapChunk(0x0253F2FEu, nnBytes);
		auto flExt = wrapChunk(0x0003u, nnChunk);
		flPayload.insert(flPayload.end(), flExt.begin(), flExt.end());
	}
	auto frameList = wrapChunk(0x000Eu, flPayload);

	// Material -> Texture -> String(texName) + String("")
	std::vector<uint8_t> texStruct;
	pushLE16(texStruct, 0x1106u);
	pushLE16(texStruct, 0);
	std::vector<uint8_t> texPayload = wrapChunk(0x0001u, texStruct);
	std::vector<uint8_t> nameBytes(texName, texName + std::strlen(texName) + 1);
	while (nameBytes.size() % 4 != 0) nameBytes.push_back(0);
	auto nameChunk = wrapChunk(0x0002u, nameBytes);
	auto maskChunk = wrapChunk(0x0002u, {0, 0, 0, 0});
	texPayload.insert(texPayload.end(), nameChunk.begin(), nameChunk.end());
	texPayload.insert(texPayload.end(), maskChunk.begin(), maskChunk.end());
	auto texChunk = wrapChunk(0x0006u, texPayload);

	std::vector<uint8_t> matStruct(28, 0);
	std::vector<uint8_t> matPayload = wrapChunk(0x0001u, matStruct);
	matPayload.insert(matPayload.end(), texChunk.begin(), texChunk.end());
	auto matChunk = wrapChunk(0x0007u, matPayload);

	std::vector<uint8_t> mlStruct;
	pushLE32(mlStruct, 1);
	pushLE32(mlStruct, 0xFFFFFFFFu);
	std::vector<uint8_t> mlPayload = wrapChunk(0x0001u, mlStruct);
	mlPayload.insert(mlPayload.end(), matChunk.begin(), matChunk.end());
	auto matList = wrapChunk(0x0008u, mlPayload);

	// Geometry: 1 triangle, 3 vertices, 1 morph target, 1 UV set
	std::vector<uint8_t> gStruct;
	pushLE32(gStruct, 0x00010004u); // 1 UV set
	pushLE32(gStruct, 1);           // numTriangles
	pushLE32(gStruct, 3);           // numVertices
	pushLE32(gStruct, 1);           // numMorphTargets
	// UVs: 3 * 8 bytes
	for (int i = 0; i < 6; ++i) pushF32(gStruct, 0.0f);
	// Triangle: v2=1, v1=0, matId=0, v3=2
	pushLE16(gStruct, 1);
	pushLE16(gStruct, 0);
	pushLE16(gStruct, 0);
	pushLE16(gStruct, 2);
	// Morph header: sphere(16) + hasVerts=1 + hasNorms=0
	pushF32(gStruct, 0.0f);
	pushF32(gStruct, 0.0f);
	pushF32(gStruct, 0.0f);
	pushF32(gStruct, 1.0f);
	pushLE32(gStruct, 1);
	pushLE32(gStruct, 0);
	// 3 vertices
	pushF32(gStruct, 0.0f); pushF32(gStruct, 0.0f); pushF32(gStruct, 0.0f);
	pushF32(gStruct, 1.0f); pushF32(gStruct, 0.0f); pushF32(gStruct, 0.0f);
	pushF32(gStruct, 0.0f); pushF32(gStruct, 1.0f); pushF32(gStruct, 0.0f);

	std::vector<uint8_t> geomPayload = wrapChunk(0x0001u, gStruct);
	geomPayload.insert(geomPayload.end(), matList.begin(), matList.end());
	if (geomExtChild && !geomExtChild->empty()) {
		auto geomExt = wrapChunk(0x0003u, *geomExtChild);
		geomPayload.insert(geomPayload.end(), geomExt.begin(), geomExt.end());
	}
	auto geomChunk = wrapChunk(0x000Fu, geomPayload);

	std::vector<uint8_t> glStruct;
	pushLE32(glStruct, 1);
	std::vector<uint8_t> glPayload = wrapChunk(0x0001u, glStruct);
	glPayload.insert(glPayload.end(), geomChunk.begin(), geomChunk.end());
	auto geomList = wrapChunk(0x001Au, glPayload);

	// Atomic: frame=0, geom=0, flags=5, unused=0
	std::vector<uint8_t> aStruct;
	pushLE32(aStruct, 0);
	pushLE32(aStruct, 0);
	pushLE32(aStruct, 5);
	pushLE32(aStruct, 0);
	auto atomic = wrapChunk(0x0014u, wrapChunk(0x0001u, aStruct));

	// Clump extension with optional embedded COL
	std::vector<uint8_t> extPayload;
	if (colChunkId != 0) {
		auto col = buildValidCol3(colFourCc);
		auto colChunk = wrapChunk(colChunkId, col);
		extPayload.insert(extPayload.end(), colChunk.begin(), colChunk.end());
	}
	auto clumpExt = wrapChunk(0x0003u, extPayload);

	std::vector<uint8_t> clumpPayload = wrapChunk(0x0001u, clumpStruct);
	clumpPayload.insert(clumpPayload.end(), frameList.begin(), frameList.end());
	clumpPayload.insert(clumpPayload.end(), geomList.begin(), geomList.end());
	clumpPayload.insert(clumpPayload.end(), atomic.begin(), atomic.end());
	clumpPayload.insert(clumpPayload.end(), clumpExt.begin(), clumpExt.end());
	return wrapChunk(0x0010u, clumpPayload);
}

} // namespace

static void testCrc32AndHelpers()
{
	const char* vec = "123456789";
	CHECK_EQ(crc32Bytes(reinterpret_cast<const uint8_t*>(vec), 9), 0xCBF43926u);

	uint32_t chunked = 0;
	chunked = crc32Update(chunked, reinterpret_cast<const uint8_t*>(vec), 4);
	chunked = crc32Update(chunked, reinterpret_cast<const uint8_t*>(vec + 4), 5);
	CHECK_EQ(chunked, 0xCBF43926u);

	CHECK(sanitizeHostForCache("192.168.1.50") == "192.168.1.50");
	CHECK(sanitizeHostForCache("../Bad:Host/Name!") == "badhostname");
	CHECK(formatServerCacheDirName("10.0.0.5", 7777) == "10.0.0.5.7777");
	CHECK(formatCacheFileName(0x1A2B3C4Du, FileType::Dff) == "0x1A2B3C4D.dff");
	CHECK(formatCacheFileName(0x000000FFu, FileType::Txd) == "0xFF.txd");

	uint32_t parsedCrc = 0;
	FileType parsedType = FileType::None;
	CHECK(parseCacheFileHash("0x1A2B3C4D.dff", parsedCrc, parsedType));
	CHECK_EQ(parsedCrc, 0x1A2B3C4Du);
	CHECK(parsedType == FileType::Dff);
	CHECK(parseCacheFileHash("0xABCDEF.txd", parsedCrc, parsedType));
	CHECK_EQ(parsedCrc, 0xABCDEFu);
	CHECK(parsedType == FileType::Txd);
	CHECK(!parseCacheFileHash("not_a_hash.dff", parsedCrc, parsedType));
	CHECK(!parseCacheFileHash("0x1234.png", parsedCrc, parsedType));

	CHECK(isSafeHttpUrl("http://192.168.1.10:7777/skin.dff"));
	CHECK(isSafeHttpUrl("https://cdn.example.com/models/box.txd"));
	CHECK(!isSafeHttpUrl("file:///etc/passwd"));
	CHECK(!isSafeHttpUrl("http://bad\r\nhost/a.dff"));

	CHECK(rewriteLoopbackUrl("http://127.0.0.1:7777/a.dff", "192.168.1.88") == "http://192.168.1.88:7777/a.dff");
	CHECK(rewriteLoopbackUrl("http://0.0.0.0:7777/a.txd", "192.168.1.88") == "http://192.168.1.88:7777/a.txd");
	CHECK(rewriteLoopbackUrl("https://cdn.example.com/a.dff", "192.168.1.88") == "https://cdn.example.com/a.dff");
	CHECK(rewriteLoopbackUrl("http://127.0.0.1:7777/a.dff", "127.0.0.1") == "http://127.0.0.1:7777/a.dff");

	// Same-port private IPv4 rewrite (e.g., server advertising WSL/NAT IP on port 7777)
	CHECK(rewritePrivateHostToServer("http://172.23.240.1:7777/items/weapons.txd", "192.168.1.3", 7777)
		== "http://192.168.1.3:7777/items/weapons.txd");
	CHECK(rewritePrivateHostToServer("http://10.0.0.5:7777/a.dff", "203.0.113.9", 7777)
		== "http://203.0.113.9:7777/a.dff");
	// Different port or public CDN must stay untouched
	CHECK(rewritePrivateHostToServer("http://172.23.240.1:8080/a.dff", "192.168.1.3", 7777)
		== "http://172.23.240.1:8080/a.dff");
	CHECK(rewritePrivateHostToServer("https://cdn.example.com/a.dff", "192.168.1.3", 7777)
		== "https://cdn.example.com/a.dff");
	CHECK(rewritePrivateHostToServer("http://8.8.8.8:7777/a.dff", "192.168.1.3", 7777)
		== "http://8.8.8.8:7777/a.dff");

	// Wire 16-bit ID decoding
	CHECK_EQ(decodeWireModelId16(static_cast<uint16_t>(static_cast<int16_t>(-1000))), -1000);
	CHECK_EQ(decodeWireModelId16(static_cast<uint16_t>(static_cast<int16_t>(-30000))), -30000);
	CHECK_EQ(decodeWireModelId16(20001u), 20001);
	CHECK_EQ(decodeWireModelId16(30000u), 30000);

	// Timed object visibility (native PC 0.3.DL CObject::IsTimeVisible semantics)
	CHECK(!isTimedObjectVisible(12, 0, 0));   // timeOn == timeOff -> false
	CHECK(!isTimedObjectVisible(12, 24, 6));  // out-of-range hour -> false
	CHECK(!isTimedObjectVisible(-1, 8, 18));  // out-of-range world hour -> false
	CHECK(isTimedObjectVisible(10, 8, 18));   // daytime range
	CHECK(!isTimedObjectVisible(18, 8, 18));  // half-open [8, 18)
	CHECK(isTimedObjectVisible(23, 20, 6));   // wraps midnight
	CHECK(isTimedObjectVisible(3, 20, 6));
	CHECK(!isTimedObjectVisible(12, 20, 6));
}

static void testArtworkManifestStateMachine()
{
	ArtworkManifest m;

	// Skin (always active) + global object (VW -1) + VW-specific object (VW 42) sharing a TXD
	CustomModelRecord skin{};
	skin.poolId = 0;
	skin.type = ModelType::Skin;
	skin.virtualWorld = -1;
	skin.baseId = 29;
	skin.newId = 20001;
	skin.dffCrc = 0x11111111u;
	skin.txdCrc = 0xAAAAAAAAu;
	skin.dffSize = 1000;
	skin.txdSize = 2000;
	CHECK(m.addRecord(skin, 3));

	CustomModelRecord objGlobal{};
	objGlobal.poolId = 1;
	objGlobal.type = ModelType::Object;
	objGlobal.virtualWorld = -1;
	objGlobal.baseId = 19374;
	objGlobal.newId = -2001;
	objGlobal.dffCrc = 0x22222222u;
	objGlobal.txdCrc = 0xAAAAAAAAu; // Shared TXD!
	objGlobal.dffSize = 1500;
	objGlobal.txdSize = 2000;
	CHECK(m.addRecord(objGlobal, 3));

	CustomModelRecord objVw42{};
	objVw42.poolId = 0xFFFFFFFFu; // Simulate open.mp runtime addCustomModel quirk!
	objVw42.type = ModelType::Object;
	objVw42.virtualWorld = 42;
	objVw42.baseId = 19374;
	objVw42.newId = -2002;
	objVw42.dffCrc = 0x33333333u;
	objVw42.txdCrc = 0xBBBBBBBBu;
	objVw42.dffSize = 3000;
	objVw42.txdSize = 4000;
	objVw42.timeOn = 20;
	objVw42.timeOff = 6;
	CHECK(m.addRecord(objVw42, 0));

	// Reject out-of-range IDs
	CustomModelRecord bad = skin;
	bad.newId = 19999;
	CHECK(!m.addRecord(bad, 4));

	// Activate VW 0 (open.mp sends vw+1 then vw)
	CHECK(m.setVirtualWorld(1));
	CHECK(m.setVirtualWorld(0));
	CHECK(!m.setVirtualWorld(0)); // Unchanged VW must not reset flags

	CHECK(m.isDownloadGateActive());
	CHECK(!m.shouldSendFinishDownload());

	// Active scope for VW 0 has 3 unique files: 0x11111111.dff, 0xAAAAAAAA.txd, 0x22222222.dff
	auto snap = m.buildProgressSnapshot();
	CHECK_EQ(snap.totalFiles, 3u);
	CHECK_EQ(snap.completedFiles, 0u);

	// Pre-cache shared TXD
	m.markResourceCached(FileType::Txd, 0xAAAAAAAAu);
	snap = m.buildProgressSnapshot();
	CHECK_EQ(snap.completedFiles, 1u);

	// Request 1st missing resource (skin DFF)
	ResourceDownloadTask t{};
	CHECK(m.popNextResourceToRequest(t));
	CHECK(t.type == FileType::Dff);
	CHECK_EQ(t.crc, 0x11111111u);
	// Single URL request in flight at a time
	ResourceDownloadTask t2{};
	CHECK(!m.popNextResourceToRequest(t2));

	// URL received -> Downloading, now next URL request is allowed (concurrency <= 4)
	uint32_t expectedSize = 0;
	CHECK(m.onUrlReceived(FileType::Dff, 0x11111111u, expectedSize));
	CHECK_EQ(expectedSize, 1000u);
	CHECK(m.popNextResourceToRequest(t2));
	CHECK(t2.type == FileType::Dff);
	CHECK_EQ(t2.crc, 0x22222222u);
	CHECK(m.onUrlReceived(FileType::Dff, 0x22222222u, expectedSize));
	CHECK_EQ(expectedSize, 1500u);

	// No more missing resources in VW 0
	ResourceDownloadTask t3{};
	CHECK(!m.popNextResourceToRequest(t3));

	// Finish downloads (skin succeeds, objGlobal fails to test non-blocking failure)
	m.onDownloadFinished(FileType::Dff, 0x11111111u, true);
	m.onDownloadFinished(FileType::Dff, 0x22222222u, false);
	CHECK(m.isModelReadyOnDisk(20001));
	CHECK(!m.isModelReadyOnDisk(-2001));

	// Now ready to send RPC 184
	CHECK(m.shouldSendFinishDownload());
	m.markFinishDownloadSent();
	CHECK(!m.shouldSendFinishDownload());
	CHECK(m.isDownloadGateActive()); // Still gated until RPC 185

	m.onDownloadCompletedRpc();
	CHECK(!m.isDownloadGateActive());

	// Switch to VW 42 -> re-triggers download for objVw42 only!
	CHECK(m.setVirtualWorld(42));
	CHECK(m.isDownloadGateActive());
	CHECK(m.popNextResourceToRequest(t));
	CHECK_EQ(t.crc, 0x33333333u);

	// Bidirectional internal slot allocator
	const int32_t s1 = m.allocateInternalSlot(20001);
	const int32_t s2 = m.allocateInternalSlot(-2001);
	CHECK(isInternalCustomSlot(s1));
	CHECK(isInternalCustomSlot(s2));
	CHECK(s1 != s2);
	CHECK_EQ(m.allocateInternalSlot(20001), s1); // Idempotent
	CHECK_EQ(m.resolveInternalSlot(20001), s1);
	CHECK_EQ(m.resolveServerModelId(s1), 20001);
	CHECK_EQ(m.resolveServerModelId(s2), -2001);

	m.reset();
	CHECK_EQ(m.resolveInternalSlot(20001), -1);
	CHECK(m.records().empty());
}

static void testTxdParserFormatsAndErrors()
{
	// 1. D3D9 BGRA32 (2x2)
	{
		std::vector<uint8_t> mip0 = {
			0x10, 0x20, 0x30, 0x80, // BGRA -> RGBA (0x30, 0x20, 0x10, 0x80)
			0xFF, 0x00, 0x00, 0xFF,
			0x00, 0xFF, 0x00, 0xFF,
			0x00, 0x00, 0xFF, 0xFF,
		};
		auto txd = buildSingleTextureTxd("test_bgra", 9u, 0x0500u, 21u, 2, 2, 32, 0x01, {}, mip0);
		CHECK(validateTxdBytes(txd.data(), txd.size()));
		auto res = decodeTxdBytes(txd.data(), txd.size());
		CHECK(res.ok);
		CHECK_EQ(res.textures.size(), 1u);
		CHECK(res.textures[0].name == "test_bgra");
		CHECK_EQ(res.textures[0].width, 2u);
		CHECK_EQ(res.textures[0].height, 2u);
		CHECK(res.textures[0].hasAlpha);
		CHECK_EQ(res.textures[0].rgba[0], 0x30u);
		CHECK_EQ(res.textures[0].rgba[1], 0x20u);
		CHECK_EQ(res.textures[0].rgba[2], 0x10u);
		CHECK_EQ(res.textures[0].rgba[3], 0x80u);
	}

	// 2. D3D9 DXT1 (4x4 solid red c0=0xF800 > c1=0x0000, idx=0)
	{
		std::vector<uint8_t> mip0 = { 0x00, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
		auto txd = buildSingleTextureTxd("test_dxt1", 9u, 0x0200u, 0x31545844u, 4, 4, 16, 0x00, {}, mip0);
		auto res = decodeTxdBytes(txd.data(), txd.size());
		CHECK(res.ok);
		CHECK_EQ(res.textures[0].rgba[0], 255u);
		CHECK_EQ(res.textures[0].rgba[1], 0u);
		CHECK_EQ(res.textures[0].rgba[2], 0u);
		CHECK_EQ(res.textures[0].rgba[3], 255u);
		CHECK(!res.textures[0].hasAlpha);
	}

	// 3. D3D9 DXT5 (4x4 alpha=0x80)
	{
		std::vector<uint8_t> mip0 = {
			0x80, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // alpha: a0=0x80, all idx=0
			0xE0, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // green RGB565=0x07E0
		};
		auto txd = buildSingleTextureTxd("test_dxt5", 9u, 0x0500u, 0x35545844u, 4, 4, 32, 0x01, {}, mip0);
		auto res = decodeTxdBytes(txd.data(), txd.size());
		CHECK(res.ok);
		CHECK(res.textures[0].hasAlpha);
		CHECK_EQ(res.textures[0].rgba[1], 255u);
		CHECK_EQ(res.textures[0].rgba[3], 0x80u);
	}

	// 4. PAL8 (2x2)
	{
		std::vector<uint8_t> pal(1024, 0);
		pal[4 * 7 + 0] = 12;
		pal[4 * 7 + 1] = 34;
		pal[4 * 7 + 2] = 56;
		pal[4 * 7 + 3] = 255;
		std::vector<uint8_t> mip0 = { 7, 7, 7, 7 };
		auto txd = buildSingleTextureTxd("test_pal8", 9u, 0x2500u, 0u, 2, 2, 8, 0x00, pal, mip0);
		auto res = decodeTxdBytes(txd.data(), txd.size());
		CHECK(res.ok);
		CHECK_EQ(res.textures[0].rgba[0], 12u);
		CHECK_EQ(res.textures[0].rgba[1], 34u);
		CHECK_EQ(res.textures[0].rgba[2], 56u);
	}

	// 5. NPOT normalization (6x6 -> 8x8 pow2)
	{
		std::vector<uint8_t> mip0(6 * 6 * 4, 0xFF);
		auto txd = buildSingleTextureTxd("test_npot", 9u, 0x0500u, 21u, 6, 6, 32, 0x00, {}, mip0);
		auto res = decodeTxdBytes(txd.data(), txd.size(), 2048, true);
		CHECK(res.ok);
		CHECK_EQ(res.textures[0].width, 8u);
		CHECK_EQ(res.textures[0].height, 8u);
	}

	// 6. Negative / corrupt cases & cumulative memory-amplification cap
	{
		const uint8_t tiny[4] = {1, 2, 3, 4};
		CHECK(!validateTxdBytes(tiny, sizeof(tiny)));
		auto badPlat = buildSingleTextureTxd("bad", 99u, 0x0500u, 21u, 2, 2, 32, 0, {}, std::vector<uint8_t>(16, 0));
		CHECK(!validateTxdBytes(badPlat.data(), badPlat.size()));
		// Truncated buffer
		auto trunc = buildSingleTextureTxd("trunc", 9u, 0x0500u, 21u, 4, 4, 32, 0, {}, std::vector<uint8_t>(64, 0));
		CHECK(!validateTxdBytes(trunc.data(), trunc.size() - 10));
		// Memory amplification guard: a tiny 16x16 DXT1 (128B compressed -> 1024B RGBA) rejected if budget < 1024B
		auto amp = buildSingleTextureTxd("amp", 9u, 0x0000u, 0x31545844u, 16, 16, 16, 0, {}, std::vector<uint8_t>(128, 0));
		CHECK(validateTxdBytes(amp.data(), amp.size(), nullptr, 1024u));
		CHECK(!validateTxdBytes(amp.data(), amp.size(), nullptr, 512u));
		auto ampDec = decodeTxdBytes(amp.data(), amp.size(), 2048u, true, 512u);
		CHECK(!ampDec.ok);
		CHECK(ampDec.failure == TxdParseFailure::BudgetExceeded);
		auto badDec = decodeTxdBytes(tiny, sizeof(tiny));
		CHECK(!badDec.ok);
		CHECK(badDec.failure == TxdParseFailure::Malformed);
	}
}

static void testDownloadRetryAndCachedCheck()
{
	// Verify typed failure retry contract (zero string matching)
	CHECK(isRetryableFailure(DownloadFailureKind::TransientNet));
	CHECK(isRetryableFailure(DownloadFailureKind::HttpServerError));
	CHECK(isRetryableFailure(DownloadFailureKind::SizeMismatch));
	CHECK(isRetryableFailure(DownloadFailureKind::CrcMismatch));
	CHECK(!isRetryableFailure(DownloadFailureKind::HttpClientError));
	CHECK(!isRetryableFailure(DownloadFailureKind::UnsafeUrl));
	CHECK(!isRetryableFailure(DownloadFailureKind::InvalidFormat));
	CHECK(!isRetryableFailure(DownloadFailureKind::BudgetExceeded));
	CHECK(!isRetryableFailure(DownloadFailureKind::Unsupported));

	ArtworkManifest m;
	CustomModelRecord rec{};
	rec.poolId = 0;
	rec.type = ModelType::Skin;
	rec.virtualWorld = -1;
	rec.baseId = 29;
	rec.newId = 20001;
	rec.dffCrc = 0x12345678u;
	rec.txdCrc = 0x87654321u;
	rec.dffSize = 512;
	rec.txdSize = 512;
	CHECK(m.addRecord(rec, 1, false, true)); // TXD already cached on disk, DFF missing
	CHECK(m.setVirtualWorld(0));
	CHECK(m.isResourceCached(FileType::Txd, rec.txdCrc));
	CHECK(m.resourceState(FileType::Dff, rec.dffCrc) == ResourceState::Missing);
	CHECK(m.hasPendingActiveResources());

	// Transient failures should re-queue up to kMaxDownloadRetries times, then fail permanently.
	for (uint8_t attempt = 0; attempt < kMaxDownloadRetries; ++attempt) {
		ResourceDownloadTask t{};
		CHECK(m.popNextResourceToRequest(t));
		CHECK_EQ(t.crc, rec.dffCrc);
		uint32_t sz = 0;
		CHECK(m.onUrlReceived(t.type, t.crc, sz));
		m.onDownloadFinished(t.type, t.crc, false, DownloadFailureKind::TransientNet, "timeout");
		CHECK(m.hasPendingActiveResources());
	}
	// Final attempt after retries exhausted -> permanent Failed
	{
		ResourceDownloadTask t{};
		CHECK(m.popNextResourceToRequest(t));
		uint32_t sz = 0;
		CHECK(m.onUrlReceived(t.type, t.crc, sz));
		m.onDownloadFinished(t.type, t.crc, false, DownloadFailureKind::TransientNet, "timeout");
		CHECK(!m.hasPendingActiveResources());
		CHECK(m.shouldSendFinishDownload());
	}

	// Permanent failure (e.g. HTTP 404) must NOT retry
	ArtworkManifest m2;
	CHECK(m2.addRecord(rec, 1));
	CHECK(m2.setVirtualWorld(0));
	m2.markResourceCached(FileType::Txd, rec.txdCrc);
	ResourceDownloadTask t2{};
	CHECK(m2.popNextResourceToRequest(t2));
	uint32_t sz2 = 0;
	CHECK(m2.onUrlReceived(t2.type, t2.crc, sz2));
	m2.onDownloadFinished(t2.type, t2.crc, false, DownloadFailureKind::HttpClientError, "HTTP 404");
	CHECK(!m2.hasPendingActiveResources());
	CHECK(m2.resourceState(FileType::Dff, rec.dffCrc) == ResourceState::Failed);
}

static void testDffValidatorAndColExtraction()
{
	// 1. Valid DFF with SA-MP 0.3.DL embedded COL3 (0x0253F2FF)
	{
		auto dff = buildMinimalDff("custom_wall", 0x0253F2FFu, 0x334C4F43u);
		auto res = validateAndPrepareDff(dff.data(), dff.size(), true);
		CHECK(res.ok);
		CHECK_EQ(res.frameCount, 1u);
		CHECK_EQ(res.geometryCount, 1u);
		CHECK_EQ(res.atomicCount, 1u);
		CHECK(res.hasEmbeddedColChunk);
		CHECK(res.colFormat == ColFormat::Col3);
		CHECK_EQ(res.embeddedCol.size(), 146u);
		CHECK_EQ(res.referencedTextures.size(), 1u);
		CHECK(res.referencedTextures[0] == "custom_wall");
		CHECK(!res.sanitizedDff.empty());
		CHECK(res.sanitizedDff.size() < dff.size());

		// Sanitized DFF must itself pass full validation and have NO remaining embedded COL!
		auto res2 = validateAndPrepareDff(res.sanitizedDff.data(), res.sanitizedDff.size(), true);
		CHECK(res2.ok);
		CHECK(!res2.hasEmbeddedColChunk);
		CHECK(res2.colFormat == ColFormat::None);
		CHECK(res2.embeddedCol.empty());
		CHECK(res2.sanitizedDff.empty());
	}

	// 2. Deep COL validator negative cases (OOB vertex index, OOB offset, NaN bounds)
	{
		ColFormat fmt = ColFormat::None;
		std::string err;
		auto goodCol = buildValidCol3();
		CHECK(validateColBytes(goodCol.data(), goodCol.size(), fmt, err));
		CHECK(fmt == ColFormat::Col3);

		// Corrupt face vertex index beyond 3 vertices -> must reject
		auto badVert = goodCol;
		badVert[badVert.size() - 8u] = 99u; // face.a = 99 (> 2)
		CHECK(!validateColBytes(badVert.data(), badVert.size(), fmt, err));

		// Corrupt offFaces to point past EOF -> must reject
		auto badOff = goodCol;
		badOff[32u + 68u] = 0xFFu;
		badOff[32u + 69u] = 0xFFu;
		CHECK(!validateColBytes(badOff.data(), badOff.size(), fmt, err));

		// Corrupt bounding sphere radius to NaN -> must reject
		auto badNan = goodCol;
		badNan[32u + 36u] = 0xFFu;
		badNan[32u + 37u] = 0xFFu;
		badNan[32u + 38u] = 0xFFu;
		badNan[32u + 39u] = 0x7Fu;
		CHECK(!validateColBytes(badNan.data(), badNan.size(), fmt, err));

		// Exporter quirk: encodedSize == total chunk length (includes 8-byte header) must be accepted
		// while still rejecting encodedSize > colLen.
		auto plus8Col = goodCol;
		const uint32_t totalLen = static_cast<uint32_t>(plus8Col.size());
		plus8Col[4] = static_cast<uint8_t>(totalLen & 0xFFu);
		plus8Col[5] = static_cast<uint8_t>((totalLen >> 8) & 0xFFu);
		plus8Col[6] = static_cast<uint8_t>((totalLen >> 16) & 0xFFu);
		plus8Col[7] = static_cast<uint8_t>((totalLen >> 24) & 0xFFu);
		CHECK(validateColBytes(plus8Col.data(), plus8Col.size(), fmt, err));
		CHECK(fmt == ColFormat::Col3);

		auto overCol = plus8Col;
		const uint32_t tooBig = totalLen + 4u;
		overCol[4] = static_cast<uint8_t>(tooBig & 0xFFu);
		overCol[5] = static_cast<uint8_t>((tooBig >> 8) & 0xFFu);
		overCol[6] = static_cast<uint8_t>((tooBig >> 16) & 0xFFu);
		overCol[7] = static_cast<uint8_t>((tooBig >> 24) & 0xFFu);
		CHECK(!validateColBytes(overCol.data(), overCol.size(), fmt, err));
	}

	// 3. Valid DFF without collision -> zero-copy (sanitizedDff empty)
	{
		auto dff = buildMinimalDff("skin_tex", 0, 0);
		auto res = validateAndPrepareDff(dff.data(), dff.size(), true);
		CHECK(res.ok);
		CHECK(!res.hasEmbeddedColChunk);
		CHECK(res.colFormat == ColFormat::None);
		CHECK(res.sanitizedDff.empty());
	}

	// 4. Truncated / corrupt DFF and oversized rwID_NODENAME (>=24B) must be rejected cleanly
	{
		auto dff = buildMinimalDff("skin_tex", 0, 0);
		auto truncRes = validateAndPrepareDff(dff.data(), dff.size() / 2, true);
		CHECK(!truncRes.ok);
		const uint8_t garbage[32] = {0xDE, 0xAD, 0xBE, 0xEF};
		CHECK(!validateAndPrepareDff(garbage, sizeof(garbage), true).ok);

		auto okNodeNameDff = buildMinimalDff("skin_tex", 0, 0, "12345678901234567890123"); // 23 bytes -> OK
		CHECK(validateAndPrepareDff(okNodeNameDff.data(), okNodeNameDff.size(), true).ok);
		auto badNodeNameDff = buildMinimalDff("skin_tex", 0, 0, "123456789012345678901234"); // 24 bytes -> overflow
		CHECK(!validateAndPrepareDff(badNodeNameDff.data(), badNodeNameDff.size(), true).ok);
	}

	// 5. 2dEffect (0x0253F2F8) Light exporter quirk: 2 entries of 96B (76B payload) wrongly stamped
	// with size=0x50 must be normalized to size=0x4C in sanitizedDff; true 80B entries stay untouched.
	{
		std::vector<uint8_t> fxBug;
		pushLE32(fxBug, 2u); // count = 2
		for (int i = 0; i < 2; ++i) {
			pushF32(fxBug, 1.0f); pushF32(fxBug, 2.0f); pushF32(fxBug, 3.0f); // pos (12B)
			pushLE32(fxBug, 0u);     // type = Light (0)
			pushLE32(fxBug, 0x50u);  // erroneously stamped 80B
			fxBug.insert(fxBug.end(), 76u, 0u); // actual 76B (0x4C) payload
		}
		auto fxChunk = wrapChunk(0x0253F2F8u, fxBug);
		auto dff = buildMinimalDff("torch_wall", 0x0253F2FFu, 0x334C4F43u, nullptr, &fxChunk);
		auto res = validateAndPrepareDff(dff.data(), dff.size(), true);
		CHECK(res.ok);
		CHECK(!res.sanitizedDff.empty());
		// Locate 2dEffect chunk inside sanitizedDff and verify both entry sizes are now 0x4C
		size_t fxPos = 0;
		for (size_t i = 0; i + 4 <= res.sanitizedDff.size(); ++i) {
			if (res.sanitizedDff[i] == 0xF8u && res.sanitizedDff[i + 1] == 0xF2u
				&& res.sanitizedDff[i + 2] == 0x53u && res.sanitizedDff[i + 3] == 0x02u) {
				fxPos = i;
				break;
			}
		}
		CHECK(fxPos > 0);
		const size_t pay = fxPos + 12u;
		CHECK_EQ(res.sanitizedDff[pay + 4u + 16u], 0x4Cu);
		CHECK_EQ(res.sanitizedDff[pay + 4u + 96u + 16u], 0x4Cu);
		// Re-validating sanitizedDff must succeed with zero further modifications!
		auto res2 = validateAndPrepareDff(res.sanitizedDff.data(), res.sanitizedDff.size(), true);
		CHECK(res2.ok);
		CHECK(res2.sanitizedDff.empty());

		// Genuine 0x50 (80B) Light entries (4 + 2*100 = 204B) must NOT be rewritten.
		std::vector<uint8_t> fxTrue80;
		pushLE32(fxTrue80, 2u);
		for (int i = 0; i < 2; ++i) {
			pushF32(fxTrue80, 1.0f); pushF32(fxTrue80, 2.0f); pushF32(fxTrue80, 3.0f);
			pushLE32(fxTrue80, 0u);
			pushLE32(fxTrue80, 0x50u);
			fxTrue80.insert(fxTrue80.end(), 80u, 0u);
		}
		auto fx80Chunk = wrapChunk(0x0253F2F8u, fxTrue80);
		auto dff80 = buildMinimalDff("torch_wall", 0, 0, nullptr, &fx80Chunk);
		auto res80 = validateAndPrepareDff(dff80.data(), dff80.size(), true);
		CHECK(res80.ok);
		CHECK(res80.sanitizedDff.empty());
	}
}

static void testRuntimeAddModelReopensDownloadCycle()
{
	ArtworkManifest m;

	// Initial empty VW 0 completes immediately.
	CHECK(m.setVirtualWorld(0));
	CHECK(m.shouldSendFinishDownload());
	m.markFinishDownloadSent();
	m.onDownloadCompletedRpc();
	CHECK(!m.isDownloadGateActive());

	// Runtime addCustomModel in a DIFFERENT VW (99) must NOT reopen VW 0's download cycle.
	CustomModelRecord otherVw{};
	otherVw.type = ModelType::Object;
	otherVw.virtualWorld = 99;
	otherVw.baseId = 19374;
	otherVw.newId = -2500;
	otherVw.dffCrc = 0x10101010u;
	otherVw.txdCrc = 0x20202020u;
	otherVw.dffSize = 512;
	otherVw.txdSize = 512;
	CHECK(m.addRecord(otherVw, 1));
	CHECK(!m.isDownloadGateActive());
	ResourceDownloadTask task{};
	CHECK(!m.popNextResourceToRequest(task));

	// Runtime addCustomModel in active scope (VW -1 or VW 0) MUST reopen download cycle.
	CustomModelRecord activeRec{};
	activeRec.type = ModelType::Object;
	activeRec.virtualWorld = -1;
	activeRec.baseId = 19374;
	activeRec.newId = -2501;
	activeRec.dffCrc = 0x30303030u;
	activeRec.txdCrc = 0x40404040u;
	activeRec.dffSize = 1024;
	activeRec.txdSize = 2048;
	CHECK(m.addRecord(activeRec, 2));
	CHECK(m.isDownloadGateActive());
	CHECK(!m.shouldSendFinishDownload());
	CHECK(m.popNextResourceToRequest(task));
	CHECK_EQ(task.crc, 0x30303030u);

	uint32_t exp = 0;
	CHECK(m.onUrlReceived(FileType::Dff, 0x30303030u, exp));
	m.onDownloadFinished(FileType::Dff, 0x30303030u, true);
	CHECK(m.popNextResourceToRequest(task));
	CHECK_EQ(task.crc, 0x40404040u);
	CHECK(m.onUrlReceived(FileType::Txd, 0x40404040u, exp));
	m.onDownloadFinished(FileType::Txd, 0x40404040u, true);

	CHECK(m.shouldSendFinishDownload());
	m.markFinishDownloadSent();
	m.onDownloadCompletedRpc();
	CHECK(!m.isDownloadGateActive());

	// Runtime add of an ALREADY-CACHED model in active scope must NOT reopen cycle or flash overlay.
	CustomModelRecord cachedRuntime{};
	cachedRuntime.type = ModelType::Object;
	cachedRuntime.virtualWorld = 0;
	cachedRuntime.baseId = 19374;
	cachedRuntime.newId = -2502;
	cachedRuntime.dffCrc = 0x50505050u;
	cachedRuntime.txdCrc = 0x60606060u;
	cachedRuntime.dffSize = 512;
	cachedRuntime.txdSize = 512;
	CHECK(m.addRecord(cachedRuntime, 3, true, true));
	CHECK(!m.isDownloadGateActive());
	CHECK(!m.shouldSendFinishDownload());
	CHECK(!m.buildProgressSnapshot().active);

	// Streaming join with serverCount > records.size() must NOT fire FinishDownload early or flash overlay.
	ArtworkManifest streaming;
	CustomModelRecord r1 = cachedRuntime;
	r1.newId = -2600;
	r1.dffCrc = 0x70000001u;
	r1.txdCrc = 0x70000002u;
	CHECK(streaming.addRecord(r1, 3, true, true));
	CHECK(streaming.setVirtualWorld(0));
	CHECK(!streaming.isManifestSettled());
	CHECK(!streaming.shouldSendFinishDownload()); // 1/3 records arrived: must wait!
	CHECK(!streaming.isManifestCompleteForPurge());
	CHECK(!streaming.buildProgressSnapshot().active); // 100% cached -> overlay stays hidden!

	CustomModelRecord r2 = r1;
	r2.newId = -2601;
	r2.dffCrc = 0x70000003u;
	CHECK(streaming.addRecord(r2, 3, true, true));
	CHECK(!streaming.shouldSendFinishDownload()); // still 2/3

	// Fallback idle settle allows FinishDownload if record #3 was invalid/dropped, while blocking purge.
	streaming.markManifestArrivalIdle();
	CHECK(streaming.isManifestSettled());
	CHECK(streaming.shouldSendFinishDownload());
	CHECK(!streaming.isManifestCompleteForPurge()); // incomplete count -> never purge cache!

	CustomModelRecord r3 = r1;
	r3.newId = -2602;
	r3.dffCrc = 0x70000004u;
	CHECK(streaming.addRecord(r3, 3, true, true));
	CHECK(streaming.isManifestCompleteForPurge());
	CHECK(!streaming.buildProgressSnapshot().active);
}

static void testStrictFsmCorruptRecoveryUrlRedactionAndHistoryCap()
{
	// Unified URL length (1024) + query/userinfo redaction
	std::string longUrl = "http://example.com/" + std::string(1020, 'a');
	CHECK(!isSafeHttpUrl(longUrl));
	CHECK(redactUrlForLog("https://user:pass@cdn.example.com:8443/models/a.dff?token=secret123#frag")
		== "https://cdn.example.com:8443/models/a.dff?<redacted>");
	CHECK(redactUrlForLog("http://192.168.1.3:7777/skin.txd")
		== "http://192.168.1.3:7777/skin.txd");

	ArtworkManifest m;
	CustomModelRecord rec{};
	rec.type = ModelType::Object;
	rec.virtualWorld = 0;
	rec.baseId = 19374;
	rec.newId = -2600;
	rec.dffCrc = 0x55555555u;
	rec.txdCrc = 0x66666666u;
	rec.dffSize = 1000;
	rec.txdSize = 2000;
	CHECK(m.addRecord(rec, 1));
	CHECK(m.setVirtualWorld(0));

	// Strict FSM: unsolicited onUrlReceived for a Missing resource MUST be rejected!
	uint32_t exp = 0;
	CHECK(!m.onUrlReceived(FileType::Dff, rec.dffCrc, exp));
	CHECK(!m.onDirectBegin(FileType::Txd, rec.txdCrc, exp));

	const uint64_t revBefore = m.revision();
	ResourceDownloadTask task{};
	CHECK(m.popNextResourceToRequest(task));
	CHECK(m.revision() > revBefore);
	CHECK(m.onUrlReceived(task.type, task.crc, exp));
	// Duplicate onUrlReceived while already Downloading MUST be rejected!
	CHECK(!m.onUrlReceived(task.type, task.crc, exp));
	m.onDownloadFinished(task.type, task.crc, true);

	CHECK(m.popNextResourceToRequest(task));
	CHECK(m.onUrlReceived(task.type, task.crc, exp));
	m.onDownloadFinished(task.type, task.crc, true);
	m.markFinishDownloadSent();
	m.onDownloadCompletedRpc();
	CHECK(!m.isDownloadGateActive());

	// Corrupt cache recovery: Cached -> Missing ONCE, reopens active gate; 2nd time -> Failed.
	CHECK(m.requeueCorruptCacheResource(FileType::Txd, rec.txdCrc));
	CHECK(m.resourceState(FileType::Txd, rec.txdCrc) == ResourceState::Missing);
	CHECK(m.isDownloadGateActive());
	CHECK(m.popNextResourceToRequest(task));
	CHECK_EQ(task.crc, rec.txdCrc);
	CHECK(m.onUrlReceived(FileType::Txd, rec.txdCrc, exp));
	m.onDownloadFinished(FileType::Txd, rec.txdCrc, true);
	// Second corrupt-cache trigger exceeds kMaxCorruptCacheRecoveries -> permanently Failed (no loop!)
	CHECK(!m.requeueCorruptCacheResource(FileType::Txd, rec.txdCrc));
	CHECK(m.resourceState(FileType::Txd, rec.txdCrc) == ResourceState::Failed);

	// Bounded m_transferOrder history across > kMaxTransferHistory resources
	ArtworkManifest big;
	for (uint32_t i = 0; i < 90u; ++i) {
		CustomModelRecord r{};
		r.type = ModelType::Object;
		r.virtualWorld = -1;
		r.baseId = 19374;
		r.newId = -2000 - static_cast<int32_t>(i);
		r.dffCrc = 0x70000000u + i;
		r.txdCrc = 0x80000000u + i;
		r.dffSize = 100;
		r.txdSize = 100;
		CHECK(big.addRecord(r, 90));
	}
	CHECK(big.setVirtualWorld(0));
	while (big.popNextResourceToRequest(task)) {
		CHECK(big.onUrlReceived(task.type, task.crc, exp));
		big.onDownloadFinished(task.type, task.crc, true);
	}
	auto snap = big.buildProgressSnapshot();
	CHECK(snap.items.size() <= kMaxTransferHistory);
}

static void testTxdMip0TruncatedAndStreaming()
{
	// 1. DXT1 8x8 requires 4 blocks * 8B = 32 bytes; 16-byte mip0 payload MUST be rejected by validateTxdBytes!
	std::vector<uint8_t> shortDxt1(16, 0xAA);
	auto badTxd = buildSingleTextureTxd("trunc_dxt1", 9u, 0x0500u, 0x31545844u, 8, 8, 32, 0, {}, shortDxt1);
	std::string err;
	TxdParseFailure fail = TxdParseFailure::None;
	CHECK(!validateTxdBytes(badTxd.data(), badTxd.size(), &err, kMaxDecodedTxdBytes, &fail));
	CHECK(fail == TxdParseFailure::Malformed);

	// 2. BGRA32 4x4 requires 64 bytes; 32-byte mip0 payload MUST be rejected!
	std::vector<uint8_t> shortRgba(32, 0xFF);
	auto badRgbaTxd = buildSingleTextureTxd("trunc_rgba", 9u, 0x0500u, 21u, 4, 4, 32, 1, {}, shortRgba);
	CHECK(!validateTxdBytes(badRgbaTxd.data(), badRgbaTxd.size(), &err, kMaxDecodedTxdBytes, &fail));

	// 3. Streaming decode & projected-bytes precheck must agree on exact RGBA footprint.
	std::vector<uint8_t> fullRgba(4 * 4 * 4, 0x7F);
	auto goodTxd = buildSingleTextureTxd("stream_tex", 9u, 0x0500u, 21u, 4, 4, 32, 1, {}, fullRgba);
	size_t projectedBytes = 0;
	CHECK(validateTxdBytes(goodTxd.data(), goodTxd.size(), &err, kMaxDecodedTxdBytes, &fail, &projectedBytes));
	CHECK_EQ(projectedBytes, 64u);
	int count = 0;
	size_t totalBytes = 0;
	CHECK(decodeTxdStreaming(goodTxd.data(), goodTxd.size(), 2048u, true, kMaxDecodedTxdBytes,
		[&](DecodedTexture&& dt) {
			++count;
			CHECK(dt.name == "stream_tex");
			CHECK_EQ(dt.width, 4u);
			CHECK_EQ(dt.height, 4u);
			CHECK_EQ(dt.rgba.size(), 64u);
			return true;
		}, &totalBytes, &fail, &err));
	CHECK_EQ(count, 1);
	CHECK_EQ(totalBytes, 64u);
}

static void testLzma1DirectChunkDecoder()
{
	// Vector 1: Repeating text (1220B -> 81B) compressed with Python lzma (lc=3, lp=0, pb=2, dict=65536)
	const std::string expected1_base = "SA-MP 0.3.DL custom artwork direct-chunk LZMA1 test payload! ";
	std::string expected1;
	for (int i = 0; i < 20; ++i) expected1 += expected1_base;
	const uint8_t comp1[] = {
		0x00,0x29,0x90,0x41,0xA3,0x69,0x91,0xC1,0xB5,0x71,0x8D,0x39,0xE2,0x00,0x44,0x57,
		0x41,0xA8,0x5B,0x95,0xE4,0xEC,0x8C,0x0C,0xBE,0x84,0xC7,0x79,0xD9,0xC0,0x12,0xB6,
		0x58,0x39,0x37,0x78,0xED,0x36,0x39,0xF9,0x67,0xFA,0x2D,0x6E,0x98,0xCF,0x03,0x99,
		0xDB,0x97,0x85,0x37,0x2E,0x27,0x9A,0x88,0x5C,0xCA,0x9D,0x1E,0xB4,0xE5,0x44,0xDB,
		0x40,0x1A,0xF0,0x00,0x8F,0xD5,0x0F,0x24,0x1F,0x35,0xF3,0xFF,0xFF,0xD2,0x4A,0x80,0x00
	};
	uint8_t out[4096]{};
	size_t decodedLen = 0;
	CHECK(decodeLzma1Chunk03dl(comp1, sizeof(comp1), out, expected1.size(), decodedLen));
	CHECK_EQ(decodedLen, expected1.size());
	CHECK(std::memcmp(out, expected1.data(), expected1.size()) == 0);

	// Vector 2: Pseudo-random binary (512B -> 324B)
	std::vector<uint8_t> expected2(512);
	for (size_t i = 0; i < expected2.size(); ++i) {
		expected2[i] = static_cast<uint8_t>((i * 37u + (i >> 3) * 13u) & 0xFFu);
	}
	const uint8_t comp2[] = {
		0x00,0x00,0x0A,0x19,0x1F,0x70,0x79,0x77,0xAC,0x84,0x48,0x23,0xBF,0x72,0xF4,0x6C,
		0x4E,0x0B,0xE4,0x94,0xE0,0x0B,0x9F,0x1E,0x49,0x4A,0x37,0x96,0xB1,0x0F,0x37,0x9B,
		0x54,0xC6,0xF1,0x87,0xF6,0x5F,0x13,0xF9,0x8E,0x30,0xC6,0x64,0x1C,0xEA,0xC9,0xC1,
		0xCF,0xF2,0x0A,0xEE,0x1C,0xA6,0xF8,0xBE,0x1D,0x69,0x2A,0xEF,0x2B,0xE7,0x92,0x9F,
		0x22,0x8F,0x56,0x16,0xA4,0xB2,0x68,0x27,0x17,0x95,0x23,0xF7,0xFF,0xFE,0x52,0x6D,
		0xB8,0xD2,0xDA,0xFD,0xB9,0x5B,0xFB,0x16,0x28,0x70,0x35,0x7B,0x6F,0xEA,0xD4,0x6F,
		0x16,0xE9,0x95,0x26,0x2C,0x4A,0xFA,0x43,0xDA,0xBF,0x26,0x27,0x97,0xD4,0x96,0xB8,
		0x70,0x16,0xC2,0xFA,0x0C,0x8C,0xBD,0x32,0x4D,0xB9,0x40,0x8F,0xC1,0x9C,0x22,0x61,
		0x7B,0x33,0xF1,0x33,0xCB,0x7E,0xF1,0xBF,0x2A,0x2C,0xE2,0xC0,0x2F,0x35,0x23,0xF7,
		0xD4,0xE5,0x7C,0x91,0xAD,0x50,0x0E,0xE2,0x4F,0x5C,0x47,0x94,0x4E,0x92,0x4F,0x53,
		0xDC,0x1D,0x6B,0xE6,0x70,0x6C,0x41,0x79,0x50,0x36,0x74,0x73,0xD7,0xDF,0xB5,0xD2,
		0xBF,0xBD,0xCD,0x8D,0x48,0x8A,0x9B,0xA2,0xE5,0x44,0x65,0xB0,0xEA,0x90,0xE9,0x2C,
		0x6C,0xC8,0x67,0xF8,0xDA,0x67,0xEB,0x50,0x16,0x26,0xD5,0x3D,0x49,0xD9,0x1F,0x45,
		0x99,0xC3,0xB2,0xB9,0x38,0x62,0x7F,0xA0,0xAB,0xD7,0x1E,0x49,0xDC,0x35,0x0F,0xA6,
		0x91,0xD6,0x8F,0x62,0x48,0x83,0x10,0xA6,0x2A,0x08,0x77,0x91,0xAD,0xF0,0x85,0x24,
		0x9D,0x90,0x20,0x7E,0xA8,0xC1,0x15,0x8C,0x74,0x99,0xBB,0x3F,0xC3,0x81,0xD5,0x17,
		0x82,0x44,0xAC,0xEE,0x1E,0xF2,0x48,0x4B,0xF6,0x62,0x3A,0x2F,0xC3,0x3F,0x55,0x5D,
		0x86,0x2E,0x66,0x6E,0x64,0x5E,0x23,0xE0,0x07,0x8B,0x7E,0x5F,0x63,0x20,0x29,0xCC,
		0x42,0x2B,0xAC,0x34,0x96,0x39,0x92,0x12,0x89,0x1A,0x13,0x66,0xF1,0x7D,0x6A,0x45,
		0x41,0xDE,0x29,0xFA,0xEA,0x0A,0x4C,0x46,0xC9,0xA0,0xB1,0xDC,0xF5,0xF3,0xFF,0xFF,
		0xC8,0x95,0x40,0x00
	};
	CHECK(decodeLzma1Chunk03dl(comp2, sizeof(comp2), out, expected2.size(), decodedLen));
	CHECK_EQ(decodedLen, expected2.size());
	CHECK(std::memcmp(out, expected2.data(), expected2.size()) == 0);

	// Vector 3: Single byte 'A'
	const uint8_t comp3[] = {0x00,0x20,0xC1,0xFB,0xFF,0xFF,0xFF,0xE0,0x00,0x00,0x00};
	CHECK(decodeLzma1Chunk03dl(comp3, sizeof(comp3), out, 1u, decodedLen));
	CHECK_EQ(decodedLen, 1u);
	CHECK_EQ(out[0], static_cast<uint8_t>('A'));

	// Negative cases: bad leading byte, truncated stream, oversized expected length
	const uint8_t badHeader[] = {0x01,0x20,0xC1,0xFB,0xFF,0xFF};
	CHECK(!decodeLzma1Chunk03dl(badHeader, sizeof(badHeader), out, 1u, decodedLen));
	CHECK(!decodeLzma1Chunk03dl(comp1, 10u, out, expected1.size(), decodedLen));
	CHECK(!decodeLzma1Chunk03dl(comp1, sizeof(comp1), out, 4097u, decodedLen));
}

static void testRetryBackoffAndAdaptiveConcurrency()
{
	ArtworkManifest m;
	for (uint32_t i = 0; i < 3u; ++i) {
		CustomModelRecord r{};
		r.type = ModelType::Object;
		r.virtualWorld = 0;
		r.baseId = 19374;
		r.newId = -3000 - static_cast<int32_t>(i);
		r.dffCrc = 0x91000000u + i;
		r.txdCrc = 0x92000000u; // shared cached TXD
		r.dffSize = 512;
		r.txdSize = 512;
		CHECK(m.addRecord(r, 3, false, true));
	}
	CHECK(m.setVirtualWorld(0));
	CHECK_EQ(m.effectiveConcurrencyLimit(), kMaxConcurrentDownloads);

	// Pop first DFF at t=1000 and fail it with TransientNet -> enters 1500ms backoff until t=2500.
	ResourceDownloadTask t1{};
	CHECK(m.popNextResourceToRequest(t1, 1000u));
	CHECK_EQ(t1.crc, 0x91000000u);
	uint32_t exp = 0;
	CHECK(m.onUrlReceived(t1.type, t1.crc, exp));
	m.onDownloadFinished(t1.type, t1.crc, false, DownloadFailureKind::TransientNet, "connect timeout", 1000u);
	CHECK(m.hasPendingActiveResources());
	CHECK(!m.shouldSendFinishDownload());

	// Next pop at t=1050 MUST pick fresh item #2 (not the backed-off item #1!).
	ResourceDownloadTask t2{};
	CHECK(m.popNextResourceToRequest(t2, 1050u));
	CHECK_EQ(t2.crc, 0x91000001u);
	CHECK(m.onUrlReceived(t2.type, t2.crc, exp));
	// Second consecutive TransientNet failure throttles concurrency from 4 -> 2.
	m.onDownloadFinished(t2.type, t2.crc, false, DownloadFailureKind::TransientNet, "connect timeout", 1100u);
	CHECK_EQ(m.effectiveConcurrencyLimit(), 2u);

	// Fresh item #3 still popped immediately at t=1150; fail twice more -> throttles to 1.
	ResourceDownloadTask t3{};
	CHECK(m.popNextResourceToRequest(t3, 1150u));
	CHECK_EQ(t3.crc, 0x91000002u);
	CHECK(m.onUrlReceived(t3.type, t3.crc, exp));
	m.onDownloadFinished(t3.type, t3.crc, false, DownloadFailureKind::TransientNet, "connect timeout", 1200u);
	CHECK_EQ(m.effectiveConcurrencyLimit(), 2u);

	// At t=2499 all 3 items are still in backoff -> pop returns false, yet FinishDownload MUST NOT fire!
	ResourceDownloadTask none{};
	CHECK(!m.popNextResourceToRequest(none, 2499u));
	CHECK(m.hasPendingActiveResources());
	CHECK(!m.shouldSendFinishDownload());

	// At t=2500 item #1 exits backoff and is popped again; succeeding resets concurrency back to 4!
	ResourceDownloadTask retry1{};
	CHECK(m.popNextResourceToRequest(retry1, 2500u));
	CHECK_EQ(retry1.crc, 0x91000000u);
	CHECK(m.onUrlReceived(retry1.type, retry1.crc, exp));
	m.onDownloadFinished(retry1.type, retry1.crc, true, DownloadFailureKind::None, "", 2550u);
	CHECK_EQ(m.effectiveConcurrencyLimit(), kMaxConcurrentDownloads);
}

int main()
{
	testCrc32AndHelpers();
	testArtworkManifestStateMachine();
	testDownloadRetryAndCachedCheck();
	testRetryBackoffAndAdaptiveConcurrency();
	testTxdParserFormatsAndErrors();
	testDffValidatorAndColExtraction();
	testRuntimeAddModelReopensDownloadCycle();
	testStrictFsmCorruptRecoveryUrlRedactionAndHistoryCap();
	testTxdMip0TruncatedAndStreaming();
	testLzma1DirectChunkDecoder();
	return check::finish();
}

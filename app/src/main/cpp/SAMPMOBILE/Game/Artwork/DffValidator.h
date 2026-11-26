#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace artwork {

enum class ColFormat : uint8_t {
	None = 0,
	Col1 = 1, // "COLL"
	Col2 = 2, // "COL2"
	Col3 = 3, // "COL3"
	Col4 = 4, // "COL4"
};

struct DffValidationResult {
	bool ok = false;
	std::string error;

	uint32_t frameCount = 0;
	uint32_t geometryCount = 0;
	uint32_t atomicCount = 0;
	bool hasSkinPlg = false;
	bool hasUvAnimDict = false;

	// Embedded collision extracted from Clump Extension (0x0253F2FF or 0x0253F2FA).
	bool hasEmbeddedColChunk = false;
	ColFormat colFormat = ColFormat::None;
	std::vector<uint8_t> embeddedCol;
	std::string colError;

	// When embedded collision chunks were stripped so GTA's 64KB PC_Scratch / unknown plugin
	// path is bypassed safely, this holds the exact rewritten DFF bytes ready for RpClumpStreamRead.
	// If empty (and ok == true), callers can feed the original DFF bytes directly.
	std::vector<uint8_t> sanitizedDff;

	// Unique texture names referenced by materials (for missing-texture diagnostics).
	std::vector<std::string> referencedTextures;
};

// Deep structural & bounds validation for a GTA:SA COL1/COL2/COL3/COL4 blob.
// Verifies FourCC, payload length, finite bounds, section counts/offsets, vertex index bounds,
// face groups, and COL3/4 shadow meshes so libGTASA's LoadCollisionModel* cannot OOB-read.
bool validateColBytes(const uint8_t* colData, size_t colLen, ColFormat& outFmt, std::string& outErr);

// Validates a PC GTA:SA RenderWare .dff file for structural integrity and safety.
// When extractAndStripCol is true, extracts any embedded COL1/2/3/4 chunk (0x0253F2FF or
// 0x0253F2FA) into result.embeddedCol and builds result.sanitizedDff with those chunks removed.
DffValidationResult validateAndPrepareDff(const uint8_t* data, size_t size, bool extractAndStripCol = true);

} // namespace artwork

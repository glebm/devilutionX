#include "utils/palette_blending.hpp"

#include <cstdint>
#include <limits>

#include <SDL.h>

#define NANOFLANN_NO_THREADS
#include <nanoflann.hpp>

namespace devilution {

// This array is read from a lot on every frame.
// We do not use `std::array` here to improve debug build performance.
// In a debug build, `std::array` accesses are function calls.
uint8_t paletteTransparencyLookup[256][256];

#if DEVILUTIONX_PALETTE_TRANSPARENCY_BLACK_16_LUT
uint16_t paletteTransparencyLookupBlack16[65536];
#endif

namespace {

struct RGB {
	uint8_t r;
	uint8_t g;
	uint8_t b;
};

uint8_t FindBestMatchForColor(SDL_Color palette[256], RGB color, int skipFrom, int skipTo)
{
	uint8_t best;
	uint32_t bestDiff = std::numeric_limits<uint32_t>::max();
	for (int i = 0; i < 256; i++) {
		if (i >= skipFrom && i <= skipTo)
			continue;
		const int diffr = palette[i].r - color.r;
		const int diffg = palette[i].g - color.g;
		const int diffb = palette[i].b - color.b;
		const uint32_t diff = diffr * diffr + diffg * diffg + diffb * diffb;

		if (bestDiff > diff) {
			best = i;
			bestDiff = diff;
		}
	}
	return best;
}

RGB BlendColors(const SDL_Color &a, const SDL_Color &b)
{
	return RGB {
		.r = static_cast<uint8_t>((static_cast<int>(a.r) + static_cast<int>(b.r)) / 2),
		.g = static_cast<uint8_t>((static_cast<int>(a.g) + static_cast<int>(b.g)) / 2),
		.b = static_cast<uint8_t>((static_cast<int>(a.b) + static_cast<int>(b.b)) / 2),
	};
}

// Wraps the palette in a interface usable with nanoflann kd-tree library.
class NanoflannPaletteWrapper {
public:
	using coord_t = int16_t;
	explicit NanoflannPaletteWrapper(const SDL_Color *palette)
	    : palette_(palette)
	{
	}

	// NOLINTNEXTLINE(readability-identifier-naming): nanoflann interface
	[[nodiscard]] size_t kdtree_get_point_count() const { return 256; }

	// NOLINTNEXTLINE(readability-identifier-naming): nanoflann interface
	[[nodiscard]] int16_t kdtree_get_pt(size_t idx, size_t dim) const
	{
		switch (dim) {
		case 0:
			return palette_[idx].r;
		case 1:
			return palette_[idx].g;
		default:
			return palette_[idx].b;
		}
	}

	template <class BBOX>
	// NOLINTNEXTLINE(readability-identifier-naming): nanoflann interface
	bool kdtree_get_bbox(BBOX &) const
	{
		return false;
	}

private:
	const SDL_Color *palette_;
};

} // namespace

void GenerateBlendedLookupTable(SDL_Color palette[256], int skipFrom, int skipTo)
{

	const NanoflannPaletteWrapper paletteData { palette };
	const nanoflann::KDTreeSingleIndexAdaptor<
	    nanoflann::L2_Simple_Adaptor<int16_t, NanoflannPaletteWrapper>,
	    NanoflannPaletteWrapper, /*DIM=*/3>
	    index { /*dimensionality=*/3, /*inputData=*/paletteData,
		    nanoflann::KDTreeSingleIndexAdaptorParams { /*leaf_max_size=*/32 } };

	for (unsigned i = 0; i < 256; i++) {
		paletteTransparencyLookup[i][i] = i;
		for (unsigned j = 0; j < i; j++) {
			uint32_t bestResult;
			int16_t bestResultDistSqr;
			nanoflann::KNNResultSet<int16_t, uint32_t> resultSet(1);
			resultSet.init(&bestResult, &bestResultDistSqr);

			const RGB q = BlendColors(palette[i], palette[j]);
			int16_t query[3] { q.r, q.g, q.b };
			index.findNeighbors(resultSet, query);
			paletteTransparencyLookup[i][j] = paletteTransparencyLookup[j][i] = bestResult;
		}
	}

#if DEVILUTIONX_PALETTE_TRANSPARENCY_BLACK_16_LUT
	for (unsigned i = 0; i < 256; ++i) {
		for (unsigned j = 0; j < 256; ++j) {
			const uint16_t index = i | (j << 8U);
			paletteTransparencyLookupBlack16[index] = paletteTransparencyLookup[0][i] | (paletteTransparencyLookup[0][j] << 8);
		}
	}
#endif
}

void UpdateBlendedLookupTableSingleColor(unsigned i, SDL_Color palette[256], int skipFrom, int skipTo)
{
	// Update blended transparency, but only for the color that was updated
	for (unsigned j = 0; j < 256; j++) {
		if (i == j) { // No need to calculate transparency between 2 identical colors
			paletteTransparencyLookup[i][j] = j;
			continue;
		}
		const uint8_t best = FindBestMatchForColor(palette, BlendColors(palette[i], palette[j]), skipFrom, skipTo);
		paletteTransparencyLookup[i][j] = paletteTransparencyLookup[j][i] = best;
	}

#if DEVILUTIONX_PALETTE_TRANSPARENCY_BLACK_16_LUT
	UpdateTransparencyLookupBlack16(i, i);
#endif
}

#if DEVILUTIONX_PALETTE_TRANSPARENCY_BLACK_16_LUT
void UpdateTransparencyLookupBlack16(unsigned from, unsigned to)
{
	for (unsigned i = from; i <= to; i++) {
		for (unsigned j = 0; j < 256; j++) {
			const uint16_t index = i | (j << 8U);
			const uint16_t reverseIndex = j | (i << 8U);
			paletteTransparencyLookupBlack16[index] = paletteTransparencyLookup[0][i] | (paletteTransparencyLookup[0][j] << 8);
			paletteTransparencyLookupBlack16[reverseIndex] = paletteTransparencyLookup[0][j] | (paletteTransparencyLookup[0][i] << 8);
		}
	}
}
#endif

} // namespace devilution

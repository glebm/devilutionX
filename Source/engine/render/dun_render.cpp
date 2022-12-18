/**
 * @file dun_render.cpp
 *
 * Implementation of functionality for rendering the level tiles.
 */

// Debugging variables
// #define DEBUG_STR
// #define DEBUG_RENDER_COLOR
// #define DEBUG_RENDER_OFFSET_X 5
// #define DEBUG_RENDER_OFFSET_Y 5

#include "engine/render/dun_render.hpp"

#include <SDL_endian.h>

#include <algorithm>
#include <climits>
#include <cstdint>

#include "lighting.h"
#include "utils/stdcompat/algorithm.hpp"
#ifdef _DEBUG
#include "miniwin/misc_msg.h"
#endif
#include "options.h"
#include "utils/attributes.h"
#ifdef DEBUG_STR
#include "engine/render/text_render.hpp"
#endif
#if defined(DEBUG_STR) || defined(DUN_RENDER_STATS)
#include "utils/str_cat.hpp"
#endif

namespace devilution {

namespace {

/** Width of a tile rendering primitive. */
constexpr int_fast16_t Width = TILE_WIDTH / 2;

/** Height of a tile rendering primitive (except triangles). */
constexpr int_fast16_t Height = TILE_HEIGHT;

/** Height of the lower triangle of a triangular or a trapezoid tile. */
constexpr int_fast16_t LowerHeight = TILE_HEIGHT / 2;

/** Height of the upper triangle of a triangular tile. */
constexpr int_fast16_t TriangleUpperHeight = TILE_HEIGHT / 2 - 1;

/** Height of the upper rectangle of a trapezoid tile. */
constexpr int_fast16_t TrapezoidUpperHeight = TILE_HEIGHT / 2;

constexpr int_fast16_t TriangleHeight = LowerHeight + TriangleUpperHeight;

/** For triangles, for each pixel drawn vertically, this many pixels are drawn horizontally. */
constexpr int_fast16_t XStep = 2;

int_fast16_t GetTileHeight(TileType tile)
{
	if (tile == TileType::LeftTriangle || tile == TileType::RightTriangle)
		return TriangleHeight;
	return Height;
}

#ifdef DEBUG_STR
std::pair<string_view, UiFlags> GetTileDebugStr(TileType tile)
{
	// clang-format off
	switch (tile) {
		case TileType::Square: return {"S", UiFlags::AlignCenter | UiFlags::VerticalCenter};
		case TileType::TransparentSquare: return {"T", UiFlags::AlignCenter | UiFlags::VerticalCenter};
		case TileType::LeftTriangle: return {"<", UiFlags::AlignRight | UiFlags::VerticalCenter};
		case TileType::RightTriangle: return {">", UiFlags::VerticalCenter};
		case TileType::LeftTrapezoid: return {"\\", UiFlags::AlignCenter};
		case TileType::RightTrapezoid: return {"/", UiFlags::AlignCenter};
		default: return {"", {}};
	}
	// clang-format on
}
#endif

#ifdef DEBUG_RENDER_COLOR
int DBGCOLOR = 0;

int GetTileDebugColor(TileType tile)
{
	// clang-format off
	switch (tile) {
		case TileType::Square: return PAL16_YELLOW + 5;
		case TileType::TransparentSquare: return PAL16_ORANGE + 5;
		case TileType::LeftTriangle: return PAL16_GRAY + 5;
		case TileType::RightTriangle: return PAL16_BEIGE;
		case TileType::LeftTrapezoid: return PAL16_RED + 5;
		case TileType::RightTrapezoid: return PAL16_BLUE + 5;
		default: return 0;
	}
}
#endif // DEBUG_RENDER_COLOR

// Masks are defined by 2 template variables:
//
// 1. `OpaquePrefix`: Whether the line starts with opaque pixels
//    followed by blended pixels or the other way around.
// 2. `PrefixIncrement`: The change to the prefix when going
//    up 1 line.
//
// The Left mask can only be applied to LeftTrapezoid and TransparentSquare.
// The Right mask can only be applied to RightTrapezoid and TransparentSquare.
// The Left/RightFoliage masks can only be applied to TransparentSquare.

// True if the given OpaquePrefix and PrefixIncrement represent foliage.
// For foliage, we skip transparent pixels instead of blending them.
template <bool OpaquePrefix, int8_t PrefixIncrement>
constexpr bool IsFoliage = PrefixIncrement != 0 && (OpaquePrefix == (PrefixIncrement > 0));

// True for foliage:
template <bool OpaquePrefix, int8_t PrefixIncrement>
constexpr bool SkipTransparentPixels = IsFoliage<OpaquePrefix, PrefixIncrement>;

// True if the entire lower half of the mask is transparent.
// True for Transparent, LeftFoliage, and RightFoliage.
template <bool OpaquePrefix, int8_t PrefixIncrement>
constexpr bool LowerHalfTransparent = (OpaquePrefix == (PrefixIncrement >= 0));

// The initial value for the prefix:
template <int8_t PrefixIncrement>
DVL_ALWAYS_INLINE int8_t InitPrefix()
{
	return PrefixIncrement >= 0 ? -32 : 64;
}

// The initial value for the prefix at y-th line (counting from the bottom).
template <int8_t PrefixIncrement>
DVL_ALWAYS_INLINE int8_t InitPrefix(int8_t y)
{
	return InitPrefix<PrefixIncrement>() + PrefixIncrement * y;
}

#ifdef DEBUG_STR
template <bool OpaquePrefix, int8_t PrefixIncrement>
std::string prefixDebugString(int8_t prefix) {
	std::string out(32, OpaquePrefix ? '0' : '1');
	const uint8_t clamped = clamp<int8_t>(prefix, 0, 32);
	out.replace(0, clamped, clamped, OpaquePrefix ? '1' : '0');
	StrAppend(out, " prefix=", prefix, " OpaquePrefix=", OpaquePrefix, " PrefixIncrement=", PrefixIncrement);
	return out;
}
#endif

enum class LightType : uint8_t {
	FullyDark,
	PartiallyLit,
	FullyLit,
};

template <LightType Light>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderLineOpaque(uint8_t *DVL_RESTRICT dst, const uint8_t *DVL_RESTRICT src, uint_fast8_t n, const uint8_t *DVL_RESTRICT tbl)
{
	if (Light == LightType::FullyDark) {
		memset(dst, 0, n);
	} else if (Light == LightType::FullyLit) {
#ifndef DEBUG_RENDER_COLOR
		memcpy(dst, src, n);
#else
		memset(dst, DBGCOLOR, n);
#endif
	} else { // Partially lit
#ifndef DEBUG_RENDER_COLOR
		while (n-- != 0) {
			*dst++ = tbl[*src++];
		}
#else
		memset(dst, tbl[DBGCOLOR], n);
#endif
	}
}

template <LightType Light>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderLineTransparent(uint8_t *DVL_RESTRICT dst, const uint8_t *DVL_RESTRICT src, uint_fast8_t n, const uint8_t *DVL_RESTRICT tbl)
{
#ifndef DEBUG_RENDER_COLOR
	if (Light == LightType::FullyDark) {
		while (n-- != 0) {
			*dst = paletteTransparencyLookup[0][*dst];
			++dst;
		}
	} else if (Light == LightType::FullyLit) {
		while (n-- != 0) {
			*dst = paletteTransparencyLookup[*dst][*src];
			++dst;
			++src;
		}
	} else { // Partially lit
		while (n-- != 0) {
			*dst = paletteTransparencyLookup[*dst][tbl[*src]];
			++dst;
			++src;
		}
	}
#else
	for (size_t i = 0; i < n; i++) {
		dst[i] = paletteTransparencyLookup[dst[i]][tbl[DBGCOLOR + 4]];
	}
#endif
}

template <LightType Light, bool Transparent>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderLineTransparentOrOpaque(uint8_t *DVL_RESTRICT dst, const uint8_t *DVL_RESTRICT src, uint_fast8_t width, const uint8_t *DVL_RESTRICT tbl)
{
	if (Transparent) {
		RenderLineTransparent<Light>(dst, src, width, tbl);
	} else {
		RenderLineOpaque<Light>(dst, src, width, tbl);
	}
}

template <LightType Light, bool OpaquePrefix, int8_t PrefixIncrement>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderLineTransparentAndOpaque(uint8_t *DVL_RESTRICT dst, const uint8_t *DVL_RESTRICT src, uint_fast8_t prefixWidth, uint_fast8_t width, const uint8_t *DVL_RESTRICT tbl)
{
	if (OpaquePrefix) {
		RenderLineOpaque<Light>(dst, src, prefixWidth, tbl);
		if (!SkipTransparentPixels<OpaquePrefix, PrefixIncrement>)
			RenderLineTransparent<Light>(dst + prefixWidth, src + prefixWidth, width - prefixWidth, tbl);
	} else {
		if (!SkipTransparentPixels<OpaquePrefix, PrefixIncrement>)
			RenderLineTransparent<Light>(dst, src, prefixWidth, tbl);
		RenderLineOpaque<Light>(dst + prefixWidth, src + prefixWidth, width - prefixWidth, tbl);
	}
}

template <LightType Light, bool OpaquePrefix, int8_t PrefixIncrement>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderLine(uint8_t *DVL_RESTRICT dst, const uint8_t *DVL_RESTRICT src, uint_fast8_t n, const uint8_t *DVL_RESTRICT tbl, int8_t prefix)
{
	if (PrefixIncrement == 0) {
		RenderLineTransparentOrOpaque<Light, OpaquePrefix>(dst, src, n, tbl);
	} else {
		RenderLineTransparentAndOpaque<Light, OpaquePrefix, PrefixIncrement>(dst, src, clamp<int8_t>(prefix, 0, n), n, tbl);
	}
}

struct Clip {
	int_fast16_t top;
	int_fast16_t bottom;
	int_fast16_t left;
	int_fast16_t right;
	int_fast16_t width;
	int_fast16_t height;
};

DVL_ALWAYS_INLINE Clip CalculateClip(int_fast16_t x, int_fast16_t y, int_fast16_t w, int_fast16_t h, const Surface &out)
{
	Clip clip;
	clip.top = y + 1 < h ? h - (y + 1) : 0;
	clip.bottom = y + 1 > out.h() ? (y + 1) - out.h() : 0;
	clip.left = x < 0 ? -x : 0;
	clip.right = x + w > out.w() ? x + w - out.w() : 0;
	clip.width = w - clip.left - clip.right;
	clip.height = h - clip.top - clip.bottom;
	return clip;
}

template <LightType Light, bool Transparent>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderSquareFull(uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, const uint8_t *DVL_RESTRICT tbl)
{
	for (auto i = 0; i < Height; ++i, dst -= dstPitch) {
		RenderLineTransparentOrOpaque<Light, Transparent>(dst, src, Width, tbl);
		src += Width;
	}
}

template <LightType Light, bool Transparent>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderSquareClipped(uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, const uint8_t *DVL_RESTRICT tbl, Clip clip)
{
	src += clip.bottom * Height + clip.left;
	for (auto i = 0; i < clip.height; ++i, dst -= dstPitch) {
		RenderLineTransparentOrOpaque<Light, Transparent>(dst, src, clip.width, tbl);
		src += Width;
	}
}

template <bool Transparent>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderSquareFullDispatchLight(uint8_t lightTableIndex, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src) {
	const uint8_t *tbl = &LightTables[static_cast<size_t>(256U * lightTableIndex)];
	if (lightTableIndex == LightsMax) {
		RenderSquareFull<LightType::FullyDark, Transparent>(dst, dstPitch, src, tbl);
	} else if (lightTableIndex == 0) {
		RenderSquareFull<LightType::FullyLit, Transparent>(dst, dstPitch, src, tbl);
	} else {
		RenderSquareFull<LightType::PartiallyLit, Transparent>(dst, dstPitch, src, tbl);
	}
}

DVL_ATTRIBUTE_HOT void RenderSquareFullDispatch(MaskType maskType, uint8_t lightTableIndex, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src) {
	switch (maskType) {
	case MaskType::Solid:
		RenderSquareFullDispatchLight</*Transparent=*/false>(lightTableIndex, dst, dstPitch, src);
		break;
	case MaskType::Transparent:
		RenderSquareFullDispatchLight</*Transparent=*/true>(lightTableIndex, dst, dstPitch, src);
		break;
	default:
		app_fatal("Invalid mask type");
	}
}

template <bool Transparent>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderSquareClippedDispatchLight(uint8_t lightTableIndex, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, Clip clip) {
	const uint8_t *tbl = &LightTables[static_cast<size_t>(256U * lightTableIndex)];
	if (lightTableIndex == LightsMax) {
		RenderSquareClipped<LightType::FullyDark, Transparent>(dst, dstPitch, src, tbl, clip);
	} else if (lightTableIndex == 0) {
		RenderSquareClipped<LightType::FullyLit, Transparent>(dst, dstPitch, src, tbl, clip);
	} else {
		RenderSquareClipped<LightType::PartiallyLit, Transparent>(dst, dstPitch, src, tbl, clip);
	}
}

DVL_ATTRIBUTE_HOT void RenderSquareClippedDispatch(MaskType maskType, uint8_t lightTableIndex, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, Clip clip) {
	switch (maskType) {
	case MaskType::Solid:
		RenderSquareClippedDispatchLight</*Transparent=*/false>(lightTableIndex, dst, dstPitch, src, clip);
		break;
	case MaskType::Transparent:
		RenderSquareClippedDispatchLight</*Transparent=*/true>(lightTableIndex, dst, dstPitch, src, clip);
		break;
	default:
		app_fatal("Invalid mask type");
	}
}

template <LightType Light, bool OpaquePrefix, int8_t PrefixIncrement>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderTransparentSquareFull(uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, const uint8_t *DVL_RESTRICT tbl)
{
	int8_t prefix = InitPrefix<PrefixIncrement>();
	for (auto i = 0; i < Height; ++i, dst -= dstPitch + Width) {
		uint_fast8_t drawWidth = Width;
		while (drawWidth > 0) {
			auto v = static_cast<int8_t>(*src++);
			if (v > 0) {
				RenderLine<Light, OpaquePrefix, PrefixIncrement>(dst, src, v, tbl, prefix - (Width - drawWidth));
				src += v;
			} else {
				v = -v;
			}
			dst += v;
			drawWidth -= v;
		}
		prefix += PrefixIncrement;
	}
}

template <LightType Light, bool OpaquePrefix, int8_t PrefixIncrement>
// NOLINTNEXTLINE(readability-function-cognitive-complexity): Actually complex and has to be fast.
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderTransparentSquareClipped(uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, const uint8_t *DVL_RESTRICT tbl, Clip clip)
{
	const auto skipRestOfTheLine = [&src](int_fast16_t remainingWidth) {
		while (remainingWidth > 0) {
			const auto v = static_cast<int8_t>(*src++);
			if (v > 0) {
				src += v;
				remainingWidth -= v;
			} else {
				remainingWidth -= -v;
			}
		}
		assert(remainingWidth == 0);
	};

	// Skip the bottom clipped lines.
	for (auto i = 0; i < clip.bottom; ++i) {
		skipRestOfTheLine(Width);
	}

	int8_t prefix = InitPrefix<PrefixIncrement>(clip.bottom);
	for (auto i = 0; i < clip.height; ++i, dst -= dstPitch + clip.width) {
		auto drawWidth = clip.width;

		// Skip initial src if clipping on the left.
		// Handles overshoot, i.e. when the RLE segment goes into the unclipped area.
		auto remainingLeftClip = clip.left;
		while (remainingLeftClip > 0) {
			auto v = static_cast<int8_t>(*src++);
			if (v > 0) {
				if (v > remainingLeftClip) {
					const auto overshoot = v - remainingLeftClip;
					RenderLine<Light, OpaquePrefix, PrefixIncrement>(dst, src + remainingLeftClip, overshoot, tbl, prefix - (Width - remainingLeftClip));
					dst += overshoot;
					drawWidth -= overshoot;
				}
				src += v;
			} else {
				v = -v;
				if (v > remainingLeftClip) {
					const auto overshoot = v - remainingLeftClip;
					dst += overshoot;
					drawWidth -= overshoot;
				}
			}
			remainingLeftClip -= v;
		}

		// Draw the non-clipped segment
		while (drawWidth > 0) {
			auto v = static_cast<int8_t>(*src++);
			if (v > 0) {
				if (v > drawWidth) {
					RenderLine<Light, OpaquePrefix, PrefixIncrement>(dst, src, drawWidth, tbl, prefix - (Width - drawWidth));
					src += v;
					dst += drawWidth;
					drawWidth -= v;
					break;
				}
				RenderLine<Light, OpaquePrefix, PrefixIncrement>(dst, src, v, tbl, prefix - (Width - drawWidth));
				src += v;
			} else {
				v = -v;
				if (v > drawWidth) {
					dst += drawWidth;
					drawWidth -= v;
					break;
				}
			}
			dst += v;
			drawWidth -= v;
		}

		// Skip the rest of src line if clipping on the right
		assert(drawWidth <= 0);
		skipRestOfTheLine(clip.right + drawWidth);
		prefix += PrefixIncrement;
	}
}

template <bool OpaquePrefix, int8_t PrefixIncrement>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderTransparentSquareFullDispatchLight(uint8_t lightTableIndex, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src) {
	const uint8_t *tbl = &LightTables[static_cast<size_t>(256U * lightTableIndex)];
	if (lightTableIndex == LightsMax) {
		RenderTransparentSquareFull<LightType::FullyDark, OpaquePrefix, PrefixIncrement>(dst, dstPitch, src, tbl);
	} else if (lightTableIndex == 0) {
		RenderTransparentSquareFull<LightType::FullyLit, OpaquePrefix, PrefixIncrement>(dst, dstPitch, src, tbl);
	} else {
		RenderTransparentSquareFull<LightType::PartiallyLit, OpaquePrefix, PrefixIncrement>(dst, dstPitch, src, tbl);
	}
}

DVL_ATTRIBUTE_HOT void RenderTransparentSquareFullDispatch(MaskType maskType, uint8_t lightTableIndex, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src) {
	switch (maskType) {
	case MaskType::Solid:
		RenderTransparentSquareFullDispatchLight</*OpaquePrefix=*/false, /*PrefixIncrement=*/0>(lightTableIndex, dst, dstPitch, src);
		break;
	case MaskType::Transparent:
		RenderTransparentSquareFullDispatchLight</*OpaquePrefix=*/true, /*PrefixIncrement=*/0>(lightTableIndex, dst, dstPitch, src);
		break;
	case MaskType::Left:
		RenderTransparentSquareFullDispatchLight</*OpaquePrefix=*/false, /*PrefixIncrement=*/2>(lightTableIndex, dst, dstPitch, src);
		break;
	case MaskType::Right:
		RenderTransparentSquareFullDispatchLight</*OpaquePrefix=*/true, /*PrefixIncrement=*/-2>(lightTableIndex, dst, dstPitch, src);
		break;
	case MaskType::LeftFoliage:
		RenderTransparentSquareFullDispatchLight</*OpaquePrefix=*/true, /*PrefixIncrement=*/2>(lightTableIndex, dst, dstPitch, src);
		break;
	case MaskType::RightFoliage:
		RenderTransparentSquareFullDispatchLight</*OpaquePrefix=*/false, /*PrefixIncrement=*/-2>(lightTableIndex, dst, dstPitch, src);
		break;
	}
}

template <bool OpaquePrefix, int8_t PrefixIncrement>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderTransparentSquareClippedDispatchLight(uint8_t lightTableIndex, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, Clip clip) {
	const uint8_t *tbl = &LightTables[static_cast<size_t>(256U * lightTableIndex)];
	if (lightTableIndex == LightsMax) {
		RenderTransparentSquareClipped<LightType::FullyDark, OpaquePrefix, PrefixIncrement>(dst, dstPitch, src, tbl, clip);
	} else if (lightTableIndex == 0) {
		RenderTransparentSquareClipped<LightType::FullyLit, OpaquePrefix, PrefixIncrement>(dst, dstPitch, src, tbl, clip);
	} else {
		RenderTransparentSquareClipped<LightType::PartiallyLit, OpaquePrefix, PrefixIncrement>(dst, dstPitch, src, tbl, clip);
	}
}

DVL_ATTRIBUTE_HOT void RenderTransparentSquareClippedDispatch(MaskType maskType, uint8_t lightTableIndex, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, Clip clip) {
	switch (maskType) {
	case MaskType::Solid:
		RenderTransparentSquareClippedDispatchLight</*OpaquePrefix=*/false, /*PrefixIncrement=*/0>(lightTableIndex, dst, dstPitch, src, clip);
		break;
	case MaskType::Transparent:
		RenderTransparentSquareClippedDispatchLight</*OpaquePrefix=*/true, /*PrefixIncrement=*/0>(lightTableIndex, dst, dstPitch, src, clip);
		break;
	case MaskType::Left:
		RenderTransparentSquareClippedDispatchLight</*OpaquePrefix=*/false, /*PrefixIncrement=*/2>(lightTableIndex, dst, dstPitch, src, clip);
		break;
	case MaskType::Right:
		RenderTransparentSquareClippedDispatchLight</*OpaquePrefix=*/true, /*PrefixIncrement=*/-2>(lightTableIndex, dst, dstPitch, src, clip);
		break;
	case MaskType::LeftFoliage:
		RenderTransparentSquareClippedDispatchLight</*OpaquePrefix=*/true, /*PrefixIncrement=*/2>(lightTableIndex, dst, dstPitch, src, clip);
		break;
	case MaskType::RightFoliage:
		RenderTransparentSquareClippedDispatchLight</*OpaquePrefix=*/false, /*PrefixIncrement=*/-2>(lightTableIndex, dst, dstPitch, src, clip);
		break;
	}
}

/** Vertical clip for the lower and upper triangles of a diamond tile (L/RTRIANGLE).*/
struct DiamondClipY {
	int_fast16_t lowerBottom;
	int_fast16_t lowerTop;
	int_fast16_t upperBottom;
	int_fast16_t upperTop;
};

template <int_fast16_t UpperHeight = TriangleUpperHeight>
DVL_ALWAYS_INLINE DiamondClipY CalculateDiamondClipY(const Clip &clip)
{
	DiamondClipY result;
	if (clip.bottom > LowerHeight) {
		result.lowerBottom = LowerHeight;
		result.upperBottom = clip.bottom - LowerHeight;
		result.lowerTop = result.upperTop = 0;
	} else if (clip.top > UpperHeight) {
		result.upperTop = UpperHeight;
		result.lowerTop = clip.top - UpperHeight;
		result.upperBottom = result.lowerBottom = 0;
	} else {
		result.upperTop = clip.top;
		result.lowerBottom = clip.bottom;
		result.lowerTop = result.upperBottom = 0;
	}
	return result;
}

DVL_ALWAYS_INLINE std::size_t CalculateTriangleSourceSkipLowerBottom(int_fast16_t numLines)
{
	return XStep * numLines * (numLines + 1) / 2 + 2 * ((numLines + 1) / 2);
}

DVL_ALWAYS_INLINE std::size_t CalculateTriangleSourceSkipUpperBottom(int_fast16_t numLines)
{
	return 2 * TriangleUpperHeight * numLines - numLines * (numLines - 1) + 2 * ((numLines + 1) / 2);
}

template <LightType Light, bool Transparent>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderLeftTriangleLower(uint8_t *DVL_RESTRICT &dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT &src, const uint8_t *DVL_RESTRICT tbl)
{
	dst += XStep * (LowerHeight - 1);
	for (auto i = 1; i <= LowerHeight; ++i, dst -= dstPitch + XStep) {
		src += 2 * (i % 2);
		const auto width = XStep * i;
		RenderLineTransparentOrOpaque<Light, Transparent>(dst, src, width, tbl);
		src += width;
	}
}

template <LightType Light, bool Transparent>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderLeftTriangleLowerClipVertical(const DiamondClipY &clipY, uint8_t *DVL_RESTRICT &dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT &src, const uint8_t *DVL_RESTRICT tbl)
{
	src += CalculateTriangleSourceSkipLowerBottom(clipY.lowerBottom);
	dst += XStep * (LowerHeight - clipY.lowerBottom - 1);
	const auto lowerMax = LowerHeight - clipY.lowerTop;
	for (auto i = 1 + clipY.lowerBottom; i <= lowerMax; ++i, dst -= dstPitch + XStep) {
		src += 2 * (i % 2);
		const auto width = XStep * i;
		RenderLineTransparentOrOpaque<Light, Transparent>(dst, src, width, tbl);
		src += width;
	}
}

template <LightType Light, bool Transparent>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderLeftTriangleLowerClipLeftAndVertical(int_fast16_t clipLeft, const DiamondClipY &clipY, uint8_t *DVL_RESTRICT &dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT &src, const uint8_t *DVL_RESTRICT tbl)
{
	src += CalculateTriangleSourceSkipLowerBottom(clipY.lowerBottom);
	dst += XStep * (LowerHeight - clipY.lowerBottom - 1) - clipLeft;
	const auto lowerMax = LowerHeight - clipY.lowerTop;
	for (auto i = 1 + clipY.lowerBottom; i <= lowerMax; ++i, dst -= dstPitch + XStep) {
		src += 2 * (i % 2);
		const auto width = XStep * i;
		const auto startX = Width - XStep * i;
		const auto skip = startX < clipLeft ? clipLeft - startX : 0;
		if (width > skip)
			RenderLineTransparentOrOpaque<Light, Transparent>(dst + skip, src + skip, width - skip, tbl);
		src += width;
	}
}

template <LightType Light, bool Transparent>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderLeftTriangleLowerClipRightAndVertical(int_fast16_t clipRight, const DiamondClipY &clipY, uint8_t *DVL_RESTRICT &dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT &src, const uint8_t *DVL_RESTRICT tbl)
{
	src += CalculateTriangleSourceSkipLowerBottom(clipY.lowerBottom);
	dst += XStep * (LowerHeight - clipY.lowerBottom - 1);
	const auto lowerMax = LowerHeight - clipY.lowerTop;
	for (auto i = 1 + clipY.lowerBottom; i <= lowerMax; ++i, dst -= dstPitch + XStep) {
		src += 2 * (i % 2);
		const auto width = XStep * i;
		if (width > clipRight)
			RenderLineTransparentOrOpaque<Light, Transparent>(dst, src, width - clipRight, tbl);
		src += width;
	}
}

template <LightType Light, bool Transparent>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderLeftTriangleFull(uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, const uint8_t *DVL_RESTRICT tbl)
{
	RenderLeftTriangleLower<Light, Transparent>(dst, dstPitch, src, tbl);
	dst += 2 * XStep;
	for (auto i = 1; i <= TriangleUpperHeight; ++i, dst -= dstPitch - XStep) {
		src += 2 * (i % 2);
		const auto width = Width - XStep * i;
		RenderLineTransparentOrOpaque<Light, Transparent>(dst, src, width, tbl);
		src += width;
	}
}

template <LightType Light, bool Transparent>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderLeftTriangleClipVertical(uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, const uint8_t *DVL_RESTRICT tbl, Clip clip)
{
	const DiamondClipY clipY = CalculateDiamondClipY(clip);
	RenderLeftTriangleLowerClipVertical<Light, Transparent>(clipY, dst, dstPitch, src, tbl);
	src += CalculateTriangleSourceSkipUpperBottom(clipY.upperBottom);
	dst += 2 * XStep + XStep * clipY.upperBottom;
	const auto upperMax = TriangleUpperHeight - clipY.upperTop;
	for (auto i = 1 + clipY.upperBottom; i <= upperMax; ++i, dst -= dstPitch - XStep) {
		src += 2 * (i % 2);
		const auto width = Width - XStep * i;
		RenderLineTransparentOrOpaque<Light, Transparent>(dst, src, width, tbl);
		src += width;
	}
}

template <LightType Light, bool Transparent>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderLeftTriangleClipLeftAndVertical(uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, const uint8_t *DVL_RESTRICT tbl, Clip clip)
{
	const DiamondClipY clipY = CalculateDiamondClipY(clip);
	const int_fast16_t clipLeft = clip.left;
	RenderLeftTriangleLowerClipLeftAndVertical<Light, Transparent>(clipLeft, clipY, dst, dstPitch, src, tbl);
	src += CalculateTriangleSourceSkipUpperBottom(clipY.upperBottom);
	dst += 2 * XStep + XStep * clipY.upperBottom;
	const auto upperMax = TriangleUpperHeight - clipY.upperTop;
	for (auto i = 1 + clipY.upperBottom; i <= upperMax; ++i, dst -= dstPitch - XStep) {
		src += 2 * (i % 2);
		const auto width = Width - XStep * i;
		const auto startX = XStep * i;
		const auto skip = startX < clipLeft ? clipLeft - startX : 0;
		RenderLineTransparentOrOpaque<Light, Transparent>(dst + skip, src + skip, width > skip ? width - skip : 0, tbl);
		src += width;
	}
}

template <LightType Light, bool Transparent>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderLeftTriangleClipRightAndVertical(uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, const uint8_t *DVL_RESTRICT tbl, Clip clip)
{
	const DiamondClipY clipY = CalculateDiamondClipY(clip);
	const int_fast16_t clipRight = clip.right;
	RenderLeftTriangleLowerClipRightAndVertical<Light, Transparent>(clipRight, clipY, dst, dstPitch, src, tbl);
	src += CalculateTriangleSourceSkipUpperBottom(clipY.upperBottom);
	dst += 2 * XStep + XStep * clipY.upperBottom;
	const auto upperMax = TriangleUpperHeight - clipY.upperTop;
	for (auto i = 1 + clipY.upperBottom; i <= upperMax; ++i, dst -= dstPitch - XStep) {
		src += 2 * (i % 2);
		const auto width = Width - XStep * i;
		if (width <= clipRight)
			break;
		RenderLineTransparentOrOpaque<Light, Transparent>(dst, src, width - clipRight, tbl);
		src += width;
	}
}

template <bool Transparent>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderLeftTriangleFullDispatchLight(uint8_t lightTableIndex, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src) {
	const uint8_t *tbl = &LightTables[static_cast<size_t>(256U * lightTableIndex)];
	if (lightTableIndex == LightsMax) {
		RenderLeftTriangleFull<LightType::FullyDark, Transparent>(dst, dstPitch, src, tbl);
	} else if (lightTableIndex == 0) {
		RenderLeftTriangleFull<LightType::FullyLit, Transparent>(dst, dstPitch, src, tbl);
	} else {
		RenderLeftTriangleFull<LightType::PartiallyLit, Transparent>(dst, dstPitch, src, tbl);
	}
}

DVL_ATTRIBUTE_HOT void RenderLeftTriangleFullDispatch(MaskType maskType, uint8_t lightTableIndex, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src) {
	switch (maskType) {
	case MaskType::Solid:
		RenderLeftTriangleFullDispatchLight</*Transparent=*/false>(lightTableIndex, dst, dstPitch, src);
		break;
	case MaskType::Transparent:
		RenderLeftTriangleFullDispatchLight</*Transparent=*/true>(lightTableIndex, dst, dstPitch, src);
		break;
	default:
		app_fatal("Invalid mask type");
	}
}

template <bool Transparent>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderLeftTriangleClipVerticalDispatchLight(uint8_t lightTableIndex, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, Clip clip) {
	const uint8_t *tbl = &LightTables[static_cast<size_t>(256U * lightTableIndex)];
	if (lightTableIndex == LightsMax) {
		RenderLeftTriangleClipVertical<LightType::FullyDark, Transparent>(dst, dstPitch, src, tbl, clip);
	} else if (lightTableIndex == 0) {
		RenderLeftTriangleClipVertical<LightType::FullyLit, Transparent>(dst, dstPitch, src, tbl, clip);
	} else {
		RenderLeftTriangleClipVertical<LightType::PartiallyLit, Transparent>(dst, dstPitch, src, tbl, clip);
	}
}

DVL_ATTRIBUTE_HOT void RenderLeftTriangleClipVerticalDispatch(MaskType maskType, uint8_t lightTableIndex, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, Clip clip) {
	switch (maskType) {
	case MaskType::Solid:
		RenderLeftTriangleClipVerticalDispatchLight</*Transparent=*/false>(lightTableIndex, dst, dstPitch, src, clip);
		break;
	case MaskType::Transparent:
		RenderLeftTriangleClipVerticalDispatchLight</*Transparent=*/true>(lightTableIndex, dst, dstPitch, src, clip);
		break;
	default:
		app_fatal("Invalid mask type");
	}
}

template <bool Transparent>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderLeftTriangleClipLeftAndVerticalDispatchLight(uint8_t lightTableIndex, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, Clip clip) {
	const uint8_t *tbl = &LightTables[static_cast<size_t>(256U * lightTableIndex)];
	if (lightTableIndex == LightsMax) {
		RenderLeftTriangleClipLeftAndVertical<LightType::FullyDark, Transparent>(dst, dstPitch, src, tbl, clip);
	} else if (lightTableIndex == 0) {
		RenderLeftTriangleClipLeftAndVertical<LightType::FullyLit, Transparent>(dst, dstPitch, src, tbl, clip);
	} else {
		RenderLeftTriangleClipLeftAndVertical<LightType::PartiallyLit, Transparent>(dst, dstPitch, src, tbl, clip);
	}
}

DVL_ATTRIBUTE_HOT void RenderLeftTriangleClipLeftAndVerticalDispatch(MaskType maskType, uint8_t lightTableIndex, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, Clip clip) {
	switch (maskType) {
	case MaskType::Solid:
		RenderLeftTriangleClipLeftAndVerticalDispatchLight</*Transparent=*/false>(lightTableIndex, dst, dstPitch, src, clip);
		break;
	case MaskType::Transparent:
		RenderLeftTriangleClipLeftAndVerticalDispatchLight</*Transparent=*/true>(lightTableIndex, dst, dstPitch, src, clip);
		break;
	default:
		app_fatal("Invalid mask type");
	}
}

template <bool Transparent>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderLeftTriangleClipRightAndVerticalDispatchLight(uint8_t lightTableIndex, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, Clip clip) {
	const uint8_t *tbl = &LightTables[static_cast<size_t>(256U * lightTableIndex)];
	if (lightTableIndex == LightsMax) {
		RenderLeftTriangleClipRightAndVertical<LightType::FullyDark, Transparent>(dst, dstPitch, src, tbl, clip);
	} else if (lightTableIndex == 0) {
		RenderLeftTriangleClipRightAndVertical<LightType::FullyLit, Transparent>(dst, dstPitch, src, tbl, clip);
	} else {
		RenderLeftTriangleClipRightAndVertical<LightType::PartiallyLit, Transparent>(dst, dstPitch, src, tbl, clip);
	}
}

DVL_ATTRIBUTE_HOT void RenderLeftTriangleClipRightAndVerticalDispatch(MaskType maskType, uint8_t lightTableIndex, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, Clip clip) {
	switch (maskType) {
	case MaskType::Solid:
		RenderLeftTriangleClipRightAndVerticalDispatchLight</*Transparent=*/false>(lightTableIndex, dst, dstPitch, src, clip);
		break;
	case MaskType::Transparent:
		RenderLeftTriangleClipRightAndVerticalDispatchLight</*Transparent=*/true>(lightTableIndex, dst, dstPitch, src, clip);
		break;
	default:
		app_fatal("Invalid mask type");
	}
}

template <LightType Light, bool Transparent>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderRightTriangleLower(uint8_t *DVL_RESTRICT &dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT &src, const uint8_t *DVL_RESTRICT tbl)
{
	for (auto i = 1; i <= LowerHeight; ++i, dst -= dstPitch) {
		const auto width = XStep * i;
		RenderLineTransparentOrOpaque<Light, Transparent>(dst, src, width, tbl);
		src += width + 2 * (i % 2);
	}
}

template <LightType Light, bool Transparent>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderRightTriangleLowerClipVertical(const DiamondClipY &clipY, uint8_t *DVL_RESTRICT &dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT &src, const uint8_t *DVL_RESTRICT tbl)
{
	src += CalculateTriangleSourceSkipLowerBottom(clipY.lowerBottom);
	const auto lowerMax = LowerHeight - clipY.lowerTop;
	for (auto i = 1 + clipY.lowerBottom; i <= lowerMax; ++i, dst -= dstPitch) {
		const auto width = XStep * i;
		RenderLineTransparentOrOpaque<Light, Transparent>(dst, src, width, tbl);
		src += width + 2 * (i % 2);
	}
}

template <LightType Light, bool Transparent>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderRightTriangleLowerClipLeftAndVertical(int_fast16_t clipLeft, const DiamondClipY &clipY, uint8_t *DVL_RESTRICT &dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT &src, const uint8_t *DVL_RESTRICT tbl)
{
	src += CalculateTriangleSourceSkipLowerBottom(clipY.lowerBottom);
	const auto lowerMax = LowerHeight - clipY.lowerTop;
	for (auto i = 1 + clipY.lowerBottom; i <= lowerMax; ++i, dst -= dstPitch) {
		const auto width = XStep * i;
		if (width > clipLeft)
			RenderLineTransparentOrOpaque<Light, Transparent>(dst, src + clipLeft, width - clipLeft, tbl);
		src += width + 2 * (i % 2);
	}
}

template <LightType Light, bool Transparent>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderRightTriangleLowerClipRightAndVertical(int_fast16_t clipRight, const DiamondClipY &clipY, uint8_t *DVL_RESTRICT &dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT &src, const uint8_t *DVL_RESTRICT tbl)
{
	src += CalculateTriangleSourceSkipLowerBottom(clipY.lowerBottom);
	const auto lowerMax = LowerHeight - clipY.lowerTop;
	for (auto i = 1 + clipY.lowerBottom; i <= lowerMax; ++i, dst -= dstPitch) {
		const auto width = XStep * i;
		const auto skip = Width - width < clipRight ? clipRight - (Width - width) : 0;
		if (width > skip)
			RenderLineTransparentOrOpaque<Light, Transparent>(dst, src, width - skip, tbl);
		src += width + 2 * (i % 2);
	}
}

template <LightType Light, bool Transparent>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderRightTriangleFull(uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, const uint8_t *DVL_RESTRICT tbl)
{
	RenderRightTriangleLower<Light, Transparent>(dst, dstPitch, src, tbl);
	for (auto i = 1; i <= TriangleUpperHeight; ++i, dst -= dstPitch) {
		const auto width = Width - XStep * i;
		RenderLineTransparentOrOpaque<Light, Transparent>(dst, src, width, tbl);
		src += width + 2 * (i % 2);
	}
}

template <LightType Light, bool Transparent>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderRightTriangleClipVertical(uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, const uint8_t *DVL_RESTRICT tbl, Clip clip)
{
	const DiamondClipY clipY = CalculateDiamondClipY(clip);
	RenderRightTriangleLowerClipVertical<Light, Transparent>(clipY, dst, dstPitch, src, tbl);
	src += CalculateTriangleSourceSkipUpperBottom(clipY.upperBottom);
	const auto upperMax = TriangleUpperHeight - clipY.upperTop;
	for (auto i = 1 + clipY.upperBottom; i <= upperMax; ++i, dst -= dstPitch) {
		const auto width = Width - XStep * i;
		RenderLineTransparentOrOpaque<Light, Transparent>(dst, src, width, tbl);
		src += width + 2 * (i % 2);
	}
}

template <LightType Light, bool Transparent>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderRightTriangleClipLeftAndVertical(uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, const uint8_t *DVL_RESTRICT tbl, Clip clip)
{
	const DiamondClipY clipY = CalculateDiamondClipY(clip);
	const int_fast16_t clipLeft = clip.left;
	RenderRightTriangleLowerClipLeftAndVertical<Light, Transparent>(clipLeft, clipY, dst, dstPitch, src, tbl);
	src += CalculateTriangleSourceSkipUpperBottom(clipY.upperBottom);
	const auto upperMax = TriangleUpperHeight - clipY.upperTop;
	for (auto i = 1 + clipY.upperBottom; i <= upperMax; ++i, dst -= dstPitch) {
		const auto width = Width - XStep * i;
		if (width <= clipLeft)
			break;
		RenderLineTransparentOrOpaque<Light, Transparent>(dst, src + clipLeft, width - clipLeft, tbl);
		src += width + 2 * (i % 2);
	}
}

template <LightType Light, bool Transparent>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderRightTriangleClipRightAndVertical(uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, const uint8_t *DVL_RESTRICT tbl, Clip clip)
{
	const DiamondClipY clipY = CalculateDiamondClipY(clip);
	const int_fast16_t clipRight = clip.right;
	RenderRightTriangleLowerClipRightAndVertical<Light, Transparent>(clipRight, clipY, dst, dstPitch, src, tbl);
	src += CalculateTriangleSourceSkipUpperBottom(clipY.upperBottom);
	const auto upperMax = TriangleUpperHeight - clipY.upperTop;
	for (auto i = 1 + clipY.upperBottom; i <= upperMax; ++i, dst -= dstPitch) {
		const auto width = Width - XStep * i;
		const auto skip = Width - width < clipRight ? clipRight - (Width - width) : 0;
		RenderLineTransparentOrOpaque<Light, Transparent>(dst, src, width > skip ? width - skip : 0, tbl);
		src += width + 2 * (i % 2);
	}
}

template <bool Transparent>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderRightTriangleFullDispatchLight(uint8_t lightTableIndex, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src) {
	const uint8_t *tbl = &LightTables[static_cast<size_t>(256U * lightTableIndex)];
	if (lightTableIndex == LightsMax) {
		RenderRightTriangleFull<LightType::FullyDark, Transparent>(dst, dstPitch, src, tbl);
	} else if (lightTableIndex == 0) {
		RenderRightTriangleFull<LightType::FullyLit, Transparent>(dst, dstPitch, src, tbl);
	} else {
		RenderRightTriangleFull<LightType::PartiallyLit, Transparent>(dst, dstPitch, src, tbl);
	}
}

DVL_ATTRIBUTE_HOT void RenderRightTriangleFullDispatch(MaskType maskType, uint8_t lightTableIndex, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src) {
	switch (maskType) {
	case MaskType::Solid:
		RenderRightTriangleFullDispatchLight</*Transparent=*/false>(lightTableIndex, dst, dstPitch, src);
		break;
	case MaskType::Transparent:
		RenderRightTriangleFullDispatchLight</*Transparent=*/true>(lightTableIndex, dst, dstPitch, src);
		break;
	default:
		app_fatal("Invalid mask type");
	}
}

template <bool Transparent>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderRightTriangleClipVerticalDispatchLight(uint8_t lightTableIndex, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, Clip clip) {
	const uint8_t *tbl = &LightTables[static_cast<size_t>(256U * lightTableIndex)];
	if (lightTableIndex == LightsMax) {
		RenderRightTriangleClipVertical<LightType::FullyDark, Transparent>(dst, dstPitch, src, tbl, clip);
	} else if (lightTableIndex == 0) {
		RenderRightTriangleClipVertical<LightType::FullyLit, Transparent>(dst, dstPitch, src, tbl, clip);
	} else {
		RenderRightTriangleClipVertical<LightType::PartiallyLit, Transparent>(dst, dstPitch, src, tbl, clip);
	}
}

DVL_ATTRIBUTE_HOT void RenderRightTriangleClipVerticalDispatch(MaskType maskType, uint8_t lightTableIndex, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, Clip clip) {
	switch (maskType) {
	case MaskType::Solid:
		RenderRightTriangleClipVerticalDispatchLight</*Transparent=*/false>(lightTableIndex, dst, dstPitch, src, clip);
		break;
	case MaskType::Transparent:
		RenderRightTriangleClipVerticalDispatchLight</*Transparent=*/true>(lightTableIndex, dst, dstPitch, src, clip);
		break;
	default:
		app_fatal("Invalid mask type");
	}
}

template <bool Transparent>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderRightTriangleClipLeftAndVerticalDispatchLight(uint8_t lightTableIndex, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, Clip clip) {
	const uint8_t *tbl = &LightTables[static_cast<size_t>(256U * lightTableIndex)];
	if (lightTableIndex == LightsMax) {
		RenderRightTriangleClipLeftAndVertical<LightType::FullyDark, Transparent>(dst, dstPitch, src, tbl, clip);
	} else if (lightTableIndex == 0) {
		RenderRightTriangleClipLeftAndVertical<LightType::FullyLit, Transparent>(dst, dstPitch, src, tbl, clip);
	} else {
		RenderRightTriangleClipLeftAndVertical<LightType::PartiallyLit, Transparent>(dst, dstPitch, src, tbl, clip);
	}
}

DVL_ATTRIBUTE_HOT void RenderRightTriangleClipLeftAndVerticalDispatch(MaskType maskType, uint8_t lightTableIndex, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, Clip clip) {
	switch (maskType) {
	case MaskType::Solid:
		RenderRightTriangleClipLeftAndVerticalDispatchLight</*Transparent=*/false>(lightTableIndex, dst, dstPitch, src, clip);
		break;
	case MaskType::Transparent:
		RenderRightTriangleClipLeftAndVerticalDispatchLight</*Transparent=*/true>(lightTableIndex, dst, dstPitch, src, clip);
		break;
	default:
		app_fatal("Invalid mask type");
	}
}

template <bool Transparent>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderRightTriangleClipRightAndVerticalDispatchLight(uint8_t lightTableIndex, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, Clip clip) {
	const uint8_t *tbl = &LightTables[static_cast<size_t>(256U * lightTableIndex)];
	if (lightTableIndex == LightsMax) {
		RenderRightTriangleClipRightAndVertical<LightType::FullyDark, Transparent>(dst, dstPitch, src, tbl, clip);
	} else if (lightTableIndex == 0) {
		RenderRightTriangleClipRightAndVertical<LightType::FullyLit, Transparent>(dst, dstPitch, src, tbl, clip);
	} else {
		RenderRightTriangleClipRightAndVertical<LightType::PartiallyLit, Transparent>(dst, dstPitch, src, tbl, clip);
	}
}

DVL_ATTRIBUTE_HOT void RenderRightTriangleClipRightAndVerticalDispatch(MaskType maskType, uint8_t lightTableIndex, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, Clip clip) {
	switch (maskType) {
	case MaskType::Solid:
		RenderRightTriangleClipRightAndVerticalDispatchLight</*Transparent=*/false>(lightTableIndex, dst, dstPitch, src, clip);
		break;
	case MaskType::Transparent:
		RenderRightTriangleClipRightAndVerticalDispatchLight</*Transparent=*/true>(lightTableIndex, dst, dstPitch, src, clip);
		break;
	default:
		app_fatal("Invalid mask type");
	}
}

template <LightType Light, bool OpaquePrefix, int8_t PrefixIncrement>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderTrapezoidUpperHalf(uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, const uint8_t *DVL_RESTRICT tbl) {
	uint_fast8_t prefixWidth = PrefixIncrement < 0 ? 32 : 0;
	for (auto i = 0; i < TrapezoidUpperHeight; ++i, dst -= dstPitch) {
		RenderLineTransparentAndOpaque<Light, OpaquePrefix, PrefixIncrement>(dst, src, prefixWidth, Width, tbl);
		if (PrefixIncrement != 0)
			prefixWidth += PrefixIncrement;
		src += Width;
	}
}

template <LightType Light, bool OpaquePrefix, int8_t PrefixIncrement>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderTrapezoidUpperHalfClipVertical(const Clip &clip, const DiamondClipY &clipY, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, const uint8_t *DVL_RESTRICT tbl) {
	const auto upperMax = TrapezoidUpperHeight - clipY.upperTop;
	int8_t prefix = InitPrefix<PrefixIncrement>(clip.bottom);
	for (auto i = 1 + clipY.upperBottom; i <= upperMax; ++i, dst -= dstPitch) {
		RenderLine<Light, OpaquePrefix, PrefixIncrement>(dst, src, Width, tbl, prefix);
		src += Width;
		prefix += PrefixIncrement;
	}
}

template <LightType Light, bool OpaquePrefix, int8_t PrefixIncrement>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderTrapezoidUpperHalfClipLeftAndVertical(const Clip &clip, const DiamondClipY &clipY, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, const uint8_t *DVL_RESTRICT tbl) {
	const auto upperMax = TrapezoidUpperHeight - clipY.upperTop;
	int8_t prefix = InitPrefix<PrefixIncrement>(clip.bottom);
	for (auto i = 1 + clipY.upperBottom; i <= upperMax; ++i, dst -= dstPitch) {
		RenderLine<Light, OpaquePrefix, PrefixIncrement>(dst, src, clip.width, tbl, prefix - clip.left);
		src += Width;
		prefix += PrefixIncrement;
	}
}

template <LightType Light, bool OpaquePrefix, int8_t PrefixIncrement>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderTrapezoidUpperHalfClipRightAndVertical(const Clip &clip, const DiamondClipY &clipY, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, const uint8_t *DVL_RESTRICT tbl) {
	const auto upperMax = TrapezoidUpperHeight - clipY.upperTop;
	int8_t prefix = InitPrefix<PrefixIncrement>(clip.bottom);
	for (auto i = 1 + clipY.upperBottom; i <= upperMax; ++i, dst -= dstPitch) {
		RenderLine<Light, OpaquePrefix, PrefixIncrement>(dst, src, clip.width, tbl, prefix);
		src += Width;
		prefix += PrefixIncrement;
	}
}

template <LightType Light, bool OpaquePrefix, int8_t PrefixIncrement>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderLeftTrapezoidFull(uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, const uint8_t *DVL_RESTRICT tbl)
{
	RenderLeftTriangleLower<Light, LowerHalfTransparent<OpaquePrefix, PrefixIncrement>>(dst, dstPitch, src, tbl);
	dst += XStep;
	RenderTrapezoidUpperHalf<Light, OpaquePrefix, PrefixIncrement>(dst, dstPitch, src, tbl);
}

template <LightType Light, bool OpaquePrefix, int8_t PrefixIncrement>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderLeftTrapezoidClipVertical(uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, const uint8_t *DVL_RESTRICT tbl, Clip clip)
{
	const DiamondClipY clipY = CalculateDiamondClipY<TrapezoidUpperHeight>(clip);
	RenderLeftTriangleLowerClipVertical<Light, LowerHalfTransparent<OpaquePrefix, PrefixIncrement>>(clipY, dst, dstPitch, src, tbl);
	src += clipY.upperBottom * Width;
	dst += XStep;
	RenderTrapezoidUpperHalfClipVertical<Light, OpaquePrefix, PrefixIncrement>(clip, clipY, dst, dstPitch, src, tbl);
}

template <LightType Light, bool OpaquePrefix, int8_t PrefixIncrement>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderLeftTrapezoidClipLeftAndVertical(uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, const uint8_t *DVL_RESTRICT tbl, Clip clip)
{
	const DiamondClipY clipY = CalculateDiamondClipY<TrapezoidUpperHeight>(clip);
	RenderLeftTriangleLowerClipLeftAndVertical<Light, LowerHalfTransparent<OpaquePrefix, PrefixIncrement>>(clip.left, clipY, dst, dstPitch, src, tbl);
	src += clipY.upperBottom * Width + clip.left;
	dst += XStep + clip.left;
	RenderTrapezoidUpperHalfClipLeftAndVertical<Light, OpaquePrefix, PrefixIncrement>(clip, clipY, dst, dstPitch, src, tbl);
}

template <LightType Light, bool OpaquePrefix, int8_t PrefixIncrement>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderLeftTrapezoidClipRightAndVertical(uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, const uint8_t *DVL_RESTRICT tbl, Clip clip)
{
	const DiamondClipY clipY = CalculateDiamondClipY<TrapezoidUpperHeight>(clip);
	RenderLeftTriangleLowerClipRightAndVertical<Light, LowerHalfTransparent<OpaquePrefix, PrefixIncrement>>(clip.right, clipY, dst, dstPitch, src, tbl);
	src += clipY.upperBottom * Width;
	dst += XStep;
	RenderTrapezoidUpperHalfClipRightAndVertical<Light, OpaquePrefix, PrefixIncrement>(clip, clipY, dst, dstPitch, src, tbl);
}

template <bool OpaquePrefix, int8_t PrefixIncrement>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderLeftTrapezoidFullDispatchLight(uint8_t lightTableIndex, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src) {
	const uint8_t *tbl = &LightTables[static_cast<size_t>(256U * lightTableIndex)];
	if (lightTableIndex == LightsMax) {
		RenderLeftTrapezoidFull<LightType::FullyDark, OpaquePrefix, PrefixIncrement>(dst, dstPitch, src, tbl);
	} else if (lightTableIndex == 0) {
		RenderLeftTrapezoidFull<LightType::FullyLit, OpaquePrefix, PrefixIncrement>(dst, dstPitch, src, tbl);
	} else {
		RenderLeftTrapezoidFull<LightType::PartiallyLit, OpaquePrefix, PrefixIncrement>(dst, dstPitch, src, tbl);
	}
}

DVL_ATTRIBUTE_HOT void RenderLeftTrapezoidFullDispatch(MaskType maskType, uint8_t lightTableIndex, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src) {
	switch (maskType) {
	case MaskType::Solid:
		RenderLeftTrapezoidFullDispatchLight</*OpaquePrefix=*/false, /*PrefixIncrement=*/0>(lightTableIndex, dst, dstPitch, src);
		break;
	case MaskType::Transparent:
		RenderLeftTrapezoidFullDispatchLight</*OpaquePrefix=*/true, /*PrefixIncrement=*/0>(lightTableIndex, dst, dstPitch, src);
		break;
	case MaskType::Left:
		RenderLeftTrapezoidFullDispatchLight</*OpaquePrefix=*/false, /*PrefixIncrement=*/2>(lightTableIndex, dst, dstPitch, src);
		break;
	default:
		app_fatal("Invalid mask type");
	}
}

template <bool OpaquePrefix, int8_t PrefixIncrement>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderLeftTrapezoidClipVerticalDispatchLight(uint8_t lightTableIndex, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, Clip clip) {
	const uint8_t *tbl = &LightTables[static_cast<size_t>(256U * lightTableIndex)];
	if (lightTableIndex == LightsMax) {
		RenderLeftTrapezoidClipVertical<LightType::FullyDark, OpaquePrefix, PrefixIncrement>(dst, dstPitch, src, tbl, clip);
	} else if (lightTableIndex == 0) {
		RenderLeftTrapezoidClipVertical<LightType::FullyLit, OpaquePrefix, PrefixIncrement>(dst, dstPitch, src, tbl, clip);
	} else {
		RenderLeftTrapezoidClipVertical<LightType::PartiallyLit, OpaquePrefix, PrefixIncrement>(dst, dstPitch, src, tbl, clip);
	}
}

DVL_ATTRIBUTE_HOT void RenderLeftTrapezoidClipVerticalDispatch(MaskType maskType, uint8_t lightTableIndex, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, Clip clip) {
	switch (maskType) {
	case MaskType::Solid:
		RenderLeftTrapezoidClipVerticalDispatchLight</*OpaquePrefix=*/false, /*PrefixIncrement=*/0>(lightTableIndex, dst, dstPitch, src, clip);
		break;
	case MaskType::Transparent:
		RenderLeftTrapezoidClipVerticalDispatchLight</*OpaquePrefix=*/true, /*PrefixIncrement=*/0>(lightTableIndex, dst, dstPitch, src, clip);
		break;
	case MaskType::Left:
		RenderLeftTrapezoidClipVerticalDispatchLight</*OpaquePrefix=*/false, /*PrefixIncrement=*/2>(lightTableIndex, dst, dstPitch, src, clip);
		break;
	default:
		app_fatal("Invalid mask type");
	}
}

template <bool OpaquePrefix, int8_t PrefixIncrement>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderLeftTrapezoidClipLeftAndVerticalDispatchLight(uint8_t lightTableIndex, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, Clip clip) {
	const uint8_t *tbl = &LightTables[static_cast<size_t>(256U * lightTableIndex)];
	if (lightTableIndex == LightsMax) {
		RenderLeftTrapezoidClipLeftAndVertical<LightType::FullyDark, OpaquePrefix, PrefixIncrement>(dst, dstPitch, src, tbl, clip);
	} else if (lightTableIndex == 0) {
		RenderLeftTrapezoidClipLeftAndVertical<LightType::FullyLit, OpaquePrefix, PrefixIncrement>(dst, dstPitch, src, tbl, clip);
	} else {
		RenderLeftTrapezoidClipLeftAndVertical<LightType::PartiallyLit, OpaquePrefix, PrefixIncrement>(dst, dstPitch, src, tbl, clip);
	}
}

DVL_ATTRIBUTE_HOT void RenderLeftTrapezoidClipLeftAndVerticalDispatch(MaskType maskType, uint8_t lightTableIndex, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, Clip clip) {
	switch (maskType) {
	case MaskType::Solid:
		RenderLeftTrapezoidClipLeftAndVerticalDispatchLight</*OpaquePrefix=*/false, /*PrefixIncrement=*/0>(lightTableIndex, dst, dstPitch, src, clip);
		break;
	case MaskType::Transparent:
		RenderLeftTrapezoidClipLeftAndVerticalDispatchLight</*OpaquePrefix=*/true, /*PrefixIncrement=*/0>(lightTableIndex, dst, dstPitch, src, clip);
		break;
	case MaskType::Left:
		RenderLeftTrapezoidClipLeftAndVerticalDispatchLight</*OpaquePrefix=*/false, /*PrefixIncrement=*/2>(lightTableIndex, dst, dstPitch, src, clip);
		break;
	default:
		app_fatal("Invalid mask type");
	}
}

template <bool OpaquePrefix, int8_t PrefixIncrement>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderLeftTrapezoidClipRightAndVerticalDispatchLight(uint8_t lightTableIndex, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, Clip clip) {
	const uint8_t *tbl = &LightTables[static_cast<size_t>(256U * lightTableIndex)];
	if (lightTableIndex == LightsMax) {
		RenderLeftTrapezoidClipRightAndVertical<LightType::FullyDark, OpaquePrefix, PrefixIncrement>(dst, dstPitch, src, tbl, clip);
	} else if (lightTableIndex == 0) {
		RenderLeftTrapezoidClipRightAndVertical<LightType::FullyLit, OpaquePrefix, PrefixIncrement>(dst, dstPitch, src, tbl, clip);
	} else {
		RenderLeftTrapezoidClipRightAndVertical<LightType::PartiallyLit, OpaquePrefix, PrefixIncrement>(dst, dstPitch, src, tbl, clip);
	}
}

DVL_ATTRIBUTE_HOT void RenderLeftTrapezoidClipRightAndVerticalDispatch(MaskType maskType, uint8_t lightTableIndex, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, Clip clip) {
	switch (maskType) {
	case MaskType::Solid:
		RenderLeftTrapezoidClipRightAndVerticalDispatchLight</*OpaquePrefix=*/false, /*PrefixIncrement=*/0>(lightTableIndex, dst, dstPitch, src, clip);
		break;
	case MaskType::Transparent:
		RenderLeftTrapezoidClipRightAndVerticalDispatchLight</*OpaquePrefix=*/true, /*PrefixIncrement=*/0>(lightTableIndex, dst, dstPitch, src, clip);
		break;
	case MaskType::Left:
		RenderLeftTrapezoidClipRightAndVerticalDispatchLight</*OpaquePrefix=*/false, /*PrefixIncrement=*/2>(lightTableIndex, dst, dstPitch, src, clip);
		break;
	default:
		app_fatal("Invalid mask type");
	}
}

template <LightType Light, bool OpaquePrefix, int8_t PrefixIncrement>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderRightTrapezoidFull(uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, const uint8_t *DVL_RESTRICT tbl)
{
	RenderRightTriangleLower<Light, LowerHalfTransparent<OpaquePrefix, PrefixIncrement>>(dst, dstPitch, src, tbl);
	RenderTrapezoidUpperHalf<Light, OpaquePrefix, PrefixIncrement>(dst, dstPitch, src, tbl);
}

template <LightType Light, bool OpaquePrefix, int8_t PrefixIncrement>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderRightTrapezoidClipVertical(uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, const uint8_t *DVL_RESTRICT tbl, Clip clip)
{
	const DiamondClipY clipY = CalculateDiamondClipY<TrapezoidUpperHeight>(clip);
	RenderRightTriangleLowerClipVertical<Light, LowerHalfTransparent<OpaquePrefix, PrefixIncrement>>(clipY, dst, dstPitch, src, tbl);
	src += clipY.upperBottom * Width;
	RenderTrapezoidUpperHalfClipVertical<Light, OpaquePrefix, PrefixIncrement>(clip, clipY, dst, dstPitch, src, tbl);
}

template <LightType Light, bool OpaquePrefix, int8_t PrefixIncrement>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderRightTrapezoidClipLeftAndVertical(uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, const uint8_t *DVL_RESTRICT tbl, Clip clip)
{
	const DiamondClipY clipY = CalculateDiamondClipY<TrapezoidUpperHeight>(clip);
	RenderRightTriangleLowerClipLeftAndVertical<Light, LowerHalfTransparent<OpaquePrefix, PrefixIncrement>>(clip.left, clipY, dst, dstPitch, src, tbl);
	src += clipY.upperBottom * Width + clip.left;
	RenderTrapezoidUpperHalfClipLeftAndVertical<Light, OpaquePrefix, PrefixIncrement>(clip, clipY, dst, dstPitch, src, tbl);
}

template <LightType Light, bool OpaquePrefix, int8_t PrefixIncrement>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderRightTrapezoidClipRightAndVertical(uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, const uint8_t *DVL_RESTRICT tbl, Clip clip)
{
	const DiamondClipY clipY = CalculateDiamondClipY<TrapezoidUpperHeight>(clip);
	RenderRightTriangleLowerClipRightAndVertical<Light, LowerHalfTransparent<OpaquePrefix, PrefixIncrement>>(clip.right, clipY, dst, dstPitch, src, tbl);
	src += clipY.upperBottom * Width;
	RenderTrapezoidUpperHalfClipRightAndVertical<Light, OpaquePrefix, PrefixIncrement>(clip, clipY, dst, dstPitch, src, tbl);
}

template <bool OpaquePrefix, int8_t PrefixIncrement>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderRightTrapezoidFullDispatchLight(uint8_t lightTableIndex, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src) {
	const uint8_t *tbl = &LightTables[static_cast<size_t>(256U * lightTableIndex)];
	if (lightTableIndex == LightsMax) {
		RenderRightTrapezoidFull<LightType::FullyDark, OpaquePrefix, PrefixIncrement>(dst, dstPitch, src, tbl);
	} else if (lightTableIndex == 0) {
		RenderRightTrapezoidFull<LightType::FullyLit, OpaquePrefix, PrefixIncrement>(dst, dstPitch, src, tbl);
	} else {
		RenderRightTrapezoidFull<LightType::PartiallyLit, OpaquePrefix, PrefixIncrement>(dst, dstPitch, src, tbl);
	}
}

DVL_ATTRIBUTE_HOT void RenderRightTrapezoidFullDispatch(MaskType maskType, uint8_t lightTableIndex, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src) {
	switch (maskType) {
	case MaskType::Solid:
		RenderRightTrapezoidFullDispatchLight</*OpaquePrefix=*/false, /*PrefixIncrement=*/0>(lightTableIndex, dst, dstPitch, src);
		break;
	case MaskType::Transparent:
		RenderRightTrapezoidFullDispatchLight</*OpaquePrefix=*/true, /*PrefixIncrement=*/0>(lightTableIndex, dst, dstPitch, src);
		break;
	case MaskType::Right:
		RenderRightTrapezoidFullDispatchLight</*OpaquePrefix=*/true, /*PrefixIncrement=*/-2>(lightTableIndex, dst, dstPitch, src);
		break;
	default:
		app_fatal("Invalid mask type");
	}
}

template <bool OpaquePrefix, int8_t PrefixIncrement>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderRightTrapezoidClipVerticalDispatchLight(uint8_t lightTableIndex, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, Clip clip) {
	const uint8_t *tbl = &LightTables[static_cast<size_t>(256U * lightTableIndex)];
	if (lightTableIndex == LightsMax) {
		RenderRightTrapezoidClipVertical<LightType::FullyDark, OpaquePrefix, PrefixIncrement>(dst, dstPitch, src, tbl, clip);
	} else if (lightTableIndex == 0) {
		RenderRightTrapezoidClipVertical<LightType::FullyLit, OpaquePrefix, PrefixIncrement>(dst, dstPitch, src, tbl, clip);
	} else {
		RenderRightTrapezoidClipVertical<LightType::PartiallyLit, OpaquePrefix, PrefixIncrement>(dst, dstPitch, src, tbl, clip);
	}
}

DVL_ATTRIBUTE_HOT void RenderRightTrapezoidClipVerticalDispatch(MaskType maskType, uint8_t lightTableIndex, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, Clip clip) {
	switch (maskType) {
	case MaskType::Solid:
		RenderRightTrapezoidClipVerticalDispatchLight</*OpaquePrefix=*/false, /*PrefixIncrement=*/0>(lightTableIndex, dst, dstPitch, src, clip);
		break;
	case MaskType::Transparent:
		RenderRightTrapezoidClipVerticalDispatchLight</*OpaquePrefix=*/true, /*PrefixIncrement=*/0>(lightTableIndex, dst, dstPitch, src, clip);
		break;
	case MaskType::Right:
		RenderRightTrapezoidClipVerticalDispatchLight</*OpaquePrefix=*/true, /*PrefixIncrement=*/-2>(lightTableIndex, dst, dstPitch, src, clip);
		break;
	default:
		app_fatal("Invalid mask type");
	}
}

template <bool OpaquePrefix, int8_t PrefixIncrement>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderRightTrapezoidClipLeftAndVerticalDispatchLight(uint8_t lightTableIndex, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, Clip clip) {
	const uint8_t *tbl = &LightTables[static_cast<size_t>(256U * lightTableIndex)];
	if (lightTableIndex == LightsMax) {
		RenderRightTrapezoidClipLeftAndVertical<LightType::FullyDark, OpaquePrefix, PrefixIncrement>(dst, dstPitch, src, tbl, clip);
	} else if (lightTableIndex == 0) {
		RenderRightTrapezoidClipLeftAndVertical<LightType::FullyLit, OpaquePrefix, PrefixIncrement>(dst, dstPitch, src, tbl, clip);
	} else {
		RenderRightTrapezoidClipLeftAndVertical<LightType::PartiallyLit, OpaquePrefix, PrefixIncrement>(dst, dstPitch, src, tbl, clip);
	}
}

DVL_ATTRIBUTE_HOT void RenderRightTrapezoidClipLeftAndVerticalDispatch(MaskType maskType, uint8_t lightTableIndex, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, Clip clip) {
	switch (maskType) {
	case MaskType::Solid:
		RenderRightTrapezoidClipLeftAndVerticalDispatchLight</*OpaquePrefix=*/false, /*PrefixIncrement=*/0>(lightTableIndex, dst, dstPitch, src, clip);
		break;
	case MaskType::Transparent:
		RenderRightTrapezoidClipLeftAndVerticalDispatchLight</*OpaquePrefix=*/true, /*PrefixIncrement=*/0>(lightTableIndex, dst, dstPitch, src, clip);
		break;
	case MaskType::Right:
		RenderRightTrapezoidClipLeftAndVerticalDispatchLight</*OpaquePrefix=*/true, /*PrefixIncrement=*/-2>(lightTableIndex, dst, dstPitch, src, clip);
		break;
	default:
		app_fatal("Invalid mask type");
	}
}

template <bool OpaquePrefix, int8_t PrefixIncrement>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderRightTrapezoidClipRightAndVerticalDispatchLight(uint8_t lightTableIndex, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, Clip clip) {
	const uint8_t *tbl = &LightTables[static_cast<size_t>(256U * lightTableIndex)];
	if (lightTableIndex == LightsMax) {
		RenderRightTrapezoidClipRightAndVertical<LightType::FullyDark, OpaquePrefix, PrefixIncrement>(dst, dstPitch, src, tbl, clip);
	} else if (lightTableIndex == 0) {
		RenderRightTrapezoidClipRightAndVertical<LightType::FullyLit, OpaquePrefix, PrefixIncrement>(dst, dstPitch, src, tbl, clip);
	} else {
		RenderRightTrapezoidClipRightAndVertical<LightType::PartiallyLit, OpaquePrefix, PrefixIncrement>(dst, dstPitch, src, tbl, clip);
	}
}

DVL_ATTRIBUTE_HOT void RenderRightTrapezoidClipRightAndVerticalDispatch(MaskType maskType, uint8_t lightTableIndex, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, Clip clip) {
	switch (maskType) {
	case MaskType::Solid:
		RenderRightTrapezoidClipRightAndVerticalDispatchLight</*OpaquePrefix=*/false, /*PrefixIncrement=*/0>(lightTableIndex, dst, dstPitch, src, clip);
		break;
	case MaskType::Transparent:
		RenderRightTrapezoidClipRightAndVerticalDispatchLight</*OpaquePrefix=*/true, /*PrefixIncrement=*/0>(lightTableIndex, dst, dstPitch, src, clip);
		break;
	case MaskType::Right:
		RenderRightTrapezoidClipRightAndVerticalDispatchLight</*OpaquePrefix=*/true, /*PrefixIncrement=*/-2>(lightTableIndex, dst, dstPitch, src, clip);
		break;
	default:
		app_fatal("Invalid mask type");
	}
}

template <bool OpaquePrefix, int8_t PrefixIncrement>
DVL_ATTRIBUTE_HOT void RenderTransparentSquareFullDispatch(uint8_t lightTableIndex, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, const uint8_t *DVL_RESTRICT tbl)
{
	if (lightTableIndex == LightsMax) {
		RenderTransparentSquareFull<LightType::FullyDark, OpaquePrefix, PrefixIncrement>(dst, dstPitch, src, tbl);
	} else if (lightTableIndex == 0) {
		RenderTransparentSquareFull<LightType::FullyLit, OpaquePrefix, PrefixIncrement>(dst, dstPitch, src, tbl);
	} else {
		RenderTransparentSquareFull<LightType::PartiallyLit, OpaquePrefix, PrefixIncrement>(dst, dstPitch, src, tbl);
	}
}

template <bool OpaquePrefix, int8_t PrefixIncrement>
DVL_ATTRIBUTE_HOT void RenderTransparentSquareClippedDispatch(uint8_t lightTableIndex, uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, const uint8_t *DVL_RESTRICT tbl, Clip clip)
{
	if (lightTableIndex == LightsMax) {
		RenderTransparentSquareClipped<LightType::FullyDark, OpaquePrefix, PrefixIncrement>(dst, dstPitch, src, tbl, clip);
	} else if (lightTableIndex == 0) {
		RenderTransparentSquareClipped<LightType::FullyLit, OpaquePrefix, PrefixIncrement>(dst, dstPitch, src, tbl, clip);
	} else {
		RenderTransparentSquareClipped<LightType::PartiallyLit, OpaquePrefix, PrefixIncrement>(dst, dstPitch, src, tbl, clip);
	}
}

// Blit with left and vertical clipping.
void RenderBlackTileClipLeftAndVertical(uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, int sx, DiamondClipY clipY)
{
	dst += XStep * (LowerHeight - clipY.lowerBottom - 1);
	// Lower triangle (drawn bottom to top):
	const auto lowerMax = LowerHeight - clipY.lowerTop;
	for (auto i = clipY.lowerBottom + 1; i <= lowerMax; ++i, dst -= dstPitch + XStep) {
		const auto w = 2 * XStep * i;
		const auto curX = sx + TILE_WIDTH / 2 - XStep * i;
		if (curX >= 0) {
			memset(dst, 0, w);
		} else if (-curX <= w) {
			memset(dst - curX, 0, w + curX);
		}
	}
	dst += 2 * XStep + XStep * clipY.upperBottom;
	// Upper triangle (drawn bottom to top):
	const auto upperMax = TriangleUpperHeight - clipY.upperTop;
	for (auto i = clipY.upperBottom; i < upperMax; ++i, dst -= dstPitch - XStep) {
		const auto w = 2 * XStep * (TriangleUpperHeight - i);
		const auto curX = sx + TILE_WIDTH / 2 - XStep * (TriangleUpperHeight - i);
		if (curX >= 0) {
			memset(dst, 0, w);
		} else if (-curX <= w) {
			memset(dst - curX, 0, w + curX);
		} else {
			break;
		}
	}
}

// Blit with right and vertical clipping.
void RenderBlackTileClipRightAndVertical(uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, int_fast16_t maxWidth, DiamondClipY clipY)
{
	dst += XStep * (LowerHeight - clipY.lowerBottom - 1);
	// Lower triangle (drawn bottom to top):
	const auto lowerMax = LowerHeight - clipY.lowerTop;
	for (auto i = clipY.lowerBottom + 1; i <= lowerMax; ++i, dst -= dstPitch + XStep) {
		const auto width = 2 * XStep * i;
		const auto endX = TILE_WIDTH / 2 + XStep * i;
		const auto skip = endX > maxWidth ? endX - maxWidth : 0;
		if (width > skip)
			memset(dst, 0, width - skip);
	}
	dst += 2 * XStep + XStep * clipY.upperBottom;
	// Upper triangle (drawn bottom to top):
	const auto upperMax = TriangleUpperHeight - clipY.upperTop;
	for (auto i = 1 + clipY.upperBottom; i <= upperMax; ++i, dst -= dstPitch - XStep) {
		const auto width = TILE_WIDTH - 2 * XStep * i;
		const auto endX = TILE_WIDTH / 2 + XStep * (TriangleUpperHeight - i + 1);
		const auto skip = endX > maxWidth ? endX - maxWidth : 0;
		if (width <= skip)
			break;
		memset(dst, 0, width - skip);
	}
}

// Blit with vertical clipping only.
void RenderBlackTileClipY(uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, DiamondClipY clipY)
{
	dst += XStep * (LowerHeight - clipY.lowerBottom - 1);
	// Lower triangle (drawn bottom to top):
	const auto lowerMax = LowerHeight - clipY.lowerTop;
	for (auto i = 1 + clipY.lowerBottom; i <= lowerMax; ++i, dst -= dstPitch + XStep) {
		memset(dst, 0, 2 * XStep * i);
	}
	dst += 2 * XStep + XStep * clipY.upperBottom;
	// Upper triangle (drawn bottom to top):
	const auto upperMax = TriangleUpperHeight - clipY.upperTop;
	for (auto i = 1 + clipY.upperBottom; i <= upperMax; ++i, dst -= dstPitch - XStep) {
		memset(dst, 0, TILE_WIDTH - 2 * XStep * i);
	}
}

// Blit a black tile without clipping (must be fully in bounds).
void RenderBlackTileFull(uint8_t *DVL_RESTRICT dst, uint16_t dstPitch)
{
	dst += XStep * (LowerHeight - 1);
	// Tile is fully in bounds, can use constant loop boundaries.
	// Lower triangle (drawn bottom to top):
	for (unsigned i = 1; i <= LowerHeight; ++i, dst -= dstPitch + XStep) {
		memset(dst, 0, 2 * XStep * i);
	}
	dst += 2 * XStep;
	// Upper triangle (drawn bottom to to top):
	for (unsigned i = 1; i <= TriangleUpperHeight; ++i, dst -= dstPitch - XStep) {
		memset(dst, 0, TILE_WIDTH - 2 * XStep * i);
	}
}

} // namespace

#ifdef DUN_RENDER_STATS
std::unordered_map<DunRenderType, size_t, DunRenderTypeHash> DunRenderStats;

string_view TileTypeToString(TileType tileType)
{
	// clang-format off
	switch (tileType) {
	case TileType::Square: return "Square";
	case TileType::TransparentSquare: return "TransparentSquare";
	case TileType::LeftTriangle: return "LeftTriangle";
	case TileType::RightTriangle: return "RightTriangle";
	case TileType::LeftTrapezoid: return "LeftTrapezoid";
	case TileType::RightTrapezoid: return "RightTrapezoid";
	default: return "???";
	}
	// clang-format on
}

string_view MaskTypeToString(MaskType maskType)
{
	// clang-format off
	switch (maskType) {
	case MaskType::Solid: return "Solid";
	case MaskType::Transparent: return "Transparent";
	case MaskType::Right: return "Right";
	case MaskType::Left: return "Left";
	case MaskType::RightFoliage: return "RightFoliage";
	case MaskType::LeftFoliage: return "LeftFoliage";
	default: return "???";
	}
	// clang-format on
}
#endif

void RenderTileFull(const Surface &out, Point position,
    LevelCelBlock levelCelBlock, MaskType maskType, uint8_t lightTableIndex)
{
	const TileType tile = levelCelBlock.type();

#ifdef DEBUG_RENDER_OFFSET_X
	position.x += DEBUG_RENDER_OFFSET_X;
#endif
#ifdef DEBUG_RENDER_OFFSET_Y
	position.y += DEBUG_RENDER_OFFSET_Y;
#endif
#ifdef DEBUG_RENDER_COLOR
	DBGCOLOR = GetTileDebugColor(tile);
#endif

	assert(position.x >= 0);
	assert(position.x + Width <= out.w());
	assert(position.y + 1 >= GetTileHeight(tile));
	assert(position.y < out.h());

	const auto *pFrameTable = reinterpret_cast<const uint32_t *>(pDungeonCels.get());
	const auto *src = reinterpret_cast<const uint8_t *>(&pDungeonCels[SDL_SwapLE32(pFrameTable[levelCelBlock.frame()])]);
	uint8_t *dst = &out[position];
	const uint16_t dstPitch = out.pitch();

#ifdef DUN_RENDER_STATS
	++DunRenderStats[DunRenderType { tile, maskType }];
#endif

	switch (tile) {
	case TileType::Square:
		RenderSquareFullDispatch(maskType, lightTableIndex, dst, dstPitch, src);
		break;
	case TileType::TransparentSquare:
		RenderTransparentSquareFullDispatch(maskType, lightTableIndex, dst, dstPitch, src);
		break;
	case TileType::LeftTriangle:
		RenderLeftTriangleFullDispatch(maskType, lightTableIndex, dst, dstPitch, src);
		break;
	case TileType::RightTriangle:
		RenderRightTriangleFullDispatch(maskType, lightTableIndex, dst, dstPitch, src);
		break;
	case TileType::LeftTrapezoid:
		RenderLeftTrapezoidFullDispatch(maskType, lightTableIndex, dst, dstPitch, src);
		break;
	case TileType::RightTrapezoid:
		RenderRightTrapezoidFullDispatch(maskType, lightTableIndex, dst, dstPitch, src);
		break;
	}

#ifdef DEBUG_STR
	const std::pair<string_view, UiFlags> debugStr = GetTileDebugStr(tile);
	DrawString(out, debugStr.first, Rectangle { Point { position.x + 2, position.y - 29 }, Size { 28, 28 } }, debugStr.second);
#endif
}

void RenderTileClipVertical(const Surface &out, Point position,
    LevelCelBlock levelCelBlock, MaskType maskType, uint8_t lightTableIndex)
{
	const TileType tile = levelCelBlock.type();

#ifdef DEBUG_RENDER_OFFSET_X
	position.x += DEBUG_RENDER_OFFSET_X;
#endif
#ifdef DEBUG_RENDER_OFFSET_Y
	position.y += DEBUG_RENDER_OFFSET_Y;
#endif
#ifdef DEBUG_RENDER_COLOR
	DBGCOLOR = GetTileDebugColor(tile);
#endif

	const int_fast16_t height = GetTileHeight(tile);
	const Clip clip = CalculateClip(position.x, position.y, Width, height, out);
	if (clip.width == Width && clip.height == height)
		return RenderTileFull(out, position, levelCelBlock, maskType, lightTableIndex);
	if (clip.height <= 0)
		return;

	const auto *pFrameTable = reinterpret_cast<const uint32_t *>(pDungeonCels.get());
	const auto *src = reinterpret_cast<const uint8_t *>(&pDungeonCels[SDL_SwapLE32(pFrameTable[levelCelBlock.frame()])]);
	uint8_t *dst = out.at(static_cast<int>(position.x + clip.left), static_cast<int>(position.y - clip.bottom));
	const uint16_t dstPitch = out.pitch();

#ifdef DUN_RENDER_STATS
	++DunRenderStats[DunRenderType { tile, maskType }];
#endif

	switch (tile) {
	case TileType::Square:
		RenderSquareClippedDispatch(maskType, lightTableIndex, dst, dstPitch, src, clip);
		break;
	case TileType::TransparentSquare:
		RenderTransparentSquareClippedDispatch(maskType, lightTableIndex, dst, dstPitch, src, clip);
		break;
	case TileType::LeftTriangle:
		RenderLeftTriangleClipVerticalDispatch(maskType, lightTableIndex, dst, dstPitch, src, clip);
		break;
	case TileType::RightTriangle:
		RenderRightTriangleClipVerticalDispatch(maskType, lightTableIndex, dst, dstPitch, src, clip);
		break;
	case TileType::LeftTrapezoid:
		RenderLeftTrapezoidClipVerticalDispatch(maskType, lightTableIndex, dst, dstPitch, src, clip);
		break;
	case TileType::RightTrapezoid:
		RenderRightTrapezoidClipVerticalDispatch(maskType, lightTableIndex, dst, dstPitch, src, clip);
		break;
	}

#ifdef DEBUG_STR
	const std::pair<string_view, UiFlags> debugStr = GetTileDebugStr(tile);
	DrawString(out, debugStr.first, Rectangle { Point { position.x + 2, position.y - 29 }, Size { 28, 28 } }, debugStr.second);
#endif
}

void RenderTileClipLeftAndVertical(const Surface &out, Point position,
    LevelCelBlock levelCelBlock, MaskType maskType, uint8_t lightTableIndex)
{
	const TileType tile = levelCelBlock.type();

#ifdef DEBUG_RENDER_OFFSET_X
	position.x += DEBUG_RENDER_OFFSET_X;
#endif
#ifdef DEBUG_RENDER_OFFSET_Y
	position.y += DEBUG_RENDER_OFFSET_Y;
#endif
#ifdef DEBUG_RENDER_COLOR
	DBGCOLOR = GetTileDebugColor(tile);
#endif

	const int_fast16_t height = GetTileHeight(tile);
	const Clip clip = CalculateClip(position.x, position.y, Width, height, out);
	if (clip.width == Width && clip.height == height)
		return RenderTileFull(out, position, levelCelBlock, maskType, lightTableIndex);
	if (clip.width <= 0 || clip.height <= 0)
		return;

	const auto *pFrameTable = reinterpret_cast<const uint32_t *>(pDungeonCels.get());
	const auto *src = reinterpret_cast<const uint8_t *>(&pDungeonCels[SDL_SwapLE32(pFrameTable[levelCelBlock.frame()])]);
	uint8_t *dst = out.at(static_cast<int>(position.x + clip.left), static_cast<int>(position.y - clip.bottom));
	const uint16_t dstPitch = out.pitch();

#ifdef DUN_RENDER_STATS
	++DunRenderStats[DunRenderType { tile, maskType }];
#endif

	switch (tile) {
	case TileType::Square:
		RenderSquareClippedDispatch(maskType, lightTableIndex, dst, dstPitch, src, clip);
		break;
	case TileType::TransparentSquare:
		RenderTransparentSquareClippedDispatch(maskType, lightTableIndex, dst, dstPitch, src, clip);
		break;
	case TileType::LeftTriangle:
		RenderLeftTriangleClipLeftAndVerticalDispatch(maskType, lightTableIndex, dst, dstPitch, src, clip);
		break;
	case TileType::RightTriangle:
		RenderRightTriangleClipLeftAndVerticalDispatch(maskType, lightTableIndex, dst, dstPitch, src, clip);
		break;
	case TileType::LeftTrapezoid:
		RenderLeftTrapezoidClipLeftAndVerticalDispatch(maskType, lightTableIndex, dst, dstPitch, src, clip);
		break;
	case TileType::RightTrapezoid:
		RenderRightTrapezoidClipLeftAndVerticalDispatch(maskType, lightTableIndex, dst, dstPitch, src, clip);
		break;
	}

#ifdef DEBUG_STR
	const std::pair<string_view, UiFlags> debugStr = GetTileDebugStr(tile);
	DrawString(out, debugStr.first, Rectangle { Point { position.x + 2, position.y - 29 }, Size { 28, 28 } }, debugStr.second);
#endif
}

void RenderTileClipRightAndVertical(const Surface &out, Point position,
    LevelCelBlock levelCelBlock, MaskType maskType, uint8_t lightTableIndex)
{
	const TileType tile = levelCelBlock.type();

#ifdef DEBUG_RENDER_OFFSET_X
	position.x += DEBUG_RENDER_OFFSET_X;
#endif
#ifdef DEBUG_RENDER_OFFSET_Y
	position.y += DEBUG_RENDER_OFFSET_Y;
#endif
#ifdef DEBUG_RENDER_COLOR
	DBGCOLOR = GetTileDebugColor(tile);
#endif

	const int_fast16_t height = GetTileHeight(tile);
	const Clip clip = CalculateClip(position.x, position.y, Width, height, out);
	if (clip.width == Width && clip.height == height)
		return RenderTileFull(out, position, levelCelBlock, maskType, lightTableIndex);
	if (clip.width <= 0 || clip.height <= 0)
		return;

	const auto *pFrameTable = reinterpret_cast<const uint32_t *>(pDungeonCels.get());
	const auto *src = reinterpret_cast<const uint8_t *>(&pDungeonCels[SDL_SwapLE32(pFrameTable[levelCelBlock.frame()])]);
	uint8_t *dst = out.at(static_cast<int>(position.x + clip.left), static_cast<int>(position.y - clip.bottom));
	const uint16_t dstPitch = out.pitch();

#ifdef DUN_RENDER_STATS
	++DunRenderStats[DunRenderType { tile, maskType }];
#endif

	switch (tile) {
	case TileType::Square:
		RenderSquareClippedDispatch(maskType, lightTableIndex, dst, dstPitch, src, clip);
		break;
	case TileType::TransparentSquare:
		RenderTransparentSquareClippedDispatch(maskType, lightTableIndex, dst, dstPitch, src, clip);
		break;
	case TileType::LeftTriangle:
		RenderLeftTriangleClipRightAndVerticalDispatch(maskType, lightTableIndex, dst, dstPitch, src, clip);
		break;
	case TileType::RightTriangle:
		RenderRightTriangleClipRightAndVerticalDispatch(maskType, lightTableIndex, dst, dstPitch, src, clip);
		break;
	case TileType::LeftTrapezoid:
		RenderLeftTrapezoidClipRightAndVerticalDispatch(maskType, lightTableIndex, dst, dstPitch, src, clip);
		break;
	case TileType::RightTrapezoid:
		RenderRightTrapezoidClipRightAndVerticalDispatch(maskType, lightTableIndex, dst, dstPitch, src, clip);
		break;
	}

#ifdef DEBUG_STR
	const std::pair<string_view, UiFlags> debugStr = GetTileDebugStr(tile);
	DrawString(out, debugStr.first, Rectangle { Point { position.x + 2, position.y - 29 }, Size { 28, 28 } }, debugStr.second);
#endif
}

void world_draw_black_tile(const Surface &out, int sx, int sy)
{
#ifdef DEBUG_RENDER_OFFSET_X
	sx += DEBUG_RENDER_OFFSET_X;
#endif
#ifdef DEBUG_RENDER_OFFSET_Y
	sy += DEBUG_RENDER_OFFSET_Y;
#endif
	auto clip = CalculateClip(sx, sy, TILE_WIDTH, TriangleHeight, out);
	if (clip.width <= 0 || clip.height <= 0)
		return;

	auto clipY = CalculateDiamondClipY(clip);
	uint8_t *dst = out.at(sx, static_cast<int>(sy - clip.bottom));
	if (clip.width == TILE_WIDTH) {
		if (clip.height == TriangleHeight) {
			RenderBlackTileFull(dst, out.pitch());
		} else {
			RenderBlackTileClipY(dst, out.pitch(), clipY);
		}
	} else {
		if (clip.right == 0) {
			RenderBlackTileClipLeftAndVertical(dst, out.pitch(), sx, clipY);
		} else {
			RenderBlackTileClipRightAndVertical(dst, out.pitch(), clip.width, clipY);
		}
	}
}

} // namespace devilution

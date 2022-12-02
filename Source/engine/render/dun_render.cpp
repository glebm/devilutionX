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

#include <algorithm>
#include <climits>
#include <cstdint>

#include "lighting.h"
#ifdef _DEBUG
#include "miniwin/misc_msg.h"
#endif
#include "options.h"
#include "utils/attributes.h"
#ifdef DEBUG_STR
#include "engine/render/text_render.hpp"
#include "utils/str_cat.hpp"
#endif

namespace devilution {

namespace {

/** Width of a tile rendering primitive. */
constexpr std::int_fast16_t Width = TILE_WIDTH / 2;

/** Height of a tile rendering primitive (except triangles). */
constexpr std::int_fast16_t Height = TILE_HEIGHT;

/** Height of the lower triangle of a triangular or a trapezoid tile. */
constexpr std::int_fast16_t LowerHeight = TILE_HEIGHT / 2;

/** Height of the upper triangle of a triangular tile. */
constexpr std::int_fast16_t TriangleUpperHeight = TILE_HEIGHT / 2 - 1;

/** Height of the upper rectangle of a trapezoid tile. */
constexpr std::int_fast16_t TrapezoidUpperHeight = TILE_HEIGHT / 2;

constexpr std::int_fast16_t TriangleHeight = LowerHeight + TriangleUpperHeight;

/** For triangles, for each pixel drawn vertically, this many pixels are drawn horizontally. */
constexpr std::int_fast16_t XStep = 2;

std::int_fast16_t GetTileHeight(TileType tile)
{
	if (tile == TileType::LeftTriangle || tile == TileType::RightTriangle)
		return TriangleHeight;
	return Height;
}

#ifdef DEBUG_STR
string_view GetTileDebugStr(TileType tile)
{
	// clang-format off
	switch (tile) {
		case TileType::Square: return "S";
		case TileType::TransparentSquare: return "T";
		case TileType::LeftTriangle: return "<";
		case TileType::RightTriangle: return ">";
		case TileType::LeftTrapezoid: return "\\";
		case TileType::RightTrapezoid: return "/";
		default: return "";
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

// Masks are defined by a `count` and 2 template variables:
//
// * uint8_t count: The number of set high bits in the current mask.
//   This counts from the largest bit (left-to-right in 0b1010111 notation).
//   E.g. if `count` is 2, the mask is 0b1100000...
// * bool Flip: If true, the set bits are 0 instead of 1.
// * int8_t Shift: As we go up one row, we shift the values by this amount.
//   -2, 0, or 2. If 0, this is a completely solid (Flip=false) or completely transparent (Flip=true) mask.

DVL_ALWAYS_INLINE uint8_t MaskClipLeft(uint8_t count, uint_fast8_t n)
{
	return std::min<uint8_t>(count + n, 32);
}

template <int8_t Shift>
DVL_ALWAYS_INLINE uint8_t MaskNextLine(uint8_t count)
{
	return count + Shift;
}

template <int8_t Shift>
DVL_ALWAYS_INLINE uint8_t MaskNextLine(uint8_t count, uint8_t n)
{
	return count + Shift * n;
}

#ifdef DEBUG_STR
template <bool Flip, int8_t Shift>
std::string maskDebugString(uint8_t count) {
	std::string out(32, Flip ? '1' : '0');
	const uint8_t satCount = std::min<uint8_t>(count, 32);
	out.replace(0, satCount, satCount, Flip ? '0' : '1');
	StrAppend(out, " count=", count, " flip=", Flip, " shift=", Shift);
	return out;
}
#endif

enum class MaskType {
	Invalid,
	Solid,
	Transparent,
	Right,
	Left,
	RightFoliage,
	LeftFoliage,
};

enum class LightType : uint8_t {
	FullyDark,
	PartiallyLit,
	FullyLit,
};

template <LightType Light>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderLineOpaque(std::uint8_t *dst, const std::uint8_t *src, std::uint_fast8_t n, const std::uint8_t *tbl)
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
		for (size_t i = 0; i < n; i++) {
			dst[i] = tbl[src[i]];
		}
#else
		memset(dst, tbl[DBGCOLOR], n);
#endif
	}
}

template <LightType Light>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderLineTransparent(std::uint8_t *dst, const std::uint8_t *src, std::uint_fast8_t n, const std::uint8_t *tbl)
{
#ifndef DEBUG_RENDER_COLOR
	if (Light == LightType::FullyDark) {
		for (size_t i = 0; i < n; i++) {
			dst[i] = paletteTransparencyLookup[0][dst[i]];
		}
	} else if (Light == LightType::FullyLit) {
		for (size_t i = 0; i < n; i++) {
			dst[i] = paletteTransparencyLookup[dst[i]][src[i]];
		}
	} else { // Partially lit
		for (size_t i = 0; i < n; i++) {
			dst[i] = paletteTransparencyLookup[dst[i]][tbl[src[i]]];
		}
	}
#else
	for (size_t i = 0; i < n; i++) {
		dst[i] = paletteTransparencyLookup[dst[i]][tbl[DBGCOLOR + 4]];
	}
#endif
}

template <LightType Light, int8_t MaskShift>
DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderLine(std::uint8_t *dst, const std::uint8_t *src, std::uint_fast8_t n, const std::uint8_t *tbl, uint8_t mask, bool maskFlip)
{
	if (MaskShift == 0) {
		if (maskFlip) {
			RenderLineTransparent<Light>(dst, src, n, tbl);
		} else {
			RenderLineOpaque<Light>(dst, src, n, tbl);
		}
	} else {
		const uint8_t lowBits = std::min<uint8_t>(32 - mask, n);
		const uint8_t highBits = n > lowBits ? n - lowBits : 0;
		if (maskFlip) {
			RenderLineOpaque<Light>(dst, src, lowBits, tbl);
			RenderLineTransparent<Light>(dst + lowBits, src + lowBits, highBits, tbl);
		} else {
			RenderLineTransparent<Light>(dst, src, lowBits, tbl);
			RenderLineOpaque<Light>(dst + lowBits, src + lowBits, highBits, tbl);
		}
	}
}

struct Clip {
	std::int_fast16_t top;
	std::int_fast16_t bottom;
	std::int_fast16_t left;
	std::int_fast16_t right;
	std::int_fast16_t width;
	std::int_fast16_t height;
};

Clip CalculateClip(std::int_fast16_t x, std::int_fast16_t y, std::int_fast16_t w, std::int_fast16_t h, const Surface &out)
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

template <LightType Light, int8_t MaskShift>
DVL_ATTRIBUTE_HOT void RenderSquareFull(std::uint8_t *dst, int dstPitch, const std::uint8_t *src, uint8_t mask, bool maskFlip, const std::uint8_t *tbl)
{
	for (auto i = 0; i < Height; ++i, dst -= dstPitch) {
		RenderLine<Light, MaskShift>(dst, src, Width, tbl, mask, maskFlip);
		src += Width;
		mask = MaskNextLine<MaskShift>(mask);
	}
}

template <LightType Light, int8_t MaskShift>
DVL_ATTRIBUTE_HOT void RenderSquareClipped(std::uint8_t *dst, int dstPitch, const std::uint8_t *src, uint8_t mask, bool maskFlip, const std::uint8_t *tbl, Clip clip)
{
	src += clip.bottom * Height + clip.left;
	for (auto i = 0; i < clip.height; ++i, dst -= dstPitch) {
		RenderLine<Light, MaskShift>(dst, src, clip.width, tbl, MaskClipLeft(mask, clip.left), maskFlip);
		src += Width;
		mask = MaskNextLine<MaskShift>(mask);
	}
}

template <LightType Light, int8_t MaskShift>
DVL_ATTRIBUTE_HOT void RenderSquare(std::uint8_t *dst, int dstPitch, const std::uint8_t *src, uint8_t mask, bool maskFlip, const std::uint8_t *tbl, Clip clip)
{
	if (clip.width == Width && clip.height == Height) {
		RenderSquareFull<Light, MaskShift>(dst, dstPitch, src, mask, maskFlip, tbl);
	} else {
		RenderSquareClipped<Light, MaskShift>(dst, dstPitch, src, mask, maskFlip, tbl, clip);
	}
}

template <LightType Light, int8_t MaskShift>
DVL_ATTRIBUTE_HOT void RenderTransparentSquareFull(std::uint8_t *dst, int dstPitch, const std::uint8_t *src, uint8_t mask, bool maskFlip, const std::uint8_t *tbl)
{
	for (auto i = 0; i < Height; ++i, dst -= dstPitch + Width) {
		std::uint_fast8_t drawWidth = Width;
		while (drawWidth > 0) {
			auto v = static_cast<std::int8_t>(*src++);
			if (v > 0) {
				RenderLine<Light, MaskShift>(dst, src, v, tbl, MaskClipLeft(mask, Width - drawWidth), maskFlip);
				src += v;
			} else {
				v = -v;
			}
			dst += v;
			drawWidth -= v;
		}
		mask = MaskNextLine<MaskShift>(mask);
	}
}

template <LightType Light, int8_t MaskShift>
// NOLINTnextLine(readability-function-cognitive-complexity): Actually complex and has to be fast.
DVL_ATTRIBUTE_HOT void RenderTransparentSquareClipped(std::uint8_t *dst, int dstPitch, const std::uint8_t *src, uint8_t mask, bool maskFlip, const std::uint8_t *tbl, Clip clip)
{
	const auto skipRestOfTheLine = [&src](std::int_fast16_t remainingWidth) {
		while (remainingWidth > 0) {
			const auto v = static_cast<std::int8_t>(*src++);
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

	for (auto i = 0; i < clip.height; ++i, dst -= dstPitch + clip.width) {
		auto drawWidth = clip.width;

		// Skip initial src if clipping on the left.
		// Handles overshoot, i.e. when the RLE segment goes into the unclipped area.
		auto remainingLeftClip = clip.left;
		while (remainingLeftClip > 0) {
			auto v = static_cast<std::int8_t>(*src++);
			if (v > 0) {
				if (v > remainingLeftClip) {
					const auto overshoot = v - remainingLeftClip;
					RenderLine<Light, MaskShift>(dst, src + remainingLeftClip, overshoot, tbl, MaskClipLeft(mask, Width - remainingLeftClip), maskFlip);
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
			auto v = static_cast<std::int8_t>(*src++);
			if (v > 0) {
				if (v > drawWidth) {
					RenderLine<Light, MaskShift>(dst, src, drawWidth, tbl, MaskClipLeft(mask, Width - drawWidth), maskFlip);
					src += v;
					dst += drawWidth;
					drawWidth -= v;
					break;
				}
				RenderLine<Light, MaskShift>(dst, src, v, tbl, MaskClipLeft(mask, Width - drawWidth), maskFlip);
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
		mask = MaskNextLine<MaskShift>(mask);
	}
}

template <LightType Light, int8_t MaskShift>
DVL_ATTRIBUTE_HOT void RenderTransparentSquare(std::uint8_t *dst, int dstPitch, const std::uint8_t *src, uint8_t mask, bool maskFlip, const std::uint8_t *tbl, Clip clip)
{
	if (clip.width == Width && clip.height == Height) {
		RenderTransparentSquareFull<Light, MaskShift>(dst, dstPitch, src, mask, maskFlip, tbl);
	} else {
		RenderTransparentSquareClipped<Light, MaskShift>(dst, dstPitch, src, mask, maskFlip, tbl, clip);
	}
}

/** Vertical clip for the lower and upper triangles of a diamond tile (L/RTRIANGLE).*/
struct DiamondClipY {
	std::int_fast16_t lowerBottom;
	std::int_fast16_t lowerTop;
	std::int_fast16_t upperBottom;
	std::int_fast16_t upperTop;
};

template <std::int_fast16_t UpperHeight = TriangleUpperHeight>
DiamondClipY CalculateDiamondClipY(const Clip &clip)
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

std::size_t CalculateTriangleSourceSkipLowerBottom(std::int_fast16_t numLines)
{
	return XStep * numLines * (numLines + 1) / 2 + 2 * ((numLines + 1) / 2);
}

std::size_t CalculateTriangleSourceSkipUpperBottom(std::int_fast16_t numLines)
{
	return 2 * TriangleUpperHeight * numLines - numLines * (numLines - 1) + 2 * ((numLines + 1) / 2);
}

template <LightType Light, int8_t MaskShift>
DVL_ATTRIBUTE_HOT void RenderLeftTriangleFull(std::uint8_t *dst, int dstPitch, const std::uint8_t *src, uint8_t mask, bool maskFlip, const std::uint8_t *tbl)
{
	dst += XStep * (LowerHeight - 1);
	for (auto i = 1; i <= LowerHeight; ++i, dst -= dstPitch + XStep) {
		src += 2 * (i % 2);
		const auto width = XStep * i;
		RenderLine<Light, MaskShift>(dst, src, width, tbl, mask, maskFlip);
		src += width;
		mask = MaskNextLine<MaskShift>(mask);
	}
	dst += 2 * XStep;
	for (auto i = 1; i <= TriangleUpperHeight; ++i, dst -= dstPitch - XStep) {
		src += 2 * (i % 2);
		const auto width = Width - XStep * i;
		RenderLine<Light, MaskShift>(dst, src, width, tbl, mask, maskFlip);
		src += width;
		mask = MaskNextLine<MaskShift>(mask);
	}
}

template <LightType Light, int8_t MaskShift>
DVL_ATTRIBUTE_HOT void RenderLeftTriangleClipVertical(std::uint8_t *dst, int dstPitch, const std::uint8_t *src, uint8_t mask, bool maskFlip, const std::uint8_t *tbl, Clip clip)
{
	const auto clipY = CalculateDiamondClipY(clip);
	src += CalculateTriangleSourceSkipLowerBottom(clipY.lowerBottom);
	dst += XStep * (LowerHeight - clipY.lowerBottom - 1);
	const auto lowerMax = LowerHeight - clipY.lowerTop;
	for (auto i = 1 + clipY.lowerBottom; i <= lowerMax; ++i, dst -= dstPitch + XStep) {
		src += 2 * (i % 2);
		const auto width = XStep * i;
		RenderLine<Light, MaskShift>(dst, src, width, tbl, mask, maskFlip);
		src += width;
		mask = MaskNextLine<MaskShift>(mask);
	}
	src += CalculateTriangleSourceSkipUpperBottom(clipY.upperBottom);
	dst += 2 * XStep + XStep * clipY.upperBottom;
	const auto upperMax = TriangleUpperHeight - clipY.upperTop;
	for (auto i = 1 + clipY.upperBottom; i <= upperMax; ++i, dst -= dstPitch - XStep) {
		src += 2 * (i % 2);
		const auto width = Width - XStep * i;
		RenderLine<Light, MaskShift>(dst, src, width, tbl, mask, maskFlip);
		src += width;
		mask = MaskNextLine<MaskShift>(mask);
	}
}

template <LightType Light, int8_t MaskShift>
DVL_ATTRIBUTE_HOT void RenderLeftTriangleClipLeftAndVertical(std::uint8_t *dst, int dstPitch, const std::uint8_t *src, uint8_t mask, bool maskFlip, const std::uint8_t *tbl, Clip clip)
{
	const auto clipY = CalculateDiamondClipY(clip);
	const auto clipLeft = clip.left;
	src += CalculateTriangleSourceSkipLowerBottom(clipY.lowerBottom);
	dst += XStep * (LowerHeight - clipY.lowerBottom - 1) - clipLeft;
	const auto lowerMax = LowerHeight - clipY.lowerTop;
	for (auto i = 1 + clipY.lowerBottom; i <= lowerMax; ++i, dst -= dstPitch + XStep) {
		src += 2 * (i % 2);
		const auto width = XStep * i;
		const auto startX = Width - XStep * i;
		const auto skip = startX < clipLeft ? clipLeft - startX : 0;
		if (width > skip)
			RenderLine<Light, MaskShift>(dst + skip, src + skip, width - skip, tbl, MaskClipLeft(mask, skip), maskFlip);
		src += width;
		mask = MaskNextLine<MaskShift>(mask);
	}
	src += CalculateTriangleSourceSkipUpperBottom(clipY.upperBottom);
	dst += 2 * XStep + XStep * clipY.upperBottom;
	const auto upperMax = TriangleUpperHeight - clipY.upperTop;
	for (auto i = 1 + clipY.upperBottom; i <= upperMax; ++i, dst -= dstPitch - XStep) {
		src += 2 * (i % 2);
		const auto width = Width - XStep * i;
		const auto startX = XStep * i;
		const auto skip = startX < clipLeft ? clipLeft - startX : 0;
		if (width > skip)
			RenderLine<Light, MaskShift>(dst + skip, src + skip, width - skip, tbl, MaskClipLeft(mask, skip), maskFlip);
		src += width;
		mask = MaskNextLine<MaskShift>(mask);
	}
}

template <LightType Light, int8_t MaskShift>
DVL_ATTRIBUTE_HOT void RenderLeftTriangleClipRightAndVertical(std::uint8_t *dst, int dstPitch, const std::uint8_t *src, uint8_t mask, bool maskFlip, const std::uint8_t *tbl, Clip clip)
{
	const auto clipY = CalculateDiamondClipY(clip);
	const auto clipRight = clip.right;
	src += CalculateTriangleSourceSkipLowerBottom(clipY.lowerBottom);
	dst += XStep * (LowerHeight - clipY.lowerBottom - 1);
	const auto lowerMax = LowerHeight - clipY.lowerTop;
	for (auto i = 1 + clipY.lowerBottom; i <= lowerMax; ++i, dst -= dstPitch + XStep) {
		src += 2 * (i % 2);
		const auto width = XStep * i;
		if (width > clipRight)
			RenderLine<Light, MaskShift>(dst, src, width - clipRight, tbl, mask, maskFlip);
		src += width;
		mask = MaskNextLine<MaskShift>(mask);
	}
	src += CalculateTriangleSourceSkipUpperBottom(clipY.upperBottom);
	dst += 2 * XStep + XStep * clipY.upperBottom;
	const auto upperMax = TriangleUpperHeight - clipY.upperTop;
	for (auto i = 1 + clipY.upperBottom; i <= upperMax; ++i, dst -= dstPitch - XStep) {
		src += 2 * (i % 2);
		const auto width = Width - XStep * i;
		if (width <= clipRight)
			break;
		RenderLine<Light, MaskShift>(dst, src, width - clipRight, tbl, mask, maskFlip);
		src += width;
		mask = MaskNextLine<MaskShift>(mask);
	}
}

template <LightType Light, int8_t MaskShift>
DVL_ATTRIBUTE_HOT void RenderLeftTriangle(std::uint8_t *dst, int dstPitch, const std::uint8_t *src, uint8_t mask, bool maskFlip, const std::uint8_t *tbl, Clip clip)
{
	if (clip.width == Width) {
		if (clip.height == TriangleHeight) {
			RenderLeftTriangleFull<Light, MaskShift>(dst, dstPitch, src, mask, maskFlip, tbl);
		} else {
			RenderLeftTriangleClipVertical<Light, MaskShift>(dst, dstPitch, src, mask, maskFlip, tbl, clip);
		}
	} else if (clip.right == 0) {
		RenderLeftTriangleClipLeftAndVertical<Light, MaskShift>(dst, dstPitch, src, mask, maskFlip, tbl, clip);
	} else {
		RenderLeftTriangleClipRightAndVertical<Light, MaskShift>(dst, dstPitch, src, mask, maskFlip, tbl, clip);
	}
}

template <LightType Light, int8_t MaskShift>
DVL_ATTRIBUTE_HOT void RenderRightTriangleFull(std::uint8_t *dst, int dstPitch, const std::uint8_t *src, uint8_t mask, bool maskFlip, const std::uint8_t *tbl)
{
	for (auto i = 1; i <= LowerHeight; ++i, dst -= dstPitch) {
		const auto width = XStep * i;
		RenderLine<Light, MaskShift>(dst, src, width, tbl, mask, maskFlip);
		src += width + 2 * (i % 2);
		mask = MaskNextLine<MaskShift>(mask);
	}
	for (auto i = 1; i <= TriangleUpperHeight; ++i, dst -= dstPitch) {
		const auto width = Width - XStep * i;
		RenderLine<Light, MaskShift>(dst, src, width, tbl, mask, maskFlip);
		src += width + 2 * (i % 2);
		mask = MaskNextLine<MaskShift>(mask);
	}
}

template <LightType Light, int8_t MaskShift>
DVL_ATTRIBUTE_HOT void RenderRightTriangleClipVertical(std::uint8_t *dst, int dstPitch, const std::uint8_t *src, uint8_t mask, bool maskFlip, const std::uint8_t *tbl, Clip clip)
{
	const auto clipY = CalculateDiamondClipY(clip);
	src += CalculateTriangleSourceSkipLowerBottom(clipY.lowerBottom);
	const auto lowerMax = LowerHeight - clipY.lowerTop;
	for (auto i = 1 + clipY.lowerBottom; i <= lowerMax; ++i, dst -= dstPitch) {
		const auto width = XStep * i;
		RenderLine<Light, MaskShift>(dst, src, width, tbl, mask, maskFlip);
		src += width + 2 * (i % 2);
		mask = MaskNextLine<MaskShift>(mask);
	}
	src += CalculateTriangleSourceSkipUpperBottom(clipY.upperBottom);
	const auto upperMax = TriangleUpperHeight - clipY.upperTop;
	for (auto i = 1 + clipY.upperBottom; i <= upperMax; ++i, dst -= dstPitch) {
		const auto width = Width - XStep * i;
		RenderLine<Light, MaskShift>(dst, src, width, tbl, mask, maskFlip);
		src += width + 2 * (i % 2);
		mask = MaskNextLine<MaskShift>(mask);
	}
}

template <LightType Light, int8_t MaskShift>
DVL_ATTRIBUTE_HOT void RenderRightTriangleClipLeftAndVertical(std::uint8_t *dst, int dstPitch, const std::uint8_t *src, uint8_t mask, bool maskFlip, const std::uint8_t *tbl, Clip clip)
{
	const auto clipY = CalculateDiamondClipY(clip);
	const auto clipLeft = clip.left;
	src += CalculateTriangleSourceSkipLowerBottom(clipY.lowerBottom);
	const auto lowerMax = LowerHeight - clipY.lowerTop;
	for (auto i = 1 + clipY.lowerBottom; i <= lowerMax; ++i, dst -= dstPitch) {
		const auto width = XStep * i;
		if (width > clipLeft)
			RenderLine<Light, MaskShift>(dst, src + clipLeft, width - clipLeft, tbl, MaskClipLeft(mask, clipLeft), maskFlip);
		src += width + 2 * (i % 2);
		mask = MaskNextLine<MaskShift>(mask);
	}
	src += CalculateTriangleSourceSkipUpperBottom(clipY.upperBottom);
	const auto upperMax = TriangleUpperHeight - clipY.upperTop;
	for (auto i = 1 + clipY.upperBottom; i <= upperMax; ++i, dst -= dstPitch) {
		const auto width = Width - XStep * i;
		if (width <= clipLeft)
			break;
		RenderLine<Light, MaskShift>(dst, src + clipLeft, width - clipLeft, tbl, MaskClipLeft(mask, clipLeft), maskFlip);
		src += width + 2 * (i % 2);
		mask = MaskNextLine<MaskShift>(mask);
	}
}

template <LightType Light, int8_t MaskShift>
DVL_ATTRIBUTE_HOT void RenderRightTriangleClipRightAndVertical(std::uint8_t *dst, int dstPitch, const std::uint8_t *src, uint8_t mask, bool maskFlip, const std::uint8_t *tbl, Clip clip)
{
	const auto clipY = CalculateDiamondClipY(clip);
	const auto clipRight = clip.right;
	src += CalculateTriangleSourceSkipLowerBottom(clipY.lowerBottom);
	const auto lowerMax = LowerHeight - clipY.lowerTop;
	for (auto i = 1 + clipY.lowerBottom; i <= lowerMax; ++i, dst -= dstPitch) {
		const auto width = XStep * i;
		const auto skip = Width - width < clipRight ? clipRight - (Width - width) : 0;
		if (width > skip)
			RenderLine<Light, MaskShift>(dst, src, width - skip, tbl, mask, maskFlip);
		src += width + 2 * (i % 2);
		mask = MaskNextLine<MaskShift>(mask);
	}
	src += CalculateTriangleSourceSkipUpperBottom(clipY.upperBottom);
	const auto upperMax = TriangleUpperHeight - clipY.upperTop;
	for (auto i = 1 + clipY.upperBottom; i <= upperMax; ++i, dst -= dstPitch) {
		const auto width = Width - XStep * i;
		const auto skip = Width - width < clipRight ? clipRight - (Width - width) : 0;
		if (width > skip)
			RenderLine<Light, MaskShift>(dst, src, width - skip, tbl, mask, maskFlip);
		src += width + 2 * (i % 2);
		mask = MaskNextLine<MaskShift>(mask);
	}
}

template <LightType Light, int8_t MaskShift>
DVL_ATTRIBUTE_HOT void RenderRightTriangle(std::uint8_t *dst, int dstPitch, const std::uint8_t *src, uint8_t mask, bool maskFlip, const std::uint8_t *tbl, Clip clip)
{
	if (clip.width == Width) {
		if (clip.height == TriangleHeight) {
			RenderRightTriangleFull<Light, MaskShift>(dst, dstPitch, src, mask, maskFlip, tbl);
		} else {
			RenderRightTriangleClipVertical<Light, MaskShift>(dst, dstPitch, src, mask, maskFlip, tbl, clip);
		}
	} else if (clip.right == 0) {
		RenderRightTriangleClipLeftAndVertical<Light, MaskShift>(dst, dstPitch, src, mask, maskFlip, tbl, clip);
	} else {
		RenderRightTriangleClipRightAndVertical<Light, MaskShift>(dst, dstPitch, src, mask, maskFlip, tbl, clip);
	}
}

template <LightType Light, int8_t MaskShift>
DVL_ATTRIBUTE_HOT void RenderLeftTrapezoidFull(std::uint8_t *dst, int dstPitch, const std::uint8_t *src, uint8_t mask, bool maskFlip, const std::uint8_t *tbl)
{
	dst += XStep * (LowerHeight - 1);
	for (auto i = 1; i <= LowerHeight; ++i, dst -= dstPitch + XStep) {
		src += 2 * (i % 2);
		const auto width = XStep * i;
		RenderLine<Light, MaskShift>(dst, src, width, tbl, mask, maskFlip);
		src += width;
		mask = MaskNextLine<MaskShift>(mask);
	}
	dst += XStep;
	for (auto i = 1; i <= TrapezoidUpperHeight; ++i, dst -= dstPitch) {
		RenderLine<Light, MaskShift>(dst, src, Width, tbl, mask, maskFlip);
		src += Width;
		mask = MaskNextLine<MaskShift>(mask);
	}
}

template <LightType Light, int8_t MaskShift>
DVL_ATTRIBUTE_HOT void RenderLeftTrapezoidClipVertical(std::uint8_t *dst, int dstPitch, const std::uint8_t *src, uint8_t mask, bool maskFlip, const std::uint8_t *tbl, Clip clip)
{
	const auto clipY = CalculateDiamondClipY<TrapezoidUpperHeight>(clip);
	src += CalculateTriangleSourceSkipLowerBottom(clipY.lowerBottom);
	dst += XStep * (LowerHeight - clipY.lowerBottom - 1);
	const auto lowerMax = LowerHeight - clipY.lowerTop;
	for (auto i = 1 + clipY.lowerBottom; i <= lowerMax; ++i, dst -= dstPitch + XStep) {
		src += 2 * (i % 2);
		const auto width = XStep * i;
		RenderLine<Light, MaskShift>(dst, src, width, tbl, mask, maskFlip);
		src += width;
		mask = MaskNextLine<MaskShift>(mask);
	}
	src += clipY.upperBottom * Width;
	dst += XStep;
	const auto upperMax = TrapezoidUpperHeight - clipY.upperTop;
	for (auto i = 1 + clipY.upperBottom; i <= upperMax; ++i, dst -= dstPitch) {
		RenderLine<Light, MaskShift>(dst, src, Width, tbl, mask, maskFlip);
		src += Width;
		mask = MaskNextLine<MaskShift>(mask);
	}
}

template <LightType Light, int8_t MaskShift>
DVL_ATTRIBUTE_HOT void RenderLeftTrapezoidClipLeftAndVertical(std::uint8_t *dst, int dstPitch, const std::uint8_t *src, uint8_t mask, bool maskFlip, const std::uint8_t *tbl, Clip clip)
{
	const auto clipY = CalculateDiamondClipY<TrapezoidUpperHeight>(clip);
	const auto clipLeft = clip.left;
	src += CalculateTriangleSourceSkipLowerBottom(clipY.lowerBottom);
	dst += XStep * (LowerHeight - clipY.lowerBottom - 1) - clipLeft;
	const auto lowerMax = LowerHeight - clipY.lowerTop;
	for (auto i = 1 + clipY.lowerBottom; i <= lowerMax; ++i, dst -= dstPitch + XStep) {
		src += 2 * (i % 2);
		const auto width = XStep * i;
		const auto startX = Width - XStep * i;
		const auto skip = startX < clipLeft ? clipLeft - startX : 0;
		if (width > skip)
			RenderLine<Light, MaskShift>(dst + skip, src + skip, width - skip, tbl, MaskClipLeft(mask, skip), maskFlip);
		src += width;
		mask = MaskNextLine<MaskShift>(mask);
	}
	src += clipY.upperBottom * Width + clipLeft;
	dst += XStep + clipLeft;
	const auto upperMax = TrapezoidUpperHeight - clipY.upperTop;
	for (auto i = 1 + clipY.upperBottom; i <= upperMax; ++i, dst -= dstPitch) {
		RenderLine<Light, MaskShift>(dst, src, clip.width, tbl, MaskClipLeft(mask, clipLeft), maskFlip);
		src += Width;
		mask = MaskNextLine<MaskShift>(mask);
	}
}

template <LightType Light, int8_t MaskShift>
DVL_ATTRIBUTE_HOT void RenderLeftTrapezoidClipRightAndVertical(std::uint8_t *dst, int dstPitch, const std::uint8_t *src, uint8_t mask, bool maskFlip, const std::uint8_t *tbl, Clip clip)
{
	const auto clipY = CalculateDiamondClipY<TrapezoidUpperHeight>(clip);
	const auto clipRight = clip.right;
	src += CalculateTriangleSourceSkipLowerBottom(clipY.lowerBottom);
	dst += XStep * (LowerHeight - clipY.lowerBottom - 1);
	const auto lowerMax = LowerHeight - clipY.lowerTop;
	for (auto i = 1 + clipY.lowerBottom; i <= lowerMax; ++i, dst -= dstPitch + XStep) {
		src += 2 * (i % 2);
		const auto width = XStep * i;
		if (width > clipRight)
			RenderLine<Light, MaskShift>(dst, src, width - clipRight, tbl, mask, maskFlip);
		src += width;
		mask = MaskNextLine<MaskShift>(mask);
	}
	src += clipY.upperBottom * Width;
	dst += XStep;
	const auto upperMax = TrapezoidUpperHeight - clipY.upperTop;
	for (auto i = 1 + clipY.upperBottom; i <= upperMax; ++i, dst -= dstPitch) {
		RenderLine<Light, MaskShift>(dst, src, clip.width, tbl, mask, maskFlip);
		src += Width;
		mask = MaskNextLine<MaskShift>(mask);
	}
}

template <LightType Light, int8_t MaskShift>
DVL_ATTRIBUTE_HOT void RenderLeftTrapezoid(std::uint8_t *dst, int dstPitch, const std::uint8_t *src, uint8_t mask, bool maskFlip, const std::uint8_t *tbl, Clip clip)
{
	if (clip.width == Width) {
		if (clip.height == Height) {
			RenderLeftTrapezoidFull<Light, MaskShift>(dst, dstPitch, src, mask, maskFlip, tbl);
		} else {
			RenderLeftTrapezoidClipVertical<Light, MaskShift>(dst, dstPitch, src, mask, maskFlip, tbl, clip);
		}
	} else if (clip.right == 0) {
		RenderLeftTrapezoidClipLeftAndVertical<Light, MaskShift>(dst, dstPitch, src, mask, maskFlip, tbl, clip);
	} else {
		RenderLeftTrapezoidClipRightAndVertical<Light, MaskShift>(dst, dstPitch, src, mask, maskFlip, tbl, clip);
	}
}

template <LightType Light, int8_t MaskShift>
DVL_ATTRIBUTE_HOT void RenderRightTrapezoidFull(std::uint8_t *dst, int dstPitch, const std::uint8_t *src, uint8_t mask, bool maskFlip, const std::uint8_t *tbl)
{
	for (auto i = 1; i <= LowerHeight; ++i, dst -= dstPitch) {
		const auto width = XStep * i;
		RenderLine<Light, MaskShift>(dst, src, width, tbl, mask, maskFlip);
		src += width + 2 * (i % 2);
		mask = MaskNextLine<MaskShift>(mask);
	}
	for (auto i = 1; i <= TrapezoidUpperHeight; ++i, dst -= dstPitch) {
		RenderLine<Light, MaskShift>(dst, src, Width, tbl, mask, maskFlip);
		src += Width;
		mask = MaskNextLine<MaskShift>(mask);
	}
}

template <LightType Light, int8_t MaskShift>
DVL_ATTRIBUTE_HOT void RenderRightTrapezoidClipVertical(std::uint8_t *dst, int dstPitch, const std::uint8_t *src, uint8_t mask, bool maskFlip, const std::uint8_t *tbl, Clip clip)
{
	const auto clipY = CalculateDiamondClipY<TrapezoidUpperHeight>(clip);
	const auto lowerMax = LowerHeight - clipY.lowerTop;
	src += CalculateTriangleSourceSkipLowerBottom(clipY.lowerBottom);
	for (auto i = 1 + clipY.lowerBottom; i <= lowerMax; ++i, dst -= dstPitch) {
		const auto width = XStep * i;
		RenderLine<Light, MaskShift>(dst, src, width, tbl, mask, maskFlip);
		src += width + 2 * (i % 2);
		mask = MaskNextLine<MaskShift>(mask);
	}
	src += clipY.upperBottom * Width;
	const auto upperMax = TrapezoidUpperHeight - clipY.upperTop;
	for (auto i = 1 + clipY.upperBottom; i <= upperMax; ++i, dst -= dstPitch) {
		RenderLine<Light, MaskShift>(dst, src, Width, tbl, mask, maskFlip);
		src += Width;
		mask = MaskNextLine<MaskShift>(mask);
	}
}

template <LightType Light, int8_t MaskShift>
DVL_ATTRIBUTE_HOT void RenderRightTrapezoidClipLeftAndVertical(std::uint8_t *dst, int dstPitch, const std::uint8_t *src, uint8_t mask, bool maskFlip, const std::uint8_t *tbl, Clip clip)
{
	const auto clipY = CalculateDiamondClipY<TrapezoidUpperHeight>(clip);
	const auto clipLeft = clip.left;
	const auto lowerMax = LowerHeight - clipY.lowerTop;
	src += CalculateTriangleSourceSkipLowerBottom(clipY.lowerBottom);
	for (auto i = 1 + clipY.lowerBottom; i <= lowerMax; ++i, dst -= dstPitch) {
		const auto width = XStep * i;
		if (width > clipLeft)
			RenderLine<Light, MaskShift>(dst, src + clipLeft, width - clipLeft, tbl, MaskClipLeft(mask, clipLeft), maskFlip);
		src += width + 2 * (i % 2);
		mask = MaskNextLine<MaskShift>(mask);
	}
	src += clipY.upperBottom * Width + clipLeft;
	const auto upperMax = TrapezoidUpperHeight - clipY.upperTop;
	for (auto i = 1 + clipY.upperBottom; i <= upperMax; ++i, dst -= dstPitch) {
		RenderLine<Light, MaskShift>(dst, src, clip.width, tbl, MaskClipLeft(mask, clipLeft), maskFlip);
		src += Width;
		mask = MaskNextLine<MaskShift>(mask);
	}
}

template <LightType Light, int8_t MaskShift>
DVL_ATTRIBUTE_HOT void RenderRightTrapezoidClipRightAndVertical(std::uint8_t *dst, int dstPitch, const std::uint8_t *src, uint8_t mask, bool maskFlip, const std::uint8_t *tbl, Clip clip)
{
	const auto clipY = CalculateDiamondClipY<TrapezoidUpperHeight>(clip);
	const auto clipRight = clip.right;
	const auto lowerMax = LowerHeight - clipY.lowerTop;
	src += CalculateTriangleSourceSkipLowerBottom(clipY.lowerBottom);
	for (auto i = 1 + clipY.lowerBottom; i <= lowerMax; ++i, dst -= dstPitch) {
		const auto width = XStep * i;
		const auto skip = Width - width < clipRight ? clipRight - (Width - width) : 0;
		if (width > skip)
			RenderLine<Light, MaskShift>(dst, src, width - skip, tbl, mask, maskFlip);
		src += width + 2 * (i % 2);
		mask = MaskNextLine<MaskShift>(mask);
	}
	src += clipY.upperBottom * Width;
	const auto upperMax = TrapezoidUpperHeight - clipY.upperTop;
	for (auto i = 1 + clipY.upperBottom; i <= upperMax; ++i, dst -= dstPitch) {
		RenderLine<Light, MaskShift>(dst, src, clip.width, tbl, mask, maskFlip);
		src += Width;
		mask = MaskNextLine<MaskShift>(mask);
	}
}

template <LightType Light, int8_t MaskShift>
DVL_ATTRIBUTE_HOT void RenderRightTrapezoid(std::uint8_t *dst, int dstPitch, const std::uint8_t *src, uint8_t mask, bool maskFlip, const std::uint8_t *tbl, Clip clip)
{
	if (clip.width == Width) {
		if (clip.height == Height) {
			RenderRightTrapezoidFull<Light, MaskShift>(dst, dstPitch, src, mask, maskFlip, tbl);
		} else {
			RenderRightTrapezoidClipVertical<Light, MaskShift>(dst, dstPitch, src, mask, maskFlip, tbl, clip);
		}
	} else if (clip.right == 0) {
		RenderRightTrapezoidClipLeftAndVertical<Light, MaskShift>(dst, dstPitch, src, mask, maskFlip, tbl, clip);
	} else {
		RenderRightTrapezoidClipRightAndVertical<Light, MaskShift>(dst, dstPitch, src, mask, maskFlip, tbl, clip);
	}
}

template <LightType Light, int8_t MaskShift>
DVL_ATTRIBUTE_HOT void RenderTileType(TileType tile, std::uint8_t *dst, int dstPitch, const std::uint8_t *src, uint8_t mask, bool maskFlip, const std::uint8_t *tbl, Clip clip)
{
	switch (tile) {
	case TileType::Square:
		RenderSquare<Light, MaskShift>(dst, dstPitch, src, mask, maskFlip, tbl, clip);
		break;
	case TileType::TransparentSquare:
		RenderTransparentSquare<Light, MaskShift>(dst, dstPitch, src, mask, maskFlip, tbl, clip);
		break;
	case TileType::LeftTriangle:
		RenderLeftTriangle<Light, MaskShift>(dst, dstPitch, src, mask, maskFlip, tbl, clip);
		break;
	case TileType::RightTriangle:
		RenderRightTriangle<Light, MaskShift>(dst, dstPitch, src, mask, maskFlip, tbl, clip);
		break;
	case TileType::LeftTrapezoid:
		RenderLeftTrapezoid<Light, MaskShift>(dst, dstPitch, src, mask, maskFlip, tbl, clip);
		break;
	case TileType::RightTrapezoid:
		RenderRightTrapezoid<Light, MaskShift>(dst, dstPitch, src, mask, maskFlip, tbl, clip);
		break;
	}
}


template <int8_t MaskShift>
DVL_ATTRIBUTE_HOT void RenderTileDispatch(uint8_t lightTableIndex, bool maskFlip, TileType tile, std::uint8_t *dst, int dstPitch, const std::uint8_t *src, const std::uint8_t *tbl, Clip clip)
{
	const uint8_t mask = MaskNextLine<MaskShift>(64, clip.bottom);
	if (lightTableIndex == LightsMax) {
		RenderTileType<LightType::FullyDark, MaskShift>(tile, dst, dstPitch, src, mask, maskFlip, tbl, clip);
	} else if (lightTableIndex == 0) {
		RenderTileType<LightType::FullyLit, MaskShift>(tile, dst, dstPitch, src, mask, maskFlip, tbl, clip);
	} else {
		RenderTileType<LightType::PartiallyLit, MaskShift>(tile, dst, dstPitch, src, mask, maskFlip, tbl, clip);
	}
}


MaskType GetMask(TileType tile, uint16_t levelPieceId, ArchType archType, bool transparency, bool foliage)
{
#ifdef _DEBUG
	if ((SDL_GetModState() & KMOD_ALT) != 0) {
		return MaskType::Solid;
	}
#endif

	if (transparency) {
		if (archType == ArchType::None) {
			return MaskType::Transparent;
		}
		if (archType == ArchType::Left && tile != TileType::LeftTriangle) {
			if (TileHasAny(levelPieceId, TileProperties::TransparentLeft)) {
				return MaskType::Left;
			}
		}
		if (archType == ArchType::Right && tile != TileType::RightTriangle) {
			if (TileHasAny(levelPieceId, TileProperties::TransparentRight)) {
				return MaskType::Right;
			}
		}
	} else if (archType != ArchType::None && foliage) {
		if (tile != TileType::TransparentSquare)
			return MaskType::Invalid;
		if (archType == ArchType::Left)
			return MaskType::LeftFoliage;
		if (archType == ArchType::Right)
			return MaskType::RightFoliage;
	}
	return MaskType::Solid;
}

// Blit with left and vertical clipping.
void RenderBlackTileClipLeftAndVertical(std::uint8_t *dst, int dstPitch, int sx, DiamondClipY clipY)
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
void RenderBlackTileClipRightAndVertical(std::uint8_t *dst, int dstPitch, std::int_fast16_t maxWidth, DiamondClipY clipY)
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
void RenderBlackTileClipY(std::uint8_t *dst, int dstPitch, DiamondClipY clipY)
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
void RenderBlackTileFull(std::uint8_t *dst, int dstPitch)
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

void RenderTile(const Surface &out, Point position,
    LevelCelBlock levelCelBlock, uint16_t levelPieceId,
    uint8_t lightTableIndex, ArchType archType,
    bool transparency, bool foliage)
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

	Clip clip = CalculateClip(position.x, position.y, Width, GetTileHeight(tile), out);
	if (clip.width <= 0 || clip.height <= 0)
		return;

	MaskType maskType = GetMask(tile, levelPieceId, archType, transparency, foliage);
	const std::uint8_t *tbl = &LightTables[256 * lightTableIndex];
	const auto *pFrameTable = reinterpret_cast<const std::uint32_t *>(pDungeonCels.get());
	const auto *src = reinterpret_cast<const std::uint8_t *>(&pDungeonCels[pFrameTable[levelCelBlock.frame()]]);
	std::uint8_t *dst = out.at(static_cast<int>(position.x + clip.left), static_cast<int>(position.y - clip.bottom));
	const auto dstPitch = out.pitch();

	switch (maskType) {
		case MaskType::Invalid:
			break;
		case MaskType::Solid:
			RenderTileDispatch</*MaskShift=*/0>(lightTableIndex, /*maskFlip=*/false, tile, dst, dstPitch, src, tbl, clip);
			break;
		case MaskType::Transparent:
			RenderTileDispatch</*MaskShift=*/0>(lightTableIndex, /*maskFlip=*/true, tile, dst, dstPitch, src, tbl, clip);
			break;
		case MaskType::Left:
			RenderTileDispatch</*MaskShift=*/2>(lightTableIndex, /*maskFlip=*/false, tile, dst, dstPitch, src, tbl, clip);
			break;
		case MaskType::Right:
			RenderTileDispatch</*MaskShift=*/-2>(lightTableIndex,/*maskFlip=*/false, tile, dst, dstPitch, src, tbl, clip);
			break;
		case MaskType::LeftFoliage:
			RenderTileDispatch</*MaskShift=*/2>(lightTableIndex, /*maskFlip=*/true, tile, dst, dstPitch, src, tbl, clip);
			break;
		case MaskType::RightFoliage:
			RenderTileDispatch</*MaskShift=*/-2>(lightTableIndex,/*maskFlip=*/true, tile, dst, dstPitch, src, tbl, clip);
			break;
	}

#ifdef DEBUG_STR
	DrawString(out, GetTileDebugStr(tile), Rectangle { Point { position.x, position.y - 30 }, Size { 32, 31 }}, UiFlags::AlignCenter | UiFlags::VerticalCenter);
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
	std::uint8_t *dst = out.at(sx, static_cast<int>(sy - clip.bottom));
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

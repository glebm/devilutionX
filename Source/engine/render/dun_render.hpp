/**
 * @file dun_render.hpp
 *
 * Interface of functionality for rendering the level tiles.
 */
#pragma once

#include <cstdint>

#include "engine/point.hpp"
#include "engine/surface.hpp"
#include "levels/dun_tile.hpp"
#include "lighting.h"

// #define DUN_RENDER_STATS
#ifdef DUN_RENDER_STATS
#include <ankerl/unordered_dense.h>
#endif

namespace devilution {

/**
 * @brief Specifies the mask to use for rendering.
 */
enum class MaskType : uint8_t {
	/** @brief The entire tile is opaque. */
	Solid,

	/** @brief The entire tile is blended with transparency. */
	Transparent,

	/**
	 * @brief Upper-right triangle is blended with transparency.
	 *
	 * Can only be used with `TileType::LeftTrapezoid` and
	 * `TileType::TransparentSquare`.
	 *
	 * The lower 16 rows are opaque.
	 * The upper 16 rows are arranged like this (🮆 = opaque, 🮐 = blended):
	 *
	 * 🮆🮆🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐
	 * 🮆🮆🮆🮆🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐
	 * 🮆🮆🮆🮆🮆🮆🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐
	 * 🮆🮆🮆🮆🮆🮆🮆🮆🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐
	 * 🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐
	 * 🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐
	 * 🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐
	 * 🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐
	 * 🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐
	 * 🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐
	 * 🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐
	 * 🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮐🮐🮐🮐🮐🮐🮐🮐
	 * 🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮐🮐🮐🮐🮐🮐
	 * 🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮐🮐🮐🮐
	 * 🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮐🮐
	 * 🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆
	 */
	Right,

	/**
	 * @brief Upper-left triangle is blended with transparency.
	 *
	 * Can only be used with `TileType::RightTrapezoid` and
	 * `TileType::TransparentSquare`.
	 *
	 * The lower 16 rows are opaque.
	 * The upper 16 rows are arranged like this (🮆 = opaque, 🮐 = blended):
	 *
	 * 🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮆🮆
	 * 🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮆🮆🮆🮆
	 * 🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮆🮆🮆🮆🮆🮆
	 * 🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮆🮆🮆🮆🮆🮆🮆🮆
	 * 🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆
	 * 🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆
	 * 🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆
	 * 🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆
	 * 🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆
	 * 🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆
	 * 🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆
	 * 🮐🮐🮐🮐🮐🮐🮐🮐🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆
	 * 🮐🮐🮐🮐🮐🮐🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆
	 * 🮐🮐🮐🮐🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆
	 * 🮐🮐🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆
	 * 🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆
	 */
	Left,

	/**
	 * @brief Only the upper-left triangle is rendered.
	 *
	 * Can only be used with `TileType::TransparentSquare`.
	 *
	 * The lower 16 rows are skipped.
	 * The upper 16 rows are arranged like this (🮆 = opaque, 🮐 = not rendered):
	 *
	 * 🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆
	 * 🮐🮐🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆
	 * 🮐🮐🮐🮐🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆
	 * 🮐🮐🮐🮐🮐🮐🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆
	 * 🮐🮐🮐🮐🮐🮐🮐🮐🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆
	 * 🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆
	 * 🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆
	 * 🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆
	 * 🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆
	 * 🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆
	 * 🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆
	 * 🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆
	 * 🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮆🮆🮆🮆🮆🮆🮆🮆
	 * 🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮆🮆🮆🮆🮆🮆
	 * 🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮆🮆🮆🮆
	 * 🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮆🮆
	 */
	RightFoliage,

	/**
	 * @brief Only the upper right triangle is rendered.
	 *
	 * Can only be used with `TileType::TransparentSquare`.
	 *
	 * The lower 16 rows are skipped.
	 * The upper 16 rows are arranged like this (🮆 = opaque, 🮐 = not rendered):
	 *
	 * 🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆
	 * 🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮐🮐
	 * 🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮐🮐🮐🮐
	 * 🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮐🮐🮐🮐🮐🮐
	 * 🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮐🮐🮐🮐🮐🮐🮐🮐
	 * 🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐
	 * 🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐
	 * 🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐
	 * 🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐
	 * 🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐
	 * 🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐
	 * 🮆🮆🮆🮆🮆🮆🮆🮆🮆🮆🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐
	 * 🮆🮆🮆🮆🮆🮆🮆🮆🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐
	 * 🮆🮆🮆🮆🮆🮆🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐
	 * 🮆🮆🮆🮆🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐
	 * 🮆🮆🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐🮐
	 */
	LeftFoliage,
};

#ifdef DUN_RENDER_STATS
struct DunRenderType {
	TileType tileType;
	MaskType maskType;
	bool operator==(const DunRenderType &other) const
	{
		return tileType == other.tileType && maskType == other.maskType;
	}
};
struct DunRenderTypeHash {
	size_t operator()(DunRenderType t) const noexcept
	{
		return std::hash<uint32_t> {}((1 < static_cast<uint8_t>(t.tileType)) | static_cast<uint8_t>(t.maskType));
	}
};
extern ankerl::unordered_dense::map<DunRenderType, size_t, DunRenderTypeHash> DunRenderStats;

std::string_view TileTypeToString(TileType tileType);

std::string_view MaskTypeToString(MaskType maskType);
#endif

/**
 * @brief Blit current world CEL to the given buffer
 * @param out Target buffer
 * @param position Target buffer coordinates
 * @param levelCelBlock The MIN block of the level CEL file.
 * @param maskType The mask to use,
 * @param tbl LightTable or TRN for a tile.
 */
void RenderTile(const Surface &out, Point position,
    LevelCelBlock levelCelBlock, MaskType maskType, const uint8_t *tbl);

/**
 * @brief Render a single color 64x31 tile ◆
 * @param out Target buffer
 * @param sx Target buffer coordinate (left corner of the tile)
 * @param sy Target buffer coordinate (bottom corner of the tile)
 * @param color Color index
 */
void RenderSingleColorTile(const Surface &out, int sx, int sy, uint8_t color = 0);

inline bool IsFullyDark(const uint8_t *DVL_RESTRICT tbl)
{
	return tbl == FullyDarkLightTable;
}

inline bool IsFullyLit(const uint8_t *DVL_RESTRICT tbl)
{
	return tbl == FullyLitLightTable;
}

/**
 * @brief Renders a tile without masking.
 */
void RenderOpaqueTile(const Surface &out, Point position, LevelCelBlock levelCelBlock, const uint8_t *tbl);

/**
 * @brief Renders a tile with transparency blending.
 */
void RenderTransparentTile(const Surface &out, Point position, LevelCelBlock levelCelBlock, const uint8_t *tbl);

/**
 * @brief Renders a tile without masking and without lighting.
 */
void RenderFullyLitOpaqueTile(TileType tile, const Surface &out, Point position, const uint8_t *DVL_RESTRICT src);

/**
 * @brief Writes a tile with the color swaps from `tbl` to `dst`.
 */
void DunTileApplyTrans(LevelCelBlock levelCelBlock, uint8_t *DVL_RESTRICT dst, const uint8_t *tbl);

} // namespace devilution

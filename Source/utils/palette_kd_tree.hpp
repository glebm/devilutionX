#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>

#include <SDL.h>

#include "utils/static_vector.hpp"

namespace devilution {

[[nodiscard]] inline uint32_t GetColorDistance(const SDL_Color &a, const std::array<uint8_t, 3> &b)
{
	const int diffr = a.r - b[0];
	const int diffg = a.g - b[1];
	const int diffb = a.b - b[2];
	return (diffr * diffr) + (diffg * diffg) + (diffb * diffb);
}

[[nodiscard]] inline uint32_t GetColorDistanceToPlane(int x1, int x2)
{
	// Our planes are axis-aligned, so a distance from a point to a plane
	// can be calculated based on just the axis coordinate.
	const int delta = x1 - x2;
	return static_cast<uint32_t>(delta * delta);
}

template <size_t N>
uint8_t GetColorComponent(const SDL_Color &);
template <>
inline uint8_t GetColorComponent<0>(const SDL_Color &c) { return c.r; }
template <>
inline uint8_t GetColorComponent<1>(const SDL_Color &c) { return c.g; }
template <>
inline uint8_t GetColorComponent<2>(const SDL_Color &c) { return c.b; }

/**
 * @brief A kd-tree used to find the nearest neighbor in the color space.
 *
 * Each level splits the space in half by red, green, and blue respectively.
 */
class PaletteKdTree {
private:
	using RGB = std::array<uint8_t, 3>;

	/**
	 * @brief Height (number of levels) of the tree, excluding the leaf level.
	 */
	static constexpr size_t Height = 5;

	/**
	 * @brief Total number of leaf nodes.
	 */
	static constexpr unsigned NumLeaves = 1U << Height;

	/**
	 * @brief Total number of non-leaf nodes.
	 */
	static constexpr unsigned NumNodes = NumLeaves - 1;

	/**
	 * @brief A non-leaf node in the k-d tree.
	 */
	struct Node {
		uint8_t pivot;
	};

	/**
	 * @brief A leaf in the k-d tree.
	 */
	struct Leaf {
		// We use inclusive indices to allow for representing [0, 255] full range.
		// An empty node is represented as [1, 0].
		uint8_t valuesBegin;
		uint8_t valuesEndInclusive;
		[[nodiscard]] bool empty() const { return valuesBegin > valuesEndInclusive; }
	};

public:
	explicit PaletteKdTree(const SDL_Color palette[256])
	    : palette_(palette)
	{
		populatePivots();
		StaticVector<uint8_t, 256> leafValues[NumLeaves];
		for (unsigned i = 0; i < 256; ++i) {
			leafValues[leafIndexForColor(palette[i])].emplace_back(i);
		}

		size_t totalLen = 0;
		for (uint8_t leafIndex = 0; leafIndex < NumLeaves; ++leafIndex) {
			Leaf &leaf = leaves_[leafIndex];
			std::span<const uint8_t> values = leafValues[leafIndex];
			if (values.empty()) {
				leaf.valuesBegin = 1;
				leaf.valuesEndInclusive = 0;
			} else {
				leaf.valuesBegin = totalLen;
				leaf.valuesEndInclusive = totalLen - 1 + values.size();
				std::copy(values.begin(), values.end(), values_.data() + totalLen);
				totalLen += values.size();
			}
		}
	}

	[[nodiscard]] uint8_t findNearestNeighbor(const RGB &rgb) const
	{
		uint8_t best;
		uint32_t bestDiff = std::numeric_limits<uint32_t>::max();
		findNearestNeighborVisit(0, rgb, 0, bestDiff, best);
		return best;
	}

private:
	template <size_t... H>
	uint8_t leafIndexForColorImpl(const SDL_Color &color,
	    std::index_sequence<H...>) // NOLINT(readability-named-parameter)
	{
		uint8_t nodeIndex = 0;
		((nodeIndex = childNodeIndex(nodeIndex, GetColorComponent<H % 3>(color) < nodes_[nodeIndex].pivot)), ...);
		return leafIndex(nodeIndex);
	}

	uint8_t leafIndexForColor(const SDL_Color &color)
	{
		return leafIndexForColorImpl(color, std::make_index_sequence<Height> {});
	}

	static uint8_t getMedian(uint8_t *begin, uint8_t *end)
	{
		uint8_t *middleItr = begin + ((end - begin) / 2);
		std::nth_element(begin, middleItr, end);
		if ((end - begin) % 2 == 0) {
			const uint8_t leftMiddleItr = *std::max_element(begin, middleItr);
			return (leftMiddleItr + *middleItr) / 2;
		}
		return *middleItr;
	}

	template <typename C>
	static uint8_t getMedian(C &c)
	{
		return getMedian(c.data(), c.data() + c.size());
	}

	template <unsigned H, size_t N>
	void maybeAddToSubdivisionForMedian(
	    unsigned nodeIndex, unsigned paletteIndex,
	    std::span<StaticVector<uint8_t, 256>, N> out)
	{
		const uint8_t color = GetColorComponent<H % 3>(palette_[paletteIndex]);
		if constexpr (N == 1) {
			out[0].emplace_back(color);
		} else {
			const bool isLeft = color < nodes_[nodeIndex].pivot;
			maybeAddToSubdivisionForMedian<H + 1>(
			    childNodeIndex(nodeIndex, isLeft),
			    paletteIndex,
			    isLeft
			        ? out.template subspan<0, N / 2>()
			        : out.template subspan<N / 2, N / 2>());
		}
	}

	template <size_t N>
	void setPivotsRecursively(
	    unsigned nodeIndex,
	    std::span<StaticVector<uint8_t, 256>, N> values)
	{
		if constexpr (N == 1) {
			nodes_[nodeIndex].pivot = getMedian(values[0]);
		} else {
			setPivotsRecursively(childNodeIndex(nodeIndex, true), values.template subspan<0, N / 2>());
			setPivotsRecursively(childNodeIndex(nodeIndex, false), values.template subspan<N / 2, N / 2>());
		}
	}

	template <size_t TargetDepth>
	void populatePivotsForTargetDepth()
	{
		constexpr size_t NumSubdivisions = 1U << TargetDepth;
		std::array<StaticVector<uint8_t, 256>, NumSubdivisions> subdivisions;
		const std::span<StaticVector<uint8_t, 256>, NumSubdivisions> subdivisionsSpan { subdivisions };
		for (unsigned i = 0; i < 256; ++i) {
			maybeAddToSubdivisionForMedian<0>(0, i, subdivisionsSpan);
		}
		setPivotsRecursively(0, subdivisionsSpan);
	}

	template <size_t... TargetDepths>
	void populatePivotsImpl(std::index_sequence<TargetDepths...> intSeq) // NOLINT(misc-unused-parameters)
	{
		(populatePivotsForTargetDepth<TargetDepths>(), ...);
	}

	void populatePivots()
	{
		populatePivotsImpl(std::make_index_sequence<Height> {});
	}

	// NOLINTNEXTLINE(misc-no-recursion)
	void findNearestNeighborVisit(uint8_t nodeIndex, const RGB &rgb, unsigned h,
	    uint32_t &bestDiff, uint8_t &best) const
	{
		if (h == Height) {
			checkLeaf(leaves_[leafIndex(nodeIndex)], rgb, bestDiff, best);
		} else {
			const Node &node = nodes_[nodeIndex];
			const unsigned coord = h % 3;

			findNearestNeighborVisit(childNodeIndex(nodeIndex, rgb[coord] < node.pivot), rgb, h + 1, bestDiff, best);

			// To see if we need to check a node's subtree, we compare the distance from the query
			// to the current best candidate vs the distance to the edge of the half-space represented
			// by the node.
			if (bestDiff == std::numeric_limits<uint32_t>::max()
			    || GetColorDistanceToPlane(node.pivot, rgb[coord]) < GetColorDistance(palette_[best], rgb)) {
				findNearestNeighborVisit(childNodeIndex(nodeIndex, rgb[coord] >= node.pivot), rgb, h + 1, bestDiff, best);
			}
		}
	}

	void checkLeaf(const Leaf &leaf, const RGB &rgb, uint32_t &bestDiff, uint8_t &best) const
	{
		for (size_t i = leaf.valuesBegin; i <= leaf.valuesEndInclusive; ++i) {
			const uint8_t paletteIndex = values_[i];
			const uint32_t diff = GetColorDistance(palette_[paletteIndex], rgb);
			if (diff < bestDiff) {
				best = paletteIndex;
				bestDiff = diff;
			}
		}
	}

	[[nodiscard]] static constexpr uint8_t childNodeIndex(uint8_t nodeIndex, bool isLeft)
	{
		return (2 * nodeIndex) + (isLeft ? 1 : 2);
	}

	[[nodiscard]] static constexpr uint8_t leafIndex(uint8_t nodeIndex)
	{
		return static_cast<uint8_t>(nodeIndex - NumNodes);
	}

	const SDL_Color *palette_;
	std::array<Node, NumNodes> nodes_;
	std::array<Leaf, NumLeaves> leaves_;
	std::array<uint8_t, 256> values_;
};

} // namespace devilution

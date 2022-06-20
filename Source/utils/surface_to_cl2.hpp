#pragma once

#include "engine/cel_sprite.hpp"
#include "engine/surface.hpp"

namespace devilution {

OwnedCelSpriteWithFrameHeight SurfaceToCl2(const Surface &surface, unsigned numFrames,
    uint8_t transparentColorIndex);

} // namespace devilution

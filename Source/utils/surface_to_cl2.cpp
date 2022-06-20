#include "utils/surface_to_cl2.hpp"

#include <cstring>

namespace devilution {

namespace {

void WriteLE32(uint8_t *out, uint32_t val)
{
	const uint32_t littleEndian = SDL_SwapLE32(val);
	memcpy(out, &littleEndian, 4);
}

void WriteLE16(uint8_t *out, uint16_t val)
{
	const uint16_t littleEndian = SDL_SwapLE16(val);
	memcpy(out, &littleEndian, 2);
}

void AppendCl2TransparentRun(unsigned width, std::vector<uint8_t> &out)
{
	while (width >= 0x7F) {
		out.push_back(0x7F);
		width -= 0x7F;
	}
	if (width == 0)
		return;
	out.push_back(width);
}

void AppendCl2FillRun(uint8_t color, unsigned width, std::vector<uint8_t> &out)
{
	while (width >= 0x3F) {
		out.push_back(0x80);
		out.push_back(color);
		width -= 0x3F;
	}
	if (width == 0)
		return;
	out.push_back(0xBF - width);
	out.push_back(color);
}

void AppendCl2SolidRun(const uint8_t *src, unsigned width, std::vector<uint8_t> &out)
{
	while (width >= 0x41) {
		out.push_back(0xBF);
		for (size_t i = 0; i < 0x41; ++i)
			out.push_back(src[i]);
		width -= 0x41;
		src += 0x41;
	}
	if (width == 0)
		return;
	out.push_back(256 - width);
	for (size_t i = 0; i < width; ++i)
		out.push_back(src[i]);
}

enum class RunState {
	TransparentRun,
	SolidRun,
	FillRun,
};

void ContinueAppendCl2Line(RunState run, int runBegin, uint8_t prevColor, int sameColorWidth,
    const uint8_t *src, unsigned width, uint8_t transparentColorIndex, std::vector<uint8_t> &out)
{
	for (int i = 0; i < static_cast<int>(width); ++i) {
		const uint8_t pixel = src[i];
		switch (run) {
		case RunState::TransparentRun:
			if (pixel == transparentColorIndex)
				continue;
			AppendCl2TransparentRun(i - runBegin, out);
			run = RunState::FillRun;
			runBegin = i;
			break;
		case RunState::FillRun:
			if (pixel == prevColor)
				continue;
			if (i - runBegin >= 3) {
				AppendCl2FillRun(prevColor, i - runBegin, out);
				// Start a new transparent or fill run depending on the current pixel:
				if (pixel == transparentColorIndex)
					run = RunState::TransparentRun;
				runBegin = i;
			} else if (pixel == transparentColorIndex) {
				// 1. Fewer than 3 same-color pixels in a fill run.
				// 2. Current pixel is transparent.
				// => Add a solid run and start a transparent run.
				AppendCl2SolidRun(src + runBegin, i - runBegin, out);
				run = RunState::TransparentRun;
				runBegin = i;
			} else {
				// 1. Fewer than 3 same-color pixels in a fill run.
				// 2. Current pixel is solid.
				// => Start a solid run instead.
				run = RunState::SolidRun;
				sameColorWidth = 1;
			}
			break;
		case RunState::SolidRun:
			if (pixel == prevColor) {
				++sameColorWidth;
				if (sameColorWidth >= 3) {
					// At least 3 same-color pixels in a solid run:
					// => Finish the previous solid run and start a fill run instead.
					AppendCl2SolidRun(src + runBegin, i + 1 - runBegin - sameColorWidth, out);
					run = RunState::FillRun;
					runBegin = i + 1 - sameColorWidth;
				}
			} else if (pixel == transparentColorIndex) {
				// Finish the solid run and start a transparent run.
				AppendCl2SolidRun(src + runBegin, i - runBegin, out);
				run = RunState::TransparentRun;
				runBegin = i;
			} else {
				sameColorWidth = 1;
			}
			break;
		}
		prevColor = pixel;
	}
	if (run == RunState::TransparentRun) {
		AppendCl2TransparentRun(width - runBegin, out);
	} else if (run == RunState::FillRun) {
		AppendCl2FillRun(prevColor, width - runBegin, out);
	} else {
		AppendCl2SolidRun(src + runBegin, width - runBegin, out);
	}
}

void AppendCl2Line(const uint8_t *src, unsigned width, uint8_t transparentColorIndex, std::vector<uint8_t> &out)
{
	ContinueAppendCl2Line(RunState::TransparentRun, /*runBegin=*/0, /*fillColor=*/transparentColorIndex, /*sameColorWidth=*/0, src, width, transparentColorIndex, out);
}

} // namespace

OwnedCelSpriteWithFrameHeight SurfaceToCl2(const Surface &surface, unsigned numFrames,
    uint8_t transparentColorIndex)
{
	// CL2 header: frame count, frame offset for each frame, file size
	std::vector<uint8_t> celData(4 * (2 + static_cast<size_t>(numFrames)));
	WriteLE32(celData.data(), numFrames);

	const auto height = static_cast<unsigned>(surface.h());
	const auto width = static_cast<unsigned>(surface.w());
	const auto pitch = static_cast<unsigned>(surface.pitch());
	const unsigned frameHeight = height / numFrames;

	// We process the surface a whole frame at a time because the lines are reversed in CEL.
	const uint8_t *dataPtr = surface.begin();
	for (unsigned frame = 1; frame <= numFrames; ++frame) {
		WriteLE32(&celData[4 * static_cast<size_t>(frame)], static_cast<uint32_t>(celData.size()));

		// Frame header: 5 16-bit offsets to 32-pixel height blocks.
		const size_t frameHeaderPos = celData.size();
		constexpr size_t FrameHeaderSize = 10;
		celData.resize(celData.size() + FrameHeaderSize);
		WriteLE16(&celData[frameHeaderPos], FrameHeaderSize);
		size_t line = frameHeight;
		dataPtr += static_cast<unsigned>(pitch * frameHeight);
		size_t transparentContinueWidth = 0;
		while (line-- != 0) {
			dataPtr -= pitch;
			size_t prevSize = celData.size();
			if (transparentContinueWidth != 0) {
				celData.pop_back();
				ContinueAppendCl2Line(
				    RunState::TransparentRun, -static_cast<int>(transparentContinueWidth), /*fillColor=*/transparentColorIndex, /*sameColorWidth=*/0,
				    dataPtr, width, transparentColorIndex, celData);
			} else {
				AppendCl2Line(dataPtr, width, transparentColorIndex, celData);
			}
			if (prevSize + 1 == celData.size() && celData[celData.size() - 1] < 0x80) {
				// The entire line is transparent.
				// In CL2, transparent lines can cross boundaries.
				transparentContinueWidth = 191 - celData.back();
			} else {
				transparentContinueWidth = 0;
			}
			switch (line) {
			case 32:
				WriteLE16(&celData[frameHeaderPos + 2], static_cast<uint16_t>(celData.size() - frameHeaderPos));
				break;
			case 64:
				WriteLE16(&celData[frameHeaderPos + 4], static_cast<uint16_t>(celData.size() - frameHeaderPos));
				break;
			case 96:
				WriteLE16(&celData[frameHeaderPos + 6], static_cast<uint16_t>(celData.size() - frameHeaderPos));
				break;
			case 128:
				WriteLE16(&celData[frameHeaderPos + 8], static_cast<uint16_t>(celData.size() - frameHeaderPos));
				break;
			}
		}
		dataPtr += static_cast<unsigned>(pitch * frameHeight);
	}

	WriteLE32(&celData[4 * (1 + static_cast<size_t>(numFrames))], static_cast<uint32_t>(celData.size()));

	auto out = std::unique_ptr<byte[]>(new byte[celData.size()]);
	memcpy(&out[0], celData.data(), celData.size());
	return OwnedCelSpriteWithFrameHeight {
		OwnedCelSprite { std::move(out), static_cast<uint16_t>(width) },
		frameHeight
	};
}

} // namespace devilution

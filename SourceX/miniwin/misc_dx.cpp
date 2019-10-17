#include "devilution.h"
#include "miniwin/ddraw.h"
#include "stubs.h"
#include <SDL.h>

namespace dvl {

WINBOOL SetCursorPos(int X, int Y)
{
	assert(window);
	int outX = X, outY = Y;
	LogicalToOutput(&outX, &outY);
	SDL_WarpMouseInWindow(window, outX, outY);
	MouseX = X;
	MouseY = Y;
	return true;
}

} // namespace dvl

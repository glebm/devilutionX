#include "devilution.h"
#include "controls/controller_motion.h"
#include "miniwin/ddraw.h"
#include "stubs.h"

namespace dvl {

WINBOOL SetCursorPos(int X, int Y)
{
	assert(window);
	LogicalToOutput(&X, &Y);
	SDL_WarpMouseInWindow(window, X, Y);
	MouseX = X;
	MouseY = Y;
	return true;
}

} // namespace dvl

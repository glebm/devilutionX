#pragma once
// Keyboard keys acting like gamepad buttons

#include "devilution.h"

#if defined(DINGUX) && defined(USE_SDL1) // RetroFW
#define HAS_KBCTRL 1

#define KBCTRL_BUTTON_DPAD_LEFT SDLK_LEFT
#define KBCTRL_BUTTON_DPAD_RIGHT SDLK_RIGHT
#define KBCTRL_BUTTON_DPAD_UP SDLK_UP
#define KBCTRL_BUTTON_DPAD_DOWN SDLK_DOWN

#if defined(DINGUX) && defined(USE_SDL1) // RetroFW
#define KBCTRL_BUTTON_A SDLK_LCTRL
#define KBCTRL_BUTTON_B SDLK_LALT
#define KBCTRL_BUTTON_X SDLK_SPACE
#define KBCTRL_BUTTON_Y SDLK_LSHIFT
#define KBCTRL_BUTTON_RIGHTSHOULDER SDLK_BACKSPACE
#define KBCTRL_BUTTON_LEFTSHOULDER SDLK_TAB
#define KBCTRL_BUTTON_START SDLK_RETURN
#define KBCTRL_BUTTON_BACK SDLK_ESCAPE
#define KBCTRL_MODIFIER_KEY SDLK_END // The suspend key on RG300
#endif

namespace dvl {

inline int translate_kbctrl_to_key(SDL_Keysym key)
{
	SDL_Keycode sym = key.sym;
	const bool isModifier = SDL_GetKeyState(nullptr)[KBCTRL_MODIFIER_KEY];
	switch (sym) {
	case KBCTRL_BUTTON_B:
		return 'H';
	case KBCTRL_BUTTON_A:
		return DVL_VK_SPACE;
	case KBCTRL_BUTTON_Y:
		return 'X';
	case KBCTRL_BUTTON_X:
		return DVL_VK_RETURN;
	case KBCTRL_BUTTON_LEFTSHOULDER:
		return 'C';
	case KBCTRL_BUTTON_RIGHTSHOULDER:
		return 'I';
	case KBCTRL_BUTTON_START:
		return DVL_VK_ESCAPE;
	case KBCTRL_BUTTON_BACK:
		return DVL_VK_TAB;
	case KBCTRL_BUTTON_DPAD_LEFT:
		return isModifier ? 'Q' : DVL_VK_LEFT;
	case KBCTRL_BUTTON_DPAD_RIGHT:
		return isModifier ? 'B' : DVL_VK_RIGHT;
	case KBCTRL_BUTTON_DPAD_UP:
		return DVL_VK_UP;
	case KBCTRL_BUTTON_DPAD_DOWN:
		return DVL_VK_DOWN;
	default:
		return 0;
	}
}

} // namespace dvl
#endif

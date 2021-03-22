#include "paths.h"

#include <SDL.h>

#ifdef USE_SDL1
#include "sdl2_to_1_2_backports.h"
#endif

namespace dvl {

namespace {

std::string *base_path = nullptr;
std::string *pref_path = nullptr;
std::string *config_path = nullptr;

void AddTrailingSlash(std::string *path) {
#ifdef _WIN32
	if (!path->empty() && path->back() != '\\')
		*path += '\\';
#else
	if (!path->empty() && path->back() != '/')
		*path += '/';
#endif
}

std::string *FromSDL(char *s) {
	auto *result = new std::string(s != nullptr ? s : "");
	if (s != nullptr) {
		SDL_free(s);
	} else {
		SDL_Log("%s", SDL_GetError());
		SDL_ClearError();
	}
	return result;
}

} // namespace

const std::string &GetBasePath()
{
#ifdef __vita__
	if (basePath == NULL) basePath = new std::string(GetPrefPath());
#else
	if (base_path == nullptr) base_path = FromSDL(SDL_GetBasePath());
#endif
	return *base_path;
}

const std::string &GetPrefPath()
{
	if (pref_path == nullptr) pref_path = FromSDL(SDL_GetPrefPath("diasurgical", "devilution"));
	return *pref_path;
}

const std::string &GetConfigPath()
{
	if (config_path == nullptr)
		config_path = FromSDL(SDL_GetPrefPath("diasurgical", "devilution"));
	return *config_path;
}

void SetBasePath(const char *path)
{
	if (base_path == nullptr) base_path = new std::string;
	*base_path = path;
	AddTrailingSlash(base_path);
}

void SetPrefPath(const char *path)
{
	if (pref_path == nullptr) pref_path = new std::string;
	*pref_path = path;
	AddTrailingSlash(pref_path);
}

void SetConfigPath(const char *path)
{
	if (config_path == nullptr)
		config_path = new std::string;
	*config_path = path;
	AddTrailingSlash(config_path);
}

} // namespace dvl

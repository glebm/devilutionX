set(ASAN OFF)
set(UBSAN OFF)
set(NONET ON)
set(USE_SDL1 ON)
set(SDL1_VIDEO_MODE_BPP 8)
set(SDL1_VIDEO_MODE_FLAGS SDL_HWSURFACE|SDL_DOUBLEBUF)
set(SDL1_FORCE_SVID_VIDEO_MODE ON)
set(TTF_FONT_NAME \"LiberationSerif-Bold.ttf\")
# Enable exception suport as they are used in dvlnet code
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fexceptions")
find_package(Freetype REQUIRED)
find_package(ZLIB REQUIRED)

# Do not warn about unknown attributes, such as [[nodiscard]].
# As this build uses an older compiler, there are lots of them.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-attributes")

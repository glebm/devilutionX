include(functions/FetchContent_MakeAvailableExcludeFromAll)
include(FetchContent)

if(DEVILUTIONX_STATIC_TSL_SPARSE_MAP)
  set(BUILD_SHARED_LIBS OFF)
else()
  set(BUILD_SHARED_LIBS ON)
endif()

FetchContent_Declare(tsl_sparse_map
    URL https://github.com/Tessil/sparse-map/archive/7ab2fa0fa210a46be069827c4e41ba676f3a6e98.tar.gz
    URL_HASH MD5=8c5fcc40cf1b737f60b1246fabc95cc4
)
FetchContent_MakeAvailableExcludeFromAll(tsl_sparse_map)

target_compile_definitions(sparse_map INTERFACE TSL_NO_EXCEPTIONS=1)

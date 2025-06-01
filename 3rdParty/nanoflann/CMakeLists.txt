include(functions/FetchContent_ExcludeFromAll_backport)
include(FetchContent)

set(BUILD_TESTING OFF)

FetchContent_Declare_ExcludeFromAll(nanoflann
  URL https://github.com/jlblancoc/nanoflann/archive/9a653cb243db6a09c94f833b28732b62f033e2b5.tar.gz
  URL_HASH MD5=b25249e7a4fbdc691a7c0125ae9a486d
)
FetchContent_MakeAvailable_ExcludeFromAll(nanoflann)

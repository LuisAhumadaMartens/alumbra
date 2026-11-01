include("${CMAKE_CURRENT_LIST_DIR}/x64-linux.cmake")

set(VCPKG_C_FLAGS
  "-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer")
set(VCPKG_CXX_FLAGS
  "-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer")
set(VCPKG_LINKER_FLAGS "-fsanitize=address,undefined")

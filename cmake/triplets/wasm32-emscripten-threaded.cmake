include("${VCPKG_ROOT_DIR}/triplets/community/wasm32-emscripten.cmake")

set(VCPKG_C_FLAGS "-msimd128 -pthread")
set(VCPKG_CXX_FLAGS "-msimd128 -pthread")
set(VCPKG_LINKER_FLAGS "-pthread")

# Fuzz tests

`error_detail_fuzz` exercises the bounded ErrorDetail payload decoder and the
shared URL/text redactor with Clang's libFuzzer, ASan and UBSan. The standalone
target does not change ordinary builds or add a dependency.

After configuring the debug preset, reuse its private dependency installation:

```sh
cmake -S test/fuzz -B build/error-fuzz -G Ninja \
  -DCMAKE_OSX_SYSROOT=macosx \
  -DALUMBRA_LIBFUZZER_LIBRARY=/opt/homebrew/opt/llvm/lib/clang/21/lib/darwin/libclang_rt.fuzzer_osx.a \
  -Dabsl_DIR="$PWD/build/debug/vcpkg_installed/arm64-osx/share/absl"
cmake --build build/error-fuzz
build/error-fuzz/error_detail_fuzz -runs=10000 -max_len=65537
```

Apple's Xcode distribution does not ship libFuzzer; the macOS example uses
Homebrew LLVM 21's libFuzzer driver with Xcode's compiler and ASan/UBSan runtime.
Use the path matching your installed LLVM version. On Linux use a
libFuzzer-enabled Clang and matching triplet and omit both macOS-specific
arguments. Persistent
corpora belong in a private build directory; never use real source credentials.

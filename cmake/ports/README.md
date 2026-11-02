# vcpkg overlay ports

`vcpkg.json` lists this directory under `overlay-ports`. Each port here is a copy of the port at the pinned
`builtin-baseline` with the smallest change that Alumbra needs, so a baseline bump must re-copy it.

## `sdl3`

Copy of vcpkg's `sdl3` 3.4.18. The only change is that the `x11` feature also sets `SDL_X11_XRANDR=ON`. The stock
port forces XRandR off, and without it SDL's X11 backend reports one generic display with no refresh rate, which the
desktop video output needs ([ADR-0030](../../docs/adr/0030-desktop-graphics-audio-and-ui-renderer.md)). Building it
needs the `libxrandr-dev` system package on Linux.

## `sdl3-shadercross`

Copy of vcpkg's `sdl3-shadercross` 3.0.0-preview2. The only change is that the `version>=` minimums on `directx-dxc`,
`sdl3` and `spirv-cross` are removed. vcpkg resolves a minimum by reading that older version from the vcpkg
repository's history, which the shallow checkout that CI uses does not have. The baseline's own versions of the
three ports (DXC 2026-05-27, SDL 3.4.18 and SPIRV-Cross 1.4.363.0) are newer than the removed minimums (2026-02-20,
3.2.16 and 1.4.309.0), so the dependency graph is the same. The port is a host tool on `x64-linux` only.

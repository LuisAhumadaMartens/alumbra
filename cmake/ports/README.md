# vcpkg overlay ports

`vcpkg.json` lists this directory under `overlay-ports`. Each port here is a copy of the port at the pinned
`builtin-baseline` with the smallest change that Alumbra needs, so a baseline bump must re-copy it.

## `sdl3`

Copy of vcpkg's `sdl3` 3.4.18. The only change is that the `x11` feature also sets `SDL_X11_XRANDR=ON`. The stock
port forces XRandR off, and without it SDL's X11 backend reports one generic display with no refresh rate, which the
desktop video output needs ([ADR-0030](../../docs/adr/0030-desktop-graphics-audio-and-ui-renderer.md)). Building it
needs the `libxrandr-dev` system package on Linux.

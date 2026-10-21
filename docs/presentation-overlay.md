# Optional presentation overlay

`PlatHooks.overlay` is an optional, platform-neutral RGBA presentation callback.
Initialize the complete `PlatHooks` structure to zero, then opt in with a callback
and optional `overlay_data`. Existing callers with no callback allocate and draw
no overlay. No SDL type or font/localization policy is exposed by `plat.h`.

The SDL3 backend composites in this order:

1. Clear the output to black.
2. Render the indexed game framebuffer into its calculated destination.
3. Render the output-resolution RGBA layer with straight-alpha blending.
4. Dim the game destination and draw the settings layer, when open.
5. Capture an optional window screenshot and present.

`PlatOverlay.rgba` contains tightly packed **R, G, B, A bytes**, top row first.
Width, height and pitch describe physical renderer output pixels. This is a
native-size texture, rendered without enlargement; a one-pixel stroke stays one
output pixel. Alpha zero is transparent, 255 opaque. The backend clears the buffer
before every callback. Return zero to skip composition even if pixels were written.

The backend owns the descriptor, pixels and texture. Write pixels only during the
callback; do not free, resize, retain or change their layout. Allocation and texture
recreation happen on output-size changes, including fullscreen and HiDPI changes.
Allocation/texture-setup failure logs once at that size and preserves the game;
the backend retries on a later size change. Hidden runs without a renderer,
minimized/occluded windows and zero-size outputs do not call the overlay.

`logical_width/height` describe the current scanout. `viewport_x/y/w/h` are the
same fractional destination used for the game, after aspect correction, integer
scaling and letterbox. Map logical points as follows:

```c
float output_x = f->viewport_x + game_x * f->viewport_w / f->logical_width;
float output_y = f->viewport_y + game_y * f->viewport_h / f->logical_height;
```

Choose rasterization rounding at the caller. Window-coordinate size is supplied
separately; it can differ from output size on HiDPI displays. Content may cover
letterbox regions if desired. The layer does not change input mapping, the VGA
scanout, indexed screenshots or replay state.

Callbacks, allocation, uploads and texture destruction run on the presentation
thread. Callback data must outlive `plat_run` (the browser loop's lifetime on Web).
For mutable game data, the caller must publish an immutable snapshot or synchronize
access; the API does not make unsynchronized shared state safe. A static demo
needs no game-thread reads. Web uses this same SDL3 path and renderer-free runs
skip it; native testing does not establish a Web build result.

SDL references: [output size](https://wiki.libsdl.org/SDL3/SDL_GetCurrentRenderOutputSize),
[RGBA byte format](https://wiki.libsdl.org/SDL3/SDL_PixelFormat),
[texture blending](https://wiki.libsdl.org/SDL3/SDL_SetTextureBlendMode).

## Standalone checks

With C11 Clang, Python and SDL3 development files on PATH:

```sh
python tools/overlaycheck.py
python tools/overlaycheck.py --sanitize --output build/overlaycheck-asan
```

No game, original executable or DOS toolchain is needed. The check compiles the
real SDL backend into a harness with synthetic scanout/settings callbacks. Software
render targets exercise independent output/window sizes (including 4x output
dimensions), integer/free scaling, aspect on/off, downscaling and scanout mode
changes. Actual host renderer checks exercise resize, fullscreen and return.
Assertions examine pixels before Present, since a swapped backbuffer is undefined.
They cover opaque and half-alpha pixels, an isolated single output pixel, logical
geometry, letterbox drawing, per-frame clearing, return-zero suppression, transparent
equivalence to no callback, and settings above the overlay. A separate `plat_run`
hidden case checks that no callback/allocation occurs. Settings are a synthetic
opaque fixture; real settings behavior should also be covered by integration tests.

Windows / Clang 23.1.3 / SDL3 3.4.18: hidden plus **26 composition scenarios PASS**,
also under AddressSanitizer and UndefinedBehaviorSanitizer. LeakSanitizer unavailable
on Windows. Synthetic HiDPI coordinates are covered; real cross-monitor DPI changes,
Linux, macOS and Web were not exercised for this change.

Baseline: `a34316463c230bafa3d4f0aa3b894a574e5fc738`.
Upstream mirror: `c54e00e9176c14d0913b5806318a30f85a8f4636`; its existing seven
tooling/profile backlog commits are not merged into this tested feature baseline.

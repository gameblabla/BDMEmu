# BDMEmu - Emulator for the Bandai Design Master 

This is a small C11 emulator for the Bandai Design Master / Denshi Mangajuku.
This is a touchscreen made by Bandai that competed with the Gameboy at the time with its own quirks.
It was pretty expensive and limited in what it can do, and the specs did not help.
The only saving grace is perhaps the decently capable 16-bits CPU but everything around it, makes the Gameboy looks next-gen.

The screen resolution of games, as it has been observed, is 160x120, not 160x150 as it has been widely reported.

It leveraged the H8-300 16-bits CPU interpreter from MAME and from there, bootstraped itself into supporting more of the hardware.
The result is that the "G" type cartridges are all playable.
Sound output is also supported, an auto calibration feature is also enabled by default so you don't have to go through the process yourself.
Due to the nature of the console, some kind of pointing device is required to make use of it.
Therefore, a gamepad is not officially supported.


# TODO

- Cycle-perfect H8/328/329 peripheral timing.
- Complete custom HG62G010 gate-array behavior.
- Confirmed media-cart CE/clock behavior.
- Exact nonvolatile drawing/media-cart RAM semantics.  External SRAM can now be loaded/saved, but retention, battery state, and media-destructive-use behavior are not fully characterized.
- Exact sound hardware.  The current implementation is a timer/PWM-style approximation because neither MAME nor the board notes identify a dedicated speaker device.
- Full H8 opcode coverage.

## Build

```sh
make -f Makefile.linux
```

The Linux makefile is named `Makefile.linux` so qmake can generate its own root `Makefile` without colliding with the hand-written build.

The core and headless frontend are C11 and have no external dependencies. SDL 1.2 and SDL3 frontends are optional and are built only when their development files are available, or when explicitly requested with `make -f Makefile.linux sdl` / `make -f Makefile.linux sdl3`. A browser/WASM frontend is built separately with `make -f Makefile.linux wasm`; it uses `clang --target=wasm32-unknown-unknown` and `wasm-ld` directly, not Emscripten.


## Browser / WASM frontend

The browser frontend is under `web/` and follows the same no-Emscripten model as the referenced web interface: a small JavaScript shell instantiates a raw WebAssembly module, copies local file bytes into the exported memory, drives one frame at a time, pulls framebuffer/audio buffers, and stores save states in browser storage.

Build it with clang/wasm and wasm-ld:

```sh
make -f Makefile.linux wasm
# or
make -f Makefile.wasm build
```

Serve it locally:

```sh
make -f Makefile.linux wasm-serve
# opens http://127.0.0.1:8008/ by default
```

The web frontend accepts one G cart, a G cart plus its M media cart, or a ZIP containing `.bin` files. It has both a combined/ZIP loader and separate G-cart / M-media-cart file fields. It auto-detects `[G.xx]` and `[M.xx]` names when present, pairs G/M carts by number when possible, and refuses to boot an M media cart by itself with a clear error. The browser loader also fetches `bdm_wasm_core.wasm` with a cache-busting build id and `cache: 'no-store'`; after replacing an older build, reload the page once so the matching JS/WASM pair is used.

The canvas is a direct touchscreen: pointer/touch events on the 160x120 LCD are converted to stylus coordinates without pointer lock or mouse grabbing. 

Browser and SDL frontends apply a one-cell frontend bias before ADC sampling; 

this compensates for the cartridge calibration math that otherwise reports the touched cell as the next pixel down/right. 

Brief taps are latched by the browser frontend long enough for the emulated ADC/calibration routine to sample them; 

the default hold is 20 ms and can be changed in the Controls panel. Auto calibration assist is enabled by default and can be disabled with the Controls-panel checkbox. 

Keyboard controls and emulator hotkeys are configurable in the browser menu. 

Defaults are `Z`/`X` for A/B, `Enter` for Start, `Right Shift` for Select, `R` reset, `F5` save state, `F8` load state, `F9` scale-mode cycle, and `F11` fullscreen.

## ROMs

ROMs are not included. They were however, all dumped and shared around.

Pass a raw Design Master G-cartridge dump as the first positional ROM path. 

## Provenance

Hardware facts and memory layout were taken from the MAME `src/mame/bandai/design_master.cpp` driver and Design Master software-list metadata. 

The CPU code was inspired by MAME's.

## SDL 1.2 frontend

```sh
make -f Makefile.linux sdl
```

## SDL 3 frontend

```sh
make -f Makefile.linux sdl3
```

SDL2 is not supported as newer platforms generally support SDL3 and older platforms would prefer SDL 1.2's software driven approach.

### Win32 / MinGW-w64

```sh
make -f Makefile.win32
```

This builds `bdm-win32.exe` with the deliberately small Win32 stack: GDI video, waveOut audio, WinMM joystick/gamepad polling, ordinary window-message keyboard input, and Win32 mouse/touch handling. It does not depend on SDL. The Makefile defaults to `i686-w64-mingw32-gcc` when GNU make would otherwise use its built-in host `cc`; override with `make -f Makefile.win32 CC=/path/to/i686-w64-mingw32-gcc` for non-standard toolchain names.

The Win32 build now targets a Windows 95-class dependency profile rather than the earlier Win32s/Windows 3.1 profile. The 32-bit target defines `BDM_WIN32_WIN95`, sets `WINVER=0x0400` / `_WIN32_WINDOWS=0x0400`, and links only the classic Win95-era UI/audio set: `comdlg32`, `user32`, `gdi32`, and `winmm` plus the C runtime selected by the toolchain. ANSI common file dialogs are restored for open/save actions. Win32 still does not use SDL, Raw Input, D3D, WASAPI, Shell32 command-line parsing, AppData/Shell folder APIs, multimon APIs, COM, registry helpers, or common-control status bars. The linker sets the Windows subsystem version to 4.0.

The Win32 frontend is now a native Windows UI rather than a command-line render window. The top-level frame owns a menu bar, recent-ROM submenu, accelerator keys, status line, and a separate LCD child view. File menu actions can open a program ROM, open program+media ROMs, replace the media ROM, reload the current pair, load/save state, quick-save/quick-load, save the LCD frame as PPM, and export captured WAV audio. Emulation, video, audio, and input menus expose pause/reset, load/reset automation toggles, scale/fullscreen, touch offsets including a reset command, touch hold time, optional crosshair cursor, and touch debug logging. The Auto-calibrate item now applies to soft reset as well as ROM load; `F10` toggles it. On Win32 the Audio menu intentionally contains only Enable audio, since the backend is fixed to waveOut when audio is enabled. The GDI video endpoint renders into a persistent off-screen compatible bitmap and then BitBlt-copies the completed frame into the LCD child window, with background erasure disabled on both the frame and LCD view to avoid visible clear/stretch flashing.

Runtime options accepted by the Win32 frontend include the same ROM/state/touch options as the SDL frontends plus:

```sh
--video gdi
--audio waveout|none
```

### Win64 / MinGW-w64

```sh
make -f Makefile.win64
```

This builds SDL3 from source first, using `scripts/build_sdl3_mingw.sh`, then links `bdm-win64.exe`. The Makefile resolves paths from its own location, so both `make -f Makefile.win64` from the repository root and direct `make -f src/Makefile.win64` use the same `scripts/` and `third_party/` directories. It defaults to `x86_64-w64-mingw32-gcc/g++/windres` unless those tools are explicitly overridden. The SDL3 source script expects the upstream SDL release tarball at:

```text
https://github.com/libsdl-org/SDL/releases/download/release-3.4.10/SDL3-3.4.10.tar.gz
```

If the tarball is not already present under `third_party/sdl/`, the script downloads it with `curl` or `wget`, configures SDL3 with CMake for static MinGW-w64 output, and installs it under `third_party/sdl/install/win64`.

The Win64 frontend is intentionally separate from the SDL3 renderer/audio frontend. It uses the same native Windows UI shell as Win32: menu bar, recent-ROM submenu, accelerators, status bar, open/reload ROM actions, save/load state actions, video/audio/input menus, and a separate LCD child view. It uses Win32 windowing and mouse/keyboard, SDL3 only for gamepad input, WASAPI with waveOut fallback for audio, and selectable audio/video endpoints. The Core Audio COM GUIDs are emitted in the WASAPI translation unit for MinGW-w64, so the Win64 link does not depend on fragile GUID import-library availability or ordering. Runtime options include:

```sh
--video d3d11|gdi
--audio wasapi|waveout|none
```

The native Windows UI also keeps the normal arrow cursor over the LCD by default; the large crosshair cursor is opt-in from Input > Use touch crosshair cursor. Ctrl+0 resets the touch offset to `(0,0)`.

The Win64 `--video d3d11` endpoint now uses a real D3D11 shader pipeline. The 160x120 LCD framebuffer is uploaded each frame as a dynamic `DXGI_FORMAT_B8G8R8A8_UNORM` texture, rendered through a point-filtered sampler onto a letterboxed triangle strip, and presented through the swap chain. Shader source is embedded in the C file and compiled at runtime through a dynamically loaded `d3dcompiler_47.dll`/older `d3dcompiler_*.dll` if available, so the executable does not link directly against `d3dcompiler`. If D3D11 or shader compilation is unavailable, the frontend falls back to the same double-buffered GDI path and reports `gdi` in the window title. Use `--video gdi` for the fully plain GDI path.

### Qt6 on Linux

```sh
qmake6 BDMEmu.pro
make
./BDMEmu game_g.bin game_m.bin
```

The Qt frontend is a thin C++/Qt6 UI shell over the C11 core and shared frontend helper. It provides menu-driven ROM loading, save/load state, reset, pause/resume, integer scaling, keyboard controls, direct pen input on the 160x120 LCD, a runtime `Auto-calibrate on load/reset` toggle (`F10`), and live audio output through the same SDL3 audio-stream queue strategy as the SDL3 frontend. The C emulator code remains usable without Qt.

Qt6 live audio requires SDL3 development files visible to `pkg-config` as `sdl3`. If SDL3 is present, qmake defines `BDM_QT_SDL3_AUDIO`, builds `src/qt/SdlAudio.cpp`, opens `SDL_OpenAudioDeviceStream`, queues mono signed 16-bit samples with `SDL_PutAudioStreamData`, and captures that same stream for `--dump-wav`. If SDL3 is not installed, qmake emits a warning and the Qt6 frontend builds without live audio; pass `CONFIG+=no_sdl3_audio` to make that fallback explicit.

Build a relocatable AppImage with:

```sh
make -f Makefile.linux appimage
# or
packaging/make_appimage.sh
```

The AppImage script performs an out-of-source Qt6 release build, stages an AppDir, then uses linuxdeploy and linuxdeploy-plugin-qt. 

Set `QMAKE=/path/to/qmake6` if qmake6 is not on PATH. Set `LINUXDEPLOY` and `LINUXDEPLOY_PLUGIN_QT` to pre-downloaded tools for offline packaging.
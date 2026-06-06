# Bandai Design Master bootstrap emulator

This is a small C11 emulator for the Bandai Design Master / Denshi Mangajuku that can run on the Web or natively for Qt6/Linux or Windows with Win32 backend.

Current scope:

- Hitachi H8/300-style 16-bit CPU interpreter with enough instructions to leave reset, run the common cartridge startup paths, execute the observed bank-switch thunk, service the timer interrupt path, read touch ADC data, and reach the Dragon Ball Z title/mode menu.
- MAME-derived base map: cartridge window at `0x0000-0x7fff`, external SRAM from `0x8000-0xfb7f`, H8-family internal RAM at `0xfb80-0xff7f`, and high I/O at `0xff80-0xffff`.
- Headless frontend.
- Opaque handles for core, video, input, and sound.
- Tentative H8 timer-derived sound backend with optional mono 16-bit PCM WAV recording.
- H8/3334 peripheral model improvements:
  - Timer16 register layout from MAME's H83337 map: `0xff90` TIER, `0xff91` TSR, `0xff92-0xff93` TCNT, `0xff94-0xff95` observed OCRA, and `0xff96` TCR.  The regular UI/startup interrupt now comes from the modeled counter/compare path rather than a separate artificial interval counter.
  - Timer8 channel register shadows and counters at `0xffc8-0xffcc` and `0xffd0-0xffd4`; these feed the sound backend and make timer status/counter reads deterministic.
  - SCI status reads at `0xff8c/0xffdc` return transmitter-ready state, which is closer to the H8 reset behavior than returning the raw erased I/O byte.
  - Port 6 data writes at `0xffbb` are tracked as resistive-panel drive state for ADC sampling.
- LCD model for the interface touched by the games:
  - `0xff80` is treated as LCD/gate-array register index.
  - `0xff81` is treated as LCD/gate-array register data.
  - The observed initialization writes register `0x12 = 0x14` and `0x14 = 0x77`, matching a 20-byte stride and 120 active scanlines.
  - The first `0x960` bytes of SRAM at `0x8000-0x895f` are mapped as a 160x120 1bpp framebuffer, MSB-first, and exported as a 160x120 frame.
- G-cart bank-latch model:
  - Writes to `0xff84` update ROM bank A15/A16 using the low two bits.
  - This matches the copied RAM thunk used by G.01/G.02/G.03: write bank, delay, then `JSR 0x0000`; nonzero banks contain `JMP` vectors at offset zero.
  - Bit 2 is reserved as a tentative media-cart select when `--media` is loaded.
- Deterministic port-7/gate-array input placeholder:
  - `0xffbe` returns port 7.
  - Bit 7 is kept high as the observed ready/handshake input.
  - Bit 4 defaults low as the active-low battery/sense line needed by the startup path.
  - The documented A/B/C/D/E menu strip and left/right page controls are modeled as touch-panel locations, not as controller-style P7 bits. Frontends map keyboard/gamepad buttons to stylus taps at those panel coordinates.
  - `--port7 HEX` remains available for exact override/fuzzing.
- H8 ADC model for the resistive touch panel:
  - `0xffe0-0xffe7` expose four 10-bit sample registers in H8 format.
  - `0xffe8` is ADCSR and starts a short delayed conversion when ADST is set.
  - `0xffe9` is ADCR.
  - Channels 0/1 model the panel vertical voltage; channels 2/3 model horizontal voltage. This channel order is required by the cartridge calibration math.
  - Port-6 drive values `0x0c`, `0x09`, and `0x06` now affect whether the sampled raw or complementary panel axis is returned, instead of ignoring the electrode drive state completely.
- Additional H8 coverage for the boot and graphics paths, including register-indirect `JSR @Rn`, memory/absolute bit operations, register `ADDX.B`, byte `DIVXU`, and corrected byte shift/rotate decoding for `SHLR`, `SHAR`, `ROTXL`, `ROTL`, `ROTXR`, and `ROTR`.
- Raw cartridge graphics preview mode for verifying that cartridge 1bpp art can pass through the LCD renderer.
- Whole-machine save states through the core API, shared by headless, SDL 1.2, SDL3, Qt6, Win32/Win64, and WASM.  State files include CPU registers/flags/PC, internal/external RAM, LCD registers/VRAM/framebuffer state, fixed-point input/touch state, H8 timer/ADC shadows, cart/media bank state, and sound timer/noise/phase state.  ROM bytes are not embedded, so load the same G/M/BIOS images before loading a state.  The save-state format is intentionally not backward compatible; after the panel-button rewrite, old `.bdmst` files are rejected instead of translated.

Not implemented yet:

- Cycle-perfect H8/328/329 peripheral timing.
- Complete custom HG62G010 gate-array behavior.
- Confirmed media-cart CE/clock behavior.
- Exact nonvolatile drawing/media-cart RAM semantics.  External SRAM can now be loaded/saved, but retention, battery state, and media-destructive-use behavior are not unknown.
- Full H8 opcode coverage.
- Compare this against real hardware in much more details than what i can do

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


## ROMs

ROMs are not included. Pass a raw Design Master G-cartridge dump as the first positional ROM path. Pass a matching raw Design/M-cartridge dump as the second positional path when the game expects its media/cart data. The older `--cart` and `--media` flags remain accepted as compatibility aliases. The optional `--bios` parameter accepts the internal H8 ROM dump if you have it, but the supplied G-cartridge dumps contain reset vectors and can boot without it.

## Provenance

Hardware facts and memory layout were taken from the MAME `src/mame/bandai/design_master.cpp` driver and Design Master software-list metadata. No MAME C++ CPU code is embedded here; the C11 interpreter is a compact implementation targeting the observed startup paths.

## SDL 1.2 frontend

The SDL frontend is kept separate from the headless backend.  The default `make -f Makefile.linux` target still builds `bdm_headless`; when `sdl-config` for SDL 1.2 is available, it also builds `bdm_sdl`.  To require the SDL frontend explicitly:

```sh
make -f Makefile.linux sdl
```

If SDL 1.2 is installed in a non-standard prefix, override the usual variables:

```sh
make -f Makefile.linux sdl SDL_CONFIG=/opt/sdl12/bin/sdl-config
# or
make -f Makefile.linux sdl SDL_CFLAGS="-I/path/to/SDL-1.2/include" SDL_LIBS="-L/path/to/lib -lSDL"
```

Run the interactive frontend with a game cart and, when applicable, the matching media cart:

```sh
./bdm_sdl \
  --cart "Dragon Ball Z Taisen-gata Search Battle [G.01] (Japan).bin" \
  --media "Dragon Ball Z [M.01] (Japan).bin"
```

Useful interactive options:

```sh
./bdm_sdl --cart game_g.bin game_m.bin --scale 4
./bdm_sdl --cart game_g.bin game_m.bin --fullscreen
./bdm_sdl --cart game_g.bin game_m.bin --no-audio
./bdm_sdl --cart game_g.bin game_m.bin --auto-title
./bdm_sdl --cart game_g.bin game_m.bin --auto-menu
./bdm_sdl --cart game_g.bin game_m.bin --dump-wav live_session.wav
./bdm_sdl --cart game_g.bin game_m.bin --touch-hold-ms 700 --touch-debug
./bdm_sdl --cart game_g.bin game_m.bin --no-auto-calibrate
./bdm_sdl --cart game_g.bin game_m.bin --load-sram ext.sram --save-sram ext.sram
./bdm_sdl --cart game_g.bin game_m.bin --state quick.bdmst
./bdm_sdl --cart game_g.bin game_m.bin --load-state menu.bdmst --save-state later.bdmst
```

SDL input mapping:

- Mouse left button maps to the resistive touchscreen/stylus. Window coordinates are divided by the selected integer scale and clipped to the active 160x120 touch/LCD area; optional touch offsets can still be adjusted for frontend calibration experiments. SDL clicks are latched for `--touch-hold-ms` milliseconds, default `20`, so a quick host mouse click is still visible long enough for normal ADC sampling. During visible firmware calibration targets, quick clicks are latched longer using `--calibration-touch-hold-ms`, default `500`, so manual calibration works even when auto-calibration is disabled. SDL 1.2 performs the startup calibration automatically by default; pass `--no-auto-calibrate` to do it manually. Press `F10` at runtime to toggle whether soft reset repeats the calibration assist. When the option is enabled, pressing `R` soft-resets the emulated unit and immediately reruns the same calibration input script; when disabled, reset leaves the firmware calibration screen for manual pen input. Use `--touch-debug` to print converted coordinates and deferred release timing.
- `A`/`B`/`C`/`D`/`E` map to the top menu strip buttons documented on the unit. `Z`/`X` remain aliases for menu A/B for older user muscle memory.
- `Left`/`Right` arrows map to the left/right page controls. `[`/`]` also map to left/right; `Backspace`/`Return` remain compatibility aliases.
- `R` resets the emulated machine.
- `F5` saves a whole-machine state to `--state` path, default `bdm_state.bdmst`.
- `F8` loads a whole-machine state from `--state` path.
- `Escape` or window close quits.


## Touch/ADC and auto-calibration behavior in this build

The browser, SDL 1.2, SDL3, Qt6, and native Windows frontends can skip the repetitive initial touchscreen calibration. SDL frontends do it by default during startup and now repeat it on soft reset when the option remains enabled; `F10` toggles this at runtime. The Qt6 and native Windows frontends expose the same option in the Emulation menu. The browser frontend exposes an Auto calibration assist checkbox and, when enabled, fast-forwards calibration immediately after ROM load or soft reset so it no longer waits for a click/focus event. The browser implementation now waits for the actual visible calibration target before injecting a touch, so a load/drop cannot miss the prompt by running too early. The injected events are ordinary stylus touches, not CPU/RAM/VRAM patches.

When auto-calibration is disabled, manual calibration targets are treated specially in the frontend input latch: a short host click on a visible calibration cross is held for 500 ms of emulated time by default, while normal drawing/menu touches still use the ordinary 20 ms latch. This matches the firmware polling cadence during calibration and fixes the previous failure where quick manual clicks were released before the cartridge sampled the ADC. Use `--calibration-touch-hold-ms N` to tune the calibration-only latch.


### Win32 / MinGW-w64

```sh
make -f Makefile.win32
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


### Qt6 on Linux

```sh
qmake6 BDMEmu.pro
make
./BDMEmu game_g.bin game_m.bin
```

The Qt frontend is a thin C++/Qt6 UI shell over the C11 core and shared frontend helper. It provides menu-driven ROM loading, save/load state, reset, pause/resume, integer scaling, keyboard controls, direct pen input on the 160x120 LCD, an optional visible A-E/page hardware-button panel from the Input menu, a runtime `Auto-calibrate on load/reset` toggle (`F10`), and live audio output through the same SDL3 audio-stream queue strategy as the SDL3 frontend. The C emulator code remains usable without Qt.

Qt6 live audio requires SDL3 development files visible to `pkg-config` as `sdl3`. If SDL3 is present, qmake defines `BDM_QT_SDL3_AUDIO`, builds `src/qt/SdlAudio.cpp`, opens `SDL_OpenAudioDeviceStream`, queues mono signed 16-bit samples with `SDL_PutAudioStreamData`, and captures that same stream for `--dump-wav`. If SDL3 is not installed, qmake emits a warning and the Qt6 frontend builds without live audio; pass `CONFIG+=no_sdl3_audio` to make that fallback explicit.

Build a relocatable AppImage with:

```sh
make -f Makefile.linux appimage
# or
packaging/make_appimage.sh
```

The AppImage script performs an out-of-source Qt6 release build, stages an AppDir, then uses linuxdeploy and linuxdeploy-plugin-qt. Set `QMAKE=/path/to/qmake6` if qmake6 is not on PATH. Set `LINUXDEPLOY` and `LINUXDEPLOY_PLUGIN_QT` to pre-downloaded tools for offline packaging.

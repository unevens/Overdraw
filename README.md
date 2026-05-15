# Overdraw — iPlug2 port

> This is the `iplug2-port` branch. It hosts an in-progress port of
> Overdraw onto the [iPlug2](https://github.com/iPlug2/iPlug2)
> framework, with a modernized GUI (GPU-accelerated via NanoVG / Metal)
> and CLAP support alongside AU + VST3.
>
> The shipping JUCE-based build lives on the `master` branch.

![Overdraw GUI](Images/screenshot.jpg?raw=true 'screenshot')

[Overdraw](https://www.unevens.net/overdraw.html) is an audio plug-in
that implements a waveshaper in which **the transfer function of
each channel is an automatable spline**.

## What's in this branch

This branch preserves the entire JUCE source tree (`Source/`,
`juicy/`) and replaces the original JUCE-based `CMakeLists.txt` with
an iPlug2-based one. The plug-in is rebuilt against iPlug2 in
`iplug/Overdraw.{h,cpp}`, but the heavy DSP is shared verbatim with
the JUCE side via `Source/OverdrawDsp.{h,cpp}` — there is exactly
one copy of the spline / waveshaper code.

Functional parity with the JUCE side: full DSP signal chain, all
params with their original names preserved, oversampling (linear
phase + minimum phase), 15-knot spline editor with the
positive+negative-quadrant transfer curves, per-channel symmetry
(folds the input via `abs()` and `sign_combine()` so negative-X
knots have no effect on a symmetric channel), the fixed origin
anchor (the curve always passes through (0, 0) so a zero input
produces a zero output — no DC offset), ShapeSymmetricSkew for
shaped knobs, ISender-driven VU meters, draggable knots,
in-editor tangent handles, double-click toggles (enabled / linked),
ghost-rendered disabled knots, live level dot per channel, side
panel that rebinds on knot selection, periodic redraw via `OnIdle`.

Two meters across the right edge — Input level and Output level —
both in M/S domain when M/S mode is on. There is no gain-reduction
meter on the Overdraw port: with a waveshaper the gain change is
implicit in the Output drop relative to the Input.

Shared UI plumbing for the two iPlug2-ported plug-ins
(Curvessor + Overdraw) lives in
[unevens/iplug-helpers](https://github.com/unevens/iplug-helpers),
included here as a submodule: the visual palette, the LCD-style
meter control with always-visible track-name labels, the templated
spline-editor control, the knob+label+caption cell layout helper,
the linkable-pair propagation logic, and the M/S-aware row-label
flipper.

## Build from source

Clone recursively (this branch pulls in iPlug2 + iplug-helpers in
addition to the JUCE-side submodules):

```
git clone --recursive --branch iplug2-port https://github.com/unevens/Overdraw
```

The build is driven by CMake (`CMakeLists.txt` at the repo root)
with presets in `CMakePresets.json`. The root CMake pulls iPlug2 in
from `./iPlug2`, runs the Homebrew-Clang-22 preflight from
`iplug-helpers/cmake/`, and then adds `iplug/` as the plug-in
subdirectory.

### macOS

Apple's bundled Clang (Xcode 17, Command Line Tools 21) and any
upstream Clang older than 22 hit a NEON-intrinsic codegen bug in
`Source/OverdrawDsp.cpp`. The fix shipped in upstream LLVM 22, so
build with **Homebrew Clang ≥ 22**:

```
brew install llvm

cmake --preset macos-ninja \
  -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++

cmake --build --preset macos-ninja
```

The top-level `CMakeLists.txt` enforces this — configuring with
Apple Clang or any clang older than 22 fails immediately with a
message pointing at the right compiler. The preflight ships in
`iplug-helpers/cmake/HomebrewClang22Preflight.cmake` so the JUCE
build and the iPlug2 build use the same enforcement logic.

This produces AU + VST3 + CLAP bundles in
`build/macos-ninja/iplug/out/`. The plug-ins are deployed
automatically to your user plug-in folders on a successful link:

- `~/Library/Audio/Plug-Ins/Components/Overdraw.component`
- `~/Library/Audio/Plug-Ins/VST3/Overdraw.vst3`
- `~/Library/Audio/Plug-Ins/CLAP/Overdraw.clap`

Universal arm64+x86_64 is on by default (see `UNIVERSAL` in
`CMakeLists.txt`); turn it off for fast single-arch dev iteration.

### Windows

The CMake presets include `windows-vs2022` and
`windows-vs2022-arm64ec`. Untested at this stage — the next milestone
after macOS polish is a Reaper smoke-test on Windows 11.

### Linux

Untested. Should work via iPlug2's cross-platform support; PRs
welcome.

## Submodules, libraries, credits

- [iPlug2](https://github.com/iPlug2/iPlug2) — the cross-platform
  audio plug-in framework. Pulled in at `./iPlug2`.
- [unevens/iplug-helpers](https://github.com/unevens/iplug-helpers)
  — shared UI plumbing for Curvessor + Overdraw iPlug2 ports. Pulled
  in at `./iplug-helpers`.
- [oversimple](https://github.com/unevens/oversimple) wraps two
  resampling libraries:
    - [HIIR](http://ldesoras.free.fr/prod.html) by Laurent de Soras
      — a 2× Upsampler/Downsampler with two-path polyphase IIR
      anti-aliasing filtering.
    - [r8brain-free-src](https://github.com/avaneev/r8brain-free-src)
      by Aleksey Vaneev — a high-quality pro audio sample rate
      converter / resampler C++ library.
- [audio-dsp](https://github.com/unevens/audio-dsp), my toolbox for
  audio DSP and SIMD instructions, built on Agner Fog's
  [vectorclass](https://github.com/vectorclass/version2).
- The JUCE-side editor (`Source/PluginProcessor.{h,cpp}`,
  `Source/PluginEditor.{h,cpp}`, the whole `juicy/` tree) is
  preserved on this branch so the shared DSP TU stays buildable
  alongside the JUCE master branch, but only the JUCE-free pieces
  (`Source/OverdrawDsp.{h,cpp}` and `juicy/SplineEditorDsp.hpp`) are
  compiled into the iPlug2 build.

## License

Overdraw is released under the GNU GPLv3 license. Full license text
and third-party notices are in the [legal/](legal/) folder.

VST is a trademark of Steinberg Media Technologies GmbH, registered
in Europe and other countries.

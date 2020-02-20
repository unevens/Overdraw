# Overdraw

![Overdraw GUI](screenshot.jpg?raw=true 'Overdraw')

[Overdraw](https://www.unevens.net/Overdraw.html) is an audio plug-in that implements a waveshaper in which the user can draw **the response curves of each channel using splines**.

## Features

- The response curves are smoothly automatable splines.
- Mid/Side Stereo.
- All parameters, and all splines, can have different values on the Left channel and on the Right channel - or on the Mid channel and on the Side channel, when in Mid/Side Stero Mode.
- Dry-Wet.
- Optional DC offset.
- Optional DC removal filter.
- Optional symmtric spline editing.
- Up to 32x Oversampling with either Minimum or Linear Phase.

## Build

Clone with

`git clone --recursive https://github.com/unevens/Overdraw`

Overdraw uses the [JUCE](https://github.com/WeAreROLI/JUCE) cross-platform C++ framework.

You'll need [Projucer](https://shop.juce.com/get-juce) to open the file `Overdraw.jucer` and generate the platform specific builds.

## Supported platforms

Overdraw is developed and tested on Windows and Ubuntu. It should work also on macOS, but I can neither confirm nor deny...

VST and VST3 binaries are available at https://www.unevens.net/Overdraw.html.

## Submodules, libraries, credits

- The [oversimple](https://github.com/unevens/hiir) submodule is a wrapper around two oversampling libraries: 
- [HIIR](https://github.com/unevens/hiir), my fork of the HIIR library by Laurent de Soras, a 2x Upsampler/Downsampler with two-path polyphase IIR anti-aliasing filtering. My fork adds support for double precision floating-point numbers, and AVX instructions. 
- [r8brain](https://github.com/unevens/r8brain/tree/include) my fork of [Aleksey Vaneev's linear phase resampling library](https://github.com/avaneev/r8brain-free-src), with added support for [pffft-double](https://github.com/unevens/pffft), my fork of Julien Pommier's PFFFT - a pretty fast FFT - with added support for double precision floating-point numbers using AVX instructions.
- [avec](https://github.com/unevens/avec) My toolbox/stash/library for SIMD audio, which uses Agner Fog's [vectorclass](https://github.com/vectorclass/version2) and [Boost.Align](https://www.boost.org/doc/libs/1_71_0/doc/html/align.html).

Overdraw is released under the GNU GPLv3 license.

VST is a trademark of Steinberg Media Technologies GmbH, registered in Europe and other countries.

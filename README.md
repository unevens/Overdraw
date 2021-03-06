# Overdraw

![Overdraw GUI](Images/screenshot.jpg?raw=true 'screenshot')

[Overdraw](https://www.unevens.net/overdraw.html) is an audio plug-in that implements a waveshaper in which **the transfer function of each channel is an automatable spline**.

## Features

- The transfer functions are smoothly automatable splines.
- Optional Mid/Side Stereo processing.
- All parameters, and all splines, can have different values on the Left channel and on the Right channel - or on the Mid channel and on the Side channel, when in Mid/Side Stero Mode.
- Dry-Wet.
- Up to 32x Oversampling with either Minimum Phase or Linear Phase Antialiasing.
- VU meter showing the difference between the input level and the output level.
- Customizable smoothing time, used to avoid zips when automating the knots of the splines, the wet amount, or the input and output gains.

## Build

Clone with

`git clone --recursive https://github.com/unevens/Overdraw`

Overdraw uses the [JUCE](https://github.com/WeAreROLI/JUCE) cross-platform C++ framework.

You'll need [Projucer](https://shop.juce.com/get-juce) to open the file `Overdraw.jucer` and generate the platform specific builds.

## Supported platforms

Overdraw is developed and tested on Windows and Ubuntu. It may also work on macOS, but I can neither confirm nor deny.

VST and VST3 binaries are available at https://www.unevens.net/overdraw.html.

## Submodules, libraries, credits

- The [oversimple](https://github.com/unevens/hiir) submodule is a wrapper around two resampling libraries:
    - [HIIR](https://github.com/unevens/hiir) library by Laurent de Soras, *"a 2x Upsampler/Downsampler with two-path polyphase IIR anti-aliasing filtering"*.
    - [r8brain-free-src](https://github.com/avaneev/r8brain-free-src), *"an high-quality pro audio sample rate converter / resampler C++ library"* by Aleksey Vaneev.
- [audio-dsp](https://github.com/unevens/audio-dsp), my toolbox for audio dsp and SIMD instructions, which uses Agner Fog's [vectorclass](https://github.com/vectorclass/version2) and [Boost.Align](https://www.boost.org/doc/libs/1_71_0/doc/html/align.html).

Overdraw is released under the GNU GPLv3 license.

VST is a trademark of Steinberg Media Technologies GmbH, registered in Europe and other countries.

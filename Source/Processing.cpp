/*
Copyright 2020 Dario Mambro

This file is part of Overdraw.

Overdraw is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Overdraw is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Overdraw.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "PluginProcessor.h"
#include "avec/dsp/SimpleHighPassMacro.hpp"
#include "avec/dsp/SplineMacro.hpp"

static void
leftRightToMidSide(double** io, int const n)
{
  for (int i = 0; i < n; ++i) {
    double m = 0.5 * (io[0][i] + io[1][i]);
    double s = 0.5 * (io[0][i] - io[1][i]);
    io[0][i] = m;
    io[1][i] = s;
  }
}

static void
midSideToLeftRight(double** io, int const n)
{
  for (int i = 0; i < n; ++i) {
    double l = io[0][i] + io[1][i];
    double r = io[0][i] - io[1][i];
    io[0][i] = l;
    io[1][i] = r;
  }
}

static void
applyGain(double** io,
          double* gain_target,
          double* gain_state,
          double const alpha,
          int const n)
{
  for (int c = 0; c < 2; ++c) {
    for (int i = 0; i < n; ++i) {
      gain_state[c] = gain_target[c] + alpha * (gain_state[c] - gain_target[c]);
      io[c][i] *= gain_state[c];
    }
  }
}

void
OverdrawAudioProcessor::applyFilter(
  VecBuffer<Vec2d>& io,
  avec::SplineInterface<Vec2d>* spline,
  avec::SplineAutomatorInterface<Vec2d>* splineAutomator)
{
  int const numIterations = 2;

  int const numActiveKnots = spline->getNumKnots();
  LOAD_SPLINE_STATE(spline, numActiveKnots, Vec2d, maxNumKnots);
  LOAD_SPLINE_AUTOMATOR(splineAutomator, numActiveKnots, Vec2d, maxNumKnots);
  LOAD_SPLINE_SYMMETRY(spline, Vec2d);

  filter->processBlock(
    io,
    io,
    numIterations,
    [&](Vec2d x) {
      Vec2d out;
      COMPUTE_SPLINE_WITH_SYMMETRY(spline, numActiveKnots, Vec2d, x, out);
      return out;
    },
    [&](Vec2d x) {
      Vec2d out;
      COMPUTE_SPLINE_WITH_SYMMETRY(spline, numActiveKnots, Vec2d, x, out);
      return select(x != 0.0, out / x, 1.0);
    },
    [&](Vec2d x, Vec2d& out, Vec2d& delta) {
      COMPUTE_SPLINE_WITH_SYMMETRY_WITH_DERIVATIVE(
        spline, numActiveKnots, Vec2d, x, out, delta);
    },
    [&]() {
      SPILINE_AUTOMATION(spline, splineAutomator, numActiveKnots, Vec2d);
    });
  STORE_SPLINE_STATE(spline, numActiveKnots);

  // filter->processBlock(
  //  io,
  //  io,
  //  numIterations,
  //  [&](Vec2d in) { return tanh(in); },
  //  [&](Vec2d in) { return select(in == 0.0, 1.0, tanh(in) / in); },
  //  [&](Vec2d in, Vec2d& out, Vec2d& delta) {
  //    out = tanh(in);
  //    delta = 1.0 - out * out;
  //  },
  //  [&]() {});
}

void
OverdrawAudioProcessor::processBlock(AudioBuffer<double>& buffer,
                                     MidiBuffer& midi)
{
  constexpr double ln10 = 2.30258509299404568402;
  constexpr double db_to_lin = ln10 / 20.0;

  ScopedNoDenormals noDenormals;

  auto const totalNumInputChannels = getTotalNumInputChannels();
  auto const totalNumOutputChannels = getTotalNumOutputChannels();
  auto const numSamples = buffer.getNumSamples();

  double* ioAudio[2] = { buffer.getWritePointer(0), buffer.getWritePointer(1) };

  // get the oversampling processors

  oversamplingGetter.update();
  auto& oversampling = oversamplingGetter.get();

  bool const isMidSideEnabled = parameters.midSide->get();

  auto [spline, splineAutomator] = parameters.spline->updateSpline(splines);

  double const smoothingTime = 0.001 * parameters.smoothingTime->get();

  double const invSampleRate = 1.0 / getSampleRate();

  double const automationAlpha =
    smoothingTime == 0.0
      ? 0.0
      : exp(-MathConstants<double>::twoPi * invSampleRate / smoothingTime);

  double const invUpsampledSampleRate = invSampleRate / oversampling.getRate();

  double const upsampledAutomationAlpha =
    smoothingTime == 0.0 ? 0.0
                         : exp(-MathConstants<double>::twoPi *
                               invUpsampledSampleRate / smoothingTime);

  if (splineAutomator) {
    splineAutomator->setSmoothingAlpha(upsampledAutomationAlpha);
  }

  double gainTarget[2][2];

  std::array<FilterType, 2> const filterType = {
    static_cast<FilterType>(parameters.filter.get(0)->getIndex()),
    static_cast<FilterType>(parameters.filter.get(1)->getIndex())
  };

  if (lastFilterType[0] != filterType[0] ||
      lastFilterType[1] != filterType[1]) {
    lastFilterType[0] = filterType[0];
    lastFilterType[1] = filterType[1];
    reset();
  }

  for (int c = 0; c < 2; ++c) {

    filter->setOutput(static_cast<avec::StateVariable<Vec2d>::Output>(
                        static_cast<int>(filterType[c]) - 1),
                      c);

    for (int i = 0; i < 2; ++i) {
      gainTarget[i][c] = exp(db_to_lin * parameters.gain[i].get(c)->get());
    }

    if (spline) {
      spline->setIsSymmetric(parameters.symmetry.get(c)->getValue() ? 1.0 : 0.0,
                             c);
    }

    auto const frequency =
      invUpsampledSampleRate * parameters.frequency.get(c)->get();

    if (filterType[c] == FilterType::normalizedBandPass) {

      filter->setupNormalizedBandPass(
        parameters.bandwidth.get(c)->get(), frequency, c);
    }
    else {
      filter->setFrequency(frequency, c);
      filter->setResonance(parameters.resonance.get(c)->get(), c);
    }

    filter->setSmoothingAlpha(upsampledAutomationAlpha);
  }

  // mid side

  if (isMidSideEnabled) {
    leftRightToMidSide(ioAudio, numSamples);
  }

  // input gain

  applyGain(ioAudio, gainTarget[0], gain[0], automationAlpha, numSamples);

  // oversampling

  oversampling.prepareBuffers(numSamples); // extra safety measure

  int const numUpsampledSamples =
    oversampling.scalarToVecUpsamplers[0]->processBlock(ioAudio, 2, numSamples);

  if (numUpsampledSamples == 0) {
    for (auto i = 0; i < totalNumOutputChannels; ++i) {
      buffer.clear(i, 0, numSamples);
    }
    return;
  }

  auto& upsampledBuffer = oversampling.scalarToVecUpsamplers[0]->getOutput();
  auto& upsampledIo = upsampledBuffer.getBuffer2(0);

  // waveshaping

  bool const isStaticWaveShaperNeeded =
    std::any_of(filterType.begin(), filterType.end(), [](auto& f) {
      return f == FilterType::waveShaper;
    });

  bool const isFilterNeeded =
    std::any_of(filterType.begin(), filterType.end(), [](auto& f) {
      return f != FilterType::waveShaper;
    });

  if (isFilterNeeded && isStaticWaveShaperNeeded) {
    auto& mixingBuffer = oversampling.interleavedBuffers[0].getBuffer2(0);

    spline->processBlock(upsampledIo, mixingBuffer, splineAutomator);
    applyFilter(upsampledIo, spline, splineAutomator);

    Vec2db isWaveShaper = Vec2db(filterType[0] == FilterType::waveShaper,
                                 filterType[1] == FilterType::waveShaper);

    for (int i = 0; i < numUpsampledSamples; ++i) {
      upsampledIo[i] = select(isWaveShaper, mixingBuffer[i], upsampledIo[i]);
    }
  }
  else if (isFilterNeeded) {
    applyFilter(upsampledIo, spline, splineAutomator);
  }
  else { // just the static waveshaping
    spline->processBlock(upsampledIo, upsampledIo, splineAutomator);
  }

  // downsample

  oversampling.vecToScalarDownsamplers[0]->processBlock(
    upsampledBuffer, ioAudio, 2, numSamples);

  applyGain(ioAudio, gainTarget[1], gain[1], automationAlpha, numSamples);

  if (isMidSideEnabled) {
    midSideToLeftRight(ioAudio, numSamples);
  }
}

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

  double const frequencyCoef = MathConstants<double>::twoPi / getSampleRate();
  double const upsampledFrequencyCoef = frequencyCoef / oversampling.getRate();

  double const automationAlpha =
    smoothingTime == 0.0 ? 0.0 : exp(-frequencyCoef / smoothingTime);

  double const upsampledAutomationAlpha =
    smoothingTime == 0.0 ? 0.0 : exp(-upsampledFrequencyCoef / smoothingTime);

  if (splineAutomator) {
    splineAutomator->setSmoothingAlpha(upsampledAutomationAlpha);
  }

  double gainTarget[2][2];

  FilterType const filter[2] = {
    static_cast<FilterType>(parameters.filter[0]->getIndex()),
    static_cast<FilterType>(parameters.filter[1]->getIndex())
  };

  for (int c = 0; c < 2; ++c) {

    for (int i = 0; i < 2; ++i) {
      gainTarget[i][c] = exp(db_to_lin * parameters.gain[i].get(c)->get());
    }

    if (spline) {
      spline->setIsSymmetric(parameters.symmetry.get(c)->getValue() ? 1.0 : 0.0,
                             c);
    }

    for (int i = 0; i < 2; ++i) {

      auto const cutoff = frequencyCoef * parameters.cutoff[i].get(c)->get();

      switch (filter[i]) {

        case FilterType::lowPass6dB:
        case FilterType::highPass6dB:
          onePole[i]->setFrequency(cutoff, c);
          onePole[i]->setSmoothingAlpha(automationAlpha);
          break;

        case FilterType::bandPass12dB:
          svf[i]->setBandPass(parameters.bandwidth[i].get(c)->get(), cutoff, c);
          svf[i]->setSmoothingAlpha(automationAlpha);
          break;

        case FilterType::highPass12dB:
        case FilterType::lowPass12dB:
          svf[i]->setFrequency(cutoff, c);
          svf[i]->setResonance(parameters.resonance[i].get(c)->get(), c);
          svf[i]->setSmoothingAlpha(automationAlpha);
          break;

        case FilterType::none:
          break;

        default:
          assert(false);
      }
    }
  }

  // mid side

  if (isMidSideEnabled) {
    leftRightToMidSide(ioAudio, numSamples);
  }

  // input gain

  applyGain(ioAudio, gainTarget[0], gain[0], automationAlpha, numSamples);

  // input filter

  interleavedInput.interleave(ioAudio, 2, numSamples);
  auto& in = interleavedInput.getBuffer2(0);

  switch (filter[0]) {

    case FilterType::lowPass6dB:
      onePole[0]->lowPass(in, in);
      break;

    case FilterType::highPass6dB:
      onePole[0]->highPass(in, in);
      break;

    case FilterType::bandPass12dB:
      svf[0]->bandPass(in, in);
      break;

    case FilterType::lowPass12dB:
      svf[0]->lowPass(in, in);
      break;

    case FilterType::highPass12dB:
      svf[0]->highPass(in, in);
      break;

    case FilterType::none:
      break;

    default:
      assert(false);
  }

  // oversampling

  oversampling.prepareBuffers(numSamples); // extra safety measure

  int const numUpsampledSamples =
    oversampling.vecToVecUpsamplers[0]->processBlock(
      interleavedInput, 2, numSamples);

  if (numUpsampledSamples == 0) {
    for (auto i = 0; i < totalNumOutputChannels; ++i) {
      buffer.clear(i, 0, numSamples);
    }
    return;
  }

  auto& upsampledBuffer = oversampling.vecToVecUpsamplers[0]->getOutput();
  auto& upsampledIo = upsampledBuffer.getBuffer2(0);

  // processing
  spline->processBlock(upsampledIo, upsampledIo, splineAutomator);

  // downsample

  oversampling.vecToVecDownsamplers[0]->processBlock(
    upsampledBuffer, 2, numSamples);

  auto& downsampled = oversampling.vecToVecDownsamplers[0]->getOutput();
  auto& out = downsampled.getBuffer2(0);

  switch (filter[1]) {

    case FilterType::lowPass6dB:
      onePole[1]->lowPass(out, out);
      break;

    case FilterType::highPass6dB:
      onePole[1]->highPass(out, out);
      break;

    case FilterType::bandPass12dB:
      svf[1]->bandPass(out, out);
      break;

    case FilterType::lowPass12dB:
      svf[1]->lowPass(out, out);
      break;

    case FilterType::highPass12dB:
      svf[1]->highPass(out, out);
      break;

    case FilterType::none:
      break;

    default:
      assert(false);
  }

  downsampled.deinterleave(ioAudio, 2, numSamples);

  applyGain(ioAudio, gainTarget[1], gain[1], automationAlpha, numSamples);

  if (isMidSideEnabled) {
    midSideToLeftRight(ioAudio, numSamples);
  }
}

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

  auto spline = parameters.spline->updateSpline(splines);

  if (parameters.spline->needsReset()) {
    spline->reset();
  }

  double const frequencyCoef =
    MathConstants<double>::twoPi / (getSampleRate() * oversampling.getRate());

  double inputGainTarget[2];
  double outputGainTarget[2];

  for (int c = 0; c < 2; ++c) {

    outputGainTarget[c] = exp(db_to_lin * parameters.outputGain.get(c)->get());
    inputGainTarget[c] = exp(db_to_lin * parameters.inputGain.get(c)->get());

    spline->setIsSymmetric(
      parameters.waveShaper.symmetry.get(c)->getValue() ? 1.0 : 0.0, c);
    spline->setDc(parameters.waveShaper.dc.get(c)->get(), c);
    spline->setWet(0.01f * parameters.waveShaper.dryWet.get(c)->get(), c);
    spline->setHighPassFrequency(
      frequencyCoef * parameters.waveShaper.dcCutoff.get(c)->get(), c);
  }

  double const automationAlpha = exp(-frequencyCoef / (0.001 * automationTime));

  spline->setSmoothingFrequency(automationAlpha);

  // mid side

  if (isMidSideEnabled) {
    leftRightToMidSide(ioAudio, numSamples);
  }

  // input gain

  applyGain(ioAudio, inputGainTarget, inputGain, automationAlpha, numSamples);

  // early return if no nodes are active

  if (!spline) {
    // output gain

    applyGain(
      ioAudio, outputGainTarget, outputGain, automationAlpha, numSamples);

    // mid side

    if (isMidSideEnabled) {
      midSideToLeftRight(ioAudio, numSamples);
    }

    return;
  }

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
  auto& upsampled_io = upsampledBuffer.getBuffer2(0);

  // processing

  spline->processBlock(upsampled_io, upsampled_io);

  // downsample

  oversampling.vecToScalarDownsamplers[0]->processBlock(
    upsampledBuffer, ioAudio, 2, numSamples);

  // output gain

  applyGain(ioAudio, outputGainTarget, outputGain, automationAlpha, numSamples);

  // mid side

  if (isMidSideEnabled) {
    midSideToLeftRight(ioAudio, numSamples);
  }
}

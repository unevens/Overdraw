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

  if (spline && parameters.spline->needsReset()) {
    spline->reset();
  }

  double inputGainTarget[2];
  double outputGainTarget[2];

  for (int c = 0; c < 2; ++c) {

    outputGainTarget[c] = exp(db_to_lin * parameters.outputGain.get(c)->get());
    inputGainTarget[c] = exp(db_to_lin * parameters.inputGain.get(c)->get());

    if (spline) {
      spline->setIsSymmetric(parameters.symmetry.get(c)->getValue() ? 1.0 : 0.0,
                             c);
    }
  }

  float const smoothingTime = 0.001 * parameters.smoothingTime->get();

  double const frequencyCoef = MathConstants<double>::twoPi / getSampleRate();

  double const automationAlpha =
    smoothingTime == 0.f
      ? 0.f
      : exp(-frequencyCoef / (smoothingTime * oversampling.getRate()));

  // mid side

  if (isMidSideEnabled) {
    leftRightToMidSide(ioAudio, numSamples);
  }

  // input gain

  applyGain(ioAudio, inputGainTarget, inputGain, automationAlpha, numSamples);

  // early return if no knots are active

  if (!spline) {

    // mid side

    if (isMidSideEnabled) {
      midSideToLeftRight(ioAudio, numSamples);
    }

    // input gain

    applyGain(
      ioAudio, outputGainTarget, outputGain, automationAlpha, numSamples);

    return;
  }

  spline->setSmoothingFrequency(automationAlpha);

  // oversampling

  oversampling.prepareBuffers(numSamples); // extra safety measure

  interleavedInput.interleave(ioAudio, 2, numSamples);

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
  auto& upsampled_io = upsampledBuffer.getBuffer2(0);

  // processing

  spline->processBlock(upsampled_io, upsampled_io);

  // downsample

  oversampling.vecToVecDownsamplers[0]->processBlock(
    upsampledBuffer, 2, numSamples);

  auto& downsampled = oversampling.vecToVecDownsamplers[0]->getOutput();

  // output gain, dry/wet and high pass

  Vec2d out_gain = Vec2d().load(outputGain);
  Vec2d out_gain_target = Vec2d().load(outputGainTarget);

  double wetTarget[2] = { parameters.dryWet.get(0)->get(),
                          parameters.dryWet.get(1)->get() };
  Vec2d wet_amount_target = Vec2d().load(wetTarget);
  Vec2d wet_amount = Vec2d().load(dryWet);

  float highPassCutoff[2] = { parameters.highPassCutoff.get(0)->get(),
                              parameters.highPassCutoff.get(1)->get() };

  highPass->setHighPassFrequency(frequencyCoef * highPassCutoff[0], 0);
  highPass->setHighPassFrequency(frequencyCoef * highPassCutoff[1], 1);

  Vec2d hp_in_mem = Vec2d().load_a(highPass->inputMemory);
  Vec2d hp_out_mem = Vec2d().load_a(highPass->outputMemory);
  Vec2d hp_alpha = Vec2d().load_a(highPass->alpha);

  auto& dryBuffer = interleavedInput.getBuffer2(0);
  auto& wetBuffer = downsampled.getBuffer2(0);

  Vec2d alpha = automationAlpha;

  for (int i = 0; i < numSamples; ++i) {
    Vec2d wet = wetBuffer[i];
    Vec2d const dry = dryBuffer[i];

    out_gain = out_gain_target + alpha * (out_gain - out_gain_target);
    wet *= out_gain;

    hp_out_mem = hp_alpha * (hp_out_mem + wet - hp_in_mem);
    hp_in_mem = wet;
    wet = hp_out_mem;

    wet_amount = wet_amount_target + alpha * (wet_amount - wet_amount_target);
    wet = dry + wet_amount * (wet - dry);
    wetBuffer[i] = wet;
  }

  downsampled.deinterleave(ioAudio, 2, numSamples);

  if (isMidSideEnabled) {
    midSideToLeftRight(ioAudio, numSamples);
  }
}

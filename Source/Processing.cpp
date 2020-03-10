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
#include "adsp/SplineMacro.hpp"

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

static inline Vec2d
toDB(Vec2d linear)
{
  return (10.0 / 2.30258509299404568402) *
         log(linear + std::numeric_limits<float>::min());
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

  bool const isMidSideEnabled = parameters.midSide->get();

  auto [spline, splineAutomator] = parameters.spline->updateSpline(splines);

  double const smoothingTime = 0.001 * parameters.smoothingTime->get();

  double const invSampleRate = 1.0 / getSampleRate();

  double const automationAlpha =
    smoothingTime == 0.0
      ? 0.0
      : exp(-MathConstants<double>::twoPi * invSampleRate / smoothingTime);

  double const invUpsampledSampleRate = invSampleRate / oversampling->getRate();

  double const upsampledAutomationAlpha =
    smoothingTime == 0.0 ? 0.0
                         : exp(-MathConstants<double>::twoPi *
                               invUpsampledSampleRate / smoothingTime);

  if (splineAutomator) {
    splineAutomator->setSmoothingAlpha(upsampledAutomationAlpha);
  }

  double gainTarget[2][2];
  double wetAmountTarget[2];

  for (int c = 0; c < 2; ++c) {

    wetAmountTarget[c] = 0.01 * parameters.wet.get(c)->get();

    for (int i = 0; i < 2; ++i) {
      gainTarget[i][c] = exp(db_to_lin * parameters.gain[i].get(c)->get());
    }

    if (spline) {
      spline->setIsSymmetric(parameters.symmetry.get(c)->getValue() ? 1.0 : 0.0,
                             c);
    }
  }

  bool const isWetPassNeeded = [&] {
    double m =
      wetAmountTarget[0] * wetAmountTarget[1] * wetAmount[0] * wetAmount[1];
    if (m == 1.0) {
      return false;
    }
    if (m == 0.0) {
      return !(wetAmountTarget[0] == 0.0 && wetAmountTarget[1] == 0.0 &&
               wetAmount[0] == 0.0 && wetAmount[1] == 0.0);
    }
    return true;
  }();

  bool const isBypassing = !isWetPassNeeded && (wetAmount[0] == 0.0);

  // mid side

  if (isMidSideEnabled) {
    leftRightToMidSide(ioAudio, numSamples);
  }

  // copy the dry signal

  dryBuffer.setNumSamples(numSamples);

  for (int c = 0; c < 2; ++c) {
    std::copy(ioAudio[c], ioAudio[c] + numSamples, dryBuffer.get()[c]);
  }

  // input gain

  applyGain(ioAudio, gainTarget[0], gain[0], automationAlpha, numSamples);

  // oversampling

  oversampling->prepareBuffers(numSamples); // extra safety measure

  int const numUpsampledSamples =
    oversampling->scalarToVecUpsamplers[0]->processBlock(
      ioAudio, 2, numSamples);

  oversampling->scalarToVecUpsamplers[1]->processBlock(
    dryBuffer.get(), 2, numSamples);

  if (numUpsampledSamples == 0) {
    for (auto i = 0; i < totalNumOutputChannels; ++i) {
      buffer.clear(i, 0, numSamples);
    }
    return;
  }

  auto& upsampledBuffer = oversampling->scalarToVecUpsamplers[0]->getOutput();
  auto& upsampledIo = upsampledBuffer.getBuffer2(0);

  // waveshaping

  if (spline && !isBypassing) {
    spline->processBlock(upsampledIo, upsampledIo, splineAutomator);
  }

  // downsampling

  oversampling->vecToVecDownsamplers[0]->processBlock(
    upsampledBuffer, 2, numUpsampledSamples, numSamples);

  oversampling->vecToVecDownsamplers[1]->processBlock(
    oversampling->scalarToVecUpsamplers[1]->getOutput(),
    2,
    numUpsampledSamples,
    numSamples);

  // dry-wet, output gain and vu meter

  auto& wetOutput = oversampling->vecToVecDownsamplers[0]->getOutput();
  auto& dryOutput = oversampling->vecToVecDownsamplers[1]->getOutput();

  constexpr double vuMeterFrequency = 10.0;

  Vec2d vuMeterAlpha =
    exp(-MathConstants<double>::twoPi * invSampleRate * vuMeterFrequency);

  auto vuMeterDryBuffer = vuMeterBuffer[0];
  auto vuMeterWetBuffer = vuMeterBuffer[1];
  auto vuMeterBufferDB = vuMeterBuffer[2];

  if (isWetPassNeeded) {

    auto& dryBuffer = dryOutput.getBuffer2(0);
    auto& wetBuffer = wetOutput.getBuffer2(0);

    Vec2d alpha = automationAlpha;

    Vec2d amount = Vec2d().load(wetAmount);
    Vec2d amountTarget = Vec2d().load(wetAmountTarget);

    Vec2d outputGain = Vec2d().load(gain[1]);
    Vec2d outputGainTarget = Vec2d().load(gainTarget[1]);

    Vec2d vuMeterDry = vuMeterDryBuffer;
    Vec2d vuMeterWet = vuMeterWetBuffer;

    for (int i = 0; i < numSamples; ++i) {
      amount = alpha * (amount - amountTarget) + amountTarget;
      outputGain = alpha * (outputGain - outputGainTarget) + outputGainTarget;
      Vec2d wet = outputGain * wetBuffer[i];
      Vec2d dry = dryBuffer[i];
      wetBuffer[i] = amount * (wet - dry) + dry;
      Vec2d wet2 = wet * wet;
      Vec2d dry2 = dry * dry;
      vuMeterWet = vuMeterAlpha * (vuMeterWet - wet2) + wet2;
      vuMeterDry = vuMeterAlpha * (vuMeterDry - dry2) + dry2;
    }

    amount.store(wetAmount);
    outputGain.store(gain[1]);

    vuMeterWetBuffer = vuMeterWet;
    vuMeterDryBuffer = vuMeterDry;
    vuMeterBufferDB = toDB(vuMeterWet) - toDB(vuMeterDry);
  }
  else {
    if (!isBypassing) {

      auto& wetBuffer = wetOutput.getBuffer2(0);
      auto& dryBuffer = dryOutput.getBuffer2(0);

      Vec2d alpha = automationAlpha;

      Vec2d outputGain = Vec2d().load(gain[1]);
      Vec2d outputGainTarget = Vec2d().load(gainTarget[1]);

      Vec2d vuMeterDry = vuMeterDryBuffer;
      Vec2d vuMeterWet = vuMeterWetBuffer;
      for (int i = 0; i < numSamples; ++i) {
        outputGain = alpha * (outputGain - outputGainTarget) + outputGainTarget;
        Vec2d wet = outputGain * wetBuffer[i];
        wetBuffer[i] = wet;
        Vec2d dry = dryBuffer[i];
        Vec2d wet2 = wet * wet;
        Vec2d dry2 = dry * dry;
        vuMeterWet = vuMeterAlpha * (vuMeterWet - wet2) + wet2;
        vuMeterDry = vuMeterAlpha * (vuMeterDry - dry2) + dry2;
      }

      outputGain.store(gain[1]);

      vuMeterWetBuffer = vuMeterWet;
      vuMeterDryBuffer = vuMeterDry;
      vuMeterBufferDB = toDB(vuMeterWet) - toDB(vuMeterDry);
    }
  }

  if (isBypassing) {
    dryOutput.deinterleave(ioAudio, 2, numSamples);
    vuMeterBufferDB = 0.0;
  }
  else {
    wetOutput.deinterleave(ioAudio, 2, numSamples);
  }

  // mid side

  if (isMidSideEnabled) {
    midSideToLeftRight(ioAudio, numSamples);
  }

  // update vu meter

  vuMeterResults[0] = vuMeterBufferDB[0];
  vuMeterResults[1] = vuMeterBufferDB[1];
}

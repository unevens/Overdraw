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

  int numActiveKnots = parameters.spline->updateSpline(*spline);

  double const smoothingTime = 0.001 * parameters.smoothingTime->get();

  double const invSampleRate = 1.0 / getSampleRate();

  double const automationAlpha =
    smoothingTime == 0.0
      ? 0.0
      : exp(-MathConstants<double>::twoPi * invSampleRate / smoothingTime);

  
  auto const oversamplingOrder =
    static_cast<uint32_t>(parameters.oversamplingOrder->getIndex());
  auto const oversamplingRate = static_cast<double>(1 << oversamplingOrder);
  auto const isOversampling = oversamplingOrder > 0;

  if (isOversampling) {
    oversampling.signal.setOrder(oversamplingOrder);
    oversampling.dry.setOrder(oversamplingOrder);

    auto const isUsingLinearPhase = parameters.oversamplingLinearPhase->get();

    oversampling.signal.setUseLinearPhase(isUsingLinearPhase);
    oversampling.dry.setUseLinearPhase(isUsingLinearPhase);
  }

  double const invUpsampledSampleRate = invSampleRate / oversamplingRate;

  double const upsampledAutomationAlpha =
    smoothingTime == 0.0 ? 0.0
                         : exp(-MathConstants<double>::twoPi *
                               invUpsampledSampleRate / smoothingTime);

  spline->automator.setSmoothingAlpha(upsampledAutomationAlpha);

  double gainTarget[2][2];
  double wetAmountTarget[2];

  for (int c = 0; c < 2; ++c) {

    wetAmountTarget[c] = 0.01 * parameters.wet.get(c)->get();

    for (int i = 0; i < 2; ++i) {
      gainTarget[i][c] = exp(db_to_lin * parameters.gain[i].get(c)->get());
    }

    spline->spline.setIsSymmetric(
      c, parameters.symmetry.get(c)->getValue() ? 1.0 : 0.0);
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

  if (isOversampling) {
    auto const numUpsampledSamples =
      oversampling.signal.upSample(ioAudio, numSamples);

    oversampling.dry.upSample(dryBuffer.get(), numSamples);

    if (numUpsampledSamples == 0) {
      for (auto i = 0; i < totalNumOutputChannels; ++i) {
        buffer.clear(i, 0, numSamples);
      }
      return;
    }
  }

  auto& upsampledBuffer = oversampling.signal.getUpSampleOutputInterleaved();
  auto& upsampledIo = upsampledBuffer.getBuffer2(0);
  auto& upsampledDryBuffer = oversampling.dry.getUpSampleOutputInterleaved();

  if (!isOversampling) {
    upsampledBuffer.setNumSamples(numSamples);
    upsampledBuffer.interleave(ioAudio, 2, numSamples);

    upsampledDryBuffer.setNumSamples(numSamples);
    upsampledDryBuffer.interleave(dryBuffer.get(), 2, numSamples);
  }

  // waveshaping

  if (!isBypassing) {
    splineDispatcher.processBlock(
      *spline, upsampledIo, upsampledIo, numActiveKnots);
  }

  // downsampling

  if (isOversampling) {
    oversampling.signal.downSample(upsampledBuffer, numSamples);
    oversampling.dry.downSample(upsampledDryBuffer, numSamples);
  }

  // dry-wet and output gain

  auto& wetOutput = isOversampling
                      ? oversampling.signal.getDownSampleOutputInterleaved()
                      : upsampledBuffer;
  auto& dryOutput = isOversampling
                      ? oversampling.dry.getDownSampleOutputInterleaved()
                      : upsampledDryBuffer;

  constexpr double vuMeterFrequency = 10.0;

  Vec2d vuMeterAlpha =
    exp(-MathConstants<double>::twoPi * invSampleRate * vuMeterFrequency);

  auto& wetData = wetOutput.getBuffer2(0);
  auto& dryData = dryOutput.getBuffer2(0);

  Vec2d alpha = automationAlpha;

  Vec2d outputGain = Vec2d().load(gain[1]);
  Vec2d outputGainTarget = Vec2d().load(gainTarget[1]);

  Vec2d vuMeterDry = vuMeterBuffer[0];
  Vec2d vuMeterWet = vuMeterBuffer[1];
  VecView<Vec2d> vuMeter = vuMeterBuffer[2];

  if (isWetPassNeeded) {

    Vec2d amount = Vec2d().load(wetAmount);
    Vec2d amountTarget = Vec2d().load(wetAmountTarget);

    for (int i = 0; i < numSamples; ++i) {
      amount = alpha * (amount - amountTarget) + amountTarget;
      outputGain = alpha * (outputGain - outputGainTarget) + outputGainTarget;
      Vec2d wet = outputGain * wetData[i];
      Vec2d dry = dryData[i];
      wetData[i] = amount * (wet - dry) + dry;
      Vec2d wet2 = wet * wet;
      Vec2d dry2 = dry * dry;
      vuMeterWet = vuMeterAlpha * (vuMeterWet - wet2) + wet2;
      vuMeterDry = vuMeterAlpha * (vuMeterDry - dry2) + dry2;
    }

    amount.store(wetAmount);
  }
  else {
    if (!isBypassing) {

      for (int i = 0; i < numSamples; ++i) {
        outputGain = alpha * (outputGain - outputGainTarget) + outputGainTarget;
        Vec2d wet = outputGain * wetData[i];
        wetData[i] = wet;
        Vec2d dry = dryData[i];
        Vec2d wet2 = wet * wet;
        Vec2d dry2 = dry * dry;
        vuMeterWet = vuMeterAlpha * (vuMeterWet - wet2) + wet2;
        vuMeterDry = vuMeterAlpha * (vuMeterDry - dry2) + dry2;
      }
    }
  }

  if (isBypassing) {
    vuMeterBuffer[2] = 0.0;
    dryOutput.deinterleave(ioAudio, 2, numSamples);
  }
  else {
    outputGain.store(gain[1]);
    vuMeterBuffer[0] = vuMeterDry;
    vuMeterBuffer[1] = vuMeterWet;
    Vec2d gainOffset = outputGainTarget * Vec2d().load(gainTarget[0]);
    gainOffset *= gainOffset;
    vuMeter = toDB(vuMeterWet) - toDB(gainOffset * vuMeterDry);

    wetOutput.deinterleave(ioAudio, 2, numSamples);
  }

  // mid side

  if (isMidSideEnabled) {
    midSideToLeftRight(ioAudio, numSamples);
  }

  // update vu meter

  vuMeterResults[0] = (float)(vuMeter[0]);
  vuMeterResults[1] = (float)(vuMeter[1]);
}

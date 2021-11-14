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

#pragma once

#include "Linkables.h"
#include "SimpleLookAndFeel.h"
#include "SplineParameters.h"
#include "adsp/Spline.hpp"
#include "oversimple/Oversampling.hpp"
#include <JuceHeader.h>

#ifndef OVERDRAW_UI_SCALE
#define OVERDRAW_UI_SCALE 0.8f
#endif

static constexpr float uiGlobalScaleFactor = OVERDRAW_UI_SCALE;

constexpr static long double operator"" _p(long double px)
{
  return px * uiGlobalScaleFactor;
}

class OverdrawAudioProcessor : public AudioProcessor
{
public:
  static constexpr int maxNumKnots = 15;

private:
  struct Parameters
  {
    AudioParameterBool* midSide;

    AudioParameterFloat* smoothingTime;

    AudioParameterChoice* oversamplingOrder;
    AudioParameterBool* oversamplingLinearPhase;

    LinkableParameter<WrappedBoolParameter> symmetry;

    std::array<LinkableParameter<AudioParameterFloat>, 2> gain;

    LinkableParameter<AudioParameterFloat> wet;

    std::unique_ptr<SplineParameters> spline;

    std::unique_ptr<AudioProcessorValueTreeState> apvts;

    Parameters(OverdrawAudioProcessor& processor);
  };

  Parameters parameters;

  using Spline = adsp::AutoSpline<Vec2d, maxNumKnots>;

  aligned_ptr<Spline> spline;

  adsp::AutoSplineDispatcher<Vec2d, maxNumKnots> splineDispatcher;

  double automationTime = 50.0;

  double gain[2][2] = { { 1.0, 1.0 }, { 1.0, 1.0 } };
  double wetAmount[2] = { 1.0, 1.0 };

  Buffer<double> dryBuffer{ 2 };

  VecBuffer<Vec2d> vuMeterBuffer{ 3 };

  // buffer for single precision processing call
  AudioBuffer<double> floatToDouble;

  // oversampling
  using Oversampler = oversimple::TOversampling<double>;

  struct Oversampling
  {
    Oversampler signal;
    Oversampler dry;
  };

  Oversampling oversampling;

public:
  // for gui
  SimpleLookAndFeel looks;
  std::array<std::atomic<float>, 2> vuMeterResults;

  Parameters& getOverdrawParameters() { return parameters; }

  void updateOversamplingLatency();

  // AudioProcessor interface

  //==============================================================================
  OverdrawAudioProcessor();
  ~OverdrawAudioProcessor();

  //==============================================================================
  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override;
  void reset() override;

#ifndef JucePlugin_PreferredChannelConfigurations
  bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
#endif

  void processBlock(AudioBuffer<float>&, MidiBuffer&) override;

  bool supportsDoublePrecisionProcessing() const override { return true; }

  void processBlock(AudioBuffer<double>& buffer, MidiBuffer& midi) override;

  //==============================================================================
  AudioProcessorEditor* createEditor() override;
  bool hasEditor() const override;

  //==============================================================================
  const String getName() const override;

  bool acceptsMidi() const override;
  bool producesMidi() const override;
  bool isMidiEffect() const override;
  double getTailLengthSeconds() const override;

  //==============================================================================
  int getNumPrograms() override;
  int getCurrentProgram() override;
  void setCurrentProgram(int index) override;
  const String getProgramName(int index) override;
  void changeProgramName(int index, const String& newName) override;

  //==============================================================================
  void getStateInformation(MemoryBlock& destData) override;
  void setStateInformation(const void* data, int sizeInBytes) override;

private:
  //==============================================================================
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OverdrawAudioProcessor)
};

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
#include "PluginEditor.h"

OverdrawAudioProcessor::Parameters::Parameters(
  OverdrawAudioProcessor& processor)
{
  std::vector<std::unique_ptr<RangedAudioParameter>> parameters;

  auto const createFloatParameter =
    [&](String name, float value, float min, float max, float step = 0.01f) {
      parameters.push_back(std::unique_ptr<RangedAudioParameter>(
        new AudioParameterFloat(name, name, { min, max, step }, value)));

      return static_cast<AudioParameterFloat*>(parameters.back().get());
    };

  auto const createWrappedBoolParameter = [&](String name, float value) {
    WrappedBoolParameter wrapper;
    parameters.push_back(wrapper.createParameter(name, value));
    return wrapper;
  };

  auto const createBoolParameter = [&](String name, float value) {
    parameters.push_back(std::unique_ptr<RangedAudioParameter>(
      new AudioParameterBool(name, name, value)));

    return static_cast<AudioParameterBool*>(parameters.back().get());
  };

  auto const createChoiceParameter =
    [&](String name, StringArray choices, int defaultIndex = 0) {
      parameters.push_back(std::unique_ptr<RangedAudioParameter>(
        new AudioParameterChoice(name, name, choices, defaultIndex)));

      return static_cast<AudioParameterChoice*>(parameters.back().get());
    };

  String const ch0Suffix = "_ch0";
  String const ch1Suffix = "_ch1";
  String const linkSuffix = "_is_linked";

  auto const createLinkableFloatParameters =
    [&](String name, float value, float min, float max, float step = 0.01f) {
      return LinkableParameter<AudioParameterFloat>{
        createWrappedBoolParameter(name + linkSuffix, true),
        { createFloatParameter(name + ch0Suffix, value, min, max, step),
          createFloatParameter(name + ch1Suffix, value, min, max, step) }
      };
    };

  auto const createLinkableBoolParameters = [&](String name, bool value) {
    return LinkableParameter<WrappedBoolParameter>{
      createWrappedBoolParameter(name + linkSuffix, true),
      { createWrappedBoolParameter(name + ch0Suffix, value),
        createWrappedBoolParameter(name + ch1Suffix, value) }
    };
  };

  auto const createLinkableChoiceParameters =
    [&](String name, StringArray choices, int defaultIndex = 0) {
      return LinkableParameter<AudioParameterChoice>{
        createWrappedBoolParameter(name + linkSuffix, true),
        { createChoiceParameter(name + ch0Suffix, choices, defaultIndex),
          createChoiceParameter(name + ch1Suffix, choices, defaultIndex) }
      };
    };

  midSide = createBoolParameter("Mid-Side", false);

  smoothingTime = createFloatParameter("Smoothing-Time", 50.0, 0.0, 500.0, 1.f);

  oversampling = { static_cast<RangedAudioParameter*>(createChoiceParameter(
                     "Oversampling", { "1x", "2x", "4x", "8x", "16x", "32x" })),
                   createWrappedBoolParameter("Linear-Phase-Oversampling",
                                              false) };

  symmetry = createLinkableBoolParameters("Symmetry", true);

  for (int i = 0; i < 2; ++i) {
    gain[i] = createLinkableFloatParameters(
      i == 0 ? "Input-Gain" : "Output-Gain", 0.f, -48.f, +48.f);
  }

  auto const isKnotActive = [&](int knotIndex) {
    std::array<int, 3> enabledKnotIndices = { 7, 9, 11 };
    return enabledKnotIndices.end() != std::find(enabledKnotIndices.begin(),
                                                 enabledKnotIndices.end(),
                                                 knotIndex);
  };

  spline = std::unique_ptr<SplineParameters>(
    new SplineParameters("",
                         parameters,
                         OverdrawAudioProcessor::maxNumKnots,
                         { -2.f, 2.f, 0.0001f },
                         { -2.f, 2.f, 0.0001f },
                         { -20.f, 20.f, 0.01f },
                         isKnotActive));

  apvts = std::unique_ptr<AudioProcessorValueTreeState>(
    new AudioProcessorValueTreeState(processor,
                                     nullptr,
                                     "OVERDRAW-PARAMETERS",
                                     { parameters.begin(), parameters.end() }));
}

OverdrawAudioProcessor::OverdrawAudioProcessor()

#ifndef JucePlugin_PreferredChannelConfigurations
  : AudioProcessor(BusesProperties()
                     .withInput("Input", AudioChannelSet::stereo(), true)
                     .withOutput("Output", AudioChannelSet::stereo(), true))
#endif

  , parameters(*this)

  , splines(avec::SplineHolder<Vec2d>::make<maxNumKnots>(true))

  , asyncOversampling([this] {
    auto oversampling = OversamplingSettings{};
    oversampling.numScalarToVecUpsamplers = 1;
    oversampling.numVecToScalarDownsamplers = 1;
    oversampling.numChannels = 2;
    oversampling.updateLatency = [this](int latency) {
      setLatencySamples(latency);
    };
    return oversampling;
  }())

  , oversamplingGetter(
      *oversimple::requestOversamplingGetter<double>(asyncOversampling))

  , oversamplingAwaiter(asyncOversampling.requestAwaiter())

{
  oversamplingAttachments = std::make_unique<OversamplingAttachments>(
    *parameters.apvts, asyncOversampling, parameters.oversampling);

  looks.simpleFontSize *= uiGlobalScaleFactor;
  looks.simpleSliderLabelFontSize *= uiGlobalScaleFactor;
  looks.simpleRotarySliderOffset *= uiGlobalScaleFactor;

  LookAndFeel::setDefaultLookAndFeel(&looks);

  asyncOversampling.startTimer();
}

void
OverdrawAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
  asyncOversampling.submitMessage([=](OversamplingSettings& oversampling) {
    oversampling.numSamplesPerBlock = samplesPerBlock;
  });

  floatToDouble = AudioBuffer<double>(4, samplesPerBlock);

  reset();

  oversamplingAwaiter.await();
}

void
OverdrawAudioProcessor::reset()
{
  parameters.spline->updateSpline(splines);
  splines.reset();

  constexpr double ln10 = 2.30258509299404568402;
  constexpr double db_to_lin = ln10 / 20.0;

  for (int i = 0; i < 2; ++i) {
    for (int c = 0; c < 2; ++c) {
      gain[i][c] = exp(db_to_lin * parameters.gain[i].get(c)->get());
    }
  }
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool
OverdrawAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
  if (layouts.getMainOutputChannels() == 2 &&
      layouts.getMainInputChannels() == 2) {
    return true;
  }
  return false;
}
#endif

void
OverdrawAudioProcessor::processBlock(AudioBuffer<float>& buffer,
                                     MidiBuffer& midiMessages)
{
  auto totalNumInputChannels = getTotalNumInputChannels();

  for (int c = 0; c < totalNumInputChannels; ++c) {
    std::copy(buffer.getReadPointer(c),
              buffer.getReadPointer(c) + buffer.getNumSamples(),
              floatToDouble.getWritePointer(c));
  }

  for (int c = totalNumInputChannels; c < 4; ++c) {
    floatToDouble.clear(c, 0, floatToDouble.getNumSamples());
  }

  processBlock(floatToDouble, midiMessages);

  for (int c = 0; c < totalNumInputChannels; ++c) {
    std::copy(floatToDouble.getReadPointer(c),
              floatToDouble.getReadPointer(c) + floatToDouble.getNumSamples(),
              buffer.getWritePointer(c));
  }
}

void
OverdrawAudioProcessor::getStateInformation(MemoryBlock& destData)
{
  auto state = parameters.apvts->copyState();
  std::unique_ptr<XmlElement> xml(state.createXml());
  copyXmlToBinary(*xml, destData);
}

void
OverdrawAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
  std::unique_ptr<XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

  if (xmlState.get() != nullptr) {
    if (xmlState->hasTagName(parameters.apvts->state.getType())) {
      parameters.apvts->replaceState(ValueTree::fromXml(*xmlState));
    }
  }
}

void
OverdrawAudioProcessor::releaseResources()
{
  floatToDouble = AudioBuffer<double>(0, 0);
}

//==============================================================================
OverdrawAudioProcessor::~OverdrawAudioProcessor() {}

const String
OverdrawAudioProcessor::getName() const
{
  return JucePlugin_Name;
}

bool
OverdrawAudioProcessor::acceptsMidi() const
{
#if JucePlugin_WantsMidiInput
  return true;
#else
  return false;
#endif
}

bool
OverdrawAudioProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
  return true;
#else
  return false;
#endif
}

bool
OverdrawAudioProcessor::isMidiEffect() const
{
#if JucePlugin_IsMidiEffect
  return true;
#else
  return false;
#endif
}

double
OverdrawAudioProcessor::getTailLengthSeconds() const
{
  return 0.0;
}

int
OverdrawAudioProcessor::getNumPrograms()
{
  return 1; // NB: some hosts don't cope very well if you tell them there are 0
            // programs, so this should be at least 1, even if you're not really
            // implementing programs.
}

int
OverdrawAudioProcessor::getCurrentProgram()
{
  return 0;
}

void
OverdrawAudioProcessor::setCurrentProgram(int index)
{}

const String
OverdrawAudioProcessor::getProgramName(int index)
{
  return {};
}

void
OverdrawAudioProcessor::changeProgramName(int index, const String& newName)
{}

bool
OverdrawAudioProcessor::hasEditor() const
{
  return true;
}

AudioProcessorEditor*
OverdrawAudioProcessor::createEditor()
{
  return new OverdrawAudioProcessorEditor(*this);
}

AudioProcessor* JUCE_CALLTYPE
createPluginFilter()
{
  return new OverdrawAudioProcessor();
}

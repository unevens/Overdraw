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

#include "PluginProcessor.h"
#include "SplineEditor.h"
#include "GainVuMeter.h"
#include <JuceHeader.h>

class OverdrawAudioProcessorEditor : public AudioProcessorEditor
{
public:
  OverdrawAudioProcessorEditor(OverdrawAudioProcessor&);
  ~OverdrawAudioProcessorEditor();

  void paint(Graphics&) override;
  void resized() override;

private:
  OverdrawAudioProcessor& processor;

  SplineEditor spline;
  SplineKnotEditor selectedKnot;

  GainVuMeter vuMeter;

  AttachedToggle midSide;
  AttachedSlider smoothing;
  Label smoothingLabel{ {}, "Smoothing Time" };

  AttachedComboBox oversampling;
  AttachedToggle linearPhase;
  Label oversamplingLabel{ {}, "Oversampling" };

  std::array<LinkableControl<AttachedSlider>, 2> gain;
  LinkableControl<AttachedSlider> wet;
  LinkableControl<AttachedToggle> symmetry;
  ChannelLabels channelLabels;

  TextEditor url;

  Colour lineColour = Colours::white;
  Colour backgroundColour = Colours::black.withAlpha(0.6f);

  Image background;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OverdrawAudioProcessorEditor)
};

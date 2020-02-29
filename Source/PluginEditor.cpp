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

#include "PluginEditor.h"
#include "PluginProcessor.h"

OverdrawAudioProcessorEditor::OverdrawAudioProcessorEditor(
  OverdrawAudioProcessor& p)

  : AudioProcessorEditor(&p)

  , processor(p)

  , spline(*p.getOverdrawParameters().spline,
           *p.getOverdrawParameters().apvts,
           &p.getOverdrawParameters().symmetry)

  , selectedKnot(*p.getOverdrawParameters().spline,
                 *p.getOverdrawParameters().apvts)

  , midSide(*this, *p.getOverdrawParameters().apvts, "Mid-Side")

  , inputGain(*p.getOverdrawParameters().apvts,
              "Input Gain",
              p.getOverdrawParameters().inputGain)

  , outputGain(*p.getOverdrawParameters().apvts,
               "Output Gain",
               p.getOverdrawParameters().outputGain)

  , symmetry(*p.getOverdrawParameters().apvts,
             "Symmetric",
             p.getOverdrawParameters().symmetry)

  , dryWet(*p.getOverdrawParameters().apvts,
           "Wet",
           p.getOverdrawParameters().dryWet)

  , highPass(*p.getOverdrawParameters().apvts,
             "HP Frequency",
             p.getOverdrawParameters().highPassCutoff)

  , channelLabels(*p.getOverdrawParameters().apvts, "Mid-Side")

  , inputGainLabels(*p.getOverdrawParameters().apvts, "Mid-Side")

  , outputGainLabels(*p.getOverdrawParameters().apvts, "Mid-Side")

  , oversampling(*this,
                 *p.getOverdrawParameters().apvts,
                 "Oversampling",
                 { "1x", "2x", "4x", "8x", "16x", "32x" })

  , linearPhase(*this,
                *p.getOverdrawParameters().apvts,
                "Linear-Phase-Oversampling")

  , smoothing(*this, *p.getOverdrawParameters().apvts, "Smoothing-Time")

  , background(ImageCache::getFromMemory(BinaryData::background_png,
                                         BinaryData::background_pngSize))

{
  addAndMakeVisible(spline);
  addAndMakeVisible(selectedKnot);
  addAndMakeVisible(inputGain);
  addAndMakeVisible(outputGain);
  addAndMakeVisible(oversamplingLabel);
  addAndMakeVisible(smoothingLabel);
  addAndMakeVisible(dryWet);
  addAndMakeVisible(highPass);
  addAndMakeVisible(symmetry);
  addAndMakeVisible(channelLabels);
  addAndMakeVisible(inputGainLabels);
  addAndMakeVisible(outputGainLabels);
  addAndMakeVisible(url);

  attachAndInitializeSplineEditors(spline, selectedKnot, 7);

  oversamplingLabel.setFont(Font(20._p, Font::bold));

  oversamplingLabel.setJustificationType(Justification::centred);
  smoothingLabel.setJustificationType(Justification::centred);

  midSide.getControl().setButtonText("Mid Side");
  linearPhase.getControl().setButtonText("Linear Phase");

  lineColour = p.looks.frontColour.darker(1.f);

  auto tableSettings = LinkableControlTable();
  tableSettings.lineColour = lineColour;
  tableSettings.backgroundColour = backgroundColour;
  selectedKnot.setTableSettings(tableSettings);

  auto const applyTableSettings = [&](auto& linkedControls) {
    linkedControls.tableSettings.lineColour = lineColour;
    linkedControls.tableSettings.backgroundColour = backgroundColour;
  };

  applyTableSettings(inputGain);
  applyTableSettings(inputGainLabels);
  applyTableSettings(outputGain);
  applyTableSettings(outputGainLabels);
  applyTableSettings(channelLabels);

  applyTableSettings(dryWet);
  applyTableSettings(symmetry);
  applyTableSettings(highPass);

  for (int c = 0; c < 2; ++c) {
    outputGain.getControl(c).setTextValueSuffix("dB");
    inputGain.getControl(c).setTextValueSuffix("dB");
    dryWet.getControl(c).setTextValueSuffix("%");
    highPass.getControl(c).setTextValueSuffix("Hz");
  }

  smoothing.getControl().setTextValueSuffix("ms");

  url.setFont({ 14._p, Font::bold });
  url.setJustification(Justification::centred);
  url.setReadOnly(true);
  url.setColour(TextEditor::ColourIds::focusedOutlineColourId, Colours::white);
  url.setColour(TextEditor::ColourIds::backgroundColourId,
                Colours::transparentBlack);
  url.setColour(TextEditor::ColourIds::outlineColourId,
                Colours::transparentBlack);
  url.setColour(TextEditor::ColourIds::textColourId,
                Colours::white.withAlpha(0.2f));
  url.setColour(TextEditor::ColourIds::highlightedTextColourId, Colours::white);
  url.setColour(TextEditor::ColourIds::highlightColourId, Colours::black);
  url.setText("www.unevens.net", dontSendNotification);
  url.setJustification(Justification::left);

  setSize(760._p, 930._p);
}

OverdrawAudioProcessorEditor::~OverdrawAudioProcessorEditor() {}

void
OverdrawAudioProcessorEditor::paint(Graphics& g)
{
  g.drawImage(background, getLocalBounds().toFloat());

  g.setColour(backgroundColour);
  g.fillRect(juce::Rectangle<int>(572._p, 10._p, 168._p, 190._p));

  g.setColour(lineColour);
  g.drawRect(572._p, 10._p, 168._p, 40._p, 1);
  g.drawRect(572._p, 10._p, 168._p, 105._p, 1);
  g.drawRect(572._p, 10._p, 168._p, 200._p, 1);

  g.drawRect(spline.getBounds().expanded(1, 1), 1);
}

void
OverdrawAudioProcessorEditor::resized()
{
  constexpr auto offset = 10._p;
  constexpr auto rowHeight = 40._p;
  constexpr auto splineEditorSide = 540._p;
  constexpr auto knotEditorHeight = 160._p;

  spline.setTopLeftPosition(offset + 1, offset + 1);
  spline.setSize(splineEditorSide - 2, splineEditorSide - 2);

  selectedKnot.setTopLeftPosition(offset, splineEditorSide + 2 * offset);
  selectedKnot.setSize(140._p * 4 + (50._p), 160._p);

  int const gainLeft = spline.getBounds().getRight() + offset + 1;
  int const outputGainBottom = spline.getBounds().getBottom() + 1;
  int const outputGainTop = outputGainBottom - 160._p;
  int const inputGainTop = outputGainTop - 160._p - offset;

  inputGainLabels.setTopLeftPosition(gainLeft, inputGainTop);
  inputGainLabels.setSize(50._p, 160._p);

  inputGain.setTopLeftPosition(gainLeft + 50._p - 1, inputGainTop);
  inputGain.setSize(140._p, 160._p);

  outputGainLabels.setTopLeftPosition(gainLeft, outputGainTop);
  outputGainLabels.setSize(50._p, 160._p);

  outputGain.setTopLeftPosition(gainLeft + 50._p - 1, outputGainTop);
  outputGain.setSize(140._p, 160._p);

  int const top = selectedKnot.getBounds().getBottom() + offset;

  int left = 10._p;

  auto const resize = [&](auto& component, int width) {
    component.setTopLeftPosition(left, top);
    component.setSize(width, 160._p);
    left += width - 1;
  };

  resize(channelLabels, 50._p);

  resize(dryWet, 140._p);
  resize(symmetry, 140._p);
  resize(highPass, 140._p);

  left += 10._p;

  Grid grid;
  using Track = Grid::TrackInfo;

  grid.templateColumns = { Track(1_fr) };

  grid.templateRows = { Track(Grid::Px(35._p)), Track(Grid::Px(30._p)),
                        Track(Grid::Px(40._p)), Track(Grid::Px(30._p)),
                        Track(Grid::Px(30._p)), Track(Grid::Px(35._p)) };

  grid.items = { GridItem(midSide.getControl())
                   .withWidth(120._p)
                   .withHeight(30._p)
                   .withAlignSelf(GridItem::AlignSelf::end)
                   .withJustifySelf(GridItem::JustifySelf::center),
                 GridItem(smoothingLabel)
                   .withAlignSelf(GridItem::AlignSelf::start)
                   .withJustifySelf(GridItem::JustifySelf::center),
                 GridItem(smoothing.getControl())
                   .withWidth(135._p)
                   .withAlignSelf(GridItem::AlignSelf::start)
                   .withJustifySelf(GridItem::JustifySelf::center),
                 GridItem(oversamplingLabel),
                 GridItem(oversampling.getControl())
                   .withWidth(70)
                   .withAlignSelf(GridItem::AlignSelf::center)
                   .withJustifySelf(GridItem::JustifySelf::center),
                 GridItem(linearPhase.getControl())
                   .withWidth(120)
                   .withAlignSelf(GridItem::AlignSelf::center)
                   .withJustifySelf(GridItem::JustifySelf::center) };

  grid.justifyContent = Grid::JustifyContent::center;
  grid.alignContent = Grid::AlignContent::center;

  grid.performLayout(juce::Rectangle<int>(
    splineEditorSide + 2 * offset, offset + 20._p, 200._p, 162._p));

  url.setTopLeftPosition(10._p, getHeight() - 18._p);
  url.setSize(160._p, 16._p);

  spline.areaInWhichToDrawKnots = juce::Rectangle<int>(
    selectedKnot.getPosition().x,
    spline.getBottom() - offset,
    jmax(spline.getWidth(), selectedKnot.getWidth()),
    selectedKnot.getBottom() - spline.getBottom() + offset);
}

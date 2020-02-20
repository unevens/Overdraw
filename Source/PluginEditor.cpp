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

  , splineEditor(*p.GetOverdrawParameters().spline,
                 *p.GetOverdrawParameters().apvts,
                 &p.GetOverdrawParameters().waveShaper)

  , nodeEditor(*p.GetOverdrawParameters().spline,
               *p.GetOverdrawParameters().apvts)

  , midSideEditor(*this, *p.GetOverdrawParameters().apvts, "Mid-Side")

  , inputGain(*p.GetOverdrawParameters().apvts,
              "Input Gain",
              p.GetOverdrawParameters().inputGain)

  , outputGain(*p.GetOverdrawParameters().apvts,
               "Output Gain",
               p.GetOverdrawParameters().outputGain)

  , symmetry(*p.GetOverdrawParameters().apvts,
             "Symmetric",
             p.GetOverdrawParameters().waveShaper.symmetry)

  , dc(*p.GetOverdrawParameters().apvts,
       "DC",
       p.GetOverdrawParameters().waveShaper.dc)

  , dryWet(*p.GetOverdrawParameters().apvts,
           "Wet",
           p.GetOverdrawParameters().waveShaper.dryWet)

  , highPass(*p.GetOverdrawParameters().apvts,
             "HP Frequency",
             p.GetOverdrawParameters().waveShaper.dcCutoff)

  , channelLabels(*p.GetOverdrawParameters().apvts, "Mid-Side")

  , inputGainLabels(*p.GetOverdrawParameters().apvts, "Mid-Side")

  , outputGainLabels(*p.GetOverdrawParameters().apvts, "Mid-Side")

  , background(ImageCache::getFromMemory(BinaryData::background_png,
                                         BinaryData::background_pngSize))

{
  addAndMakeVisible(splineEditor);
  addAndMakeVisible(nodeEditor);
  addAndMakeVisible(inputGain);
  addAndMakeVisible(outputGain);
  addAndMakeVisible(oversamplingLabel);
  addAndMakeVisible(oversampling);
  addAndMakeVisible(linearPhase);
  addAndMakeVisible(midSideLabel);
  addAndMakeVisible(dc);
  addAndMakeVisible(dryWet);
  addAndMakeVisible(highPass);
  addAndMakeVisible(symmetry);
  addAndMakeVisible(channelLabels);
  addAndMakeVisible(inputGainLabels);
  addAndMakeVisible(outputGainLabels);
  addAndMakeVisible(url);

  AttachSplineEditorsAndInitialize(splineEditor, nodeEditor);

  oversamplingLabel.setFont(Font(20, Font::bold));
  midSideLabel.setFont(Font(20, Font::bold));

  oversamplingLabel.setJustificationType(Justification::centred);
  midSideLabel.setJustificationType(Justification::centred);

  linearPhase.setButtonText("Linear Phase");

  for (int i = 0; i <= 5; ++i) {
    oversampling.addItem(std::to_string(1 << i) + "x", i + 1);
  }

  auto const OnOversamplingChange = [this] {
    bool isLinearPhase = linearPhase.getToggleState();
    int order = oversampling.getSelectedId() - 1;
    processor.asyncOversampling.submitMessage(
      [=](oversimple::OversamplingSettings& oversampling) {
        oversampling.linearPhase = isLinearPhase;
        oversampling.order = order;
      });
  };

  linearPhase.onClick = OnOversamplingChange;
  oversampling.onChange = OnOversamplingChange;

  lineColour = p.looks.frontColour.darker(1.f);

  auto tableSettings = LinkableControlTable();

  auto const applyTableSettings = [&](auto& linkedControls) {
    linkedControls.tableSettings.lineColour = lineColour;
    linkedControls.tableSettings.backgroundColour = backgroundColour;
  };

  tableSettings.lineColour = lineColour;
  tableSettings.backgroundColour = backgroundColour;
  nodeEditor.setTableSettings(tableSettings);

  applyTableSettings(inputGain);
  applyTableSettings(inputGainLabels);
  applyTableSettings(outputGain);
  applyTableSettings(outputGainLabels);
  applyTableSettings(channelLabels);

  applyTableSettings(dc);
  applyTableSettings(dryWet);
  applyTableSettings(symmetry);
  applyTableSettings(highPass);

  url.setFont({ 14, Font::bold });
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

  setSize(720, 890);

  startTimer(250);
}

OverdrawAudioProcessorEditor::~OverdrawAudioProcessorEditor() {}

void
OverdrawAudioProcessorEditor::paint(Graphics& g)
{
  g.drawImage(background, getLocalBounds().toFloat());

  g.setColour(backgroundColour);
  g.fillRect(532, 10, 160, 330);

  g.setColour(lineColour);
  g.drawRect(532, 10, 160, 64, 1);
  g.drawRect(532, 10, 160, 150, 1);

  g.drawRect(splineEditor.getBounds().expanded(1, 1), 1);
}

void
OverdrawAudioProcessorEditor::resized()
{
  constexpr int offset = 10;
  constexpr int rowHeight = 40;
  constexpr int splineEditorSide = 500;
  constexpr int nodeEditorHeight = 160;

  splineEditor.setTopLeftPosition(offset + 1, offset + 1);
  splineEditor.setSize(splineEditorSide - 2, splineEditorSide - 2);

  nodeEditor.setTopLeftPosition(offset, splineEditorSide + 2 * offset);
  nodeEditor.setSize(4 * 140 + 50 - 4, 160);

  int const gainLeft = splineEditor.getBounds().getRight() + offset + 1;
  int const outputGainBottom = splineEditor.getBounds().getBottom() + 1;
  int const outputGainTop = outputGainBottom - 160;
  int const inputGainTop = outputGainTop - 160 - offset;

  inputGainLabels.setTopLeftPosition(gainLeft, inputGainTop);
  inputGainLabels.setSize(50, 160);

  inputGain.setTopLeftPosition(gainLeft + 49, inputGainTop);
  inputGain.setSize(140, 160);

  outputGainLabels.setTopLeftPosition(gainLeft, outputGainTop);
  outputGainLabels.setSize(50, 160);

  outputGain.setTopLeftPosition(gainLeft + 49, outputGainTop);
  outputGain.setSize(140, 160);

  int const top = nodeEditor.getBounds().getBottom() + offset;

  int left = 10;

  auto const resize = [&](auto& component, int width = 140) {
    component.setTopLeftPosition(left, top);
    component.setSize(width, 160);
    left += width - 1;
  };

  resize(channelLabels, 50);

  resize(dryWet);
  resize(symmetry);
  resize(dc);
  resize(highPass);

  left += 10;

  Grid grid;
  using Track = Grid::TrackInfo;

  grid.templateColumns = { Track(1_fr) };

  grid.templateRows = {
    Track(30_px), Track(30_px), Track(30_px), Track(30_px), Track(30_px)
  };

  grid.items = { GridItem(midSideLabel),
                 GridItem(midSideEditor.getControl())
                   .withWidth(30)
                   .withAlignSelf(GridItem::AlignSelf::center)
                   .withJustifySelf(GridItem::JustifySelf::center),
                 GridItem(oversamplingLabel),
                 GridItem(oversampling)
                   .withWidth(70)
                   .withAlignSelf(GridItem::AlignSelf::center)
                   .withJustifySelf(GridItem::JustifySelf::center),
                 GridItem(linearPhase)
                   .withWidth(120)
                   .withAlignSelf(GridItem::AlignSelf::center)
                   .withJustifySelf(GridItem::JustifySelf::center) };

  grid.justifyContent = Grid::JustifyContent::center;
  grid.alignContent = Grid::AlignContent::center;

  grid.performLayout(
    juce::Rectangle(splineEditorSide + 2 * offset + 12, 10, 150, 150));

  url.setTopLeftPosition(10, getHeight() - 18);
  url.setSize(160, 16);

  splineEditor.areaInWhichToDrawNodes = juce::Rectangle(
    splineEditor.getPosition().x,
    splineEditor.getPosition().x,
    jmax(splineEditor.getWidth(), nodeEditor.getWidth()),
    nodeEditor.getPosition().y + nodeEditor.getHeight() + offset);
}

void
OverdrawAudioProcessorEditor::timerCallback()
{
  processor.oversamplingGuiGetter.update();
  auto& overSettings = processor.oversamplingGuiGetter.get();

  linearPhase.setToggleState(overSettings.linearPhase, dontSendNotification);
  oversampling.setSelectedId(overSettings.order + 1, dontSendNotification);
}

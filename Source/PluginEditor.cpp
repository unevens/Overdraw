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

  , gain{ { { *p.getOverdrawParameters().apvts,
              "Input Gain",
              p.getOverdrawParameters().gain[0] },
            { *p.getOverdrawParameters().apvts,
              "Output Gain",
              p.getOverdrawParameters().gain[0] } } }

  , cutoff{ { { *p.getOverdrawParameters().apvts,
                "Input Cutoff",
                p.getOverdrawParameters().cutoff[0] },
              { *p.getOverdrawParameters().apvts,
                "Output Cutoff",
                p.getOverdrawParameters().cutoff[1] } } }

  , resonance{ { std::make_unique<LinkableControl<AttachedSlider>>(
                   *p.getOverdrawParameters().apvts,
                   "Input Resonance",
                   p.getOverdrawParameters().resonance[0]),
                 std::make_unique<LinkableControl<AttachedSlider>>(
                   *p.getOverdrawParameters().apvts,
                   "Output Resonance",
                   p.getOverdrawParameters().resonance[1]) } }

  , symmetry(*p.getOverdrawParameters().apvts,
             "Symmetry",
             p.getOverdrawParameters().symmetry)

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

  , filter{ { { *this,
                *p.getOverdrawParameters().apvts,
                "Input-Filter",
                {
                  "None",
                  "Low Pass 6dB",
                  "High Pass 6dB",
                  "Band Pass 12dB",
                  "Low Pass 12dB",
                  "High Pass 12dB",
                } },
              { *this,
                *p.getOverdrawParameters().apvts,
                "Output-Filter",
                {
                  "None",
                  "Low Pass 6dB",
                  "High Pass 6dB",
                  "Band Pass 12dB",
                  "Low Pass 12dB",
                  "High Pass 12dB",
                } } } }

  , smoothing(*this, *p.getOverdrawParameters().apvts, "Smoothing-Time")

  , background(ImageCache::getFromMemory(BinaryData::background_png,
                                         BinaryData::background_pngSize))

{
  addAndMakeVisible(spline);
  addAndMakeVisible(selectedKnot);
  addAndMakeVisible(oversamplingLabel);
  addAndMakeVisible(smoothingLabel);
  addAndMakeVisible(symmetry);
  addAndMakeVisible(inputFilterLabel);
  addAndMakeVisible(outputFilterLabel);
  addAndMakeVisible(channelLabels);
  addAndMakeVisible(inputGainLabels);
  addAndMakeVisible(outputGainLabels);
  addAndMakeVisible(url);

  for (int i = 0; i < 2; ++i) {
    addAndMakeVisible(gain[i]);
    addAndMakeVisible(cutoff[i]);
    addAndMakeVisible(*resonance[i]);
  }

  attachAndInitializeSplineEditors(spline, selectedKnot, 7);

  inputFilterLabel.setJustificationType(Justification::centred);
  outputFilterLabel.setJustificationType(Justification::centred);
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

  applyTableSettings(inputGainLabels);
  applyTableSettings(outputGainLabels);
  applyTableSettings(channelLabels);
  applyTableSettings(symmetry);

  for (int i = 0; i < 2; ++i) {

    applyTableSettings(gain[i]);
    applyTableSettings(cutoff[i]);
    applyTableSettings(*resonance[i]);

    for (int c = 0; c < 2; ++c) {
      gain[i].getControl(c).setTextValueSuffix("dB");
      cutoff[i].getControl(c).setTextValueSuffix("Hz");
    }

    filterAttachment[i] = std::make_unique<FloatAttachment>(
      *p.getOverdrawParameters().apvts,
      i == 0 ? "Input-Filter" : "Output-Filter",
      [this, i] { onFilterChanged(i); },
      NormalisableRange<float>(0.f, 5.f, 1.f));

    filterType[i] =
      static_cast<FilterType>((int)(filterAttachment[i]->getValue()));

    setupFilterControls(i);
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

  setSize(825._p, 995._p);
}

OverdrawAudioProcessorEditor::~OverdrawAudioProcessorEditor() {}

void
OverdrawAudioProcessorEditor::paint(Graphics& g)
{
  g.drawImage(background, getLocalBounds().toFloat());

  g.setColour(backgroundColour);

  constexpr int left = 625._p;
  constexpr int width = (190._p) - 1;
  constexpr int height = 405._p;
  constexpr int top = 10._p;

  g.fillRect(juce::Rectangle<int>(left, top, width, height));

  g.setColour(lineColour);
  g.drawRect(left, top, width, 85._p, 1);
  g.drawRect(left, top, width, 165._p, 1);
  g.drawRect(left, top, width, 205._p, 1);
  g.drawRect(left, top, width, 285._p, 1);
  g.drawRect(left, top, width, height, 1);

  g.drawRect(spline.getBounds().expanded(1, 1), 1);
}

void
OverdrawAudioProcessorEditor::resized()
{
  constexpr auto offset = 10._p;
  constexpr auto rowHeight = 40._p;
  constexpr auto splineEditorSide = 605._p;
  constexpr auto knotEditorHeight = 160._p;

  spline.setTopLeftPosition(offset + 1, offset + 1);
  spline.setSize(splineEditorSide - 2, splineEditorSide - 2);

  selectedKnot.setTopLeftPosition(offset, splineEditorSide + 2 * offset);
  selectedKnot.setSize(140._p * 4 + (50._p), 160._p);

  int const gainLeft = spline.getBounds().getRight() + offset + 1;

  int const inputGainTopAlignment = (415._p) + offset;
  int const inputGainBottomAlignment =
    selectedKnot.getBottom() + 1 - offset - 2 * 160._p;

  int const inputGainTop =
    inputGainTopAlignment +
    1.0 * (inputGainBottomAlignment - inputGainTopAlignment);

  int const outputGainTop = inputGainTop + offset + 160._p;

  inputGainLabels.setTopLeftPosition(gainLeft, inputGainTop);
  inputGainLabels.setSize(50._p, 160._p);

  gain[0].setTopLeftPosition(gainLeft + 50._p - 1, inputGainTop);
  gain[0].setSize(140._p, 160._p);

  outputGainLabels.setTopLeftPosition(gainLeft, outputGainTop);
  outputGainLabels.setSize(50._p, 160._p);

  gain[1].setTopLeftPosition(gainLeft + 50._p - 1, outputGainTop);
  gain[1].setSize(140._p, 160._p);

  int const top = selectedKnot.getBounds().getBottom() + offset;

  int left = 10._p;

  auto const resize = [&](auto& component, int width) {
    component.setTopLeftPosition(left, top);
    component.setSize(width, 160._p);
    left += width - 1;
  };

  resize(channelLabels, 50._p);

  resize(symmetry, 120._p);
  for (int i = 0; i < 2; ++i) {
    resize(cutoff[i], 140._p);
    resize(*resonance[i], 180._p);
  }

  Grid grid;
  using Track = Grid::TrackInfo;

  grid.templateColumns = { Track(1_fr) };

  grid.templateRows = { Track(Grid::Px(40._p)), Track(Grid::Px(40._p)),
                        Track(Grid::Px(40._p)), Track(Grid::Px(40._p)),
                        Track(Grid::Px(40._p)), Track(Grid::Px(40._p)),
                        Track(Grid::Px(40._p)), Track(Grid::Px(40._p)),
                        Track(Grid::Px(40._p)), Track(Grid::Px(40._p)) };

  grid.items = { GridItem(inputFilterLabel),
                 GridItem(filter[0].getControl())
                   .withWidth(170._p)
                   .withHeight(30._p)
                   .withAlignSelf(GridItem::AlignSelf::center)
                   .withJustifySelf(GridItem::JustifySelf::center),
                 GridItem(outputFilterLabel),
                 GridItem(filter[1].getControl())
                   .withWidth(170._p)
                   .withHeight(30._p)
                   .withAlignSelf(GridItem::AlignSelf::center)
                   .withJustifySelf(GridItem::JustifySelf::center),
                 GridItem(midSide.getControl())
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
                   .withHeight(30._p)
                   .withAlignSelf(GridItem::AlignSelf::center)
                   .withJustifySelf(GridItem::JustifySelf::center),
                 GridItem(linearPhase.getControl())
                   .withWidth(120)
                   .withAlignSelf(GridItem::AlignSelf::center)
                   .withJustifySelf(GridItem::JustifySelf::center) };

  grid.justifyContent = Grid::JustifyContent::center;
  grid.alignContent = Grid::AlignContent::center;

  grid.performLayout(juce::Rectangle<int>(
    splineEditorSide + 2 * offset, offset, (190._p) - 1, 400._p));

  url.setTopLeftPosition(10._p, getHeight() - 18._p);
  url.setSize(160._p, 16._p);

  spline.areaInWhichToDrawKnots = juce::Rectangle<int>(
    selectedKnot.getPosition().x,
    spline.getBottom() - offset,
    jmax(spline.getWidth(), selectedKnot.getWidth()),
    selectedKnot.getBottom() - spline.getBottom() + offset);
}

void
OverdrawAudioProcessorEditor::onFilterChanged(int filterIndex)
{
  int const i = filterIndex;

  if (!filterAttachment[i]) {
    return;
  }

  auto const newFilterType =
    static_cast<FilterType>((int)(filterAttachment[i]->getValue()));

  if (filterType[i] == newFilterType) {
    return;
  }

  filterType[i] = newFilterType;

  setupFilterControls(i);
}

void
OverdrawAudioProcessorEditor::setupFilterControls(int filterIndex)
{
  int const i = filterIndex;

  removeChildComponent(resonance[i].get());

  bool const useBandwidth = filterType[i] == FilterType::bandPass12dB;

  resonance[i] = std::make_unique<LinkableControl<AttachedSlider>>(
    *processor.getOverdrawParameters().apvts,
    String(i == 0 ? "Input " : "Output ") +
      (useBandwidth ? "Bandwidth" : "Resonance"),
    useBandwidth ? processor.getOverdrawParameters().bandwidth[i]
                 : processor.getOverdrawParameters().resonance[i]);

  addAndMakeVisible(*resonance[i]);

  for (int c = 0; c < 2; ++c) {
    resonance[i]->getControl(c).setTextValueSuffix(useBandwidth ? "oct" : "");
  }

  resonance[i]->tableSettings.lineColour = lineColour;
  resonance[i]->tableSettings.backgroundColour = backgroundColour;

  bool const resonanceEnabled = useBandwidth ||
                                filterType[i] == FilterType::lowPass12dB ||
                                filterType[i] == FilterType::highPass12dB;

  for (int c = 0; c < 2; ++c) {
    resonance[i]->getControl(c).setEnabled(resonanceEnabled);
  }
  resonance[i]->getLinked()->setEnabled(resonanceEnabled);
  resonance[i]->getLabel().setEnabled(resonanceEnabled);

  bool const cutoffEnabled = filterType[i] != FilterType::none;

  for (int c = 0; c < 2; ++c) {
    cutoff[i].getControl(c).setEnabled(cutoffEnabled);
  }
  cutoff[i].getLinked()->setEnabled(cutoffEnabled);
  cutoff[i].getLabel().setEnabled(cutoffEnabled);

  resized();
}

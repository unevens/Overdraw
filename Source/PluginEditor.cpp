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
              p.getOverdrawParameters().gain[1] } } }

  , frequency{ *p.getOverdrawParameters().apvts,
               "Frequency",
               p.getOverdrawParameters().frequency }

  , resonance(*p.getOverdrawParameters().apvts,
              "Resonance",
              p.getOverdrawParameters().resonance)

  , bandwidth(*p.getOverdrawParameters().apvts,
              "Bandwidth",
              p.getOverdrawParameters().bandwidth)

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

  , filterTypeControl{ *p.getOverdrawParameters().apvts,
                       "Filter",
                       OverdrawAudioProcessor::filterNames,
                       p.getOverdrawParameters().filter }

  , smoothing(*this, *p.getOverdrawParameters().apvts, "Smoothing-Time")

  , background(ImageCache::getFromMemory(BinaryData::background_png,
                                         BinaryData::background_pngSize))

{
  addAndMakeVisible(spline);
  addAndMakeVisible(selectedKnot);
  addAndMakeVisible(oversamplingLabel);
  addAndMakeVisible(smoothingLabel);
  addAndMakeVisible(symmetry);
  addAndMakeVisible(filterTypeControl);
  addAndMakeVisible(channelLabels);
  addAndMakeVisible(inputGainLabels);
  addAndMakeVisible(outputGainLabels);
  addAndMakeVisible(url);
  addAndMakeVisible(frequency);
  addAndMakeVisible(resonance);
  addAndMakeVisible(bandwidth);

  for (int i = 0; i < 2; ++i) {
    addAndMakeVisible(gain[i]);
  }

  attachAndInitializeSplineEditors(spline, selectedKnot, 7);

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

  applyTableSettings(filterTypeControl);
  applyTableSettings(frequency);
  applyTableSettings(resonance);
  applyTableSettings(bandwidth);

  for (int i = 0; i < 2; ++i) {
    applyTableSettings(gain[i]);
  }

  for (int c = 0; c < 2; ++c) {
    frequency.getControl(c).setTextValueSuffix("Hz");
  }

  for (int i = 0; i < 2; ++i) {
    for (int c = 0; c < 2; ++c) {
      gain[i].getControl(c).setTextValueSuffix("dB");
    }
  }

  for (int c = 0; c < 2; ++c) {
    filterAttachment[c] = std::make_unique<FloatAttachment>(
      *p.getOverdrawParameters().apvts,
      c == 0 ? "Filter_ch0" : "Filter_ch1",
      [this] { onFilterChanged(); },
      NormalisableRange<float>(
        0.f, OverdrawAudioProcessor::numFilterTypes, 1.f));
  }

  linkFilterAttachment =
    std::make_unique<BoolAttachment>(*p.getOverdrawParameters().apvts,
                                     "Filter_is_linked",
                                     [this] { onFilterChanged(); });

  onFilterChanged();

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
  constexpr int height = 240._p;
  constexpr int top = 10._p;

  g.fillRect(juce::Rectangle<int>(left, top, width, height));

  g.setColour(lineColour);
  g.drawRect(left, top, width, 45._p, 1);
  g.drawRect(left, top, width, 125._p, 1);
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

  int const inputGainTopAlignment = splineEditorSide - 320._p;
  int const inputGainBottomAlignment =
    selectedKnot.getBottom() + 1 - offset - 2 * 160._p;

  int const inputGainTop =
    inputGainTopAlignment +
    0.0 * (inputGainBottomAlignment - inputGainTopAlignment);

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
  resize(filterTypeControl,
         getWidth() - 2 * offset - 3 * (140._p) - (120._p) - (50._p) + 4);
  resize(frequency, 140._p);
  resize(resonance, 140._p);
  resize(bandwidth, 140._p);

  Grid grid;
  using Track = Grid::TrackInfo;

  grid.templateColumns = { Track(1_fr) };

  grid.templateRows = { Track(Grid::Px(40._p)), Track(Grid::Px(40._p)),
                        Track(Grid::Px(40._p)), Track(Grid::Px(40._p)),
                        Track(Grid::Px(40._p)), Track(Grid::Px(40._p)) };

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
    splineEditorSide + 2 * offset, offset, (190._p) - 1, 240._p));

  url.setTopLeftPosition(10._p, getHeight() - 18._p);
  url.setSize(160._p, 16._p);

  spline.areaInWhichToDrawKnots = juce::Rectangle<int>(
    selectedKnot.getPosition().x,
    spline.getBottom() - offset,
    jmax(spline.getWidth(), selectedKnot.getWidth()),
    selectedKnot.getBottom() - spline.getBottom() + offset);
}

void
OverdrawAudioProcessorEditor::onFilterChanged()
{
  if (!linkFilterAttachment) {
    return;
  }

  for (int i = 0; i < 2; ++i) {

    if (!filterAttachment[i]) {
      return;
    }

    bool const isLinked = linkFilterAttachment->getValue();

    filterType[i] = static_cast<FilterType>(
      (int)(filterAttachment[isLinked ? 0 : i]->getValue()));
  }

  setupFilterControls();
}

void
OverdrawAudioProcessorEditor::setupFilterControls()
{
  for (int i = 0; i < 2; ++i) {
    bool const useFilter = filterType[i] != FilterType::waveShaper;

    bool const useBandwidth = filterType[i] == FilterType::normalizedBandPass;

    resonance.getControl(i).setEnabled(useFilter && !useBandwidth);
    bandwidth.getControl(i).setEnabled(useFilter && useBandwidth);
    frequency.getControl(i).setEnabled(useFilter);
  }

  auto const enableLabelAndLink = [](auto& c) {
    bool const isEnabled =
      c.getControl(0).isEnabled() || c.getControl(1).isEnabled();
    c.getLabel().setEnabled(isEnabled);
    c.getLabel().setEnabled(isEnabled);
    c.getLinked()->setEnabled(isEnabled);
  };

  enableLabelAndLink(resonance);
  enableLabelAndLink(bandwidth);
  enableLabelAndLink(frequency);
}

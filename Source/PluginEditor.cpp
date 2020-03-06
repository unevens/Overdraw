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

  , wet(*p.getOverdrawParameters().apvts, "Wet", p.getOverdrawParameters().wet)

  , symmetry(*p.getOverdrawParameters().apvts,
             "Symmetry",
             p.getOverdrawParameters().symmetry)

  , channelLabels(*p.getOverdrawParameters().apvts, "Mid-Side")

  , oversampling(*this,
                 *p.getOverdrawParameters().apvts,
                 "Oversampling",
                 { "1x", "2x", "4x", "8x", "16x", "32x" })

  , linearPhase(*this,
                *p.getOverdrawParameters().apvts,
                "Linear-Phase-Oversampling")

  , smoothing(*this, *p.getOverdrawParameters().apvts, "Smoothing-Time")

  , vuMeter({ { &p.vuMeterResults[0], &p.vuMeterResults[1] } },
            36.f,
            [](float x) { return std::sqrt(x); })

  , background(ImageCache::getFromMemory(BinaryData::background_png,
                                         BinaryData::background_pngSize))

{
  addAndMakeVisible(spline);
  addAndMakeVisible(selectedKnot);
  addAndMakeVisible(oversamplingLabel);
  addAndMakeVisible(smoothingLabel);
  addAndMakeVisible(symmetry);
  addAndMakeVisible(wet);
  addAndMakeVisible(vuMeter);
  addAndMakeVisible(url);
  addAndMakeVisible(channelLabels);

  for (int i = 0; i < 2; ++i) {
    addAndMakeVisible(gain[i]);
  }

  attachAndInitializeSplineEditors(spline, selectedKnot, 7);

  oversamplingLabel.setJustificationType(Justification::centred);
  smoothingLabel.setJustificationType(Justification::centred);

  midSide.getControl().setButtonText("Mid Side");
  linearPhase.getControl().setButtonText("Linear Phase");

  vuMeter.internalColour = backgroundColour;

  lineColour = p.looks.frontColour.darker(1.f);

  auto tableSettings = LinkableControlTable();
  tableSettings.lineColour = lineColour;
  tableSettings.backgroundColour = backgroundColour;
  selectedKnot.setTableSettings(tableSettings);

  auto const applyTableSettings = [&](auto& linkedControls) {
    linkedControls.tableSettings.lineColour = lineColour;
    linkedControls.tableSettings.backgroundColour = backgroundColour;
  };

  applyTableSettings(channelLabels);
  applyTableSettings(symmetry);
  applyTableSettings(wet);

  for (int i = 0; i < 2; ++i) {
    applyTableSettings(gain[i]);
  }

  for (int i = 0; i < 2; ++i) {
    for (int c = 0; c < 2; ++c) {
      gain[i].getControl(c).setTextValueSuffix("dB");
    }
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

  setSize(825._p, 1010._p);
}

OverdrawAudioProcessorEditor::~OverdrawAudioProcessorEditor() {}

void
OverdrawAudioProcessorEditor::paint(Graphics& g)
{
  g.drawImage(background, getLocalBounds().toFloat());

  g.setColour(backgroundColour);

  constexpr int left = 625._p;
  constexpr int width = (190._p) - 1;
  constexpr int top = 625._p;

  auto const makeRect = [&](juce::Rectangle<int> r) {
    g.setColour(backgroundColour);
    g.fillRect(r);
    g.setColour(lineColour);
    g.drawRect(r, 1);
  };

  makeRect({ left, top, width, (int)40._p });
  makeRect({ left, top + (int)80._p, width, (int)80._p });
  makeRect({ left, top + (int)210._p, width, (int)120._p });

  g.setColour(lineColour);
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

  int left = offset;

  auto const resize = [&](auto& c, int width) {
    c.setTopLeftPosition(left, selectedKnot.getBottom() + offset);
    c.setSize(width, 160._p);
    left += width - 1;
  };

  resize(channelLabels, 50._p);
  resize(symmetry, 140._p);
  resize(gain[0], 140._p);
  resize(gain[1], 140._p);
  resize(wet, 140._p);

  left = splineEditorSide + 2 * offset;
  int top = splineEditorSide + 2 * offset;
  int const width = (190._p) - 1;

  using Track = Grid::TrackInfo;

  midSide.getControl().setTopLeftPosition(left + 40._p, top);
  midSide.getControl().setSize(100._p, 40._p);

  top += 80._p;

  {
    Grid grid;
    using Track = Grid::TrackInfo;

    grid.templateColumns = { Track(1_fr) };

    grid.templateRows = { Track(Grid::Px(40._p)), Track(Grid::Px(40._p)) };
    grid.items = {
      GridItem(smoothingLabel)
        .withAlignSelf(GridItem::AlignSelf::start)
        .withJustifySelf(GridItem::JustifySelf::center),
      GridItem(smoothing.getControl())
        .withWidth(135._p)
        .withAlignSelf(GridItem::AlignSelf::start)
        .withJustifySelf(GridItem::JustifySelf::center),
    };

    grid.performLayout(juce::Rectangle<int>(left, top, width, 80._p));
  }

  top += 130._p;

  {
    Grid grid;
    using Track = Grid::TrackInfo;

    grid.templateColumns = { Track(1_fr) };

    grid.templateRows = { Track(Grid::Px(40._p)),
                          Track(Grid::Px(40._p)),
                          Track(Grid::Px(40._p)) };
    grid.items = { GridItem(oversamplingLabel),
                   GridItem(oversampling.getControl())
                     .withWidth(70)
                     .withHeight(30._p)
                     .withAlignSelf(GridItem::AlignSelf::center)
                     .withJustifySelf(GridItem::JustifySelf::center),
                   GridItem(linearPhase.getControl())
                     .withWidth(120)
                     .withAlignSelf(GridItem::AlignSelf::center)
                     .withJustifySelf(GridItem::JustifySelf::center) };

    grid.performLayout(juce::Rectangle<int>(left, top, width, 120._p));
  }

  vuMeter.setTopLeftPosition(left + 0.5f * (width - 89._p), offset);
  vuMeter.setSize(89._p, splineEditorSide);

  url.setTopLeftPosition(10._p, getHeight() - 18._p);
  url.setSize(160._p, 16._p);

  spline.areaInWhichToDrawKnots = juce::Rectangle<int>(
    selectedKnot.getPosition().x,
    spline.getBottom() - offset,
    jmax(spline.getWidth(), selectedKnot.getWidth()),
    selectedKnot.getBottom() - spline.getBottom() + offset);
}

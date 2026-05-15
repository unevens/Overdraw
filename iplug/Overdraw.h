#pragma once

#include "IPlug_include_in_plug_hdr.h"

// JUCE-free Overdraw DSP + its dependencies (audio-dsp, oversimple, avec).
#include "OverdrawDsp.h"
#include "oversimple/Oversampling.hpp"
#include "avec/Alignment.hpp"
#include "avec/Buffer.hpp"

// ISender for audio-thread → UI-thread VU meter packets.
#include "ISender.h"

// Forward declarations to keep IControls.h out of the plugin header — the
// .cpp includes it directly when it needs the full ITextControl /
// IVTrackControlBase definitions.
namespace iplug {
namespace igraphics {
  class ITextControl;
  class IVTrackControlBase;
}
}

#include <array>
#include <atomic>
#include <mutex>

const int kNumPresets = 1;

// Parameter layout — mirrors the JUCE-side APVTS layout. See:
//   /Users/io/dev/Overdraw/Source/PluginProcessor.cpp ::Parameters
//   /Users/io/dev/Overdraw/juicy/SplineParameters.cpp ::SplineParameters
// Linkable pairs are stored as (ch0, ch1, link-bool) triples; knots as
// 10-param blocks matching SplineParameters' construction order. Symmetry
// is a *bool* linkable pair — special-cased in OnReset / ProcessBlock and
// in the matrix UI cell.

#define OVERDRAW_KNOT_ENUM(i)                                                  \
  kKnot##i##_enabled, kKnot##i##_linked,                                       \
  kKnot##i##_X_ch0,   kKnot##i##_Y_ch0,                                        \
  kKnot##i##_Tan_ch0, kKnot##i##_Smooth_ch0,                                   \
  kKnot##i##_X_ch1,   kKnot##i##_Y_ch1,                                        \
  kKnot##i##_Tan_ch1, kKnot##i##_Smooth_ch1

enum EParams
{
  // -- Globals --
  kMidSide = 0,
  kSmoothingTime,
  kOversampling,
  kLinearPhaseOversampling,

  // -- Linkable pairs (ch0, ch1, link-bool) --
  // NOTE: Symmetry is a bool-bool-bool triple (not float-float-bool). The
  // matrix UI puts toggles in the L / R rows instead of knobs.
  kSymmetry_ch0,    kSymmetry_ch1,    kSymmetry_link,
  kWet_ch0,         kWet_ch1,         kWet_link,
  kInputGain_ch0,   kInputGain_ch1,   kInputGain_link,
  kOutputGain_ch0,  kOutputGain_ch1,  kOutputGain_link,

  // -- Spline knots (15 editable × 10 = 150). No fixed anchors. Default-
  //    enabled indices are 7, 9, 11 (JUCE's `enabledKnotIndices`).
  OVERDRAW_KNOT_ENUM(1),
  OVERDRAW_KNOT_ENUM(2),
  OVERDRAW_KNOT_ENUM(3),
  OVERDRAW_KNOT_ENUM(4),
  OVERDRAW_KNOT_ENUM(5),
  OVERDRAW_KNOT_ENUM(6),
  OVERDRAW_KNOT_ENUM(7),
  OVERDRAW_KNOT_ENUM(8),
  OVERDRAW_KNOT_ENUM(9),
  OVERDRAW_KNOT_ENUM(10),
  OVERDRAW_KNOT_ENUM(11),
  OVERDRAW_KNOT_ENUM(12),
  OVERDRAW_KNOT_ENUM(13),
  OVERDRAW_KNOT_ENUM(14),
  OVERDRAW_KNOT_ENUM(15),

  kNumParams
};

#undef OVERDRAW_KNOT_ENUM

// Layout invariants used by the loop-based Init code and the DSP.
static_assert(kSymmetry_ch1     - kSymmetry_ch0     == 1, "linkable layout");
static_assert(kSymmetry_link    - kSymmetry_ch0     == 2, "linkable layout");
static_assert(kWet_ch0          - kSymmetry_ch0     == 3, "linkable triple stride");
static_assert(kKnot2_enabled    - kKnot1_enabled    == 10, "knot stride");

enum ECtrlTags
{
  kCtrlTagTitle = 0,
  kCtrlTagVersionNumber,
  kCtrlTagLevelMeter,
  kCtrlTagGainMeter,
  kCtrlTagSplineEditor,
};

using namespace iplug;
using namespace igraphics;

class Overdraw final : public Plugin
{
public:
  Overdraw(const InstanceInfo& info);

  // Runs on the UI thread. Drains both sender queues and pushes packets to
  // the meter controls via their ctrl tags.
  void OnIdle() override;

  // Fires when the host closes the plugin editor. IGraphics destroys all
  // controls on close (RemoveAllControls in OnViewDestroyed), so we must
  // null our cached side-panel control pointers before the idle timer
  // dereferences them. (Same crash class as Curvessor's iplug2 port.)
  void OnUIClose() override;

  // Linkable-pair propagation: when a ch0 or ch1 param changes and the
  // pair's _is_linked toggle is on, copy the change to the other channel
  // so both knobs stay in sync during a drag (and both stay in sync at
  // rest, so unlinking later doesn't pop ch1 to a stale value).
  void OnParamChangeUI(int paramIdx, EParamSource source = kUnknown) override;

private:
  // Guards against infinite recursion when OnParamChangeUI calls
  // SendParameterValueFromUI on the other channel — that call re-enters
  // OnParamChangeUI for the other param, which would otherwise try to
  // mirror back to the first and loop forever.
  bool mPropagatingLinkChange = false;

public:

#if IPLUG_EDITOR
  bool OnHostRequestingSupportedViewConfiguration(int width, int height) override { return true; }
#endif

#if IPLUG_DSP
  void OnReset() override;
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;

private:
  // Read all 150 spline-knot params into mDsp->autoSpline. Overdraw has
  // no fixed anchor (unlike Curvessor). Returns the number of active
  // (enabled) knots. Mirrors juicy/SplineParameters::updateSpline(AutoSpline&).
  int UpdateSplineFromParams();

  // Reconfigure the wet + dry oversamplers if Oversampling or
  // Linear-Phase-Oversampling changed since the last call. Called from
  // ProcessBlock under mOversamplingMutex.
  void EnsureOversamplingCurrent();

  // Read a linkable param honoring its _is_linked toggle. When linked, both
  // channels return the value at ch0Idx; otherwise each channel returns its
  // own. Matches juicy/LinkableParameter::get(channel).
  double GetLinkable(int ch0Idx, int channel) const
  {
    const bool linked = GetParam(ch0Idx + 2)->Bool();
    return GetParam(ch0Idx + (linked ? 0 : channel))->Value();
  }
  bool GetLinkableBool(int ch0Idx, int channel) const
  {
    const bool linked = GetParam(ch0Idx + 2)->Bool();
    return GetParam(ch0Idx + (linked ? 0 : channel))->Bool();
  }
#endif

#if IPLUG_DSP
  avec::aligned_ptr<overdraw::Dsp> mDsp;

  oversimple::OversamplingSettings mOversamplingSettings;
  oversimple::TOversampling<double> mWetOversampling;
  oversimple::TOversampling<double> mDryOversampling;
  std::recursive_mutex mOversamplingMutex;

  // Tracks the last applied oversampling settings so we can detect param drift.
  int mLastOversamplingOrder = 0;
  bool mLastOversamplingLinearPhase = false;

  // Holds the dry signal for wet/dry blending after oversampling round-trip.
  // Allocated to GetBlockSize() in OnReset.
  avec::Buffer<double> mDryBuffer{2};

  // Per-channel smoothed gain state (linear amplitude), updated per-sample
  // in ProcessBlock from the param targets through automationAlpha.
  double mInputGain[2]  = { 1.0, 1.0 };
  double mOutputGain[2] = { 1.0, 1.0 };
  double mWetAmount[2]  = { 1.0, 1.0 };

  // VU meter rolling mean-squared accumulators (one for wet path, one for
  // dry path). The UI shows wet_dB − dry_dB so the user sees the dB delta
  // the waveshaper+output-gain is currently applying. Initialised on reset.
  Vec2d mVuMeterWet{ 0.0 };
  Vec2d mVuMeterDry{ 0.0 };
#endif

#if IPLUG_DSP || IPLUG_EDITOR
  // VU meter snapshots: written from the audio thread, read from the GUI timer.
  std::array<std::atomic<float>, 2> mLevelVuMeterResults{};
  std::array<std::atomic<float>, 2> mGainVuMeterResults{};

  // Cross-thread queues for the IVMeterControl widgets. Carrying linear
  // amplitudes (not dB) — IVMeterControl::EResponse::Log calls AmpToDB
  // internally and maps the result to [0,1] for the bar fill.
  ISender<2> mLevelMeterSender;
  ISender<2> mGainMeterSender;
#endif

public:
  // Selected-knot state for the side panel. Updated by the spline editor on
  // click / drag and read by Draw to highlight + by SetSelectedKnot to rebind
  // the panel knobs.
  // Default = i=11 (the rightmost of the default-active knots 7, 9, 11) so
  // the panel binds to the rightmost knot on first show.
  int mSelectedKnot = 11;
  int mSelectedChannel = 0;

  // Rebind the side-panel knot knobs to the selected knot's params. Called
  // from the spline editor's OnMouseDown.
  void SetSelectedKnot(int knotIdx, int channel);

private:
  // Pointers to the side-panel controls so SetSelectedKnot can rebind them.
  // Populated when mLayoutFunc attaches them; null until then.
  iplug::igraphics::IControl* mKnotPanelKnobX = nullptr;
  iplug::igraphics::IControl* mKnotPanelKnobY = nullptr;
  iplug::igraphics::IControl* mKnotPanelKnobTan = nullptr;
  iplug::igraphics::IControl* mKnotPanelKnobSmoothness = nullptr;
  iplug::igraphics::IControl* mKnotPanelLink = nullptr;
  // Matrix row labels — "Left"/"Right" → "Mid"/"Side" when the Mid-Side
  // toggle is on, kept in sync from OnIdle.
  iplug::igraphics::ITextControl* mRowLabelL = nullptr;
  iplug::igraphics::ITextControl* mRowLabelR = nullptr;
  // Level + gain meter pointers — same M/S-aware label flip as the matrix
  // row labels, applied via the meter's per-track SetTrackName.
  iplug::igraphics::IVTrackControlBase* mLevelMeter = nullptr;
  iplug::igraphics::IVTrackControlBase* mGainMeter  = nullptr;
};

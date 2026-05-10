/*
Copyright 2020-2026 Dario Mambro

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

// IMPORTANT: do NOT include JuceHeader.h or anything that pulls JUCE in.
// The whole point of this TU is to compile the heavy spline + NEON intrinsic
// codegen in isolation from the JUCE headers, which together push the Apple
// Clang frontend over the bus-error threshold.

#include "OverdrawDsp.h"

namespace overdraw {

void
Dsp::waveshape(VecBuffer<Vec2d>& io, int const numActiveKnots)
{
  autoSpline.processBlock(io, io, numActiveKnots);
}

} // namespace overdraw

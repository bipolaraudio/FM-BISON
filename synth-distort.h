
/*
	FM. BISON hybrid FM synthesis -- Clip, distort & misc. wave shaping functions.
	(C) njdewit technologies (visualizers.nl) & bipolaraudio.nl
	MIT license applies, please see https://en.wikipedia.org/wiki/MIT_License or LICENSE in the project root!

	- This is a collection, not all are used
	- There's tons more in Will Pirkle's code (came with his book) if you're looking for more
*/

#pragma once

#include "synth-global.h"

namespace SFM
{
	// Soft clipping without using tanh()
	// Source: https://www.kvraudio.com/forum/viewtopic.php?t=410794
	SFM_INLINE float PolyClip(float sample)
	{
		SFM_ASSERT(sample >= -2.f && sample <= 2.f); // [-2..2] yields a normalized sample, anything outside goes towards infinity
		return 1.5f*sample - 0.5f*sample*sample*sample;
	}

	SFM_INLINE float HyperbolicClip(float sample, float drive) // Drive interpreted as linear gain
	{
		return ultra_tanhf(sample*drive); // Little imprecise around 0, but that's perfectly OK
	}

	// Suggested by Thorsten Ørts
	SFM_INLINE float ThorstenClip(float sample)
	{
		// This function won't venture into infinity, whereas SoftClipPoly() alone does
		// It does however *not* stay within normalized sample bounds
		sample = BhaskaraSinf(PolyClip(sample));		
		return sample;
	}

	// Udo Zölzer's soft clip (https://github.com/johannesmenzel/SRPlugins/wiki/DSP-ALGORITHMS-Saturation)
	SFM_INLINE static float ZoelzerClip(float sample)
	{
		if (sample > 0.f)
			sample =  1.f - expf(-sample);
		else
			sample = -1.f + expf(sample);

		return sample;
	}

	// Cubic distortion #1
	SFM_INLINE float ClassicCubicClip(float sample)
	{
		sample = Clamp(sample);
		return sample - sample*sample*sample/3.f;
	}

	// Cubic distortion #2 (with variable amount)
	SFM_INLINE float CubicClip(float sample, float amount)
	{	
		SFM_ASSERT(amount >= 0.f && amount <= 1.f);
		return sample - amount*(1.f/3.f) * (sample*sample*sample);
	}

	// "Squarepusher" (or just 'arctangent distortion')
	SFM_INLINE static float Squarepusher(float sample, float amount)
	{
		const float scaledAmt = 1.f + amount*31.f; // Amount doesn't really have to be [0..1], it's a (soft) clip so you can drive it up the wall as much as you want
		return atanf(sample*scaledAmt)*(2.f/kPI);  // FIXME: try *a* fast_atanf() without bounds if this one shows up in performance measurements too much
	}
}

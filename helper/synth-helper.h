
/*
	FM. BISON hybrid FM synthesis -- Helper functions (mostly math related to frequencies, notes et cetera).
	(C) njdewit technologies (visualizers.nl) & bipolaraudio.nl
	MIT license applies, please see https://en.wikipedia.org/wiki/MIT_License or LICENSE in the project root!
*/

#pragma once

// To modify floating point behaviour (SSE)
#include <xmmintrin.h>
#include <pmmintrin.h>

#include <cmath>
#include <limits>

#include "../synth-global.h"

#include "synth-math.h"

namespace SFM 
{
	// Use this to, within a scope, treat all denormals as zero; this save a lot of headache when working with floating point numbers!
	// Important: SSE (and possibly AVX, check, FIXME) only!
	class DisableDenormals
	{
	public:
		DisableDenormals()
		{
			m_mxcsrRestore = _mm_getcsr();

			// Set SSE FTZ and DAZ flags (https://www.cita.utoronto.ca/~merz/intel_c10b/main_cls/mergedProjects/fpops_cls/common/fpops_set_ftz_daz.htm)
			// Basically we're treating all DEN as zero
			_MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
			_MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
		}

		~DisableDenormals()
		{
			// Restore SSE flags
			_mm_setcsr(m_mxcsrRestore);
		}

	private:
		unsigned m_mxcsrRestore;		
	};

	/* ----------------------------------------------------------------------------------------------------

		Frequency

	 ------------------------------------------------------------------------------------------------------ */

	// Frequency to pitch
	template<typename FloatType>
	SFM_INLINE static FloatType CalculatePitch(FloatType frequency, unsigned sampleRate)
	{
		return frequency/sampleRate;
	}

	// Note to frequency
	SFM_INLINE static float NoteToFreq(unsigned note)
	{
		return kBaseHz * powf(2.f, (note - 69.f) / 12.f /* 1 octave equals 12 semitones */);
	}

	/* ----------------------------------------------------------------------------------------------------

		dB/Gain

	 ------------------------------------------------------------------------------------------------------ */

	// To and from dB
	SFM_INLINE static float GainTodB(float amplitude) { return 20.f * log10f(amplitude); }
	SFM_INLINE static float dBToGain(float dB)        { return powf(10.f, dB/20.f);      }

	// The following 2 functions were procured from some code I stumbled across by "ChunkWare Music Software",
	// and I've added them to the arsenal; I haven't done any testing but they're probably a bit less
	// precise than their textbook counterparts above, but perhaps faster

	SFM_INLINE static float Lin2dB(float linear) 
	{
		constexpr float LOG_2_DB = 8.6858896380650365530225783783321f;
		return logf(linear)*LOG_2_DB;
	}
	
	SFM_INLINE static float dB2Lin(float dB) 
	{
		constexpr float DB_2_LOG = 0.11512925464970228420089957273422f;
		return expf(dB*DB_2_LOG);
	}

	/* ----------------------------------------------------------------------------------------------------

		Misc.

	 ------------------------------------------------------------------------------------------------------ */

	// Integer is power of 2?
	SFM_INLINE static bool IsPow2(unsigned value)
	{
		return value != 0 && !(value & (value - 1));
	}

	// Hard clamp (sample)
	SFM_INLINE static float Clamp(float sample)
	{
		if (sample > 1.f)
			return 1.f;
		
		if (sample < -1.f)
			return -1.f;
		
		return sample;
	}

	// Stereo linking (and rectification)
	SFM_INLINE static float GetRectifiedMaximum(float sampleL, float sampleR)
	{
		return std::max<float>(fabsf(sampleL), fabsf(sampleR));
	}

	/* ----------------------------------------------------------------------------------------------------

		Integrity checks/assertions.

	 ------------------------------------------------------------------------------------------------------ */

	// Floating point error detection
	SFM_INLINE static bool FloatCheck(float value)
	{
		if (value != 0.f && fabsf(value) < std::numeric_limits<float>::denorm_min())
			return false;
			 
		if (true == std::isnan(value))
			return false;

		if (true == std::isinf(value))
			return false;

		return true;
	}
	
	// 'Unreferenced formal parameter'
	#pragma warning (push)
	#pragma warning (disable: 4100)

	SFM_INLINE static void FloatAssert(float value)
	{
		SFM_ASSERT(true == FloatCheck(value));
	}

	// Sample check function (range & floating point).
	SFM_INLINE static void SampleAssert(float sample)
	{
		SFM_ASSERT(true == FloatCheck(sample));	
		SFM_ASSERT(sample >= -1.f && sample <= 1.f);
	}

	#pragma warning (pop)

	/* ----------------------------------------------------------------------------------------------------

		Helper functions for SVF & Biquad filter.

		Please note that the SVF filter is not 100% stable w/o oversampling, therefore until another
		filter for continuous operation (PostPass) is in place the state should be reset when it falls
		silent

		Obviously that's *not* pretty and must be fixed (FIXME, see Github issue(s))

	 ------------------------------------------------------------------------------------------------------ */

	// Normalized cutoff [0..1] to Hz
	SFM_INLINE static float BQ_CutoffToHz(float cutoff, unsigned Nyquist)
	{
		// Allowed according to SVF impl.: [16.0..Nyquist]
		const float maxCutoff = (Nyquist < kBiquadMaxFilterCutoffHz) ? Nyquist : kBiquadMaxFilterCutoffHz;
		SFM_ASSERT_NORM(cutoff);
		return kBiquadMinFilterCutoffHz + cutoff*(maxCutoff-kBiquadMinFilterCutoffHz);
	}

	// Normalized cutoff [0..1] to Hz
	SFM_INLINE static float SVF_CutoffToHz(float cutoff, unsigned Nyquist, float minCutoff = kSVFMinFilterCutoffHz)
	{
		// Allowed according to SVF impl.: [16.0..Nyquist]
		const float maxCutoff = (Nyquist < kSVFMaxFilterCutoffHz) ? Nyquist : kSVFMaxFilterCutoffHz;
		SFM_ASSERT_NORM(cutoff);
		return minCutoff + cutoff*(maxCutoff-minCutoff);
	}

	// Hz to normalized cutoff
	SFM_INLINE static float SVF_HzToCutoff(float Hz, unsigned MaxCutoffHz = SFM::kSVFMaxFilterCutoffHz, float minCutoff = SFM::kSVFMinFilterCutoffHz)
	{
		const float denominator = MaxCutoffHz-minCutoff;
		SFM_ASSERT(denominator > 0.f);
		return (Hz-minCutoff)/denominator;
	}

	// Normalized resonance [0..1] to Q
	SFM_INLINE static float SVF_ResoToQ(float resonance, float minimum = kSVFMinFilterQ)
	{
		// Allowed: [0.025..40.0]
		// Default: 0.5
		SFM_ASSERT(resonance >= 0.f && resonance <= 1.f);
		SFM_ASSERT(minimum >= 0.f);

		const float Q = minimum + resonance*(kSVFMaxFilterQ-minimum);
		SFM_ASSERT(Q <= 40.f);
		return Q;
	}

	/* ----------------------------------------------------------------------------------------------------

		Single prec. 'snap-to-zero' (nicked from JUCE)

	 ------------------------------------------------------------------------------------------------------ */

	SFM_INLINE static float SnapToZero(float value)
	{
		return (false == (value < -1.0e-8f || value > 1.0e-8f)) ? 0.f : value;
	}

	/* ----------------------------------------------------------------------------------------------------

		Specific curve(s)

	 ------------------------------------------------------------------------------------------------------ */
	
	// A curve Paul Coops came up with (sampled an acoustic piano if I'm not mistaken)
	// that sounds more realistic than linear key tracking for, well, I suppose, most
	// string instruments
	SFM_INLINE static float AcousticTrackingCurve(float value, float scale)
	{
		SFM_ASSERT(value >= 0.f && value <= 1.f);

//		const float scale = 1.448f;
//		const float offset = -1.23f;
//		const float postScale = 0.9f;
		
		const float linear = (value*1.448f + -1.23f);
		const float squaredClipped = fast_tanhf(linear*linear); // Introduces non-linearity (FIXME: use Clamp()?)
		
		// This value is used as a multiplier, so we want the inverse (this curve
		// only makes the envelope shorter)
		return 1.f - 0.9f*(scale * (1.f-squaredClipped));
	}
}

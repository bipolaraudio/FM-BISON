
/*
	FM. BISON hybrid FM synthesis -- Stateless oscillator functions.
	(C) visualizers.nl & bipolaraudio.nl
	MIT license applies, please see https://en.wikipedia.org/wiki/MIT_License or LICENSE in the project root!

	- Phase is [0..1], this range must be adhered to except for oscSine() and oscCos()
	- Band-limited (PolyBLEP) oscillators are called 'oscPoly...'
*/

#pragma once

#include "synth-global.h"

namespace SFM
{
	/*
		Sin/Cos
	*/

	SFM_INLINE static float oscSine(float phase) 
	{
		return fast_sinf(phase); 
	}
	
	SFM_INLINE static float oscCos(float phase) 
	{ 
		return fast_cosf(phase); 
	}

	/*	Naive implementations (not band-limited) */

	SFM_INLINE static float oscSaw(float phase)
	{
		SFM_ASSERT(phase >= 0.f && phase <= 1.f);
		phase += 0.5f;
		return 2.f*phase - 1.f;
	}

	SFM_INLINE static float oscRamp(float phase)
	{
		return -1.f*oscSaw(phase);
	}

	SFM_INLINE static float oscSquare(float phase)
	{
		SFM_ASSERT(phase >= 0.f && phase <= 1.f);
		return phase >= 0.5f ? 1.f : -1.f;
	}

	SFM_INLINE static float oscTriangle(float phase)
	{
		SFM_ASSERT(phase >= 0.f && phase <= 1.f);
		return -2.f * (fabsf(-1.f + (2.f*phase))-0.5f);
	}

	SFM_INLINE static float oscPulse(float phase, float duty)
	{
		SFM_ASSERT(phase >= 0.f && phase <= 1.f);
		SFM_ASSERT(duty >= 0.f && duty <= 1.f);

		const float cycle = phase;
		return (cycle < duty) ? 1.f : -1.f;
	}

	SFM_INLINE static float oscBox(float phase)
	{
		SFM_ASSERT(phase >= 0.f && phase <= 1.f);
		return phase >= 0.25f && phase <= 0.75f ? 1.f : -1.f;
	}

	/*
		Band-limited (PolyBLEP) oscillators
		
		- Based on: https://github.com/martinfinke/PolyBLEP
		- A copy of the original can be found @ /3rdparty/PolyBlep/...
		- There are a lot more waveforms ready to use (though I shouldn't go overboard, but perhaps focus on more variations of sine)
		
		I've kept this implementation and it's helper functions all in one spot, though I did
		rename a few things to keep it consistent with my style.
	*/

	namespace Poly
	{
		template<typename T> SFM_INLINE static T Squared(const T &value)
		{
			return value*value;
		}

		template<typename T> SFM_INLINE static int64_t bitwiseOrZero(const T &value) 
		{
			return static_cast<int64_t>(value) | 0;
		}

		// Adapted from "Phaseshaping Oscillator Algorithms for Musical Sound Synthesis" by Jari Kleimola, Victor Lazzarini, Joseph Timoney, and Vesa Valimaki.
		// http://www.acoustics.hut.fi/publications/papers/smc2010-phaseshaping/
		SFM_INLINE static float BLEP_Original(double point, double dT /* This is, usually, just the pitch of the oscillator */) 
		{
			if (point < dT)
				// Discontinuities between 0 & 1
				return (float) -Squared(point/dT - 1.0);
			else if (point > 1.0 - dT)
				// Discontinuities between -1 & 0
				return (float) Squared((point - 1.0)/dT + 1.0);
			else
				return 0.f;
		}
	
		// Source: http://metafunction.co.uk/all-about-digital-oscillators-part-2-blits-bleps/
		SFM_INLINE static float BLEP(double point, double dT /* This is, usually, just the pitch of the oscillator */) 
		{
			if (point < dT)
			{
				// Discontinuities between 0 & 1
				point /= dT;
				return float(point+point - point*point - 1.0);
			}			
			else if (point > 1.0 - dT)
			{
				// Discontinuities between -1 & 0
				point = (point - 1.0)/dT;
				return float(point*point + point+point + 1.0);
			}
			else
				return 0.f;
		}

		SFM_INLINE static float BLAMP(double point, double dT)
		{
			if (point < dT) 
			{
				point = point / dT - 1.0;
				return float(-1.0 / 3.0 * Squared(point) * point);
			} 
			else if (point > 1.0 - dT) 
			{
				point = (point - 1.0) / dT + 1.0;
				return float(1.0 / 3.0 * Squared(point) * point);
			} 
			else 
				return 0.f;
		}
	}

	SFM_INLINE static float oscPolySquare(float phase, double pitch)
	{
		SFM_ASSERT(phase >= 0.f && phase <= 1.f);
		SFM_ASSERT(pitch > 0.0);

		float P1 = phase + 0.5f;
		P1 -= Poly::bitwiseOrZero(P1);

		float square = phase < 0.5f ? 1.f : -1.f;
		square += Poly::BLEP(phase, pitch) - Poly::BLEP(P1, pitch);

		return square;
	}

	SFM_INLINE static float oscPolySaw(float phase, double pitch)
	{
		SFM_ASSERT(phase >= 0.f && phase <= 1.f);
		SFM_ASSERT(pitch > 0.0);

		float P1 = phase + 0.5f;
		P1 -= Poly::bitwiseOrZero(P1);

		float saw = 2.f*P1 - 1.f;
		saw -= Poly::BLEP(P1, pitch);

		return saw;
	}

	SFM_INLINE static float oscPolyRamp(float phase, double pitch)
	{
		SFM_ASSERT(phase >= 0.f && phase <= 1.f);
		SFM_ASSERT(pitch > 0.0);

		float P1 = phase;
		P1 -= Poly::bitwiseOrZero(P1);

		float ramp = 1.f - 2.f*P1;
		ramp += Poly::BLEP(P1, pitch);

		return ramp;
	}

	SFM_INLINE static float oscPolyTriangle(float phase, double pitch)
	{
		SFM_ASSERT(phase >= 0.f && phase <= 1.f);
		SFM_ASSERT(pitch > 0.0);

		float P1 = phase + 0.25f;
		float P2 = phase + 0.75f;
		P1 -= Poly::bitwiseOrZero(P1);
		P2 -= Poly::bitwiseOrZero(P2);

		float triangle = phase*4.f;
		if (triangle >= 3.f)
		{
			triangle -= 4.f;
		}
		else if (triangle > 1.f)
		{
			triangle = 2.f - triangle;
		}

		triangle += 4.f * float(pitch) * (Poly::BLAMP(P1, pitch) - Poly::BLAMP(P2, pitch));

		return triangle;
	}

	SFM_INLINE static float oscPolyRectifiedSine(float phase, double pitch)
	{
		SFM_ASSERT(phase >= 0.f && phase <= 1.f);
		SFM_ASSERT(pitch > 0.0);

		float P1 = phase + 0.25f;
		P1 -= Poly::bitwiseOrZero(P1);

//		float rectified = 2.f * sinf(kPI * P1) - 4.f/kPI;
//		rectified += k2PI * pitch * Poly::BLAMP(P1, pitch);

		float rectified = 2.f * oscSine(0.5f * P1) - 4.f*0.5f;
		rectified += 2.f * float(pitch) * Poly::BLAMP(P1, pitch);

		return rectified;
	}

	SFM_INLINE static float oscPolyTrapezoid(float phase, double pitch)
	{
		SFM_ASSERT(phase >= 0.f && phase <= 1.f);
		SFM_ASSERT(pitch > 0.0);

		float triangle = phase*4.f;
		if (triangle >= 3.f)
		{
			triangle -= 4.f;
		}
		else if (triangle > 1.f)
		{
			triangle = 2.f - triangle;
		}

		triangle = std::fmaxf(-1.f, std::fminf(1.f, 2.f*triangle));
		
		float P1 = phase + 0.125f;
		P1 -= Poly::bitwiseOrZero(P1);

		float P2 = P1 + 0.5f;
		P2 -= Poly::bitwiseOrZero(P2);

		// Triangle #1
		triangle += 4.f * float(pitch) * (Poly::BLAMP(P1, pitch) - Poly::BLAMP(P2, pitch));

		P1 = phase + 0.375f;
		P1 -= Poly::bitwiseOrZero(P1);

		P2 = P1 + 0.5f;
		P2 -= Poly::bitwiseOrZero(P2);

		// Triangle #2
		const float trapezoid = triangle += 4.f * float(pitch) * (Poly::BLAMP(P1, pitch) - Poly::BLAMP(P2, pitch));

		return trapezoid;
	}

	SFM_INLINE static float oscPolyRectangle(float phase, double pitch, float width)
	{
		SFM_ASSERT(phase >= 0.f && phase <= 1.f);
		SFM_ASSERT(pitch > 0.0);
		SFM_ASSERT(width > 0.f && width <= 1.f);

		float P1 = phase + 1.f-width;
		P1 -= Poly::bitwiseOrZero(P1);

		float rectangle = -2.f*width;
		if (phase < width)
			rectangle += 2.f;

		rectangle += Poly::BLEP(phase, pitch) - Poly::BLEP(P1, pitch);

		return rectangle;
	}

	/*
		White noise
	*/

	SFM_INLINE static float oscWhiteNoise()
	{
		return mt_randfc();
	}
}

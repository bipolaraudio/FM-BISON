
/*
	FM. BISON hybrid FM synthesis -- Misc. math functions & constants.
	(C) njdewit technologies (visualizers.nl) & bipolaraudio.nl
	MIT license applies, please see https://en.wikipedia.org/wiki/MIT_License or LICENSE in the project root!
*/

#pragma once

#include "../synth-global.h"

namespace SFM
{
	// Constants (taken from visualizers.nl's Std3DMath library)
	constexpr float kPI = 3.1415926535897932384626433832795f;
	constexpr float kHalfPI = kPI*0.5f;
	constexpr float k2PI = 2.f*kPI;
	constexpr float kEpsilon = 5.96e-08f; // Max. error for single precision (32-bit)
	constexpr float kGoldenRatio = 1.61803398875f;
	constexpr float kGoldenRatioConjugate = 0.61803398875f;
	constexpr float kRootHalf = 0.70710678118f;
	constexpr float kExp = 2.7182818284f;

	// Bezier smoothstep
	SFM_INLINE static float smoothstepf(float t)
	{
		SFM_ASSERT(t >= 0.f && t <= 1.f);
		return t*t * (3.f - 2.f*t);
	}

	SFM_INLINE static double smoothstep(double t)
	{
		SFM_ASSERT(t >= 0.0 && t <= 1.0);
		return t*t * (3.0 - 2.0*t);
	}

	// If case you wonder: plot it in Desmos' grapher
	SFM_INLINE static float steepstepf(float t)
	{
		SFM_ASSERT(t >= 0.f && t <= 1.f);
		return 1.f-expf(-t*4.f);
	}

	SFM_INLINE static double steepstep(double t)
	{
		SFM_ASSERT(t >= 0.0 && t <= 1.0);
		return 1.0-exp(-t*4.0);
	}

	// Inverse square
	SFM_INLINE static float invsqrf(float x)
	{
		SFM_ASSERT(x >= 0.f && x <= 1.f);
		x = 1.f-x;
		return 1.f - x*x;
	}

	// Scalar interpolation
	template<typename T>
	SFM_INLINE static const T lerpf(const T &a, const T &b, float t)
	{
		// Discussion: https://stackoverflow.com/questions/4353525/floating-point-linear-interpolation
//		SFM_ASSERT(t >= 0.f && t <= 1.f);
		return a*(1.f-t) + b*t;
//		return a + (b-a)*t;
	}

	template<typename T>
	SFM_INLINE static const T lerp(const T &a, const T &b, double t)
	{
//		SFM_ASSERT(t >= 0.0 && t <= 1.0);
		return a*(1.0-t) + b*t;
	}
	
	// Cosine interpolation (single & double prec.)
	SFM_INLINE static float cosinterpf(float a, float b, float t)
	{
		t = (1.f - cosf(t*kPI)) * 0.5f;
		return a*(1.f-t) + b*t;
	}

	SFM_INLINE static double cosinterp(double a, double b, double t)
	{
		t = (1.0 - cos(t*kPI)) * 0.5;
		return a*(1.0-t) + b*t;
	}

	// (GLSL) frac()
	SFM_INLINE static float fracf(float value) 
	{ 
		return fabsf(value - int(value)); 
	}

	// (HLSL) saturate
	SFM_INLINE static float saturatef(float value)
	{
		if (value < 0.f)
			return 0.f;

		else if (value > 1.f)
			return 1.f;

		return value;
	}

	// Logarithmic 'interpolator'
	SFM_INLINE static float logstepf(float x)
	{
//		SFM_ASSERT_BINORM(x);
		return 1.f - 1.f/(1.f+x);
	}

	// Approx. sinf() derived from mr. Bhaskara's theorem
	SFM_INLINE static float BhaskaraSinf(float x)
	{
		return 16.f * x * (kPI-x) / (5.f * kPI*kPI - 4.f * x * (kPI-x));
	}

	// Langrange interpolation; array sizes must match order; 'xPos' is the X value whose Y is calculated
	SFM_INLINE static float LagrangeInterpolatef(float *pX, float *pY, unsigned order, float xPos)
	{
		SFM_ASSERT(nullptr != pX && nullptr != pY);
		SFM_ASSERT(order > 0);
		
		float result = 0.f;

		for (unsigned iY = 0; iY < order; ++iY)
		{
			float length = 1.f;
			
			for (unsigned iX = 0; iX < order; ++iX)
			{
				if (iX != iY)
					length *= (xPos - pX[iX]) / (pX[iY]-pX[iX]);
			}

			result += length*pY[iY];
		}

		return result;
	}
}

#include "synth-fast-cosine.h"
#include "synth-fast-tan.h"
#include "synth-math-easings.h"

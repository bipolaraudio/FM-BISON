
/*
	FM. BISON hybrid FM synthesis -- Misc. math functions & constants.
	(C) visualizers.nl & bipolaraudio.nl
	MIT license applies, please see https://en.wikipedia.org/wiki/MIT_License or LICENSE in the project root!
*/

#pragma once

namespace SFM
{
	// Constants (taken from visualizers.nl's Std3DMath library)
	constexpr float kPI = 3.1415926535897932384626433832795f;
	constexpr float kHalfPI = kPI*0.5f;
	constexpr float k2PI = 2.f*kPI;
	constexpr float kEpsilon = 5.96e-08f; // Max. error for single precision (32-bit).
	constexpr float kGoldenRatio = 1.61803398875f;
	constexpr float kRootHalf = 0.70710678118f;

	// Bezier smoothstep
	SFM_INLINE static float smoothstepf(float t)
	{
		SFM_ASSERT(t >= 0.f && t <= 1.f);
		return t*t * (3.f - 2.f*t);
	}

	// Inverse square
	SFM_INLINE static float invsqrf(float x)
	{
		x = 1.f-x;
		return 1.f - x*x;
	}

	// Scalar interpolation
	template<typename T>
	SFM_INLINE static const T lerpf(const T &a, const T &b, float t)
	{
		// Discussion: https://stackoverflow.com/questions/4353525/floating-point-linear-interpolation
		return a*(1.f-t) + b*t;
//		return a + (b-a)*t;
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
	
	// Approx. sinf() derived from mr. Bhaskara's theorem
	SFM_INLINE static float BhaskaraSinf(float x)
	{
		return 16.f * x * (kPI-x) / (5.f * kPI*kPI - 4.f * x * (kPI-x));
	}


	// Langrage interpolator; number of values pointed to by pX and pY much match order,
	// 'xPos' is the X value whose Y we'll interpolate 
	SFM_INLINE static float LagrangeInterpolation(float *pX, float *pY, unsigned order, float xPos)
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

	
/*
	FM. BISON hybrid FM synthesis -- Oscillator (DCO/LFO).
	(C) visualizers.nl & bipolaraudio.nl
	MIT license applies, please see https://en.wikipedia.org/wiki/MIT_License or LICENSE in the project root!
*/

#include "synth-oscillator.h" 

namespace SFM
{
	const float kPolyWidthAt44100Hz = 1.f;

	// Called *once* by Bison::Bison()
	/* static */ void Oscillator::CalculateSupersawDetuneTable()
	{	
		for (unsigned iSaw = 0; iSaw < kNumPolySupersaws; ++iSaw)
		{
			s_supersawDetune[iSaw] = powf(2.f, (kPolySupersawDetune[iSaw]*0.01f)/12.f);
		}
	}

	SFM_INLINE static float CalcPolyWidth(float frequency, unsigned sampleRate, float widthRatio)
	{
		return frequency/(sampleRate/widthRatio);
	}

	/* static */ alignas(16) float Oscillator::s_supersawDetune[kNumPolySupersaws] = { 0.f };

	float Oscillator::Sample(float phaseShift, float phaseMod /* = 1.f */)
	{
		const float phase = m_phases[0].Sample();

		// FIXME: try to skip fmodf() if certain conditions are met
		const float modulated = fabsf(fmodf((phase+phaseShift) * phaseMod, 1.f));

		// Ratio to adjust PolyBLEP width
		const auto sampleRate      = GetSampleRate();
		const float polyWidthRatio = (sampleRate/44100.f)*kPolyWidthAt44100Hz;

		// PolyBLEP width for (first) oscillator
		const float polyWidth = CalcPolyWidth(GetFrequency(), sampleRate, polyWidthRatio);
		
		// This switch statement has never shown up during profiling
		float signal = 0.f;
		switch (m_form)
		{
			case kStatic:
				signal = 0.f;
				break;

			/* Bandlimited (DCO/LFO) */

			case kSine:
				signal = oscSine(modulated);
				break;
					
			case kCosine:
				signal = oscCos(modulated);
				break;
				
			case kPolyTriangle:
				signal = oscPolyTriangle(modulated, polyWidth);
				
				break;

			case kPolySquare:
				signal = oscPolySquare(modulated, polyWidth);
				break;

			case kPolySaw:
				signal = oscPolySaw(modulated, polyWidth);
				break;

			case kPolySupersaw:
				{
					// Modulation & feedback ignored!
					// I *could* fix this but it'd be computationally expensive and doesn't fit the oscillator type anyway.

					const float polyWidthScale = 0.33f;
					const float subGain = 0.354813397f; // -9dB
	
					for (unsigned iSaw = 0; iSaw < kNumPolySupersaws; ++iSaw)
					{
						auto &phaseObj = m_phases[iSaw];
						const float subPolyWidth = polyWidthScale*CalcPolyWidth(phaseObj.GetFrequency(), sampleRate, polyWidthRatio);
						signal += oscPolySaw(phaseObj.Sample(), subPolyWidth)*subGain;
					}
				}

				break;

			case kPolyRectSine:
				signal = oscPolyRectifiedSine(modulated, polyWidth);
				break;

			/* Noise */
				
			case kPinkNoise:
				signal = m_pinkOsc.Sample();
				break;

			/* LFO */

			case kSampleAndHold:
				signal = m_SandH.Sample(modulated, m_pinkOsc.Sample());
				break;

			case kRamp:
				signal = oscRamp(modulated);
				break;

			case kSaw:
				signal = oscSaw(modulated);
				break;

			case kSquare:
				signal = oscSquare(modulated);
				break;

			case kTriangle:
				signal = oscTriangle(modulated);
				break;
			
			// Unused/Unimplemented oscillators
			default:
				signal = oscWhiteNoise();

				SFM_ASSERT(false);

				break;
		}

		// Can not check for range here (because of, for example, kPolySupersaw)
		FloatAssert(signal);

		return signal;
	}
}


/*
	FM. BISON hybrid FM synthesis -- "auto-wah" implementation (WIP).
	(C) visualizers.nl & bipolaraudio.nl
	MIT license applies, please see https://en.wikipedia.org/wiki/MIT_License or LICENSE in the project root!
*/

#include "synth-auto-wah.h"
#include "synth-distort.h"

namespace SFM
{
	// Local constant parameters
	// Each of these could be a parameter but I *chose* these values; we have enough knobs as it is (thanks Stijn ;))
	constexpr double kPreLowCutQ    =     2.0; // Q (SVF range)
	constexpr float  kLPResoMin     =   0.01f; //
	constexpr float  kLPResoMax     =    0.5f; //
	constexpr float  kLPCutLFORange =   0.33f; // Normalized Hz
	constexpr float  kLPCutMax      =    0.9f; //
	constexpr float  kVoxRateScale  =     2.f;

	// -9dB
	constexpr float kVoxGhostNoiseGain = 0.35481338923357547f; 

	void AutoWah::Apply(float *pLeft, float *pRight, unsigned numSamples)
	{
		// This effect is big and expensive, we'll skip it if not used
		if (0.f == m_curWet.Get() && 0.f == m_curWet.GetTarget())
		{
			m_curResonance.Skip(numSamples);
			m_curAttack.Skip(numSamples);
			m_curHold.Skip(numSamples);
			m_curRate.Skip(numSamples);
			m_curSpeak.Skip(numSamples);
			m_curSpeakVowel.Skip(numSamples);
			m_curSpeakVowelMod.Skip(numSamples);
			m_curSpeakGhost.Skip(numSamples);
			m_curCut.Skip(numSamples);
			m_curWet.Skip(numSamples);

			for (unsigned iSample = 0; iSample  < numSamples; ++iSample)
			{
				const float sampleL = pLeft[iSample];
				const float sampleR = pRight[iSample];

				// Keep running RMS calc.
				m_RMS.Run(sampleL, sampleR);
			}

			// Done
			return;
		}

		for (unsigned iSample = 0; iSample  < numSamples; ++iSample)
		{
			// Get parameters
			const float resonance = m_curResonance.Sample();
			const float curAttack = m_curAttack.Sample();
			const float curHold   = m_curHold.Sample();
			const float curRate   = m_curRate.Sample();
			const float voxWet    = m_curSpeak.Sample();
			const float voxVow    = m_curSpeakVowel.Sample();
			const float voxMod    = m_curSpeakVowelMod.Sample();
			const float voxGhost  = m_curSpeakGhost.Sample();
			const float lowCut    = m_curCut.Sample()*0.125f; // Nyquist/8 is more than enough!
			const float wetness   = m_curWet.Sample();
			
			// Set parameters
			m_sideEnv.SetAttack(curAttack * 100.f); // FIXME: why does it sound right at a tenth of the time set?
			m_sideEnv.SetRelease(curHold  * 100.f); //

			m_LFO.SetFrequency(curRate);

			m_voxOscPhase.SetFrequency(curRate*kVoxRateScale);
			m_voxGhostEnv.SetRelease(kMinWahGhostReleaseMS + voxGhost*(kMaxWahGhostReleaseMS-kMinWahGhostReleaseMS));

			// Input
			const float sampleL = pLeft[iSample];
			const float sampleR = pRight[iSample];

			// Calc. RMS and feed it to sidechain to obtain (enveloped) gain
			const float signaldB = m_RMS.Run(sampleL, sampleR);
			const float envdB =  m_sideEnv.Apply(signaldB);
			const float envGain = std::min<float>(1.f, dB2Lin(envdB));

			if (envGain < kEpsilon)
			{
				// Attempt at sync.
				m_voxOscPhase.Reset();
				m_voxSandH.Reset();
			}

			// Cut off high end: that's what we'll work with
			float preFilteredL = sampleL, preFilteredR = sampleR;
			m_preFilterHP.updateCoefficients(CutoffToHz(lowCut, m_Nyquist, 0.f), kPreLowCutQ, SvfLinearTrapOptimised2::HIGH_PASS_FILTER, m_sampleRate);
			m_preFilterHP.tick(preFilteredL, preFilteredR);

			// Store remainder to add back into mix
			const float remainderL = sampleL-preFilteredL;
			const float remainderR = sampleR-preFilteredR;

			/*
				Post filter (LPF)
			*/

			float filteredL = preFilteredL, filteredR = preFilteredR;
			
			// Sample LFO (FIXME: study a few pedals to evaluate the need for this once more)
			const float LFO = m_LFO.Sample(0.f);
			
			// Calc. cutoff
			const float cutRange    = envGain*kLPCutLFORange;
			const float normCutoff  = cutRange + (envGain * (1.f - 2.f*cutRange)) + LFO*cutRange;

			SFM_ASSERT(normCutoff >= 0.f && normCutoff <= 1.f);
			const float cutoffHz = CutoffToHz(normCutoff*kLPCutMax, m_Nyquist);

			// Calc. Q (less signal: lower resonance peak)
			const float rangeQ = (kLPResoMax-kLPResoMin)*resonance;            
			const float normQ  = kLPResoMin + rangeQ*(1.f-envGain);
			const float Q        = ResoToQ(normQ);             

			m_postFilterLP.updateLowpassCoeff(cutoffHz, Q, m_sampleRate);
			m_postFilterLP.tick(filteredL, filteredR);

			/*
				Add (low) remainder to signal
			*/

			filteredL += remainderL;
			filteredR += remainderR;

			/*
				Vowelize
			*/
			
			// Calc. vox. LFO A (sample) and B (amplitude)
			const float voxPhase = m_voxOscPhase.Sample();
			const float oscInput = mt_randfc();
			const float voxOsc   = m_voxSandH.Sample(voxPhase, oscInput);
			const float toLFO    = 1.f-expf(-voxMod*4.f); // smoothstepf(voxMod);
			const float voxLFO_A = lerpf<float>(0.f, voxOsc, toLFO);
			const float voxLFO_B = lerpf<float>(1.f, fabsf(voxOsc), toLFO);
			
			// Calc. vox. "ghost" noise
			const float ghostRand = mt_randfc();
			const float ghostSig  = ghostRand*kVoxGhostNoiseGain;
			const float ghostEnv  = m_voxGhostEnv.Apply(envGain * voxLFO_B * voxGhost);
			const float ghost     = ghostSig*ghostEnv;

			// I dislike frequent fmodf() calls but according to MSVC's profiler we're in the clear
			// I add a small amount to the maximum since we need to actually reach kMaxWahSpeakVowel
			static_assert(unsigned(kMaxWahSpeakVowel) < VowelizerV1::kNumVowels-1);
			const float vowel = fabsf(fmodf(voxVow+voxLFO_A, kMaxWahSpeakVowel + 0.001f /* Leaks into 'U' vowel, which is quite similar */));
		
			// Filter and mix
			float vowelL = filteredL + ghost, vowelR = filteredR + ghost;
			m_vowelizerV1.Apply(vowelL, vowelR, vowel);

			filteredL = lerpf<float>(filteredL, vowelL, voxWet);
			filteredR = lerpf<float>(filteredR, vowelR, voxWet);

			/*
				Final mix
			*/

			pLeft[iSample]  = lerpf<float>(sampleL, filteredL, wetness);
			pRight[iSample] = lerpf<float>(sampleR, filteredR, wetness);
		}
	}
}

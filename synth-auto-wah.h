
/*
	FM. BISON hybrid FM synthesis -- "auto-wah" implementation (WIP).
	(C) visualizers.nl & bipolaraudio.nl
	MIT license applies, please see https://en.wikipedia.org/wiki/MIT_License or LICENSE in the project root!

	FIXME: 
		- Currently working on sound & testing (see synth-post-pass.cpp) first draft
		- Decide on initial set of parameters & rig them
		- Vary those vowels a little bit according to, ...?
*/

#pragma once

#include "3rdparty/SvfLinearTrapOptimised2.hpp"

#include "synth-global.h"
#include "synth-followers.h"
#include "synth-delay-line.h"
#include "synth-vowelizer.h"
#include "synth-one-pole-filters.h"

namespace SFM
{
	class AutoWah
	{
	private:

	public:
		// I've added possible parameter names (more adventurous than usual!) & ranges
		const float kWahDelay = 0.003f;                // Constant (for now)
		const float kWahPeakRelease = 0.01f;           // "Bite"  [0.01..0.1]
		const float kWahAttack = 0.01f;                // "Speed" [0.01..1.0]
		const float kWahRelease = 0.0f;                // "Hold"  [0.0..0.1]
		const float kWahLookahead = kGoldenRatio*0.1f; // Constant (for now)
		const float kWahQCutoff = 120.f;               // "Slime"

		AutoWah(unsigned sampleRate, unsigned Nyquist) :
			m_sampleRate(sampleRate), m_Nyquist(Nyquist)
,			m_outDelayL(sampleRate, kWahDelay)
,			m_outDelayR(sampleRate, kWahDelay)
,			m_detectorL(sampleRate, kWahPeakRelease)
,			m_detectorR(sampleRate, kWahPeakRelease)
,			m_gainShaper(sampleRate, kWahAttack, kWahRelease)
,			m_resoLPF(kWahQCutoff/sampleRate)
,			m_lookahead(kWahLookahead)
		{
		}

		~AutoWah() {}

		SFM_INLINE void SetParameters(float bite, float attack, float release, float slime, float wetness)
		{
			// SFM_ASSERT(bite >= kConstant && bite <= kConstant);
			// SFM_ASSERT(slime >= kConstant && slime <= kConstant);
			SFM_ASSERT(wetness >= 0.f && wetness <= 1.f);

			// Borrowed compressor's constants:
			SFM_ASSERT(attack >= kMinCompAttack && attack <= kMaxCompAttack);
			SFM_ASSERT(release >= kMinCompRelease && attack <= kMaxCompRelease);

			m_gainShaper.SetAttack(attack);
			m_gainShaper.SetRelease(release);

			// FIXME
//			m_resoLPF.SetCutoff(...);

			m_wetness   = wetness;
			m_lookahead = kWahLookahead;
		}

		SFM_INLINE void Apply(float &left, float &right)
		{
			// Delay input signal
			m_outDelayL.Write(left);
			m_outDelayR.Write(right);

			// Detect peak using non-delayed signal
			// FIXME: might it be an idea to share this detection with Compressor?
			const float peakOutL  = m_detectorL.Apply(left);
			const float peakOutR  = m_detectorR.Apply(right);
			const float peakSum   = fast_tanhf(peakOutL+peakOutR)*0.5f; // Soft clip peak sum sounds *good*
			const float sum       = peakSum;
			const float sumdB     = (0.f != sum) ? GainTodB(sum) : kMinVolumedB;

			// Calculate gain
			const float gaindB      = m_gainShaper.Apply(sumdB);
			const float gain        = dBToGain(gaindB);
			const float clippedGain = fast_tanhf(gain);
			
			// Grab (delayed) signal
			
			// This is *correct*
//			const auto  delayL   = (m_outDelayL.size() - 1)*m_lookahead;
//			const auto  delayR   = (m_outDelayR.size() - 1)*m_lookahead; // Just in case R's size differs from L (for whatever reason)
//			const float delayedL = m_outDelayL.Read(delayL);
//			const float delayedR = m_outDelayR.Read(delayR);

			// This sounded better for Compressor, test some more! (FIXME)
			const float delayedL = lerpf<float>(left,  m_outDelayL.ReadNearest(-1), m_lookahead);
			const float delayedR = lerpf<float>(right, m_outDelayR.ReadNearest(-1), m_lookahead);

			float filteredL = delayedL, filteredR = delayedR;

			// Vowel: A
			const float vowInputA = (delayedL+delayedR)*0.25f*clippedGain;
			const float vowelA = m_vowelizers[1].Apply(vowInputA, Vowelizer::kA);
			filteredL += vowelA;
			filteredR += vowelA;

			// Vowel: U
//			const float vowInputU = (delayedL+delayedR)*0.25f*clippedGain;
			const float vowelU = m_vowelizers[0].Apply(vowInputA, Vowelizer::kU);
			filteredL += vowelU;
			filteredR += vowelU;

			// Calculate cutoff
			const float cutoff = CutoffToHz(clippedGain, m_Nyquist);
			
			// Filter (LP)
			const float Q = ResoToQ(1.f-clippedGain);
			const float filteredQ = m_resoLPF.Apply(Q);
			m_filterLP.updateCoefficients(cutoff, filteredQ, SvfLinearTrapOptimised2::LOW_PASS_FILTER, m_sampleRate);
			m_filterLP.tick(filteredL, filteredR);
			
			// Mix (FIXME: dry/wet)
			const float mixGain = 0.707f; // Approx. -3dB
			left  = filteredL*mixGain;
			right = filteredR*mixGain;
		}

	private:
		const unsigned m_sampleRate;
		const unsigned m_Nyquist;

		DelayLine m_outDelayL, m_outDelayR;
		PeakDetector m_detectorL, m_detectorR;
		GainShaper m_gainShaper;
		LowpassFilter m_resoLPF;
		SvfLinearTrapOptimised2 m_filterLP;
		Vowelizer m_vowelizers[2];

		// Local parameters
		float m_wetness;
		float m_lookahead;
	};
}

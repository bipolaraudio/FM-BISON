
/*
	FM. BISON hybrid FM synthesis -- Basic compressor.
	(C) visualizers.nl & bipolaraudio.nl
	MIT license applies, please see https://en.wikipedia.org/wiki/MIT_License or LICENSE in the project root!

	Basic impl. that got me started: https://github.com/buosseph/JuceCompressor
	Thanks to Tammo Hinrichs for a few good tips!
*/

#pragma once

#include "synth-global.h"
#include "synth-followers.h"
#include "synth-delay-line.h"
#include "synth-interpolated-parameter.h"

namespace SFM
{
	class Compressor
	{
	public:
		// FIXME: parameter?
		const float kCompMaxDelay = 0.01f; 

		Compressor(unsigned sampleRate) :
			m_sampleRate(sampleRate)
,			m_outDelayL(sampleRate, kCompMaxDelay)
,			m_outDelayR(sampleRate, kCompMaxDelay)
,			m_detectorL(sampleRate)
,			m_detectorR(sampleRate)
,			m_RMSDetector(sampleRate, 0.01f /* 10MS: Twice Reaper's compressor default */)
,			m_gainShaper(sampleRate, kDefCompAttack, kDefCompRelease)
,			m_curPeakToRMS(kDefCompPeakToRMS, sampleRate, kDefParameterLatency)
,			m_curThresholddB(kDefCompThresholddB, sampleRate, kDefParameterLatency)
,			m_curKneedB(kDefCompKneedB, sampleRate, kDefParameterLatency)
,			m_curRatio(kDefCompRatio, sampleRate, kDefParameterLatency)
,			m_curGaindB(kDefCompGaindB, sampleRate, kDefParameterLatency)
,			m_curAttack(kDefCompAttack, sampleRate, kDefParameterLatency)
,			m_curRelease(kDefCompRelease, sampleRate, kDefParameterLatency)
,			m_curLookahead(0.f, sampleRate, kDefParameterLatency)
		{
		}

		~Compressor() {}

		SFM_INLINE void SetParameters(float peakToRMS, float thresholddB, float kneedB, float ratio, float gaindB, float attack, float release, float lookahead)
		{
			SFM_ASSERT(peakToRMS >= 0.f && peakToRMS <= 1.f);
			SFM_ASSERT(thresholddB >= kMinCompThresholdB && thresholddB <= kMaxCompThresholdB);
			SFM_ASSERT(kneedB >= kMinCompKneedB && kneedB <= kMaxCompKneedB);
			SFM_ASSERT(ratio >= kMinCompRatio && ratio <= kMaxCompRatio);
			SFM_ASSERT(gaindB >= kMinCompGaindB && gaindB <= kMaxCompGaindB);
			SFM_ASSERT(attack >= kMinCompAttack && attack <= kMaxCompAttack);
			SFM_ASSERT(release >= kMinCompRelease && attack <= kMaxCompRelease);
			SFM_ASSERT(lookahead >= 0.f && lookahead <= 1.f);

			m_curPeakToRMS.SetTarget(peakToRMS);
			m_curThresholddB.SetTarget(thresholddB);
			m_curKneedB.SetTarget(kneedB);
			m_curRatio.SetTarget(ratio);
			m_curGaindB.SetTarget(gaindB);
			m_curAttack.SetTarget(attack);
			m_curRelease.SetTarget(release);
			m_curLookahead.SetTarget(lookahead);
		}

		void Apply(float *pLeft, float *pRight, unsigned numSamples);

	private:
		const unsigned m_sampleRate;

		DelayLine m_outDelayL, m_outDelayR;
		PeakDetector m_detectorL, m_detectorR;
		RMSDetector m_RMSDetector;
		GainShaper m_gainShaper;

		// Interpolated parameters
		InterpolatedParameter<kLinInterpolate> m_curPeakToRMS;
		InterpolatedParameter<kLinInterpolate> m_curThresholddB;
		InterpolatedParameter<kLinInterpolate> m_curKneedB;
		InterpolatedParameter<kLinInterpolate> m_curRatio;
		InterpolatedParameter<kLinInterpolate> m_curGaindB;
		InterpolatedParameter<kLinInterpolate> m_curAttack;
		InterpolatedParameter<kLinInterpolate> m_curRelease;
		InterpolatedParameter<kLinInterpolate> m_curLookahead;
	};
}

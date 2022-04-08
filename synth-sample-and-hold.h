
/*
	FM. BISON hybrid FM synthesis -- S&H oscillator (or rather a filter).
	(C) njdewit technologies (visualizers.nl) & bipolaraudio.nl
	MIT license applies, please see https://en.wikipedia.org/wiki/MIT_License or LICENSE in the project root!
*/

#pragma once

#include "synth-global.h"
#include "synth-stateless-oscillators.h"
#include "synth-phase.h"
#include "synth-interpolated-parameter.h"
#include "synth-signal-follower.h"

namespace SFM
{
	class SampleAndHold
	{
	public:
		SampleAndHold(unsigned sampleRate) :
			m_sampleRate(sampleRate)
,			m_curSignal(0.f, sampleRate, m_slewRate, 0.f, 1.f)
		{
			Reset();
		}

		~SampleAndHold() {}

		SFM_INLINE float Sample(float phase, float input)
		{
			const float curGate = oscSquare(phase);
			
			// Gate?
			if (m_prevGate != curGate)
			{
				const float curSignal = m_curSignal.Get();
				m_curSignal.SetRate(m_sampleRate, m_slewRate);
				m_curSignal.Set(curSignal);
				m_curSignal.SetTarget(input);
			}

			m_prevGate = curGate;

			// Slew towards target input
			return Clamp(m_curSignal.Sample());
		}

		void SetSlewRate(float rateInSeconds)
		{
			SFM_ASSERT(rateInSeconds >= kMinSandHSlewRate && rateInSeconds <= kMaxSandHSlewRate)
			m_slewRate = rateInSeconds;
		}

		void Reset()
		{
			m_prevGate = -1.f;
			m_curSignal.Set(0.f);
		}

	private:
		// Parameters
		/* const */ unsigned m_sampleRate;

		float m_slewRate = kDefSandHSlewRate;

		// State
		float m_prevGate = -1.f;
		InterpolatedParameter<kLinInterpolate, false> m_curSignal;
	};
}

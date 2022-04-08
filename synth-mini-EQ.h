
/*
	FM. BISON hybrid FM synthesis -- Mini EQ (3-band).
	(C) njdewit technologies (visualizers.nl) & bipolaraudio.nl
	MIT license applies, please see https://en.wikipedia.org/wiki/MIT_License or LICENSE in the project root!

	FIXME:
		- Turn into (semi-)3-band full cut EQ
*/

#pragma once

#include "3rdparty/filters/Biquad.h"

#include "synth-global.h"
#include "synth-interpolated-parameter.h"

namespace SFM
{
	// Use the peak filter to tweak Q to a sort of satisfactory shape: https://www.earlevel.com/main/2013/10/13/biquad-calculator-v2/
	constexpr float kMidQ = kNormalGainAtCutoff; // kPI*2.f;

	// Centre frequencies (I hope, also, looked at https://blog.landr.com/eq-basics-everything-musicians-need-know-eq/ for ref.)
	constexpr float kLoHz  = 80.f;
	constexpr float kMidHz = 1200.f;
	constexpr float kHiHz  = 4000.f;

	class MiniEQ
	{
	public:
		MiniEQ(unsigned sampleRate, bool withMid) :
			m_withMid(withMid)
,			m_bassFc(kLoHz/sampleRate)
,			m_trebleFc(kHiHz/sampleRate)
,			m_midFc(kMidHz/sampleRate)
,			m_bassdB(0.f, sampleRate, kDefParameterLatency, 0.f, 1.f)
,			m_trebledB(0.f, sampleRate, kDefParameterLatency, 0.f, 1.f)
,			m_middB(0.f, sampleRate, kDefParameterLatency, 0.f, 1.f)
		{
			SetBiquads();
		}

		~MiniEQ() {}

		void SetTargetdBs(float bassdB, float trebledB, float middB = 0.f);

		SFM_INLINE void Apply(float &sampleL, float &sampleR)
		{
			SetBiquads();

			if (true == m_withMid)
			{
				m_midPeak.process(sampleL, sampleR); // First push or pull MID freq.
			}
			
			float loL = sampleL, loR = sampleR;
			m_bassShelf.process(loL, loR);

			float hiL = sampleL, hiR = sampleR;
			m_trebleShelf.process(hiL, hiR);

			// Not sure if this is right at all, I'll keep the ticket open, but it does the trick for now
			sampleL = (loL+hiL)*kNormalGainAtCutoff;
			sampleR = (loR+hiR)*kNormalGainAtCutoff;
		}
		
		// Code duplication, but what are we going to do about it outside of a huge overhaul?
		SFM_INLINE float ApplyMono(float sample)
		{
			SetBiquads();

			if (true == m_withMid)
				sample = m_midPeak.processMono(sample);

			float LO = sample;
			LO = m_bassShelf.processMono(LO);

			float HI = sample;
			HI = m_trebleShelf.processMono(HI);

			return (LO+HI)*kNormalGainAtCutoff;
		}

	private:
		const bool m_withMid;

		const float m_bassFc;
		const float m_trebleFc;
		const float m_midFc;

		Biquad m_bassShelf;
		Biquad m_trebleShelf;
		Biquad m_midPeak;

		InterpolatedParameter<kLinInterpolate, false> m_bassdB;
		InterpolatedParameter<kLinInterpolate, false> m_trebledB;
		InterpolatedParameter<kLinInterpolate, false> m_middB;
		
		// Only called if necessary
		SFM_INLINE void SetBiquads()
		{
			m_bassShelf.setBiquad(bq_type_lowshelf, m_bassFc, 0.f, m_bassdB.Sample());        // Bass
			m_trebleShelf.setBiquad(bq_type_highshelf, m_trebleFc, 0.f, m_trebledB.Sample()); // Treble

			// Mid?
			if (true == m_withMid)
				m_midPeak.setBiquad(bq_type_peak, m_midFc, kMidQ, m_middB.Sample());
		}
	};
}


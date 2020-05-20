
/*
	FM. BISON hybrid FM synthesis -- Reverb effect based on FreeVerb.
	(C) visualizers.nl & bipolaraudio.nl
	MIT license applies, please see https://en.wikipedia.org/wiki/MIT_License or LICENSE in the project root!
*/

#include "synth-reverb.h"

namespace SFM
{
	// Defaults are tuned for 44.1KHz, so we need to adjust
	SFM_INLINE static size_t ScaleNumSamples(unsigned sampleRate, size_t numSamples)
	{
		const float scale = sampleRate/44100.f;
		return size_t(floorf(numSamples*scale));
	}

	/*
		Default taken from a decent implementation (can't remember the URL)
	*/

	// Added to R
	const unsigned kStereoSpread = 23;

	// L
	const size_t kCombSizes[kReverbNumCombs] = {
		1116,
		1188,
		1277,
		1356,
		1422,
		1491,
		1557,
		1617
	};

	// L
	const size_t kAllPassSizes[kReverbNumAllPasses] = {
		556,
		441,
		341,
		225
	};

	// FIXME: test variations (https://christianfloisand.wordpress.com/tag/all-pass-filter/ mentions slightly modulating the first pass)
	const float kAllPassDefFeedback = 0.5f; // Def: 0.5
	
	const float kDefaultRoomSize = 0.8f;
	const float kDefaultWidth = 2.f;

	Reverb::Reverb(unsigned sampleRate, unsigned Nyquist) :
		m_sampleRate(sampleRate), m_Nyquist(Nyquist)
,		m_preDelayLine(sampleRate, kReverbPreDelayMax)
,		m_width(kDefaultWidth)
,       m_roomSize(kDefaultRoomSize)
,		m_preDelay(kDefReverbPreDelay)
,		m_curWet(0.f, sampleRate, kDefParameterLatency)
,		m_curWidth(kMinReverbWidth, sampleRate, kDefParameterLatency)
,		m_curRoomSize(0.f, sampleRate, kDefParameterLatency)
,		m_curDampening(0.f, sampleRate, kDefParameterLatency)
,		m_curPreDelay(0.f, sampleRate, kDefParameterLatency*3.f)
,		m_curLP(CutoffToHz(kDefReverbFilter, Nyquist), sampleRate, kDefParameterLatency)
,		m_curHP(CutoffToHz(kDefReverbFilter, Nyquist), sampleRate, kDefParameterLatency)
	{
		// Semi-fixed
		static_assert(8 == kReverbNumCombs);
		static_assert(4 == kReverbNumAllPasses);

		// Adjusted stereo spread
		const size_t stereoSpread = ScaleNumSamples(sampleRate, kStereoSpread);
		
		// Allocate buffer
		size_t totalBufSize = 0;
		
		for (auto size : kCombSizes)
		{
			size = ScaleNumSamples(sampleRate, size);
			totalBufSize += size + (size+stereoSpread);
		}
		
		for (auto size : kAllPassSizes)
		{
			size = ScaleNumSamples(sampleRate, size);
			totalBufSize += size + (size+stereoSpread);
		}
		
		m_totalBufSize = totalBufSize*sizeof(float);
		m_buffer = reinterpret_cast<float*>(mallocAligned(m_totalBufSize, 16));
		
		// Set sizes and pointers
		size_t offset = 0;
		
		for (unsigned iComb = 0; iComb < kReverbNumCombs; ++iComb)
		{
			const size_t size = ScaleNumSamples(sampleRate, kCombSizes[iComb]);
			float *pCur = m_buffer+offset;
			m_combsL[iComb].SetSizeAndBuffer(size, pCur);
			m_combsR[iComb].SetSizeAndBuffer(size+stereoSpread, pCur+size);
			offset += size + (size+stereoSpread);
		}
		
		for (unsigned iAllPass = 0; iAllPass < kReverbNumAllPasses; ++iAllPass)
		{
			const size_t size = ScaleNumSamples(sampleRate, kAllPassSizes[iAllPass]);
			float *pCur = m_buffer + offset;
			m_allPassesL[iAllPass].SetSizeAndBuffer(size, pCur);
			m_allPassesR[iAllPass].SetSizeAndBuffer(size+stereoSpread, pCur+size);
			offset += size + (size+stereoSpread);
		}

		Reset();
	}

	const float kFixedGain = 0.015f; // Ref. implementation = 0.015f

	void Reverb::Apply(float *pLeft, float *pRight, unsigned numSamples, float wet, float lowpass, float highpass)
	{
		SFM_ASSERT(nullptr != pLeft && nullptr != pRight);
		SFM_ASSERT(wet >= 0.f && wet <= 1.f);
		SFM_ASSERT(lowpass >= 0.f && lowpass <= 1.f);
		SFM_ASSERT(highpass >= 0.f && highpass <= 1.f);

		// Set parameter targets
		// Some are class members and I want to keep it that way
		m_curWet.SetTarget(wet);
		m_curWidth.SetTarget(m_width);
		m_curRoomSize.SetTarget(m_roomSize);
		m_curDampening.SetTarget(m_dampening);
		m_curPreDelay.SetTarget(m_preDelay);

		// Avoid the edges of the spectrum
		const float lowCutoffHz  = CutoffToHz(lowpass*0.9f + 0.1f, m_Nyquist);
		const float highCutoffHz = CutoffToHz(0.1f + (1.f-highpass)*0.9f, m_Nyquist);
		m_curLP.SetTarget(lowCutoffHz);
		m_curHP.SetTarget(highCutoffHz);

		for (unsigned iSample = 0; iSample < numSamples; ++iSample)
		{
			const float curWet = m_curWet.Sample() * kMaxReverbWet; // Doesn't sound like much if fully open, consider different mix below? (FIXME)
			const float dry = 1.f-curWet;

			// Stereo effect
			const float width = m_curWidth.Sample();
			const float wet1  = curWet*(width/2.f + 0.5f);
			const float wet2  = curWet*((1.f-width)/2.f);

			float outL = 0.f;
			float outR = 0.f;

			const double defQ = 0.5;
			m_preLPF.updateLowpassCoeff(m_curLP.Sample(),  defQ, m_sampleRate);
			m_preHPF.updateHighpassCoeff(m_curHP.Sample(), defQ, m_sampleRate); 
			
			const float inL = *pLeft;
			const float inR = *pRight;
			
			// Apply LPF & HPF filters (FIXME: mabe just do these for the single "mixed" signal?)
			float LPF_L = inL, LPF_R = inR;
			float HPF_L = inL, HPF_R = inR;
			m_preLPF.tick(LPF_L, LPF_R);
			m_preHPF.tick(HPF_L, HPF_R);

			// Simple mix (does the job)
			const float mix = ( (LPF_L*lowpass + LPF_R*lowpass)*0.5f + (HPF_L*highpass + HPF_R*highpass)*0.5f );

			// Pre-delay			
			m_preDelayLine.Write(mix);
			const float monaural = m_preDelayLine.Read(m_sampleRate*m_curPreDelay.Sample()) * kFixedGain;

			// Accumulate comb filters in parallel
			const float dampening = m_curDampening.Sample();
			const float roomSize  = m_curRoomSize.Sample();

			for (unsigned iComb = 0; iComb < kReverbNumCombs; ++iComb)
			{ 
				auto &combL = m_combsL[iComb];
				auto &combR = m_combsR[iComb];

				combL.SetDampening(dampening);
				combR.SetDampening(dampening);

				outL += combL.Apply(monaural, roomSize);
				outR += combR.Apply(monaural, roomSize);
			}

			// Apply remaining all pass filters in series
			for (unsigned iAllPass = 0; iAllPass < kReverbNumAllPasses; ++iAllPass)
			{
				auto &passL = m_allPassesL[iAllPass];
				auto &passR = m_allPassesR[iAllPass];
			
				outL = passL.Apply(outL, kAllPassDefFeedback);
				outR = passR.Apply(outR, kAllPassDefFeedback);
			}

			// Mix
			const float left  = outL*wet1 + outR*wet2 + inL*dry;
			const float right = outR*wet1 + outL*wet2 + inR*dry;
			*pLeft++  = left;
			*pRight++ = right;
		}
	}
}

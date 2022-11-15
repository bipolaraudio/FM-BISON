
/*
	FM. BISON hybrid FM synthesis -- Reverb effect based on FreeVerb.
	(C) njdewit technologies (visualizers.nl) & bipolaraudio.nl
	MIT license applies, please see https://en.wikipedia.org/wiki/MIT_License or LICENSE in the project root!
*/

#include "synth-reverb.h"

namespace SFM
{
	// Defaults are tuned for 44.1KHz, so we need to adjust (can be done linearly, since we're working in time (sample) domain)
	SFM_INLINE static size_t ScaleNumSamples(unsigned sampleRate, size_t numSamples)
	{
		const float scale = sampleRate/44100.f;
		return size_t(floorf(numSamples*scale));
	}

	/*
		Defaults taken from a decent FreeVerb implementation
	*/

	// Added to R
	constexpr unsigned kStereoSpread = 23;

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
	constexpr float kAllPassDefFeedback = 0.6f; // FIXME: this is a very interesting parameter to play with!
	
	constexpr float kDefaultRoomSize = 0.8f;
	constexpr float kDefaultWidth = 2.f;

	// Pre-delay line length (in seconds)
	constexpr float kReverbPreDelayLen = 0.5f; // 500MS

	Reverb::Reverb(unsigned sampleRate, unsigned Nyquist) :
		m_sampleRate(sampleRate), m_Nyquist(Nyquist)
,		m_preEQ(sampleRate, false)
,		m_preDelayLine(sampleRate, kReverbPreDelayLen)
,		m_width(kDefaultWidth)
,		m_roomSize(kDefaultRoomSize)
,		m_preDelay(0.f)
,		m_curWet(0.f, sampleRate, kDefParameterLatency, 0.f, 1.f)
,		m_curWidth(kMinReverbWidth, sampleRate, kDefParameterLatency, kMinReverbWidth, kMaxReverbWidth)
,		m_curRoomSize(0.f, sampleRate, kDefParameterLatency, 0.f, 1.f)
,		m_curDampening(0.f, sampleRate, kDefParameterLatency, 0.f, 1.f)
,		m_curPreDelay(0.f, sampleRate, kDefParameterLatency * 4.f /* Longer */, 0.f, 1.f)
,		m_curBassdB(0.f, sampleRate, kDefParameterLatency, 0.f, 1.f)
,		m_curTrebledB(0.f, sampleRate, kDefParameterLatency, 0.f, 1.f)
	{
		// Semi-fixed
		static_assert(8 == kReverbNumCombs);
		static_assert(4 == kReverbNumAllPasses);

		// Adjusted stereo spread
		const size_t stereoSpread = ScaleNumSamples(sampleRate, kStereoSpread);
		
		// Allocate single sequential buffer
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
		
		// Clear buffer
		memset(m_buffer, 0, m_totalBufSize);
	}

	constexpr float kFixedGain = 0.015f; // Taken from ref. implementation 

	void Reverb::Apply(float *pLeft, float *pRight, unsigned numSamples, float wet, float bassTuningdB, float trebleTuningdB)
	{
		SFM_ASSERT(nullptr != pLeft && nullptr != pRight);
		SFM_ASSERT_NORM(wet);
		SFM_ASSERT(bassTuningdB >= kMiniEQMindB && bassTuningdB <= kMiniEQMaxdB);
		SFM_ASSERT(trebleTuningdB >= kMiniEQMindB && trebleTuningdB <= kMiniEQMaxdB);

		// Set parameter targets
		m_curWet.SetTarget(wet);
		m_curWidth.SetTarget(m_width);
		m_curRoomSize.SetTarget(m_roomSize);
		m_curDampening.SetTarget(m_dampening);
		m_curPreDelay.SetTarget(m_preDelay);

		m_preEQ.SetTargetdBs(bassTuningdB, trebleTuningdB);

		for (unsigned iSample = 0; iSample < numSamples; ++iSample)
		{
			const float curWet = m_curWet.Sample() * kMaxReverbWet; // Doesn't sound like much if fully open, consider different mix below? (FIXME)
			const float dry = 1.f-curWet;

			// Stereo (width) effect
			const float width = m_curWidth.Sample();
			const float wet1  = curWet*(width*0.5f + 0.5f);
			const float wet2  = curWet*((1.f-width)*0.5f);
			
			// In & out
			const float inL = *pLeft;
			const float inR = *pRight;

			float outL = 0.f;
			float outR = 0.f;

			// Mix to monaural & apply EQ
			/* const */ float monaural = 0.5f*inR + 0.5f*inL;
			monaural = m_preEQ.ApplyMono(monaural);

			// Apply pre-delay			
			m_preDelayLine.Write(monaural);
			monaural = m_preDelayLine.ReadNormalized(m_curPreDelay.Sample()) * kFixedGain;

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

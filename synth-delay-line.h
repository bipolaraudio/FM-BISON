
/*
	FM. BISON hybrid FM synthesis -- Fractional delay line w/feeedback.
	(C) visualizers.nl & bipolaraudio.nl
	MIT license applies, please see https://en.wikipedia.org/wiki/MIT_License or LICENSE in the project root!
*/

#pragma once

#include "synth-global.h"

namespace SFM
{
	class DelayLine
	{
	public:
		DelayLine(size_t size) :
			m_size(size)
,			m_buffer((float *) mallocAligned(size * sizeof(float), 16))
,			m_writeIdx(0)
,			m_curSize(size)
		{
			Reset();
		}

		DelayLine(unsigned sampleRate, float lenghtInSec) :
			DelayLine(size_t(sampleRate*lenghtInSec)) 
		{}

		~DelayLine()
		{
			freeAligned(m_buffer);
		}

		void Reset()
		{
			memset(m_buffer, 0, m_size*sizeof(float));
		}

		void Resize(size_t numSamples)
		{
			Reset();
			
			SFM_ASSERT(numSamples > 0 && numSamples <= m_size);
			m_curSize = numSamples;

			m_writeIdx = 0;
		}

		SFM_INLINE void Write(float sample)
		{
			const unsigned index = m_writeIdx % m_curSize;
			m_buffer[index] = sample;
			++m_writeIdx;
		}

		// For feedback path (call after Write())
		SFM_INLINE void WriteFeedback(float sample, float feedback)
		{
			SFM_ASSERT(feedback >= 0.f && feedback <= 1.f);
			const unsigned index = (m_writeIdx-1) % m_curSize;
			const float newSample = m_buffer[index] + sample*feedback;
			m_buffer[index] = newSample;
		}

		// Delay is specified in samples relative to sample rate
		// ** Write first, then read **
		SFM_INLINE float Read(float delay) const
		{
			const size_t from = (m_writeIdx-1-int(delay)) % m_curSize;
			const size_t to   = (from > 0) ? from-1 : m_curSize-1;
			const float fraction = fracf(delay);
			const float A = m_buffer[from];
			const float B = m_buffer[to];
			return lerpf<float>(A, B, fraction);
		}
		
		// Read to max. oldest sample in buffer (range [0..1])
		SFM_INLINE float ReadNormalized(float delay) const
		{
			return Read((m_size-1)*delay);
		}

		// Read without interpolation
		SFM_INLINE float ReadNearest(int delay) const
		{
			const size_t index = (m_writeIdx-1-delay) % m_curSize;
			return m_buffer[index];
		}

		size_t size() const { return m_size; }

	private:
		const size_t m_size;
		float *m_buffer;
		unsigned m_writeIdx;

		size_t m_curSize;
	};
}

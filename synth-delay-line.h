
/*
	FM. BISON hybrid FM synthesis -- Fractional delay line w/feeedback.
	(C) njdewit technologies (visualizers.nl) & bipolaraudio.nl
	MIT license applies, please see https://en.wikipedia.org/wiki/MIT_License or LICENSE in the project root!

	A few rules:
	- Always write first, then read and write feedback
	- Read() and ReadNearest() will wrap around
	- ReadNormalized() reads up to the line's size (i.e. the very last written sample)

	FIXME: add Catmull-Rom (cubic) interpolation? https://www.kvraudio.com/forum/viewtopic.php?p=7852862#p7852862
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

		// Non-copyable
		DelayLine& operator=(const DelayLine&) = delete;

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

		SFM_INLINE void Write(float sample)
		{
			const unsigned index = m_writeIdx % m_curSize;
			m_buffer[index] = sample;
			++m_writeIdx;
		}

		// For feedback path (call after Write())
		// As we've got enough headroom this approach works (adding), for an integer version
		// you'd need decent fixed point precision (24:8 or 16:16?)
		SFM_INLINE void WriteFeedback(float sample, float feedback)
		{
			SFM_ASSERT(feedback >= 0.f && feedback <= 1.f);
			const unsigned index = (m_writeIdx-1) % m_curSize;
			const float newSample = m_buffer[index] + sample*feedback;
			m_buffer[index] = newSample;
		}

		// Delay is specified in (fractional) number of samples
		// Many other interpolation methods are used and recommended other than linear (can cause high-frequency signal attenuation)
		SFM_INLINE float Read(float delay) const
		{
			const size_t from = (m_writeIdx-1-int(delay)) % m_curSize;
			const size_t to   = (from > 0) ? from-1 : m_curSize-1;
			const float fraction = fracf(delay);
			const float A = m_buffer[from];
			const float B = m_buffer[to];
			return lerpf<float>(A, B, fraction);
		}

		// Read without interpolation
		SFM_INLINE float ReadNearest(int delay) const
		{
			const size_t index = (m_writeIdx-1-delay) % m_curSize;
			return m_buffer[index];
		}
		
		// Read using normalized range [0..1]
		SFM_INLINE float ReadNormalized(float delay) const
		{
			SFM_ASSERT(delay >= 0.f && delay <= 1.f);
			return Read((m_size-1)*delay);
		}

		size_t size() const { return m_size; }

	private:
		const size_t m_size;
		float *m_buffer;

		unsigned m_writeIdx;
		size_t m_curSize;
	};

	// This class is primarily intended to alleviate small latencies (such as correction when using fourth order filters, to name one)
	// Only accepts sizes that are a power of 2
	class StereoLatencyDelayLine
	{
	public:
		StereoLatencyDelayLine(size_t size) :
			m_size(size)
,			m_writeIdx(0)
		{
			m_buffer.resize(size);
		}

		~StereoLatencyDelayLine() {}

		// Write samples
		void Write(float left, float right)
		{
			const auto writeIdx = m_writeIdx % m_size;
			m_buffer[writeIdx][0] = left;
			m_buffer[writeIdx][1] = right;

			++m_writeIdx;
		}

		// Returns last written samples (first sample is left, second is right)
		const std::array<float, 2> &Read() const
		{
			const size_t readIdx = ((m_writeIdx-1)-(m_size-1)) % m_size;
			return m_buffer[readIdx];
		}

	private:
		const size_t m_size;
		std::vector<std::array<float, 2>> m_buffer;
		
		size_t m_writeIdx;
	};
}

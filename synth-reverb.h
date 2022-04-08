
/*
	FM. BISON hybrid FM synthesis -- Reverb effect based on FreeVerb.
	(C) njdewit technologies (visualizers.nl) & bipolaraudio.nl
	MIT license applies, please see https://en.wikipedia.org/wiki/MIT_License or LICENSE in the project root!

	FIXME:
		- Supply all parameters at once using a single SetParameters() function
*/

#pragma once

#include "synth-global.h"
#include "synth-oscillator.h"
#include "synth-interpolated-parameter.h"
#include "synth-delay-line.h"
#include "synth-mini-EQ.h"

namespace SFM
{
	// Specific delay line essentially; this one does not own it's buffer!
	class ReverbComb
	{
	public:
		ReverbComb() :
			m_size(0), m_buffer(nullptr), m_writeIdx(0), m_dampening(0.f), m_previous(0.f)
		{}

		~ReverbComb() {}
		
		void SetSizeAndBuffer(size_t size, float *pBuffer)
		{
			SFM_ASSERT(size > 0);
			SFM_ASSERT(nullptr != pBuffer);
			
			m_size = size;
			m_buffer = pBuffer;
			
			Reset();
		}

		void Reset()
		{
			SFM_ASSERT(nullptr != m_buffer);
			memset(m_buffer, 0, m_size*sizeof(float));
		}
		
		void SetDampening(float value)
		{
			SFM_ASSERT(value >= 0.f && value < 1.f);
			m_dampening = value;
		}

		SFM_INLINE float Apply(float sample, float feedback = 0.f)
		{
			SFM_ASSERT(nullptr != m_buffer);
			
			const unsigned index = m_writeIdx % m_size;
			
			const float current = m_buffer[index];
			m_previous = current*(1.f-m_dampening) + (m_previous*m_dampening);
			m_buffer[index] = sample + feedback*m_previous;

			++m_writeIdx;

			return current;
		}

		size_t size() const { return m_size; }

	private:
		size_t m_size;
		float *m_buffer;
		unsigned m_writeIdx;
		
		float m_dampening;
		float m_previous;
	};

	// Roughly follows the same design as ReverbComb
	class ReverbAllPass
	{
	public:
		ReverbAllPass() :
			m_size(0), m_buffer(nullptr), m_writeIdx(0)
		{}

		~ReverbAllPass() {}

		void SetSizeAndBuffer(size_t size, float *pBuffer)
		{
			SFM_ASSERT(size > 0);
			SFM_ASSERT(nullptr != pBuffer);
			
			m_size = size;
			m_buffer = pBuffer;
			
			Reset();
		}

		void Reset()
		{
			SFM_ASSERT(nullptr != m_buffer);
			memset(m_buffer, 0, m_size*sizeof(float));
		}

		SFM_INLINE float Apply(float sample, float feedback = 0.f)
		{
			SFM_ASSERT(nullptr != m_buffer);
			
			const unsigned index = m_writeIdx % m_size;
			
			const float current = m_buffer[index];
			const float output = -sample + current;
			m_buffer[index] = sample + (current*feedback);
			
			++m_writeIdx;

			return output;
		}

		size_t size() const { return m_size; }

	private:
		size_t m_size;
		float *m_buffer;
		unsigned m_writeIdx;
	};

	// Warning: you can't just change these!
	constexpr unsigned kReverbNumCombs = 8;
	constexpr unsigned kReverbNumAllPasses = 4;

	// Max. room size to prevent infinite reverberation
	constexpr float kReverbMaxRoomSize = 0.9f;

	class Reverb
	{
	public:
		Reverb(unsigned sampleRate, unsigned Nyquist);
		
		~Reverb()
		{
			freeAligned(m_buffer);
		}
	
	public:
		SFM_INLINE void SetWidth(float width)
		{
			SFM_ASSERT(width >= SFM::kMinReverbWidth);
			m_width = width;
		}

		SFM_INLINE void SetRoomSize(float size)
		{
			SFM_ASSERT_NORM(size);
			size *= kReverbMaxRoomSize;
			
			// Taken from ref. implementation
			const float scale = 0.28f;
			const float offset = 0.7f;
			
			m_roomSize = (size*scale) + offset;
		}
		
		SFM_INLINE void SetDampening(float dampening)
		{
			SFM_ASSERT_NORM(dampening);
			dampening *= 0.4f; // Taken from ref. implementation
			m_dampening = dampening;			
		}

		SFM_INLINE void SetPreDelay(float preDelay)
		{
			SFM_ASSERT_NORM(preDelay);
			m_preDelay = preDelay;
		}

		// Samples are read & written sequentially so one buffer per channel suffices
		void Apply(float *pLeft, float *pRight, unsigned numSamples, float wet, float bassTuning, float trebleTuning);

	private:
		const unsigned m_sampleRate;
		const unsigned m_Nyquist;
		const unsigned m_NyquistAt44100 = 44100/2;

		MiniEQ m_preEQ;
		DelayLine m_preDelayLine;

		ReverbComb m_combsL[kReverbNumCombs], m_combsR[kReverbNumCombs];
		ReverbAllPass m_allPassesL[kReverbNumAllPasses], m_allPassesR[kReverbNumAllPasses];

		// Parameters
		float m_width;
		float m_roomSize;
		float m_dampening;
		float m_preDelay;

		// Interpolated parameters
		InterpolatedParameter<kLinInterpolate, true> m_curWet;
		InterpolatedParameter<kLinInterpolate, true> m_curWidth;
		InterpolatedParameter<kLinInterpolate, true> m_curRoomSize;
		InterpolatedParameter<kLinInterpolate, true> m_curDampening;
		InterpolatedParameter<kLinInterpolate, true> m_curPreDelay;
		InterpolatedParameter<kLinInterpolate, false> m_curBassdB, m_curTrebledB;

		// Single buffer is used for all passes, this likely favors cache (FIXME: check)
		size_t m_totalBufSize;
		float *m_buffer;
	};
}

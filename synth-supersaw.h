
/*
	FM. BISON hybrid FM synthesis -- Self-contained JP-8000 style supersaw oscillator.
	(C) visualizers.nl & bipolaraudio.nl
	MIT license applies, please see https://en.wikipedia.org/wiki/MIT_License or LICENSE in the project root!

	- Ref.: https://pdfs.semanticscholar.org/1852/250068e864215dd7f12755cf00636868a251.pdf (copy in repository)
	- Free running: all phases are updated by Bison::Render() if the oscillator is not being used
	
	FIXME:
		- Minimize beating (especially at lower frequencies)
		- Review filter
		- SSE implementation

	For now I'm picking the single precision version since I can't hear the f*cking difference ;)
*/

#pragma once

#include "3rdparty/filters/Biquad.h"

#include "synth-global.h"
#include "synth-stateless-oscillators.h"
#include "synth-one-pole-filters.h"

namespace SFM
{
	// Number of oscillators
	constexpr unsigned kNumSupersawOscillators = 7;

	// Relation between frequencies (slightly asymmetric)
	// Centre oscillator moved from position 4 to 1
	constexpr float kSupersawRelative[kNumSupersawOscillators] = 
	{
/*
		// According to Adam Szabo
		 0.f, 
		-0.11002313f, 
		-0.06288439f, 
		-0.01952356f,
		 0.01991221f,
		 0.06216538f, 
		 0.10745242f
*/

		// According to Alex Shore
		 0.f,
		-0.11002313f,
		-0.06288439f,
		-0.03024148f, 
		 0.02953130f,
		 0.06216538f,
		 0.10745242f
	};

	class Supersaw
	{
		class DCBlocker
		{
		public:
			void Reset()
			{
				m_prevSample = 0.f;
				m_feedback   = 0.f;
			}

			SFM_INLINE float Apply(float sample)
			{
				constexpr float R = 0.9925f; // What "everyone" uses in a leaky integrator is 0.995
				m_feedback = sample-m_prevSample + R*m_feedback;
				m_prevSample = sample;
				return m_feedback;
			}

		private:
			float m_prevSample = 0.f;
			float m_feedback   = 0.f;
		};
		
	public:
		Supersaw() : 
			m_sampleRate(1) 
		{
			// Initialize phases with random values between [0..1] and let's hope that at least a few of them are irrational
			for (auto &phase : m_phase)
				phase = mt_randf();
		}

		void Initialize(float frequency, unsigned sampleRate, float detune, float mix)
		{
			m_sampleRate = sampleRate;
			
			// Set JP-8000 controls
			SetDetune(detune);
			SetMix(mix);

			// Reset filter
			m_HPF.reset();

			// Reset DC blocker
			m_blocker.Reset();

			// Set frequency (pitch, filter)
			m_frequency = 0.f; 
			SetFrequency(frequency);
		}

		SFM_INLINE void SetFrequency(float frequency)
		{	
			m_frequency = frequency;
				
			// Calc. pitch
			for (unsigned iOsc = 0; iOsc < kNumSupersawOscillators; ++iOsc)
			{
				const float offset   = m_curDetune*kSupersawRelative[iOsc];
				const float freqOffs = frequency * offset;
				const float detuned  = frequency + freqOffs;
				const float pitch    = CalculatePitch<float>(detuned, m_sampleRate);
				m_pitch[iOsc] = pitch;
			}

			// Set HPF
			constexpr float Q = kDefGainAtCutoff*kPI*0.5f; // FIXME: what the f*ck kind of value is *this* then?
			m_HPF.setBiquad(bq_type_highpass, m_frequency/m_sampleRate, Q, 0.f);
		}

		// FIXME: code duplication
		SFM_INLINE void PitchBend(float bend)
		{
			if (1.f == bend)
				return;

			const float frequency = m_frequency*bend;

			// Calc pitch.
			for (unsigned iOsc = 0; iOsc < kNumSupersawOscillators; ++iOsc)
			{
				const float offset   = m_curDetune*kSupersawRelative[iOsc];
				const float freqOffs = frequency * offset;
				const float detuned  = frequency + freqOffs;
				const float pitch    = CalculatePitch<float>(detuned, m_sampleRate);
				m_pitch[iOsc] = pitch;
			}

			// Set HPF
			constexpr float Q = kDefGainAtCutoff*kPI*0.5f;
			m_HPF.setBiquad(bq_type_highpass, frequency/m_sampleRate, Q, 0.f);
		}

		SFM_INLINE float Sample()
		{
			// Centre oscillator
			const float main = Oscillate(0);

			// Side oscillators
			float sides = 0.f;
			for (unsigned iOsc = 1; iOsc < kNumSupersawOscillators; ++iOsc)
				sides += Oscillate(iOsc);

			float signal = m_HPF.processMono(main*m_mainMix + sides*m_sideMix);
			signal = m_blocker.Apply(signal);

			return signal;
		}
		
		// Advance phase by a number of samples (used by Bison::Render() for true 'free running') <-- FIXME!
		SFM_INLINE void Skip(unsigned numSamples)
		{
			for (unsigned iOsc = 0; iOsc < kNumSupersawOscillators; ++iOsc)
			{
				float &phase = m_phase[iOsc];
				phase = fmodf(phase + numSamples*m_pitch[iOsc], 1.f);
			}
		}

		SFM_INLINE float GetFrequency() const
		{	
			// Fundamental freq.
			return m_frequency;
		}

		SFM_INLINE float GetPhase() const
		{
			// Return main oscillator's phase
			return m_phase[0];
		}

	private:
		unsigned m_sampleRate;
		float m_frequency = 0.f;

		float m_curDetune = 0.f;
		float m_mainMix   = 0.f; 
		float m_sideMix   = 0.f;

		float m_phase[kNumSupersawOscillators] = { 0.f };
		float m_pitch[kNumSupersawOscillators] = { 0.f };

		Biquad m_HPF;
		DCBlocker m_blocker;

		void SetDetune(float detune /* [0..1] */);
		void SetMix(float mix /* [0..1] */);

		SFM_INLINE float Tick(unsigned iOsc)
		{
			SFM_ASSERT(iOsc < kNumSupersawOscillators);

			float &phase = m_phase[iOsc];
			float  pitch = m_pitch[iOsc];

			const float oscPhase = phase;
			SFM_ASSERT(oscPhase >= 0.f && oscPhase <= 1.f);

			phase += pitch;

			if (phase > 1.f)
				phase -= 1.f;

			return oscPhase;
		}

		SFM_INLINE float Oscillate(unsigned iOsc)
		{
			return oscPolySaw(Tick(iOsc), m_pitch[iOsc]);
		}
	};
}


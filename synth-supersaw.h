
/*
	FM. BISON hybrid FM synthesis -- Self-contained JP-8000 style supersaw oscillator.
	(C) njdewit technologies (visualizers.nl) & bipolaraudio.nl
	MIT license applies, please see https://en.wikipedia.org/wiki/MIT_License or LICENSE in the project root!

	- Ref.: https://pdfs.semanticscholar.org/1852/250068e864215dd7f12755cf00636868a251.pdf (copy in repository)
	- Free running: all phases are updated by Bison::Render() if the oscillator is not being used
	
	FIXME:
		- Not in [-1..1] - is this a problem?
		- Minimize beating (especially at lower frequencies)
		- Review filter
		- SSE implementation
		- Too much implementation lives in this header file

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
		static void CalculateDetuneTable();

		Supersaw() : 
			m_sampleRate(1) 
		{
			// Initialize phases with values between [0..1] and let's hope that at least a few of them are irrational
			for (auto &phase : m_phase)
                phase = oscSine(0.11f + 0.1f*mt_randf()); // Should be irrational
		}

		void Initialize(float frequency, unsigned sampleRate, float detune, float mix);

		// Allows on the fly adjustment of the 2 key JP-8000 parameters (read: whilst note held)
		SFM_INLINE void SetFrequency(float frequency, float detune, float mix)
		{
			// Set JP-8000 controls
			SetDetune(detune);
			SetMix(mix);

			m_frequency = frequency;
			OnFrequencyChange(m_frequency);
		}

		SFM_INLINE void PitchBend(float bend)
		{
			// An unnecessary OnFrequencyChange() isn't cheap enough to just disregard
			if (1.f == bend)
				return;

			const float frequency = m_frequency*bend;
			OnFrequencyChange(frequency);
		}

		SFM_INLINE float Sample()
		{
			// Centre oscillator
			const float main = Oscillate(0, false);

			// Side oscillators
			float sides = 0.f;
			for (unsigned iOsc = 1; iOsc < kNumSupersawOscillators; ++iOsc)
				sides += Oscillate(iOsc, (iOsc < kNumSupersawOscillators-1) ? true : false);

            float signal = main*m_mainMix + sides*m_sideMix;

            // m_HPF.processMono(signal);
            // signal = m_blocker.Apply(signal);

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

		SFM_INLINE float Oscillate(unsigned iOsc, bool subtractPureSine)
		{
            const float phase = Tick(iOsc);
            float polySaw = oscPolySaw(phase, m_pitch[iOsc]);
            if (subtractPureSine)
                polySaw -= oscSine(phase);
            return polySaw;
		}

		SFM_INLINE void OnFrequencyChange(float frequency)
		{
			// Calc. pitch for each oscillator
			for (unsigned iOsc = 0; iOsc < kNumSupersawOscillators; ++iOsc)
			{
				const float offset   = m_curDetune*kSupersawRelative[iOsc];
				const float freqOffs = frequency * offset;
				const float detuned  = frequency + freqOffs;
				const float pitch    = CalculatePitch<float>(detuned, m_sampleRate);
				m_pitch[iOsc] = pitch;
			}

			// Cut lower end
			// constexpr float Q = kNormalGainAtCutoff*kPI*0.5f; // Totally arbitrary.. (FIXME?)
			// m_HPF.setBiquad(bq_type_highpass, frequency/m_sampleRate, Q, 0.f);
		}
		
		// Key parameters (detune & mix)
		SFM_INLINE void SetDetune(float detune /* [0..1] */)
		{
//			m_curDetune = (float) SampleDetuneCurve(detune);
			m_curDetune = SampleDetuneTable(detune);
			SFM_ASSERT(m_curDetune >= 0.f && m_curDetune <= 1.f);
		}
	
		SFM_INLINE void SetMix(float mix /* [0..1] */)
		{
			SFM_ASSERT_NORM(mix);
			m_mainMix = -0.55366f*mix + 0.99785f;
			m_sideMix = -0.73764f*powf(mix, 2.f) + 1.2841f*mix + 0.044372f;
		}
		
		// See impl.
		static double SampleDetuneCurve(double detune);
		static float SampleDetuneTable(float detune);
	};
}


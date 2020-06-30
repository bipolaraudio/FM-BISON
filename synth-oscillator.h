
/*
	FM. BISON hybrid FM synthesis -- Oscillator (VCO/LFO).
	(C) visualizers.nl & bipolaraudio.nl
	MIT license applies, please see https://en.wikipedia.org/wiki/MIT_License or LICENSE in the project root!

	FIXME:
		- I'm not happy about Oscillator containing specific state and multiple phase objects just to
		  support a handful of special cases
		- https://github.com/bipolaraudio/FM-BISON/issues/84
*/

#pragma once

#pragma warning(push)
#pragma warning(disable: 4324) // Tell MSVC to shut it about padding I'm aware of

#include "3rdparty/SvfLinearTrapOptimised2.hpp"

#include "synth-global.h"
#include "synth-phase.h"
#include "synth-stateless-oscillators.h"
#include "synth-pink-noise.h"
#include "synth-sample-and-hold.h"
#include "synth-supersaw.h"

namespace SFM
{
	class Oscillator
	{
	public:
		// Supported waveforms
		enum Waveform
		{
			kNone,

			// Band-limited
			kSine,
			kCosine,
			kPolyTriangle,
			kPolySquare,
			kPolySaw,
			kPolyRamp,
			kPolyRectifiedSine,
			kPolyTrapezoid,
			kPolyRectangle,
			kBump,

			// Very soft approximation of ramp & saw (for LFO)
			kSoftRamp,
			kSoftSaw,
			
			// Supersaw
			kSupersaw,

			// Raw/LFO
			kRamp,
			kSaw,
			kSquare,
			kTriangle,
			kPulse,

			// Noise
			kWhiteNoise,
			kPinkNoise,

			// S&H (for LFO)
			kSampleAndHold
		};

	private:
		/* const */ Waveform m_form;
		Phase m_phases[kNumSupersawOscillators]; // FIXME: reduce footprint by allocating all but one separately?

		// Oscillators with state
		PinkNoise     m_pinkNoise;
		SampleAndHold m_sampleAndHold;

		// Supersaw utility class & filter
		Supersaw m_supersaw;
		SvfLinearTrapOptimised2 m_HPF;

		// Signal
		float m_signal = 0.f;
		
	public:
		Oscillator(unsigned sampleRate = 1) :
			m_sampleAndHold(sampleRate)
		{
			Initialize(kNone, 0.f, sampleRate, 0.f);
		}

		void Initialize(Waveform form, float frequency, unsigned sampleRate, float phaseShift)
		{
			m_form = form;
			
			m_pinkNoise     = PinkNoise();
			m_sampleAndHold = SampleAndHold(sampleRate);

			if (kSupersaw != m_form)
			{
				m_phases[0].Initialize(frequency, sampleRate, phaseShift);
			}
			else
			{
				// FIXME: parametrize, somehow
				m_supersaw.SetDetune(0.46f);
				m_supersaw.SetMix(0.4f);

				m_HPF.updateHighpassCoeff(frequency, 0.3, sampleRate);

				for (unsigned iOsc = 0; iOsc < kNumSupersawOscillators; ++iOsc)
					m_phases[iOsc].Initialize(m_supersaw.CalculateDetunedFreq(iOsc, frequency), sampleRate, mt_randf() /* Important: randomized phases, prevents 'whizzing' */);
			}
		}

		SFM_INLINE void PitchBend(float bend)
		{
			if (kSupersaw != m_form)
			{
				m_phases[0].PitchBend(bend);
			}
			else
			{
				for (auto &phase : m_phases)
					phase.PitchBend(bend);
			}
		}

		SFM_INLINE void SetFrequency(float frequency)
		{
			if (kSupersaw != m_form)
			{
				m_phases[0].SetFrequency(frequency);
			}
			else
			{
				m_HPF.updateHighpassCoeff(frequency, 0.3, GetSampleRate());

				for (unsigned iOsc = 0; iOsc < kNumSupersawOscillators; ++iOsc)
					m_phases[iOsc].SetFrequency(m_supersaw.CalculateDetunedFreq(iOsc, frequency));
			}
		}

		SFM_INLINE void SetSampleAndHoldSlewRate(float rate)
		{
			m_sampleAndHold.SetSlewRate(rate);
		}

		SFM_INLINE void Reset()
		{
			for (auto &phase : m_phases)
				phase.Reset();
		}
		
		SFM_INLINE float    GetFrequency()   const { return m_phases[0].GetFrequency();  }
		SFM_INLINE unsigned GetSampleRate()  const { return m_phases[0].GetSampleRate(); }
		SFM_INLINE float    GetPhase()       const { return m_phases[0].Get();           } // Warning: this value *can* be out of bounds! [0..1]
		SFM_INLINE Waveform GetWaveform()    const { return m_form;                      }

		float Sample(float phaseShift);
	};
}

#pragma warning(pop)

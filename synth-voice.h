
/*
	FM. BISON hybrid FM synthesis -- FM voice render (stereo).
	(C) njdewit technologies (visualizers.nl) & bipolaraudio.nl
	MIT license applies, please see https://en.wikipedia.org/wiki/MIT_License or LICENSE in the project root!

	Important:
		- This used to be a POD structure and it's not quite a C++ class (potential FIXME), initialization and manipulation of
		  members, for a large part, happens in FM_BISON.cpp and the Sample() function
		- (R) means (to be) interpolated per-sample; I might've forgotten to mark 1 or 2 though
*/

#pragma once

#include "3rdparty/filters/SvfLinearTrapOptimised2.hpp"
#include "3rdparty/filters/Biquad.h"

#include "synth-global.h"
#include "synth-oscillator.h"
#include "patch/synth-patch-global.h"
#include "synth-envelope.h"
#include "synth-one-pole-filters.h"
#include "synth-signal-follower.h"

namespace SFM
{
	class Voice
	{
	public:
		// Key slot (-1 means it's a rogue voice)
		int m_key;

		// Offset in samples until actually triggered (must happen within a single Render() cycle)
		unsigned m_sampleOffs;

		// Velocity
		float m_velocity;

		// Fundamental frequency
		float m_fundamentalFreq;

		enum State
		{
			kIdle      = 0, // Silent / Available
			kPlaying   = 1, // In full swing
			kReleasing = 2, // Releasing
			kStolen    = 3  // Stolen (quickly fade)
		} m_state;

		// Can be true in all non-kIdle states
		bool m_sustained;
		
		// Modulation buffer (1 sample delay, FIXME)
		float m_modSamples[kNumOperators+1]; // First slot for index -1

		struct Operator
		{
			// This function is called by Voice::Reset()
			void Reset(unsigned sampleRate);

			bool enabled;
			
			// Frequency
			float setFrequency; // As calculated by CalcOpFreq()
			InterpolatedParameter<kMulInterpolate, false> curFreq;

			// Detune offset (used in jitter)
			float detuneOffs;

			// Key tracking (higher note, shorter envelope)
			float keyTracking;

			// Oscillator, amplitude & envelope
			InterpolatedParameter<kLinInterpolate, true> amplitude; // (R)
			InterpolatedParameter<kLinInterpolate, true> index;     // (R)
			Oscillator oscillator;
			Envelope envelope;

			// Indices: -1 means none, modulator indices must be larger than operator index
			// Yes, this means there is 1 frame of delay, but @ 44.1kHz that amounts to: 2,2675736961451247165532879818594e-5 and that value only gets smaller;
			int modulators[3], iFeedback;
			bool noModulation; // Small optimization (see Voice::Render()), initialized by PostInitialize()

			// Feedback (R)
			// See: https://www.reddit.com/r/FMsynthesis/comments/85jfrb/dx7_feedback_implementation/
			InterpolatedParameter<kLinInterpolate, true> feedbackAmt;
			float feedback; // Operator feedback

			// LFO influence
			float ampMod;
			float pitchMod;
			float panMod;

			// *Very* basic distortion (R)
			InterpolatedParameter<kLinInterpolate, false> softClip;

			// Panning ([0..1], 0.5 is center) (R)
			InterpolatedParameter<kLinInterpolate, true> panning;

			// Is carrier (output)
			bool isCarrier;
			
			// Filters
			Biquad filter;                     // Operator filter
			SvfLinearTrapOptimised2 modFilter; // Filter can be used to take the edge off an operator to be used as modulator (Set to default by Reset(), could be a Biquad, sure, but this is tweaked to work)

			// Gain envelope
			FollowerEnvelope envGain;

			// Supersaw parameters (R)
			InterpolatedParameter<kLinInterpolate, true> supersawDetune;
			InterpolatedParameter<kLinInterpolate, true> supersawMix;
		} m_operators[kNumOperators];

		// LFO oscillators
		Oscillator m_LFO1, m_LFO2;
		Oscillator m_modLFO;

		// Main filter (used in FM_BISON.cpp)
		SvfLinearTrapOptimised2 m_filterSVF;
		
		// Filter (amplitude) envelope
		Envelope m_filterEnvelope;

		// Pitch (range & envelope)
		int m_pitchBendRange; // Applied to LFO, envelope & modulation
		PitchEnvelope m_pitchEnvelope;

		// Freq. glide
		float m_freqGlide;

		// Global amplitude
		InterpolatedParameter<kLinInterpolate, true> m_globalAmp;

	private:
		void ResetOperators(unsigned sampleRate);

	public:
		void Reset(unsigned sampleRate);
		
		// Call after every initialization
		void PostInitialize();

		bool IsIdle()      const { return kIdle      == m_state; }
		bool IsPlaying()   const { return kPlaying   == m_state; }
		bool IsReleasing() const { return kReleasing == m_state; }
		bool IsStolen()    const { return kStolen    == m_state; }
		bool IsSustained() const { return m_sustained;           }

		// Checks if all carrier envelopes are idle
		bool IsDone();

		// Release all envelopes & sets state to kReleasing
		void OnRelease(); 

		// Used for voice stealing & monophonic mode
		float GetSummedOutput(); /* const */

		// Render "dry" FM voice (see impl. for param. ranges)
		void Sample(float &left, float &right, float pitchBend, float ampBend /* Linear gain */, float modulation, float LFOBias, float LFOModDepth);
	};
}

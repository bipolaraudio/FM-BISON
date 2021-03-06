
/*
	FM. BISON hybrid FM synthesis -- FM voice render (stereo).
	(C) visualizers.nl & bipolaraudio.nl
	MIT license applies, please see https://en.wikipedia.org/wiki/MIT_License or LICENSE in the project root!
*/

#include "synth-voice.h"
#include "synth-distort.h"

namespace SFM
{
	void Voice::ResetOperators(unsigned sampleRate)
	{
		// NULL operators
		for (unsigned iOp = 0; iOp < kNumOperators; ++iOp)
		{
			m_operators[iOp].Reset(sampleRate);
		}
	}

	// Full reset
	void Voice::Reset(unsigned sampleRate)
	{
		ResetOperators(sampleRate);

		// Not bound, zero velocity, zero offset
		m_key = -1;
		m_velocity = 0.f;
		m_sampleOffs = 0;

		// Disable
		m_state = kIdle;
		m_sustained = false;

		// LFO
		m_LFO1   = Oscillator(sampleRate);
		m_LFO2   = Oscillator(sampleRate);
		m_modLFO = Oscillator(sampleRate);

		// Filter envelope
		m_filterEnvelope.Reset();

		// Pitch (envelope)
		m_pitchBendRange = kDefPitchBendRange;
		m_pitchEnvelope.Reset(sampleRate);

		// Reset filter
		m_filterSVF1.resetState();
		m_filterSVF2.resetState();

		// Def. glide
		m_freqGlide = kDefPolyFreqGlide;
	}

	bool Voice::IsDone() /* const */
	{
		if (kIdle != m_state)
		{
			for (auto &voiceOp : m_operators)
			{
				if (true == voiceOp.enabled && true == voiceOp.isCarrier)
				{
					// Carrier operators should never be infinite!
					SFM_ASSERT(false == voiceOp.envelope.IsInfinite());

					// Has the envelope ran it's course yet?
					if (false == voiceOp.envelope.IsIdle())
						return false;
				}
			}
		}

		return true;
	}

	void Voice::OnRelease()
	{
		SFM_ASSERT(kPlaying == m_state);

		m_filterEnvelope.Stop();
		m_pitchEnvelope.Stop();

		for (auto &voiceOp : m_operators)
		{
			if (true == voiceOp.enabled)
			{
				voiceOp.envelope.Stop();
			}
		}

		m_state = kReleasing;
	}

	float Voice::GetSummedOutput()
	{
		float summed = 0.f;
		unsigned numCarriers = 0;

		for (auto &voiceOp : m_operators)
		{
			if (true == voiceOp.enabled && true == voiceOp.isCarrier)
			{
				summed += voiceOp.envelope.Get();
				++numCarriers;
			}
		}

		return summed;
	}

	
	/* ----------------------------------------------------------------------------------------------------

		Voice render loop, this is the most essential part of the FM engine
		
		FIXME: optimize!
			
	 ------------------------------------------------------------------------------------------------------ */

	 // Tame
//	constexpr float kFeedbackScale = 0.75f;
	
	// Bright
	constexpr float kFeedbackScale = 1.f;

	void Voice::Sample(float &left, float &right, float pitchBend, float ampBend, float modulation, float LFOBlend, float LFOModDepth)
	{
		//
		// Render?
		//
	    
		if (kIdle == m_state || m_sampleOffs > 0)
		{
			SFM_ASSERT(kIdle != m_state); // Idle voices shouldn't be sampled
			
			// MIDI sync.
			--m_sampleOffs;

			left  = 0.f;
			right = 0.f;

			return;
		}
		
		//
		// Parameter assertions
		//

		SFM_ASSERT(ampBend >= dB2Lin(-kAmpBendRange) && ampBend <= dB2Lin(kAmpBendRange)); // Linear gain
		SFM_ASSERT(pitchBend >= -1.f && pitchBend <= 1.f);
		SFM_ASSERT(modulation >= 0.f && modulation <= 1.f);
		SFM_ASSERT(LFOBlend >= 0.f && LFOBlend <= 1.f);
		SFM_ASSERT(LFOModDepth >= 0.f);
		
		//
		// Calculate LFO value
		//
		
		const float modLFO = m_modLFO.Sample(0.f);

		auto modulate = [](float input, float modulation, float depth)
		{
			const float sample = input*modulation;
			return lerpf<float>(input, sample, depth);
		};

		const float LFO1 = modulate(m_LFO1.Sample(0.f /* Do something funky here? */), modLFO, LFOModDepth);
		const float LFO2 = modulate(m_LFO2.Sample(0.f), modLFO, LFOModDepth);
		const float blend = lerpf<float>(LFO1, LFO2, LFOBlend);

		const float LFO = blend;

		SFM_ASSERT_BINORM(LFO);
        
		//
		// Calc. pitch envelope & bend multipliers
		//
		
		const float pitchRangeOct = m_pitchBendRange/12.f;
		const float pitchEnv = powf(2.f, m_pitchEnvelope.Sample(false)*pitchRangeOct); // Sample pitch envelope (does not sustain!)
		pitchBend = powf(2.f, pitchBend*pitchRangeOct);

		//
		// Process all operators top-down
		// This imposes the limitation that an operator can only be modulated by one below it
		//
        
		alignas(16) float modSamples[kNumOperators] = { 0.f }; // Samples for modulation
		float mixL = 0.f, mixR = 0.f; // Carrier mix

		for (int iOp = kNumOperators-1; iOp >= 0; --iOp)
		{
			// Top-down
			Operator &voiceOp = m_operators[iOp];

			if (true == voiceOp.enabled)
			{
				const float curFreq = voiceOp.curFreq.Sample();
				const float curAmplitude = voiceOp.amplitude.Sample();
				const float curIndex = voiceOp.index.Sample();
				const float curEG = voiceOp.envelope.Sample();
				const float curSquarepusher = voiceOp.drive.Sample();
				const float curFeedbackAmt = voiceOp.feedbackAmt.Sample() * kFeedbackScale;
				const float curPanning = voiceOp.panning.Sample();
				
				// Set base freq.
				auto &oscillator = voiceOp.oscillator;

				if (Oscillator::Waveform::kSupersaw != oscillator.GetWaveform())
				{
					oscillator.SetFrequency(curFreq);
				}
				else
				{
					// Special case
					const float curDetune = voiceOp.supersawDetune.Sample();
					const float curMix    = voiceOp.supersawMix.Sample();
					
					oscillator.GetSupersaw().SetFrequency(curFreq, curDetune, curMix);
				}

				// Get modulation from max. 3 sources
				float phaseShift = 0.f;
				for (unsigned iMod = 0; iMod < 3; ++iMod)
				{
					const int iModulator = voiceOp.modulators[iMod];

					// Sanity checks
					SFM_ASSERT(-1 == iModulator || iModulator < kNumOperators);
					SFM_ASSERT(-1 == iModulator || iModulator > iOp);

					if (-1 != iModulator) // By branching here I'm potentially sparing an fmodf() in Oscillator::Sample()
					{
						// Add sample to phase
						const float sample = modSamples[iModulator];
						phaseShift += 1.f+sample;
					}
				}
				
				// Get feedback
				float feedback = 0.f;
				if (-1 != voiceOp.iFeedback)
				{
					const int iFeedback = voiceOp.iFeedback;

					// Sanity check
					SFM_ASSERT(iFeedback < kNumOperators);
					
					// Grab operator's current feedback (and make sure it's either zero or positive)
					feedback = m_operators[iFeedback].feedback;
					SFM_ASSERT(feedback >= 0.f);
				}

				// Vibrato: pitch bend, pitch envelope & pitch LFO
				const float pitchLFO = powf(2.f, LFO*voiceOp.pitchMod*modulation * pitchRangeOct);
				const float vibrato = pitchBend*pitchEnv*pitchLFO;
				oscillator.PitchBend(vibrato);

				// Calculate sample
				float sample = oscillator.Sample(phaseShift+feedback);

				// LFO tremolo
				const float tremolo = 1.f - fabsf(LFO*voiceOp.ampMod);
				sample = lerpf<float>(sample, sample*tremolo, modulation);

				// Apply envelope
				sample *= curEG;

				// Apply "Squarepusher" distortion
				if (0.f != curSquarepusher)
				{
					const float squared = Squarepusher(sample, curSquarepusher);
					sample = lerpf<float>(sample, squared, curSquarepusher);
				}

#if !defined(SFM_DISABLE_FX)

				// Apply filter
				bool hasOpFilter = true;

				switch (voiceOp.filter.getType())
				{
				case bq_type_none:
					hasOpFilter = false;
					break;

				default:
					// I'm assuming the filter is set up properly
					sample = voiceOp.filter.processMono(sample);
				}

#else

				bool hasOpFilter = false;

#endif

				// Store (filtered) sample for modulation, with modulation index applied
				float modSample = sample*curIndex;
				
				if (false == hasOpFilter && SvfLinearTrapOptimised2::NO_FLT_TYPE != voiceOp.modFilter.getFilterType())
				{
					// Only apply if modulator filter set (only applied to a few waveforms)
					voiceOp.modFilter.tickMono(modSample);
				}

				modSamples[iOp] = modSample;

				// Apply (linear) amplitude to sample (including possible 'bend')
				sample *= curAmplitude*ampBend;

				// Add sample to gain envelope (for VU meter)
				const float absModSample = fabsf(modSample);                     
				const float gainSample = (voiceOp.isCarrier)                         // Carrier prioritized if both (FIXME?)
					? sample                                                     // Adj. for actual volume
					: absModSample/(kEpsilon+curIndex);                          // Normalized (with a little hack that prevents a branch to check for zero, which in turn *might* push the value a teensy bit (kEpsilon) out of range)
				voiceOp.envGain.Apply(gainSample);

				// Update feedback
				voiceOp.feedback = 0.25f*(voiceOp.feedback*0.995f + fabsf(sample)*curFeedbackAmt);
								
				if (true == voiceOp.isCarrier)
				{
					// Calc. panning
					const float panMod = voiceOp.panMod;
					/* const */ float panning = (0.f == panMod)
						? curPanning
						: LFO*panMod*modulation*0.5f + 0.5f; // If panning modulation is set it overrides manual panning

					// Because parameter interpolation is not very precise, and a negative square root is in that it is unforgiving
					panning = Clamp(panning); 

					const float carrierL = sample*sqrtf(1.f-panning);
					const float carrierR = sample*sqrtf(panning);
					
					// We've had some trouble here (see above, negative square root...)
					FloatAssert(carrierL);
					FloatAssert(carrierR);

					// Apply panning & mix (square law panning retains equal power)
					mixL += carrierL;
					mixR += carrierR;
				}
			}
		} 

		left  = mixL;
		right = mixR;
	}
}

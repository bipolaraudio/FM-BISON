
/*
	FM. BISON hybrid FM synthesis -- Post-processing pass.
	(C) njdewit technologies (visualizers.nl) & bipolaraudio.nl
	MIT license applies, please see https://en.wikipedia.org/wiki/MIT_License or LICENSE in the project root!

	FIXME:
		- Almost the entire path is implemented in Apply(), chop this up into smaller pieces?
		- Write own (or adapt public domain) up- and downsampling routines (currently using JUCE's)
		- The list of parameters is rather huge, pass through a structure?
*/

#pragma once

#include "3rdparty/filters/SvfLinearTrapOptimised2.hpp"
#include "3rdparty/filters/MusicDSPModel.h"
#include "3rdparty/filters/Biquad.h"

// Include JUCE (for juce::dsp::Oversampling)
#include <JuceHeader.h>

#include "synth-global.h"
#include "synth-delay-line.h"
#include "synth-phase.h"
#include "synth-one-pole-filters.h"
#include "synth-interpolated-parameter.h"
#include "synth-reverb.h"
#include "synth-compressor.h"
#include "synth-auto-wah-vox.h"
#include "synth-mini-EQ.h"

namespace SFM
{
	const unsigned kNumPhaserStages = 8;

	class PostPass
	{
	public:
		PostPass(unsigned sampleRate, unsigned maxSamplesPerBlock, unsigned Nyquist);
		~PostPass();

		// FIXME: this parameter list is just too ridiculously long!
		void Apply(unsigned numSamples,
		           float rateBPM, unsigned overideFlagsRateBPM, /* See impl. for details! */
				   float wahResonance, float wahAttack, float wahHold, float wahRate, float wahDrivedB, float wahSpeak, float wahSpeakVowel, float wahSpeakVowelMod, float wahSpeakGhost, float wahSpeakCut, float wahSpeakReso, float wahCut, float wahWet,
		           float cpRate, float cpWet, bool isChorus,
		           float delayInSec, float delayWet, float delayDrivedB, float delayFeedback, float delayFeedbackCutoff, float delayTapeWow,
		           float postCutoff, float postReso, float postDrivedB, float postWet,
		           float tubeDistort, float tubeDrive, float tubeOffset, float tubeTone, bool tubeToneReso,
		           float reverbWet, float reverbRoomSize, float reverbDampening, float reverbWidth, float reverbLP, float reverbHP, float reverbPreDelay,
		           float compThresholddB, float compKneedB, float compRatio, float compGaindB, float compAttack, float compRelease, float compLookahead, bool compAutoGain, float compRMSToPeak,
				   float bassTuning, float trebleTuning, float midTuning, float masterVol,
		           const float *pLeftIn, const float *pRightIn, float *pLeftOut, float *pRightOut);

		// Intended for a graphical indicator
		float GetCompressorBite() const
		{
			const float bite = m_compressorBiteLPF.Get();
			SFM_ASSERT_NORM(bite);

			return bite;
		}
		
		// Returns approx. latency in samples
		float GetLatency() const;

	private:
		SFM_INLINE void SetChorusRate(float rate /* [0..1] */, float scale)
		{
			rate *= scale;

			m_chorusSweep.SetFrequency(rate);

			// This is a happy little accident since SetFrequency() expects a frequency but
			// gets a 10th of the pitch instead; but it sounds good so I'm not messing with this
			m_chorusSweepMod.SetFrequency(m_chorusSweep.GetPitch()*0.1f);
		}

		SFM_INLINE void SetPhaserRate(float rate, float scale) // [0..1]
		{
			rate *= scale;

			m_phaserSweep.SetFrequency(rate);
		}
		
		void ApplyChorus(float sampleL, float sampleR, float &outL, float &outR, float wetness);
		void ApplyPhaser(float sampleL, float sampleR, float &outL, float &outR, float wetness);
		
		const unsigned m_sampleRate;
		const unsigned m_Nyquist;
		const unsigned m_sampleRate4X; // Convenience

		// Intermediate buffers
		float *m_pBufL = nullptr;
		float *m_pBufR = nullptr;

		// Delay lines & delay's interpolated parameters
		Phase m_tapeDelayLFO;
		SinglePoleLPF m_tapeDelayLPF;
		DelayLine m_delayLineL;
		DelayLine m_delayLineM;
		DelayLine m_delayLineR;
		CascadedSinglePoleLPF m_delayFeedbackLPF_L, m_delayFeedbackLPF_R;
		InterpolatedParameter<kLinInterpolate, true> m_curDelayInSec;
		InterpolatedParameter<kLinInterpolate, true> m_curDelayWet;
		InterpolatedParameter<kLinInterpolate, false> m_curDelayDrive;
		InterpolatedParameter<kLinInterpolate, true> m_curDelayFeedback;
		InterpolatedParameter<kLinInterpolate, true> m_curDelayFeedbackCutoff;
		InterpolatedParameter<kLinInterpolate, true> m_curDelayTapeWow;

		// Chorus
		DelayLine m_chorusDL;
		Phase m_chorusSweep, m_chorusSweepMod;
		SinglePoleLPF m_chorusSweepLPF1, m_chorusSweepLPF2;

		// Phaser
		SvfLinearTrapOptimised2 m_allpassFilters[kNumPhaserStages];
		Phase m_phaserSweep;
		SinglePoleLPF m_phaserSweepLPF;

		// Oversampling (JUCE, FIXME)
		juce::dsp::Oversampling<float> m_oversampling4X;

		// Post filter & interpolated parameters
		MusicDSPMoog m_postFilter;
		InterpolatedParameter<kLinInterpolate, true> m_curPostCutoff;
		InterpolatedParameter<kLinInterpolate, true> m_curPostReso;
		InterpolatedParameter<kLinInterpolate, false> m_curPostDrive;
		InterpolatedParameter<kLinInterpolate, true> m_curPostWet;

		// Tube distortion filter (AA), DC blocker & interpolated parameters
		InterpolatedParameter<kLinInterpolate, true> m_curTubeDist;
		InterpolatedParameter<kLinInterpolate, false> m_curTubeDrive;
		InterpolatedParameter<kLinInterpolate, false> m_curTubeOffset;
		InterpolatedParameter<kLinInterpolate, true> m_curTubeTone; // Normalized cutoff
		SvfLinearTrapOptimised2 m_tubeToneFilter;
		StereoDCBlocker m_tubeDCBlocker;	
		
		// Post
		MiniEQ m_postEQ;
		Biquad m_killLow;
				
		// External effects
		AutoWah m_wah;
		Reverb m_reverb;
		Compressor m_compressor;

		// Exposed to be used, chiefly, as indicator
		CascadedSinglePoleLPF m_compressorBiteLPF;

		// Misc.
		InterpolatedParameter<kLinInterpolate, true> m_curChorusWet;
		InterpolatedParameter<kLinInterpolate, true> m_curPhaserWet;
		InterpolatedParameter<kLinInterpolate, false> m_curMasterVol;
	};
}

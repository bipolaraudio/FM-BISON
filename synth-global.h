
/*
	FM. BISON hybrid FM synthesis -- Global includes, constants & utility functions: include on top of every header or autonomous CPP.
	(C) visualizers.nl & bipolaraudio.nl
	MIT license applies, please see https://en.wikipedia.org/wiki/MIT_License or LICENSE in the project root!
*/

#pragma once

// Include JUCE
#include "../JuceLibraryCode/JuceHeader.h"

#ifndef _DEBUG
	#ifdef _WIN32
		#define SFM_INLINE __forceinline // Muscle MSVC into doing what we ask for
	#else
		#define SFM_INLINE __inline // Let inline functions do *not* appear in symbol tables
	#endif
#else
	#define SFM_INLINE inline 
#endif

// _DEBUG should be defined in all debug builds, regardless of platform
// PROFILE_BUILD overrides _DEBUG
// For FM-BISON's internal version I've done this in Projucer

#if defined(_DEBUG) && !defined(PROFILE_BUILD)
	#ifdef _WIN32
		#define SFM_ASSERT(condition) if (!(condition)) __debugbreak();
	#else
		#define SFM_ASSERT(condition) jassert(condition); // FIXME: replace for platform equivalent (see jassert() impl.)
	#endif
#else
	#define SFM_ASSERT(condition)
#endif

// Set to 1 to kill all SFM log output
#if defined(_DEBUG) && !defined(PROFILE_BUILD)
	#define SFM_NO_LOGGING 0
#else
	#define SFM_NO_LOGGING 1
#endif

// Set to 1 to let FM. BISON handle denormals
#define SFM_KILL_DENORMALS 0

// Define to disable all FX (including per-voice filter)
// #define SFM_DISABLE_FX

// Define to disable extra voice rendering thread
// #define SFM_DISABLE_VOICE_THREAD

namespace SFM
{
	/*
		Almost all constants used across FM. BISON are defined here; I initially chose this approach because there weren't so many
		of them and almost none were too specific to certain parts of the code. 
		
		This has changed over time but I still, for now, decide to stick with having most of them here instead of spreading them across various files.
		The criteria, roughly, should be that they are either used in multiple files and/or exposed to the host software.
		
		Very specific constants should live close to their implementation.

		So: so long as every constant the host needs is defined here we're in the clear.
	*/

	// Max. number of voices to render using the main (single) thread
	// 32 is based on 64 being a reasonable total
	// FIXME: make this or rather using the second thread at all a setting?
	constexpr unsigned kSingleThreadMaxVoices = 32;

	// Max. fixed frequency (have fun with it!)
	constexpr float kMaxFixedHz = 96000.f;

	// Default parameter latency (used for per-sample interpolation of parameters and controls)
	constexpr float kDefParameterLatency = 0.01f; // 10MS

	// Default ParameterSlew MS (sampled per frame (Render() call))
	constexpr float kDefParameterSlewMS = 3.f;

	// Polyphony constraints
	constexpr unsigned kMinVoices = 1;
	constexpr unsigned kMaxVoices = 128;

	// Default number of vioces
	constexpr unsigned kDefMaxVoices = 32; // Very safe and fast
	
	// Number of FM synthesis operators (changing this value requires a thorough check)
	constexpr unsigned kNumOperators = 6;
	
	// Base note Hz (A4)
	// "Nearly all modern symphony orchestras in Germany and Austria and many in other countries in continental Europe (such as Russia, Sweden and Spain) tune to A = 443 Hz." (Wikipedia)
	constexpr double kBaseHz = 444.0; // But I don't :-)

	// Max. pitch bend range (in semitones)
	constexpr unsigned kMaxPitchBendRange = 48; // +/- 4 octaves
	constexpr unsigned kDefPitchBendRange = 12; // +/- 1 octave

	// Jitter: max. note drift (in cents, -/+)
	constexpr unsigned kMaxNoteJitter = 50; // Half a note

	// Jitter: max. detune (in cents, -/+)
	constexpr float kMaxDetuneJitter = 1.f; // 100th of a note

	// Main filter resonance range (max. must be < 40.f, or so the manual says)
	// Engine adds kMinFilterResonance automatically!
	constexpr float kMinFilterResonance = 0.025f;
	constexpr float kMaxFilterResonance = 14.f;
	constexpr float kFilterResonanceRange = kMaxFilterResonance-kMinFilterResonance;
	
	// Min. filter cutoff; range is simply [0..1]
	constexpr float kMinFilterCutoff = 0.f;

	// Default main filter settings
	constexpr float kDefFilterCutoff    = 1.f;          // No (or minimal) filtering (when in lowpass mode, at least)
	constexpr float kDefFilterResonance = 0.f;          // Filter's default Q
	constexpr float kMinFilterCutoffHz  = 16.f;         // See impl.
	constexpr float kMainCutoffAftertouchRange = 0.66f; // Limits aftertouch cutoff to avoid that low range of the cutoff that's not allowed (SVF, < 16.0), which causes filter instability
	
	// Resonance range is limited for a smoother "knob feel" for both the main (voice) filter & the per operator filters (which will remain at this value)
	constexpr float kDefFilterResonanceLimit = 0.6f;

	// Reverb default lowpass & highpass (normalized)
	constexpr float kDefReverbFilter = 1.f;

	// Default post-pass filter drive range & default (dB)	
	constexpr float kMinPostFilterDrivedB = -3.f;
	constexpr float kMaxPostFilterDrivedB =  6.f;
	constexpr float kDefPostFilterDrivedB =  0.f;

	// Tube distortion drive & offset
	constexpr float kMinTubeDrive  =   0.f;
	constexpr float kMaxTubeDrive  = 100.f;
	constexpr float kDefTubeDrive  =  10.f;
	constexpr float kMinTubeOffset = -0.1f;
	constexpr float kMaxTubeOffset =  0.1f;

	// Envelope rate multiplier range (or 'global')
	// Range (as in seconds) taken from Arturia DX7-V (http://downloads.arturia.com/products/dx7-v/manual/dx7-v_Manual_1_0_EN.pdf)
	constexpr float kEnvMulMin = 0.1f;
	constexpr float kEnvMulMax = 60.f;
	constexpr float kEnvMulRange = kEnvMulMax-kEnvMulMin;

	// Multiplier on ADSR envelope ratio (release) for piano (CP) sustain pedal mode
	// The higher the value, the more linear (and thus longer) the release phase will be
	constexpr float	kEnvPianoSustainRatioMul = 1000.f;

	// Gain per voice (in dB)
	// This keeps the voice mix nicely within acceptable range (approx. 8 voices, see https://www.kvraudio.com/forum/viewtopic.php?t=275702)
	constexpr float kVoiceGaindB = -9.f;
	
	// Chorus/Phaser effect (synth-post-pass.cpp) max. wetness
	constexpr float kMaxChorusPhaserWet = 0.707f; // -3dB

	// Reverb effect sounds best until mixed to around 50-60 percent as well (wetness)
	constexpr float kMaxReverbWet = 0.55f;

	// Chorus/Phaser rate multipliers (Hz)
	constexpr float kMaxChorusRate = 12.f;
	constexpr float kMaxPhaserRate = 8.f;
	
	// Master output volume range & default in dB
	constexpr int kInfVolumedB   = -1000; // Arbitrary number
	constexpr int kMinVolumedB   =   -96;
	constexpr int kMaxVolumedB   =     3;
	constexpr int kDefVolumedB   =   -12;
	constexpr int kVolumeRangedB = kMaxVolumedB-kMinVolumedB;

	// (Monophonic) frequency glide (in seconds)
	constexpr float kMaxFreqGlide = 1.f;        // 1000MS
	constexpr float kDefMonoFreqGlide = 0.066f; //   66MS
	constexpr float kDefPolyFreqGlide = 0.1f;   //  100MS
	constexpr float kDefMonoGlideAtt = 0.33f;   // [0..1], the larger the punchier
	
	// Slew parameters for S&H
	constexpr float kMinSandHSlewRate = 0.001f;  //  1MS
	constexpr float kMaxSandHSlewRate =  0.05f;  // 50MS
	constexpr float kDefSandHSlewRate = 0.005f;  //  5MS

	// Reverb width range & default
	constexpr float kMinReverbWidth = 0.f;
	constexpr float kMaxReverbWidth = 2.f;
	constexpr float kDefReverbWidth = 0.5f;

	// Reverb pre-delay line max. size
	constexpr float kReverbPreDelayMax = 0.5f;   // 500MS
	constexpr float kDefReverbPreDelay = 0.001f; //  10MS

	// Compressor range & defaults
	constexpr float kMinCompThresholdB  =  -60.f; 
	constexpr float kMaxCompThresholdB  =    6.f;
	constexpr float kDefCompThresholddB = kMaxCompThresholdB;
	constexpr float kMinCompKneedB      =    0.f;
	constexpr float kMaxCompKneedB      =   12.f;
	constexpr float kDefCompKneedB      = kMinCompKneedB;
	constexpr float kMinCompRatio       =    1.f;
	constexpr float kMaxCompRatio       =   20.f;
	constexpr float kDefCompRatio       = kMinCompRatio;
	constexpr float kMinCompGaindB      =   -6.f;
	constexpr float kMaxCompGaindB      =   60.f;
	constexpr float kDefCompGaindB      =    0.f;
	constexpr float kMinCompAttack      = 0.001f; // 1MS
	constexpr float kMaxCompAttack      =    1.f; // 1 sec.
	constexpr float kDefCompAttack      =  0.10f; // 10MS
	constexpr float kMinCompRelease     = 0.001f; // 1MS
	constexpr float kMaxCompRelease     =    1.f; // 1 sec.
	constexpr float kDefCompRelease     = 0.100f; // 100MS

	// Auto-wah range & defaults
	constexpr float kDefWahResonance     =   0.5f; // 50%
	constexpr float kMinWahAttack        = 0.001f; // 1MS
	constexpr float kMaxWahAttack        =    1.f; // 1 sec.
	constexpr float kDefWahAttack        = 0.125f; // 125MS 
	constexpr float kMinWahHold          = 0.001f; // 1MS
	constexpr float kMaxWahHold          =    1.f; // 1 sec.
	constexpr float kDefWahHold          =  0.25f; // 250MS

	constexpr float kMinWahRate          =    0.f; // DX7 rate (synth-DX7-LFO-table.h)
	constexpr float kMaxWahRate          =   25.f; //
	constexpr float kDefWahRate          =  12.5f; //

	constexpr float kMinWahDrivedB       =  -12.f;
	constexpr float kMaxWahDrivedB       =   12.f;
	constexpr float kDefWahDrivedB       =    3.f;

	constexpr float kMaxWahSpeakVowel    =    3.f;

	// Low cut (for post-pass)
	constexpr float kLowCutHz = 16.f;

	// Size of main delay effect's line in seconds
	constexpr float kMainDelayInSec = 4.f; // Min. 15BPM

	// Default piano pedal settings & mul. range
	constexpr float kDefPianoPedalFalloff = 0.f; // Slowest
	constexpr float kPianoPedalMinReleaseMul = 1.f;
	constexpr float kPianoPedalMaxReleaseMul = 10.f;                     // Rather arbitrary, in fact I'm not sure if this should be a feature at all! (FIXME)
	constexpr float kDefPianoPedalReleaseMul = kPianoPedalMinReleaseMul; // So because of that, by default, the influence of this parameter is nil.

	// Modulator input is low passed a little bit for certain waveforms to "take the top off"
	constexpr float kModulatorLP = 0.875f; // Normalized range [0..1]

	// LFO modulation speed in steps (exponential)
	constexpr int kMinLFOModSpeed = -8;
	constexpr int kMaxLFOModSpeed =  8;
};

// All helper functionality is at your disposal by default
// I might *not* want to do this if the set grows significantly bigger
#include "helper/synth-log.h"
#include "helper/synth-math.h"
#include "helper/synth-random.h"
#include "helper/synth-helper.h"
#include "helper/synth-fast-tan.h"
#include "helper/synth-fast-cosine.h"
#include "helper/synth-aligned-alloc.h"
#include "helper/synth-ring-buffer.h"
#include "helper/synth-MIDI.h"

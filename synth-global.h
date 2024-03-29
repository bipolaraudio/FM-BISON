
/*
	FM. BISON hybrid FM synthesis -- Global includes, constants & utility functions: include on top of every header or autonomous CPP.
	(C) njdewit technologies (visualizers.nl) & bipolaraudio.nl
	MIT license applies, please see https://en.wikipedia.org/wiki/MIT_License or LICENSE in the project root!
*/

#pragma once

// Include JUCE
#include <JuceHeader.h>

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

// Use these for all normalized variables (25/08/2020)
#define SFM_ASSERT_NORM(variable)   SFM_ASSERT(variable >=  0.f && variable <= 1.f)
#define SFM_ASSERT_BINORM(variable) SFM_ASSERT(variable >= -1.f && variable <= 1.f)

// Use this for range checks (06/12 -> start using them!)
#define SFM_ASSERT_RANGE(variable, minimum, maximum) SFM_ASSERT(variable >= minimum && variable <= maximum)
#define SFM_ASSERT_RANGE_BI(variable, range) SFM_ASSERT(variable >= -range && variable <= range)

// Set to 1 to kill all SFM log output
#if defined(_DEBUG) && !defined(PROFILE_BUILD)
	#define SFM_NO_LOGGING 0
#else
	#define SFM_NO_LOGGING 1
#endif

// Set to 1 to let FM. BISON handle denormals
#define SFM_KILL_DENORMALS 1

// Define to disable all FX (including per-voice filter)
// #define SFM_DISABLE_FX

// Define to disable extra voice rendering thread
#define SFM_DISABLE_VOICE_THREAD

namespace SFM
{
	/*
		Almost all contants used by FM. BISON and it's host are defined here; I initially chose to stick them in one place because 
		this was a much smaller project and there simply weren't too many them

		This changed over time and now more and more of them are defined close to or in the implementation they're relevant instead; 
		new ones that go here should be either be in use in various places (ask yourself why), or, important: be of use by the host
		software

		I've added some comment blocks to make this easier to navigate
	*/

	// ----------------------------------------------------------------------------------------------
	// Voices
	// ----------------------------------------------------------------------------------------------

	// Max. number of voices & samples to render using the main (single) thread
	// 32 is based on 64 being a reasonable total
	// Only relevant when !defined(SFM_DISABLE_VOICE_THREAD)
	constexpr unsigned kSingleThreadMaxVoices = 32;
	constexpr unsigned kMultiThreadMinSamples = 512;

	// Max. fixed frequency (have fun with it!)
	constexpr float kMaxFixedHz = 96000.f;
	constexpr float kDefaultFixedHz = 440.f;

	// Polyphony constraints
	constexpr unsigned kMinPolyVoices = 1;
	constexpr unsigned kMaxPolyVoices = 128;

	// Default number of vioces
	constexpr unsigned kDefMaxPolyVoices = 32; // Safe and fast

	// ----------------------------------------------------------------------------------------------
	// Default InterpolatedParameter latency (used for per-sample interpolation)
	// ----------------------------------------------------------------------------------------------

	constexpr float kDefParameterLatency = 0.01f; // 10MS

	// ----------------------------------------------------------------------------------------------
	// Default InterpolatedParameter latency (used for per-sample interpolation)
	// ----------------------------------------------------------------------------------------------

	constexpr float kGlobalAmpCutTime = 0.01f; // 10MS;

	// ----------------------------------------------------------------------------------------------
	// Number of FM synthesis operators (changing this value requires a thorough check)
	// ----------------------------------------------------------------------------------------------

	constexpr unsigned kNumOperators = 6;

	// ----------------------------------------------------------------------------------------------
	// Base note Hz (A4)
	//
	// "Nearly all modern symphony orchestras in Germany and Austria and many in other countries in 
	//  continental Europe (such as Russia, Sweden and Spain) tune to A = 443 Hz." (Wikipedia)
	// ----------------------------------------------------------------------------------------------

	constexpr float kBaseHz = 440.f;

	// ----------------------------------------------------------------------------------------------
	// Pitch bend & amplitude "bend" range
	// ----------------------------------------------------------------------------------------------

	// Max. pitch bend range (in semitones)
	constexpr unsigned kMaxPitchBendRange = 48; // +/- 4 octaves
	constexpr unsigned kDefPitchBendRange = 12; // +/- 1 octave

	constexpr float kAmpBendRange = 6.f; // -6dB to 6dB

	// ----------------------------------------------------------------------------------------------
	// Jitter
	// ----------------------------------------------------------------------------------------------

	// Jitter: max. note drift (in cents, -/+)
	constexpr unsigned kMaxNoteJitter = 50; // Half a note

	// Jitter: max. detune (in cents, -/+)
	constexpr float kMaxDetuneJitter = 1.f; // 100th of a note

	// ----------------------------------------------------------------------------------------------
	// (Mini) EQ, default is 0.f (0 dB, or neutral gain)
	// ----------------------------------------------------------------------------------------------

	constexpr float kMiniEQMindB =  -64.f;
	constexpr float kMiniEQMaxdB =   12.f;

	// ----------------------------------------------------------------------------------------------
	// Filter
	// ----------------------------------------------------------------------------------------------

	// Per operator filter peak dB range
	constexpr float kMinOpFilterPeakdB =  -24.f;
	constexpr float kMaxOpFilterPeakdB =   12.f;
	constexpr float kDefOpFilterPeakdB =   -3.f;

	// Main (SVF) filter Q range (not to be confused with normalized resonance, max. must be <= 40.f, the impl. says)
	// Helper function ResoToQ() scales to range automatically
	constexpr float kSVFLowestFilterQ = 0.025f; // Use carefully, a Q has shown to cause instability (filter slowly 'blowing up')
	constexpr float kSVFMinFilterQ    = 0.5f;   // See https://www.earlevel.com/main/2003/03/02/the-digital-state-variable-filter/
	constexpr float kSVFMaxFilterQ    = 40.f;   // Actual max. is 40.f
	constexpr float kSVFFilterQRange  = kSVFMaxFilterQ-kSVFMinFilterQ;

	constexpr float kMinFilterCutoff      =  0.f;    // Normalized min. filter cutoff; range is simply [0..1] (use SVF_CutoffToHz())
	constexpr float kSVFMinFilterCutoffHz =  0.f;    // Min 16.f (See impl.) -> FIXME: I'm violating this to get a full cut!
	constexpr float kSVFMaxFilterCutoffHz = 22050.f; // Nyquist @ 44.1KHz
	
	// Biquad filter range
	constexpr float kBiquadMinFilterCutoffHz = 20.f;    
	constexpr float kBiquadMaxFilterCutoffHz = 22050.f; 

	// Default main (SVF) filter settings
	constexpr float kDefMainFilterCutoff       =   1.f; // Normalized; no (or minimal) filtering (when acting as LPF at least)
	constexpr float kMainCutoffAftertouchRange = 0.66f; // Limits aftertouch cutoff to avoid that low range of the cutoff that's not allowed (SVF, < 16.0), which may cause filter instability
	constexpr float kDefMainFilterResonance    =   0.f; // Filter's default normalized resonance
	
	// Normalized resonance range (can be) limited for main voice filter
	constexpr float kDefMainFilterResonanceLimit = 0.5f;

	// Default post-pass filter drive range & default (dB)	
	constexpr float kMinPostFilterDrivedB = -3.f;
	constexpr float kMaxPostFilterDrivedB =  9.f;
	constexpr float kDefPostFilterDrivedB =  0.f;

	// Post-pass filter cutoff range
	constexpr float kMinPostFilterCutoffHz = 40.f;
	constexpr float kMaxPostFilterCutoffHz = 22050.f;
	constexpr float kPostFilterCutoffRange = kMaxPostFilterCutoffHz-kMinPostFilterCutoffHz;

	// Normal magnitude (gain) response at cutoff point (it's 1.0/SQRT(2.0)) (Biquad, name more or less coined by Nigel Redmon)
	// Also: https://www.youtube.com/watch?v=f8ITRgPgzmY&ab_channel=Lantertronics-AaronLanterman
	// Often default Q; it's where the poles make a perfect 45 degree angle with the axis!
	constexpr float kNormalGainAtCutoff = 0.707106769f; 

	// ----------------------------------------------------------------------------------------------
	// Tube distortion
	// ----------------------------------------------------------------------------------------------

	// Tube distortion drive & offset
	constexpr float kMinTubeDrive    =   0.f;
	constexpr float kMaxTubeDrive    = 100.f;
	constexpr float kDefTubeDrive    =  10.f;
	constexpr float kDefTubeTone     =   1.f; // Normalized cutoff
	constexpr bool  kDefTubeToneReso = false; 
	constexpr float kMinTubeOffset   = -0.1f;
	constexpr float kMaxTubeOffset   =  0.1f;

	// ----------------------------------------------------------------------------------------------
	// Envelope
	// ----------------------------------------------------------------------------------------------

	// Envelope rate multiplier range (or 'global')
	// Range (as in seconds) taken from Arturia DX7-V 
	// Ref. http://downloads.arturia.com/products/dx7-v/manual/dx7-v_Manual_1_0_EN.pdf
	constexpr float kEnvMulMin = 0.1f;
	constexpr float kEnvMulMax = 60.f;
	constexpr float kEnvMulRange = kEnvMulMax-kEnvMulMin;

	// ----------------------------------------------------------------------------------------------
	// Gain per voice (in dB)
	// This keeps the voice mix nicely within acceptable range, approx. 8 voices, 
	// see https://www.kvraudio.com/forum/viewtopic.php?t=275702
	// ----------------------------------------------------------------------------------------------

	constexpr float kVoiceGaindB = -9.f;
	constexpr float kVoiceGain = 0.354813397f;

	// ----------------------------------------------------------------------------------------------
	// Chorus/Phaser
	// ----------------------------------------------------------------------------------------------
	
	// Chorus/Phaser effect (synth-post-pass.cpp) max. wetness
	constexpr float kMaxChorusPhaserWet = 0.707f; // -3dB

	// Chorus/Phaser rate multipliers (Hz)
	constexpr float kMaxChorusRate = 12.f;
	constexpr float kMaxPhaserRate = 8.f;

	// ----------------------------------------------------------------------------------------------
	// Master output volume range & default in dB + our definition of -INF
	// ----------------------------------------------------------------------------------------------

	constexpr int kMinVolumedB   =  -100;
	constexpr int kMaxVolumedB   =     3;
	constexpr int kVolumeRangedB = kMaxVolumedB-kMinVolumedB;

	// At -12dB a single pure sine at max. amplitude yields -16dB, the idea being that at this 
	// volume you can start adding layers without running into the red too quickly
	constexpr int kDefVolumedB = -12; 

	// Nicked from juce::Decibels
	constexpr float kInfdB  = -100.f; 
	constexpr float kInfLin = 9.99999975e-06f; // dB2Lin(kInfdB)

	// ----------------------------------------------------------------------------------------------
	// (Monophonic) frequency glide (in seconds)
	// ----------------------------------------------------------------------------------------------

	constexpr float kMaxFreqGlide = 1.f;        // 1000MS
	constexpr float kDefMonoFreqGlide = 0.066f; //   66MS
	constexpr float kDefPolyFreqGlide = 0.1f;   //  100MS
	constexpr float kDefMonoGlideAtt = 0.33f;   // [0..1], the larger the punchier

	// ----------------------------------------------------------------------------------------------
	// Slew parameters for S&H
	// ----------------------------------------------------------------------------------------------

	constexpr float kMinSandHSlewRate = 0.001f;  //  1MS
	constexpr float kMaxSandHSlewRate =  0.05f;  // 50MS
	constexpr float kDefSandHSlewRate = 0.005f;  //  5MS

	// ----------------------------------------------------------------------------------------------
	// Reverb
	// ----------------------------------------------------------------------------------------------

	// Effect sounds best until mixed to max. 50-60 percent (wetness)
	constexpr float kMaxReverbWet = 0.55f;

	// Stereo width range & default
	constexpr float kMinReverbWidth = 0.f;
	constexpr float kMaxReverbWidth = 2.f;
	constexpr float kDefReverbWidth = 0.5f;

	// Default dampening
	constexpr float kDefReverbDampening = 0.5f;

	// ----------------------------------------------------------------------------------------------
	// Compressor range & defaults
	// ----------------------------------------------------------------------------------------------

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
	constexpr float kDefCompAttack      = 0.500f; // 500MS
	constexpr float kMinCompRelease     = 0.001f; // 1MS
	constexpr float kMaxCompRelease     =    1.f; // 1 sec.
	constexpr float kDefCompRelease     = 0.500f; // 500MS
	constexpr float kDefCompLookahead   =    1.f; // Set to max. it'll react immediately (feels counterintuitive, but basically 'lookahead' is implemented using a delay)

	// ----------------------------------------------------------------------------------------------
	// Auto-wah/Vox range & defaults
	// ----------------------------------------------------------------------------------------------

	constexpr float kDefWahResonance     =   0.5f; // 50%
	constexpr float kMinWahAttack        = 0.001f; // 1MS
	constexpr float kMaxWahAttack        =    1.f; // 1 sec.
	constexpr float kDefWahAttack        = 0.333f; // 333MS 
	constexpr float kMinWahHold          = 0.001f; // 1MS
	constexpr float kMaxWahHold          =    1.f; // 1 sec.
	constexpr float kDefWahHold          = 0.666f; // 666MS

	constexpr float kMinWahRate          =    0.f; // DX7 rate (synth-DX7-LFO-table.h)
	constexpr float kMaxWahRate          =  100.f; // 
	constexpr float kDefWahRate          =    8.f; //

	constexpr float kMinWahDrivedB       =  -12.f; 
	constexpr float kMaxWahDrivedB       =   12.f;
	constexpr float kDefWahDrivedB       =    3.f;

	constexpr float kMaxWahSpeakVowel    =    3.f;

	// ----------------------------------------------------------------------------------------------
	// Size of main delay effect's line in seconds & drive (dB) range
	// ----------------------------------------------------------------------------------------------

	constexpr float kMainDelayInSec = 8.f; // Min. 7.5BPM

	// Defined here because kMainDelayInSec dictates it

	// FIXME: use to check if rate is in bounds in FM_BISON.cpp!
	constexpr float kMinBPM = 60.f/kMainDelayInSec;

	constexpr float kMinDelayDrivedB = -12.f;
	constexpr float kMaxDelayDrivedB =  12.f;
	constexpr float kDefDelayDrivedB =   0.f;

	// ----------------------------------------------------------------------------------------------
	// Default piano pedal settings
	// Important: this feature relies on the envelope having non-zero decay
	// ----------------------------------------------------------------------------------------------

	constexpr float kDefPianoPedalFalloff     = 0.f;                      // Slowest, meaning that it will lengthen the decay by kPianoPedalFalloffRange (max.)
	constexpr float kPianoPedalFalloffRange   = 3.f;                      // 
	constexpr float kPianoPedalMinReleaseMul  = 1.f;
	constexpr float kPianoPedalMaxReleaseMul  = 3.f;                      // 
	constexpr float kDefPianoPedalReleaseMul  = kPianoPedalMinReleaseMul; // So because of that, by default, the influence of this parameter is nil.
	
	// ----------------------------------------------------------------------------------------------
	// Modulator input is low passed a little bit for certain waveforms to "take the top off", 
	// so modulation sounds less glitchy out of the box
	// ----------------------------------------------------------------------------------------------

	constexpr float kModulatorLP = 0.875f; // Normalized range [0..1]

	// ----------------------------------------------------------------------------------------------
	// LFO modulation speed in steps (exponential)
	// ----------------------------------------------------------------------------------------------

	constexpr int kMinLFOModSpeed = -8;
	constexpr int kMaxLFOModSpeed =  8;

	// ----------------------------------------------------------------------------------------------
	// Default supersaw (JP-8000) parameters
	// ----------------------------------------------------------------------------------------------

	constexpr float kDefSupersawDetune = 0.f; // It will sound like a saw this way
	constexpr float kDefSupersawMix    = 0.f; //

	// ----------------------------------------------------------------------------------------------
	// Post low cut settings
	// ----------------------------------------------------------------------------------------------

	constexpr float kLowCutHz =  20.f;
	constexpr float kLowCutQ  =  kNormalGainAtCutoff;
//	constexpr float kLowCutdB = -64.f;
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

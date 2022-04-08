
/*
	FM. BISON hybrid FM synthesis
	(C) njdewit technologies (visualizers.nl) & bipolaraudio.nl
	MIT license applies, please see https://en.wikipedia.org/wiki/MIT_License or LICENSE in the project root!
*/

// Shut it, MSVC
#ifndef _CRT_SECURE_NO_WARNINGS
	#define _CRT_SECURE_NO_WARNINGS
#endif

#include "FM_BISON.h"

#include "synth-global.h"
#include "patch/synth-patch-global.h"
#include "synth-DX7-LFO-table.h"

namespace SFM
{
	static bool s_performStaticInit = true;

	/* ----------------------------------------------------------------------------------------------------

		Constructor/Destructor

	 ------------------------------------------------------------------------------------------------------ */
	
	Bison::Bison()
	{
		if (true == s_performStaticInit)
		{
			// Calculate LUTs & initialize random generator
			InitializeRandomGenerator();
			CalculateMIDIToFrequencyLUT();
			InitializeFastCosine();
			Supersaw::CalculateDetuneTable();

			s_performStaticInit = false;
		}
		
		// Reset entire patch
		m_patch.ResetToEngineDefaults();

		// Initialize polyphony
		const bool monophonic = Patch::VoiceMode::kMono == m_patch.voiceMode;
		m_curPolyphony = (false == monophonic) ? m_patch.maxPolyVoices : 1;

		Log("Instance of FM. BISON engine initalized");
		Log("Suzie, call DR. BISON, tell him it's for me...");

		/*
			IMPORTANT: call SetSamplingProperties() before Render()
		*/
	}

	Bison::~Bison() 
	{
		DeleteRateDependentObjects();

		Log("Instance of FM. BISON engine released");
	}

	/* ----------------------------------------------------------------------------------------------------

		OnSetSamplingProperties() is called by JUCE, but what it does is set up the basic properties
		necessary to start rendering so it can be used in any situation

	 ------------------------------------------------------------------------------------------------------ */

	// Called by JUCE's prepareToPlay()
	void Bison::OnSetSamplingProperties(unsigned sampleRate, unsigned samplesPerBlock)
	{
		Log("BISON::OnSetSamplingProperties(" + std::to_string(sampleRate) + ", " + std::to_string(samplesPerBlock) + ")");

		m_sampleRate       = sampleRate;
		m_samplesPerBlock  = samplesPerBlock;

		m_Nyquist = sampleRate>>1;

		/* 
			Reset sample rate dependent global objects
		*/

		DeleteRateDependentObjects();

		// Stop & reset all voices, clear slots & wipe requests
		for (unsigned iVoice = 0; iVoice < kMaxPolyVoices; ++iVoice)		
			m_voices[iVoice].Reset(m_sampleRate);

		for (unsigned iSlot = 0; iSlot < 128; ++iSlot)
			m_keyToVoice[iSlot] = -1;

		m_polyVoiceReq.clear();
		m_polyVoiceReleaseReq.clear();

		m_resetVoices = false;
		 
		// Reset BPM
		m_BPM = 0.0;
		m_resetPhaseBPM = true;

		// Voice mode
		m_curVoiceMode = m_patch.voiceMode;

		// Reset monophonic state
		m_monoSequence.clear();
		m_monoVoiceReq.key = VoiceRequest::kInvalid;
		m_monoVoiceReleaseReq.key = MonoVoiceReleaseRequest::kInvalid;

		// Reset filter type
		m_curFilterType = SvfLinearTrapOptimised2::NO_FLT_TYPE;

		// Allocate intermediate buffers (a pair for each thread)
		m_pBufL[0] = reinterpret_cast<float *>(mallocAligned(m_samplesPerBlock*sizeof(float), 16));
		m_pBufL[1] = reinterpret_cast<float *>(mallocAligned(m_samplesPerBlock*sizeof(float), 16));
		m_pBufR[0] = reinterpret_cast<float *>(mallocAligned(m_samplesPerBlock*sizeof(float), 16));
		m_pBufR[1] = reinterpret_cast<float *>(mallocAligned(m_samplesPerBlock*sizeof(float), 16));

		// Create effects
		m_postPass = new PostPass(m_sampleRate, m_samplesPerBlock, m_Nyquist);

		// Start global LFO phase
		m_globalLFO = new Phase(m_sampleRate);
		const float freqLFO = MIDI_To_DX7_LFO_Hz(m_patch.LFORate);
		m_globalLFO->Initialize(freqLFO, m_sampleRate);

		// Reset global interpolated parameters
		m_curLFOBlend    = { m_patch.LFOBlend, m_sampleRate, kDefParameterLatency, 0.f, 1.f };
		m_curLFOModDepth = { m_patch.LFOModDepth, m_sampleRate, kDefParameterLatency, 0.f, 1.f };
		m_curCutoff      = { SVF_CutoffToHz(m_patch.cutoff, m_Nyquist), m_sampleRate, kDefParameterLatency * 10.f /* Longer */, kSVFMinFilterCutoffHz, kSVFMaxFilterCutoffHz};
		m_curQ           = { SVF_ResoToQ(m_patch.resonance), m_sampleRate, kDefParameterLatency, kSVFMinFilterQ, kSVFMaxFilterQ};
		m_curPitchBend   = { 0.f, m_sampleRate, kDefParameterLatency, -1.f, 1.f };
		m_curAmpBend     = { 1.f /* 0dB */, m_sampleRate, kDefParameterLatency, 0.f, 1.f };
		m_curModulation  = { 0.f, m_sampleRate, kDefParameterLatency * 1.5f /* Longer */, 0.f, 1.f };
		m_curAftertouch  = { 0.f, m_sampleRate, kDefParameterLatency * 3.f  /* Longer */, 0.f, 1.f };

		// Reset operator peaks (visualization)
		for (float &peak : m_opPeaks)
			peak = 0.f;
	}

	// Cleans up after OnSetSamplingProperties()
	void Bison::DeleteRateDependentObjects()
	{
		// Allocate intermediate sample buffers
		if (nullptr != m_pBufL[0]) // Good enough to assume
		{
			freeAligned(m_pBufL[0]);
			freeAligned(m_pBufL[1]);
			freeAligned(m_pBufR[0]);
			freeAligned(m_pBufR[1]);
		}

		m_pBufL[0] = m_pBufL[1] = m_pBufR[0] = m_pBufR[1] = nullptr;

		// Release post-pass
		delete m_postPass;
		m_postPass = nullptr;

		// Delete global LFO
		delete m_globalLFO;
		m_globalLFO = nullptr;
	}

	/* ----------------------------------------------------------------------------------------------------

		Voice management

		Polyphonic mode works as you'd expect

		Monophonic mode:
		- Each note uses it's own velocity for all calculations, instead of carrying over the
		  initial one (like I can hear my Yamaha TG77 do)
		- When a note is released, the last note in the sequence will play; to restart lift all keys
		- Glide speed and velocity attenuation can be controlled through parameters

	 ------------------------------------------------------------------------------------------------------ */

	// Release voice, but retain key slot
	void Bison::ReleaseVoice(int index)
	{
		SFM_ASSERT(index >= 0 && index < kMaxPolyVoices);

		Voice &voice = m_voices[index];
		SFM_ASSERT(true == voice.IsPlaying());
		
		// Lift possible sustain & release
		voice.m_sustained = false;
		voice.OnRelease();

		const int key = voice.m_key;
		Log("Voice released: " + std::to_string(index) + " for key: " + std::to_string(key));
	}
	
	// Free voice & key slot immediately
	void Bison::FreeVoice(int index)
	{
		SFM_ASSERT(index >= 0 && index < kMaxPolyVoices);

		Voice &voice = m_voices[index];

		SFM_ASSERT(false == voice.IsIdle());

		// Set to idle, lift possible sustain
		voice.m_state = Voice::kIdle;
		voice.m_sustained = false;

		// Decrease global count
		SFM_ASSERT(m_voiceCount > 0);
		--m_voiceCount;
		
		// Free key
		const int key = voice.m_key;
		if (-1 != key)
		{
			FreeKey(voice.m_key);
			voice.m_key = -1;

			Log("Voice freed: " + std::to_string(index) + " for key: " + std::to_string(key));
		}
		else
			Log("Voice freed: " + std::to_string(index));
	}

	// Steal voice (quick fade)
	void Bison::StealVoice(int index)
	{
		SFM_ASSERT(index >= 0 && index < kMaxPolyVoices);

		Voice &voice = m_voices[index];
		SFM_ASSERT(false == voice.IsIdle());

		// Can not steal what has already been stolen!
		SFM_ASSERT(false == voice.IsStolen());

		// Not active? Does not need to be stolen
		if (true == voice.IsIdle())
			return;

		// Flag as stolen
		voice.m_state = Voice::kStolen;
		
		// Initiate fade out
		const float curGlobalAmp = voice.m_globalAmp.Get();
		voice.m_globalAmp.SetRate(m_sampleRate, kGlobalAmpCutTime);
		voice.m_globalAmp.Set(curGlobalAmp);
		voice.m_globalAmp.SetTarget(0.f);

		// Free key (if not already done)
		const int key = voice.m_key;
		if (-1 != key)
		{
			FreeKey(key);
			voice.m_key = -1;

			Log("Voice stolen: " + std::to_string(index) + " for key: " + std::to_string(key));
		}
		else
			Log("Voice stolen (not bound to key): " + std::to_string(index));
	}

	void Bison::NoteOn(unsigned key, float frequency, float velocity, unsigned timeStamp)
	{
		const bool monophonic = Patch::VoiceMode::kMono == m_patch.voiceMode;

		SFM_ASSERT(key <= 127);
		SFM_ASSERT(velocity >= 0.f && velocity <= 1.f);

		// In case of duplicates honour the first NOTE_ON
		for (const auto &request : m_polyVoiceReq)
			if (request.key == key)
			{
				Log("Duplicate NoteOn() for key: " + std::to_string(key));
				return;
			}

		VoiceRequest request;
		request.key            = key;
		request.frequency      = frequency;
		request.velocity       = velocity;
		request.timeStamp      = timeStamp;
		
		const int index = GetVoice(key);

		if (false == monophonic)
		{
			/* Polyphonic */

			// Key bound to voice (retrigger)?
			if (index >= 0)
			{
				Voice &voice = m_voices[index];

				if (false == voice.IsIdle())
				{
					/* Polyphonic */

					// Steal if still playing (performance fix)
					if (false == voice.IsStolen())
						StealVoice(index);

					Log("NoteOn() retrigger: " + std::to_string(key) + ", voice: " + std::to_string(index));
				}
			}

			// Issue request
			if (m_polyVoiceReq.size() < m_curPolyphony)
			{
				m_polyVoiceReq.push_back(request);
			}
			else
			{
				// Replace last request (FIXME: honour time stamp?)
				m_polyVoiceReq.pop_back();
				m_polyVoiceReq.push_back(request);
			}
		}
		else
		{
			/* Monophonic */

			SFM_ASSERT(1 == m_curPolyphony);

			Log("NoteOn() monophonic, key: " + std::to_string(key));

			if (false == m_monoVoiceReq.MonoIsValid() || request.timeStamp <= m_monoVoiceReq.timeStamp)
			{
				m_monoVoiceReq = request;

				Log("Monophonic: is audible request");
			}

			// Always add requests to sequence
			m_monoSequence.emplace_front(request);
		}
	}

	void Bison::NoteOff(unsigned key, unsigned timeStamp)
	{
		const bool monophonic = Patch::VoiceMode::kMono == m_patch.voiceMode;

		SFM_ASSERT(key <= 127);

		// In case of duplicates honour the first NOTE_OFF
		for (auto request : m_polyVoiceReleaseReq)
			if (request == key)
			{
				Log("Duplicate NoteOff() for key: " + std::to_string(key));
				return;
			}

		if (false == monophonic)
		{
			/* Polyphonic */

			const int index = GetVoice(key);
			if (index >= 0)
			{
				// Issue request				
				m_polyVoiceReleaseReq.push_back(key);
			}
		
			// It might be that a deferred request matches this NOTE_OFF, in which case we get rid of the request
			for (auto iReq = m_polyVoiceReq.begin(); iReq != m_polyVoiceReq.end(); ++iReq)
			{
				if (iReq->key == key)
				{
					// Erase and break since NoteOn() ensures there are no duplicates in the deque
					m_polyVoiceReq.erase(iReq);

					Log("Deferred NoteOn() removed due to matching NOTE_OFF for key: " + std::to_string(key));

					break;
				}
			}
		}
		else
		{
			/* Monophonic */

			Log("NoteOff() monophonic, key: " + std::to_string(key));

			const int index = GetVoice(key);
			if (index >= 0)
			{
				SFM_ASSERT(0 == index);

				// Key actually playing?
				if (m_voices[index].IsPlaying())
				{
					m_monoVoiceReleaseReq.key = key;
					m_monoVoiceReleaseReq.timeStamp = timeStamp;

					Log("Monophonic: is release request of playing note");
				}
			}
			
			// Remove occurence			
			for (auto iReq = m_monoSequence.begin(); iReq != m_monoSequence.end(); ++iReq)
			{
				if (iReq->key == key)
				{
					m_monoSequence.erase(iReq);

					Log("Monophonic: key removed from sequence");

					break;
				}
			}
		}
	}

	/* ----------------------------------------------------------------------------------------------------

		Voice initialization helper functions

	 ------------------------------------------------------------------------------------------------------ */

	// Calc. operator frequency
	static float CalcOpFreq(float fundamentalFreq, float detuneOffs, const PatchOperators::Operator &patchOp)
	{
		float frequency;
		if (true == patchOp.fixed)
		{
			frequency = (float) patchOp.coarse;
			SFM_ASSERT(frequency >= 0.f && frequency <= kMaxFixedHz);

			// Fixed requency does not necessarily have to be below Nyquist
			// Ratio, fine and detune controls are disabled in VST UI
		}
		else
		{
			frequency = fundamentalFreq;

			const int   coarse = patchOp.coarse;              // Ratio
			const int     fine = patchOp.fine;                // Semitones
			const float detune = patchOp.detune + detuneOffs; // Cents

			SFM_ASSERT(coarse >= kCoarseMin && coarse <= kCoarseMax);
			SFM_ASSERT(abs(fine) <= kFineRange);
			SFM_ASSERT(abs(detune) <= kDetuneRange);
			
			frequency *= powf(2.f, (detune*0.01f)/12.f);

			if (coarse < 0)
				frequency /= abs(coarse-1);
			else if (coarse > 1)
				frequency *= coarse;
			
			frequency *= powf(2.f, fine/12.f);
		}

		return frequency;
	}

	// Calc. multiplier for operator amplitude or modulation index
	static float CalcOpLevel(unsigned key, float velocity, const PatchOperators::Operator &patchOp)
	{
		float multiplier = 1.f;

		// Factor in velocity (linear)
		multiplier = lerpf<float>(multiplier, multiplier*velocity, patchOp.velSens);
		
		// Apply L/R breakpoint cut & level scaling (subtractive/additive & linear/exponential, like the DX7)
		const unsigned breakpoint = patchOp.levelScaleBP;

		if (true == patchOp.cutLeftOfLSBP && true == patchOp.cutRightOfLSBP)
		{
			// Range cut
			const unsigned sides = 127-breakpoint;

			unsigned left = sides/2;
			const unsigned remainder = left % 12;
			left += 12-remainder; // Align right to high C note

			const unsigned right = left+breakpoint;
			
			if (key < left || key > right)
				multiplier = 0.f;
		}
		else if (true == patchOp.cutLeftOfLSBP && key < breakpoint)
			// Cut left
			multiplier = 0.f;
		else if (true == patchOp.cutRightOfLSBP && key > breakpoint)
			// Cut right
			multiplier = 0.f;
		
		// We didn't cut, so apply level scaling as usual
		if (0.f != multiplier)
		{
			// Apply level scaling
			const unsigned numSemis = patchOp.levelScaleRange;
			if (0 != numSemis)
			{
				const bool keyIsLeftOfBP  = key < breakpoint;
				const bool keyIsRightOfBP = key > breakpoint;

				const float levelStep = 1.f/numSemis;

				int distance = 0;
				float amount = 0.f;
				bool isExponential = false;

				if (true == keyIsLeftOfBP)
				{
					distance = breakpoint-key;
					amount = patchOp.levelScaleL;
					isExponential = patchOp.levelScaleExpL;
				}
				else if (true == keyIsRightOfBP)
				{
					distance = key-breakpoint;
					amount = patchOp.levelScaleR;
					isExponential = patchOp.levelScaleExpR;
				}

				// Calculate normalized distance from BP
				distance = std::min<int>(numSemis, abs(distance));
				const float linear = smoothstepf(distance*levelStep); // This (smoothstep) takes the edges off, results in a smoother glide
				const float factor = false == isExponential ? linear : powf(linear, 1.f-linear) /* -EXP/+EXP */;

				if (amount < 0.f)
					// Fade out by gradually interpolating towards lower level
					multiplier = lerpf<float>(multiplier, multiplier*(1.f-fabsf(amount)), factor);
				else if (amount > 0.f)
					// Fade in by adding to set level
					multiplier = lerpf<float>(multiplier, std::min<float>(1.f, multiplier+fabsf(amount)), factor);

				// Subtractive as well as additive scaling leave the multiplier (output) on the other side
				// of the breakpoint intact; this makes it intuitive to use this feature (I think)
			}
		}

		SFM_ASSERT(multiplier >= 0.f && multiplier <= 1.f);
		
		// Return level multiplier!
		return multiplier;
	}

	// Simply scales [-1..1] to [0.0..1.0]
	SFM_INLINE static float CalcPanning(const PatchOperators::Operator &patchOp)
	{
		SFM_ASSERT(fabsf(patchOp.panning) <= 1.f);
		return 0.5f*patchOp.panning + 0.5f;
	}


	SFM_INLINE static float CalcPhaseJitter(float jitter)
	{
		SFM_ASSERT(jitter >= 0.f && jitter <= 1.f);
		return jitter*mt_randf()*0.25f; // [0..90] deg.
	}

	SFM_INLINE static float CalcPhaseShift(const Voice::Operator &voiceOp, const PatchOperators::Operator &patchOp)
	{
		float shift = 0.f;
		if (false == patchOp.keySync)
		{
			shift = voiceOp.oscillator.GetPhase() * mt_randf(); // FIXME: keep phase running or at least have global ones for each operator
		}

		return shift;
	}

	// Returns [-1..1]
	SFM_INLINE static float CalcOpCutoffKeyTracking(unsigned key, float cutoffKeyTrack)
	{
		SFM_ASSERT(key >= 0 && key <= 127);
		const float normalizedKey = key/127.f;

		/* const */ float tracking = cutoffKeyTrack;
		SFM_ASSERT_BINORM(tracking);

		return tracking*normalizedKey;
	}

	// Set up (static) operator filter
	static void SetOperatorFilters(unsigned key, unsigned sampleRate, Biquad &filter, SvfLinearTrapOptimised2 &modFilter, const PatchOperators::Operator &patchOp)
	{
		SFM_ASSERT(sampleRate > 0);

		const unsigned Nyquist = sampleRate/2;

//		filter.resetState();    //
//		filter.reset();         // <- Shouldn't be necessary, as we always initialize it or set it to 'bq_type_none'
		modFilter.resetState(); //
		
		const float normQ = patchOp.resonance;
		
		// Calculate keytracking (only for LPF & HPF)
		float tracking = -1.f, cutoffNormFrom = -1.f, cutoffNormTo = -1.f;
		
		if (PatchOperators::Operator::kLowpassFilter == patchOp.filterType || PatchOperators::Operator::kHighpassFilter == patchOp.filterType)
		{
			const float cutoffKeyTrack = patchOp.cutoffKeyTrack;
			tracking = CalcOpCutoffKeyTracking(key, cutoffKeyTrack);

			cutoffNormFrom = patchOp.cutoff;
			cutoffNormTo = (tracking >= 0.f) ? 1.f : 0.f;
		
			tracking = fabsf(tracking);
		}
		
		float cutoffNorm = -1.f; // Causes assertion if not set for kLowpassFilter/kHighpassFilter
		const float biQ = 0.01f + 9.99f*normQ; // See Biquad.h
		switch (patchOp.filterType)
		{
		default:
			SFM_ASSERT(false);

		case PatchOperators::Operator::kNoFilter:
			filter.setBiquad(bq_type_none, 0.f, 0.f, 0.f);
			break;

		case PatchOperators::Operator::kLowpassFilter:
			cutoffNorm = lerpf<float>(cutoffNormFrom, cutoffNormTo, tracking); // Keytrack
			filter.setBiquad(bq_type_lowpass, BQ_CutoffToHz(cutoffNorm, Nyquist)/sampleRate, biQ, 0.f);
			break;

		case PatchOperators::Operator::kHighpassFilter:
			cutoffNorm = lerpf<float>(cutoffNormFrom, 1.f-cutoffNormTo, tracking); // Keytrack
			filter.setBiquad(bq_type_highpass, BQ_CutoffToHz(cutoffNorm, Nyquist)/sampleRate, biQ, 0.f);
			break;

		// FIXME: why not track the next ones too?
		case PatchOperators::Operator::kBandpassFilter:
			filter.setBiquad(bq_type_bandpass, BQ_CutoffToHz(patchOp.cutoff, Nyquist)/sampleRate, biQ, 0.f);
			break;

		case PatchOperators::Operator::kPeakFilter:
			SFM_ASSERT(patchOp.peakdB >= kMinOpFilterPeakdB && patchOp.peakdB <= kMaxOpFilterPeakdB);
			filter.setBiquad(bq_type_peak, BQ_CutoffToHz(patchOp.cutoff, Nyquist)/sampleRate, biQ, patchOp.peakdB);
			break;
		}
		
		switch (patchOp.waveform)
		{
			// These waveforms shall remain unaltered
			case Oscillator::kSine:
			case Oscillator::kCosine:
			case Oscillator::kPolyTriangle:
			case Oscillator::kSupersaw: // Checked and it's not necessary (though at first I assumed it would be)
				modFilter.updateNone();
				break;
			
			// Filter the remaining waveforms a little to "take the top off"
			default:
				modFilter.updateLowpassCoeff(SVF_CutoffToHz(kModulatorLP, Nyquist), kSVFLowestFilterQ, sampleRate);
				break;
		}
	}
	
	// Calc. tracking (linear or 'acoustically curved')
	SFM_INLINE static float CalcKeyTracking(unsigned key, const PatchOperators::Operator &patchOp)
	{
		SFM_ASSERT(key >= 0 && key <= 127);
		const float normalizedKey = key/127.f;

		SFM_ASSERT(patchOp.envKeyTrack >= 0.f && patchOp.envKeyTrack <= 1.f);
		
		return (false == patchOp.acousticEnvKeyTrack)
			? 1.f - 0.9f*patchOp.envKeyTrack*normalizedKey
			: AcousticTrackingCurve(normalizedKey, patchOp.envKeyTrack); // See impl. for details
	}

	// Calc. voice soft clipping (distortion) amount
	SFM_INLINE static float CalcSoftClip(float opVelocity, const PatchOperators::Operator &patchOp)
	{
		const float softClip = lerpf<float>(patchOp.drive, patchOp.drive*opVelocity, patchOp.velSens);
		return softClip;
	}

	// Calc. LFO frequencies
	SFM_INLINE static void CalcLFOFreq(float &frequency /* Set to base freq. */, float &modFrequency, int speedAdj)
	{
		SFM_ASSERT(frequency > 0.f);
		SFM_ASSERT(speedAdj >= kMinLFOModSpeed && speedAdj <= kMaxLFOModSpeed);

		const float freqSpeedAdj = powf(2.f, float(speedAdj));
		modFrequency = frequency*freqSpeedAdj;
	}

	// Initializes LFOs (used on top of Voice::Sample())
	void Bison::InitializeLFOs(Voice &voice, float jitter)
	{
		// Calc. shift
		float phaseShift = (true == m_patch.LFOKeySync)
			? 0.f // Synchronized 
			: m_globalLFO->Get(); // Free running

		// Add jitter
		phaseShift += CalcPhaseJitter(jitter);
		
		// Frequencies
		float frequency = m_globalLFO->GetFrequency(), modFrequency;
		CalcLFOFreq(frequency, modFrequency, m_patch.LFOModSpeed);

		// Set up LFOs
		voice.m_LFO1.Initialize(m_patch.LFOWaveform1,   frequency,    m_sampleRate, phaseShift);
		voice.m_LFO2.Initialize(m_patch.LFOWaveform2,   frequency,    m_sampleRate, phaseShift);
		voice.m_modLFO.Initialize(m_patch.LFOWaveform3, modFrequency, m_sampleRate, phaseShift);
	}

	/* ----------------------------------------------------------------------------------------------------

		Voice initialization; there's a separate function for a monophonic voice

	 ------------------------------------------------------------------------------------------------------ */
	
	// Initialize new voice
	void Bison::InitializeVoice(const VoiceRequest &request, unsigned iVoice)
	{
		Voice &voice = m_voices[iVoice];

		// No voice reset, this function should initialize all necessary components
		// and be able to use previous values such as oscillator phase to enable/disable

		// Voice not sustained
		voice.m_sustained = false;

		// Offset in samples (to be within a single Render() pass)
		SFM_ASSERT(request.timeStamp <= m_samplesPerBlock);
		voice.m_sampleOffs = request.timeStamp;

		const unsigned key = request.key;        // Key
		const float jitter = m_patch.jitter;     // Jitter
		const float velocity = request.velocity; // Velocity

		// Store key & velocity immediately (used by CalcOpLevel())
		voice.m_key = key;
		voice.m_velocity = velocity;

		// Get fundamental freq. (using JUCE-supplied freq. for now)
		float fundamentalFreq = (-1.f == request.frequency)
			? float(g_MIDIToFreqLUT[key])
			: request.frequency;

		// Note frequency jitter
		const float noteJitter = jitter*mt_randfc()*kMaxNoteJitter;
		fundamentalFreq *= powf(2.f, (noteJitter*0.01f)/12.f);

		voice.m_fundamentalFreq = fundamentalFreq;
		
		// Initialize LFO
		InitializeLFOs(voice, jitter);

		// Get dry FM patch		
		PatchOperators &patchOps = m_patch.operators;

		// Default glide (in case frequency is manipulated whilst playing)
		voice.m_freqGlide = kDefPolyFreqGlide;

		// Acoustic scaling: more velocity can mean longer envelope decay phase
		// This is specifically designed for piano, guitar et cetera: strum or strike
		// harder and the decay phase will be longer
		const float envAcousticScaling = 1.f + (velocity*velocity)*m_patch.acousticScaling;

		// Set up voice operators
		for (unsigned iOp = 0; iOp < kNumOperators; ++iOp)
		{
			const PatchOperators::Operator &patchOp = patchOps.operators[iOp];
			Voice::Operator &voiceOp = voice.m_operators[iOp];

			voiceOp.enabled   = patchOp.enabled;
			voiceOp.isCarrier = patchOp.isCarrier;

			voiceOp.envGain.Reset();

			if (true == voiceOp.enabled)
			{
				// Operator velocity
				const float opVelocity = (false == patchOp.velocityInvert) ? velocity : 1.f-velocity;

				// (Re)set constant/static filters
				SetOperatorFilters(key, m_sampleRate, voiceOp.filter, voiceOp.modFilter, patchOp);
				
				// Store detune jitter
				voiceOp.detuneOffs = jitter*mt_randfc()*patchOp.detune*kMaxDetuneJitter;
	
				const float frequency = CalcOpFreq(fundamentalFreq, voiceOp.detuneOffs, patchOp);
				
				// Get amplitude & index
				const float level = CalcOpLevel(key, opVelocity, patchOp);
				const float amplitude = patchOp.output*level, index = patchOp.index*level;

				voiceOp.oscillator.Initialize(
					patchOp.waveform, frequency, m_sampleRate, CalcPhaseShift(voiceOp, patchOp), patchOp.supersawDetune, patchOp.supersawMix);

				// Set supersaw parameters for interpolation
				voiceOp.supersawDetune.SetRate(m_sampleRate, kDefParameterLatency);
				voiceOp.supersawDetune.Set(patchOp.supersawDetune);

				voiceOp.supersawMix.SetRate(m_sampleRate, kDefParameterLatency);
				voiceOp.supersawMix.Set(patchOp.supersawMix);

				// Set (static) amplitude
				voiceOp.amplitude.SetRate(m_sampleRate, kDefParameterLatency);
				voiceOp.amplitude.Set(amplitude);

				// Set (static) index
				voiceOp.index.SetRate(m_sampleRate, kDefParameterLatency);
				voiceOp.index.Set(index);
					
				// No interpolation
				voiceOp.curFreq.SetRate(m_sampleRate, kDefPolyFreqGlide);
				voiceOp.curFreq.Set(frequency);
				voiceOp.setFrequency = frequency;

				// Envelope key tracking
				const float envKeyTracking = CalcKeyTracking(key, patchOp);
				
				// Start envelope
				voiceOp.envelope.Start(patchOp.envParams, m_sampleRate, patchOp.isCarrier, envKeyTracking, envAcousticScaling); 

				// Modulation sources
				voiceOp.modulators[0] = patchOp.modulators[0];
				voiceOp.modulators[1] = patchOp.modulators[1];
				voiceOp.modulators[2] = patchOp.modulators[2];

				// Feedback
				voiceOp.iFeedback   = patchOp.feedback;
				voiceOp.feedbackAmt = { patchOp.feedbackAmt, m_sampleRate, kDefParameterLatency, 0.f, 1.f };
				voiceOp.feedback    = 0.f;
				
				// LFO influence
				voiceOp.ampMod   = patchOp.ampMod;
				voiceOp.pitchMod = patchOp.pitchMod;
				voiceOp.panMod   = patchOp.panMod;

				// Panning
				voiceOp.panning = { CalcPanning(patchOp), m_sampleRate, kDefParameterLatency, 0.f, 1.f };

				// Soft clip (distortion)
				voiceOp.softClip = { CalcSoftClip(opVelocity, patchOp), m_sampleRate, kDefParameterLatency, 0.f, 1.f };
			}
		}

		// Reset main filter
		voice.m_filterSVF.resetState();

		// Start filter envelope
		voice.m_filterEnvelope.Start(m_patch.filterEnvParams, m_sampleRate, false, 1.f, envAcousticScaling);

		// Start pitch envelope
		voice.m_pitchBendRange = m_patch.pitchBendRange;
		voice.m_pitchEnvelope.Start(m_patch.pitchEnvParams, m_sampleRate);

		// Voice is now playing
		voice.m_state = Voice::kPlaying;
		++m_voiceCount;

		// Store (new) index in key slot
		SFM_ASSERT(-1 == GetVoice(key));
		SetKey(key, iVoice);


		voice.PostInitialize();
	}

	// Specialized function for monophonic voices
	// Not ideal due to some code duplication, but easier to modify and follow
	void Bison::InitializeMonoVoice(const VoiceRequest &request)
	{
		Voice &voice = m_voices[0];

		// No voice reset, this function should initialize all necessary components
		// and be able to use previous values such as oscillator phase to enable/disable
		
		// Reset (do *not* glide) if voice has been let go
		bool reset = voice.IsReleasing() || voice.IsDone();

		// Voice not sustained
		voice.m_sustained = false;

		const unsigned key = request.key;    // Key
		const float jitter = m_patch.jitter; // Jitter

		const float velocity = request.velocity;

		// Store key & velocity immediately (used by CalcOpLevel())
		voice.m_key = key;
		voice.m_velocity = velocity;

		// Get fundamental freq. (using JUCE-supplied freq. for now)
		float fundamentalFreq = (-1.f == request.frequency)
			? float(g_MIDIToFreqLUT[key])
			: request.frequency;

		// Note frequency jitter
		const float noteJitter = jitter * (-1.f + mt_randf()*2.f);
		fundamentalFreq *= powf(2.f, (noteJitter*kMaxNoteJitter*0.01f)/12.f);

		voice.m_fundamentalFreq = fundamentalFreq;
		
		if (true == reset)
		{
			// Initialize LFO
			InitializeLFOs(voice, jitter);
		}

		// See InitializeVoice()
		const float envAcousticScaling = 1.f + (velocity*velocity)*m_patch.acousticScaling;

		// Get patch operators		
		PatchOperators &patchOps = m_patch.operators;

		// Calc. attenuated glide (using new velocity, feels more natural)
		float monoGlide = m_patch.monoGlide;
		float glideAtt = 1.f - m_patch.monoAtt*request.velocity;
		voice.m_freqGlide = monoGlide*glideAtt;

		// Set up voice operators
		for (unsigned iOp = 0; iOp < kNumOperators; ++iOp)
		{
			const PatchOperators::Operator &patchOp = patchOps.operators[iOp];
			Voice::Operator &voiceOp = voice.m_operators[iOp];

			voiceOp.enabled   = patchOp.enabled;
			voiceOp.isCarrier = patchOp.isCarrier;

			voiceOp.envGain.Reset();

			if (true == voiceOp.enabled)
			{
				// Operator velocity
				const float opVelocity = (false == patchOp.velocityInvert) ? velocity : 1.f-velocity;

				if (true == reset)
				{
					// (Re)set constant/static filters
					SetOperatorFilters(key, m_sampleRate, voiceOp.filter, voiceOp.modFilter, patchOp);
				}

				// Store detune jitter
				voiceOp.detuneOffs = jitter*mt_randfc()*patchOp.detune*kMaxDetuneJitter;
				
				const float frequency = CalcOpFreq(fundamentalFreq, voiceOp.detuneOffs, patchOp);

				// Get amplitude & index
				const float level = CalcOpLevel(key, opVelocity, patchOp);
				const float amplitude = patchOp.output*level, index = patchOp.index*level;

				if (true == reset)
				{
					voiceOp.oscillator.Initialize(
						patchOp.waveform, frequency, m_sampleRate, CalcPhaseShift(voiceOp, patchOp), patchOp.supersawDetune, patchOp.supersawMix);

					// Set supersaw parameters for interpolation
					voiceOp.supersawDetune.SetRate(m_sampleRate, kDefParameterLatency);
					voiceOp.supersawDetune.Set(patchOp.supersawDetune);

					voiceOp.supersawMix.SetRate(m_sampleRate, kDefParameterLatency);
					voiceOp.supersawMix.Set(patchOp.supersawMix);

					// Set amplitude
					voiceOp.amplitude.SetRate(m_sampleRate, kDefParameterLatency);
					voiceOp.amplitude.Set(amplitude);

					// Set index
					voiceOp.index.SetRate(m_sampleRate, kDefParameterLatency);
					voiceOp.index.Set(index);

					// Set freq.
					voiceOp.curFreq.SetRate(m_sampleRate, voice.m_freqGlide);
					voiceOp.curFreq.Set(frequency);

					const float envKeyTracking = CalcKeyTracking(key, patchOp);
					voiceOp.envelope.Start(patchOp.envParams, m_sampleRate, patchOp.isCarrier, envKeyTracking, envAcousticScaling); 
				}
				else
				{
					// Glide
					voiceOp.supersawDetune.SetTarget(patchOp.supersawDetune);
					voiceOp.supersawMix.SetTarget(patchOp.supersawMix);

					voiceOp.amplitude.SetTarget(amplitude);
					voiceOp.index.SetTarget(index);
					
					// See synth-interpolated-parameter.h to see why I do this in this particular order
					const float curFreq = voiceOp.curFreq.Get();
					voiceOp.curFreq.SetRate(m_sampleRate, voice.m_freqGlide);
					voiceOp.curFreq.Set(curFreq);
					voiceOp.curFreq.SetTarget(frequency);
				}

				voiceOp.setFrequency = frequency;

				// Modulation sources
				voiceOp.modulators[0] = patchOp.modulators[0];
				voiceOp.modulators[1] = patchOp.modulators[1];
				voiceOp.modulators[2] = patchOp.modulators[2];

				// Feedback
				voiceOp.iFeedback   = patchOp.feedback;
				voiceOp.feedbackAmt = { patchOp.feedbackAmt, m_sampleRate, kDefParameterLatency, 0.f, 1.f };
				voiceOp.feedback    = 0.f;
				
				// LFO influence
				voiceOp.ampMod   = patchOp.ampMod;
				voiceOp.pitchMod = patchOp.pitchMod;
				voiceOp.panMod   = patchOp.panMod;

				// Panning
				voiceOp.panning = { CalcPanning(patchOp), m_sampleRate, kDefParameterLatency, 0.f, 1.f };

				// Soft clip (distortion)
				voiceOp.softClip = { CalcSoftClip(opVelocity, patchOp), m_sampleRate, kDefParameterLatency, 0.f, 1.f };
			}
		}

		if (true == reset)
		{
			// Reset main filter
			voice.m_filterSVF.resetState();
			
			// Start filter envelope
			voice.m_filterEnvelope.Start(m_patch.filterEnvParams, m_sampleRate, false, 1.f, envAcousticScaling);

			// Start pitch envelope
			voice.m_pitchBendRange = m_patch.pitchBendRange;
			voice.m_pitchEnvelope.Start(m_patch.pitchEnvParams, m_sampleRate);
		}

		// Voice is now playing
		voice.m_state = Voice::kPlaying;
		++m_voiceCount;

		// Store (new) index in key slot
		SFM_ASSERT(-1 == GetVoice(key));
		SetKey(key, 0);


		voice.PostInitialize();
	}

	/* ----------------------------------------------------------------------------------------------------

		Voice logic handling, (to be) called by Render().

	 ------------------------------------------------------------------------------------------------------ */

	// Prepare voices for Render() pass
	void Bison::UpdateVoicesPreRender()
	{
		m_modeSwitch = m_curVoiceMode != m_patch.voiceMode;
		const bool monophonic = Patch::VoiceMode::kMono == m_curVoiceMode;

		/*
			If voice mode changes, first steal all active voices and let a Render() pass run
		*/

		if (true == m_modeSwitch || true == m_resetVoices)
		{
			if (true == m_modeSwitch)
				Log("Voice mode switch (stealing voices)");

			if (true == m_resetVoices)
				Log("Asked to reset all voices");

			// Steal *all* active voices
			for (unsigned iVoice = 0; iVoice < kMaxPolyVoices; ++iVoice)
			{
				Voice &voice = m_voices[iVoice];
				
				if (false == voice.IsIdle() && false == voice.IsStolen())
				{
					StealVoice(iVoice);

					Log("Voice mode switch / Voice reset, stealing voice: " + std::to_string(iVoice));
				}
			}

			m_polyVoiceReq.clear();
			m_polyVoiceReleaseReq.clear();

			// Set voice mode state
			m_curVoiceMode = m_patch.voiceMode;

			// Monophonic?
			if (true == monophonic)
			{
				// Set polyphony
				m_curPolyphony = 1;

				// Clear state
				m_monoSequence.clear();
				m_monoVoiceReq.key = VoiceRequest::kInvalid;
				m_monoVoiceReleaseReq.key = MonoVoiceReleaseRequest::kInvalid;
			}
			
			// Release stolen voices
			return;
		}

		/*
			Handle all release requests (polyphonic)
		*/

		if (false == monophonic)
		{
			/* Polyphonic */

			std::deque<VoiceReleaseRequest> remainder;
		
			for (auto key : m_polyVoiceReleaseReq)
			{
				const int index = GetVoice(key);
			
				// Voice still allocated?
				if (index >= 0)
				{
					Voice &voice = m_voices[index];

					if (false == voice.IsSustained())
					{
						// Voice *not* releasing?
						if (true != voice.IsReleasing())
						{
							// Release voice
							ReleaseVoice(index);
						}
					}
					else
					{
						// Voice is sustained, defer request
						remainder.emplace_back(key);
					}
				}
			}

			m_polyVoiceReleaseReq = remainder;
		}

		/*
			Handle all voice requests (polyphonic)
		*/
		
		if (false == monophonic)
		{
			/* Polyphonic */

			// Sort list by time stamp
			// The front of the deque will be the first (smallest) time stamp; we'll honour requests in that order
			std::sort(m_polyVoiceReq.begin(), m_polyVoiceReq.end(), [](const auto &left, const auto &right) -> bool { return left.timeStamp < right.timeStamp; } );

			// Allocate voices (simple first-fit)
			while (m_polyVoiceReq.size() > 0 && m_voiceCount < m_curPolyphony)
			{
				// Pick first free voice
				for (unsigned iVoice = 0; iVoice < m_curPolyphony; ++iVoice)
				{
					Voice &voice = m_voices[iVoice];

					if (true == voice.IsIdle())
					{
						// Initialize (also pops request)
						InitializeVoice(iVoice);
						break;
					}
				}
			}
			
			// If we still have requests, try to steal (releasing or sustaining) voices in order to 
			// free up slots that can be used to spawn these voices the next frame (no gaurantee though!)

			size_t remainingRequests = m_polyVoiceReq.size();

			if (remainingRequests > 0)
			{
				struct VoiceRef
				{
					unsigned iVoice;
					float summedOutput;
				};

				std::vector<VoiceRef> voiceRefs;

				for (unsigned iVoice = 0; iVoice < m_curPolyphony; ++iVoice)
				{
					Voice &voice = m_voices[iVoice];

					if (false == voice.IsStolen())
					{
						const bool isReleasing = voice.IsReleasing();
						const bool isSustained = voice.IsSustained();
						
						// Only consider releasing or sustained voices
						if (true == isReleasing || true == isSustained)
						{
							VoiceRef voiceRef;
							voiceRef.iVoice = iVoice;
							voiceRef.summedOutput = voice.GetSummedOutput();

							voiceRefs.emplace_back(voiceRef);
						}
					}
				}

				// Sort list from low to high summed output
				std::sort(voiceRefs.begin(), voiceRefs.end(), [](const auto &left, const auto &right ) -> bool { return left.summedOutput < right.summedOutput; } );

				for (auto &voiceRef : voiceRefs)
				{
					// Steal voice
					const unsigned iVoice = voiceRef.iVoice;
					StealVoice(iVoice);
					Log("Voice stolen (index): " + std::to_string(iVoice));
					
					if (--remainingRequests == 0)
						break;
				}

				if (remainingRequests != 0)
				{
					// FIXME: I think it's a viable strategy to drop the remaining requests?
					Log("Could not steal enough voices: " + std::to_string(remainingRequests) + " remaining.");
				}
			}

			// Now first in line to be allocated next frame
			for (auto &request : m_polyVoiceReq)
				request.timeStamp = 0;
		}

		/*
			Handle all requests (monophonic)
		*/

		/* else */ if (true == monophonic)
		{
			/* Monophonic */

			/* const */ auto &voice = m_voices[0];

			bool fromSequence = false;

			// If no NOTE_ON but audible/playing NOTE_OFF, fetch previous note in sequence (if any), or reset the sequence if silent
			if (false == m_monoVoiceReq.MonoIsValid() && true == m_monoVoiceReleaseReq.IsValid())
			{
				float output = 0.f;
				if (false == voice.IsIdle())
					output = voice.GetSummedOutput();

				const bool isSilent = 0.f == output;

				if (false == isSilent)
				{
					if (false == m_monoSequence.empty())
					{
						// Request is last note in sequence (released key removed in NoteOff())
						m_monoVoiceReq = m_monoSequence.front();

						fromSequence = true;

						Log("Monophonic: trigger previous note in sequence: " + std::to_string(m_monoVoiceReq.key));
					}
				}
				else
				{
					// Sequence fell silent
					m_monoSequence.clear();

					Log("Monophonic: sequence fell silent, erased request(s)");
				}
			}

			// Got a valid request?
			if (true == m_monoVoiceReq.MonoIsValid())
			{
				// Releasing at the same time?
				if (true == m_monoVoiceReleaseReq.IsValid())
				{
					if (true == voice.IsPlaying() && false == fromSequence)
					{
						// Release, and by doing so, make a fresh start: why is this important?
						// If timing is so tight it falls within the same Render() pass, it should still approximate the behaviour as closely as possible, and just resorting
						// to glide is, while you could probably defend it in your manual, not the right thing to do and not what a producer would expect
						ReleaseVoice(0);
					}
				}

				// We're always transitioning to another note, which effectively validates the release request
				m_monoVoiceReleaseReq.key = MonoVoiceReleaseRequest::kInvalid;

				// Current key
				const auto key = voice.m_key;

				// Copy and quickly fade if it's releasing
				if (false == voice.IsIdle() && true == voice.IsReleasing())
				{
					m_voices[1] = m_voices[0]; // Copy
					m_voices[1].m_key = -1;    // Unbind
					StealVoice(1);             // Steal
				}
				
				// Free up slot immediately
				if (-1 != key)
					FreeKey(key);

				// Keep number of voices in check
				if (m_voiceCount > 1)
					--m_voiceCount;

				InitializeVoice(0);

				m_monoVoiceReq.key = VoiceRequest::kInvalid;
			}
			else if (true == m_monoVoiceReleaseReq.IsValid())
			{
				const auto key  = m_monoVoiceReleaseReq.key;
				const int index = GetVoice(key);
				
				// We can only ever release the first slot as we merely use the second to steal a voice
				SFM_ASSERT(0 == index);

				if (false == voice.IsSustained())
				{
					if (false == voice.IsReleasing())
					{
						ReleaseVoice(0);
					}

					// Invalidate request, otherwise keep it (and any consecutive one) until the sustain pedal is released
					m_monoVoiceReleaseReq.key = MonoVoiceReleaseRequest::kInvalid;
				}
			}

			// Rationale: second voice may only be used to quickly cut the previous voice
			SFM_ASSERT(true == m_voices[1].IsIdle() || true == m_voices[1].IsStolen());
		}

		/*
			Update real-time voice parameters
		*/

		for (unsigned iVoice = 0; iVoice < m_curPolyphony; ++iVoice)
		{
			Voice &voice = m_voices[iVoice];
			{
				// Active?
				if (false == voice.IsIdle())
				{
					// Playing and not stolen?
					if (false == voice.IsDone() && false == voice.IsStolen())
					{
						// Still bound to a key?
						// FIXME: I could do this for all voices that satisfy the condition above (and just retain the last known amplitude, issue created 16/09/2020)
						if (-1 != voice.m_key)
						{
							// Update active voice:
							// - Each (active) operator has a set of parameters that need per-sample interpolation 
							// - Most of these are updated in this loop
							// - The set of parameters (also outside of this object) isn't conclusive and may vary depending on the use of FM. BISON (currently: VST plug-in)

							for (unsigned iOp = 0; iOp < kNumOperators; ++iOp)
							{
								auto &voiceOp = voice.m_operators[iOp];

								// Update per-sample interpolated parameters
								if (true == voiceOp.enabled)
								{
									const float fundamentalFreq = voice.m_fundamentalFreq;
									const PatchOperators::Operator &patchOp = m_patch.operators.operators[iOp];

									// Get velocity & frequency
									const float opVelocity = (false == patchOp.velocityInvert) ? voice.m_velocity : 1.f-voice.m_velocity;
									const float frequency = CalcOpFreq(fundamentalFreq, voiceOp.detuneOffs, patchOp);

									// Get amplitude & index
									const float level = CalcOpLevel(voice.m_key, opVelocity, patchOp);
									const float amplitude = patchOp.output*level, index = patchOp.index*level;
								
									// Interpolate freq. if necessary
									if (frequency != voiceOp.setFrequency)
									{
										voiceOp.curFreq.SetTarget(frequency);
										voiceOp.setFrequency = frequency;
									}

									// Set amplitude & index
									voiceOp.amplitude.SetTarget(amplitude);
									voiceOp.index.SetTarget(index);

									// Square(pusher) (or "drive")
									const float softClip = CalcSoftClip(opVelocity, patchOp);
									voiceOp.softClip.SetTarget(softClip);

									// Feedback amount
									voiceOp.feedbackAmt.SetTarget(patchOp.feedbackAmt);
					
									// Panning (as set by static parameter)
									voiceOp.panning.SetTarget(CalcPanning(patchOp));

									// Supersaw parameters
									voiceOp.supersawDetune.SetTarget(patchOp.supersawDetune);
									voiceOp.supersawMix.SetTarget(patchOp.supersawMix);
								}
							}
						}
						else
						{
							// Not bound to key: voice must be releasing (though after a recent (13/05/2020) adjustment this should no longer happen in polyphonic mode)
							SFM_ASSERT(true == voice.IsReleasing());
						}
					}
				}
			}
		}
	}

	// Update voices after Render() pass
	void Bison::UpdateVoicesPostRender()
	{
		// Free (stolen) voices
		for (unsigned iVoice = 0; iVoice < kMaxPolyVoices /* Evaluate all! */; ++iVoice)
		{
			Voice &voice = m_voices[iVoice];

			if (false == voice.IsIdle())
			{
				const bool stolenAndCut = true == voice.IsStolen() && 0.f == voice.m_globalAmp.Get();
				if (true == stolenAndCut || true == voice.IsDone())
				{
					FreeVoice(iVoice);

					// Full reset after switch
					if (true == m_modeSwitch)
					{
						voice.Reset(m_sampleRate);
					}
				}
			}
		}

		// Possible mode switch complete
		m_modeSwitch = false;
	}

	// Update sustain state
	void Bison::UpdateSustain()
	{
		if (Patch::kNoPedal == m_patch.sustainType || Patch::kWahPedal == m_patch.sustainType)
			return;

		// Sustain of pitch envelope is taken care of in synth-voice.cpp!

		const bool state = m_sustain;

		const bool monophonic = Patch::VoiceMode::kMono == m_curVoiceMode;

		if (Patch::kSynthPedal == m_patch.sustainType || true == monophonic /* Monophonic *always* uses synthesizer style pedal mode */)
		{
			/*
				Emulate the synthesizer type behaviour (like the DX7) which means that the envelope runs until it hits the sustain phase
				and remains there until the pedal is lifted before it releases the voices; pressure is ignored.
			*/

			if (true == state)
			{
				// Sustain all playing voices (ignore NOTE_OFF)
				for (unsigned iVoice = 0; iVoice < m_curPolyphony; ++iVoice)
				{
					Voice &voice = m_voices[iVoice];
					if (true == voice.IsPlaying() && false == voice.IsSustained())
					{
						voice.m_sustained = true;
						Log("Voice sustained (synth.): " + std::to_string(iVoice));
					}
				}
			}
			else
			{
				// Release all sustained (playing) voices
				for (unsigned iVoice = 0; iVoice < m_curPolyphony; ++iVoice)
				{
					Voice &voice = m_voices[iVoice];
					if (true == voice.IsPlaying() && true == voice.IsSustained())
					{
						voice.m_sustained = false;
						Log("Voice no longer sustained (synth.): " + std::to_string(iVoice));
					}
				}
			}
		}
		else if (Patch::kPianoPedal == m_patch.sustainType)
		{
			/*
				Emulation of piano (or CP) behaviour.
			*/

			const float pedalFalloff    = m_patch.pianoPedalFalloff;
			const float pedalReleaseMul = m_patch.pianoPedalReleaseMul;
			
			if (true == state)
			{
				// Sustain all playing voices
				for (unsigned iVoice = 0; iVoice < m_curPolyphony; ++iVoice)
				{
					Voice &voice = m_voices[iVoice];
					if (true == voice.IsPlaying() && false == voice.IsSustained())
					{
						voice.m_sustained = true;

						// Pitch envelope is taken care of in Voice::Sample()
						voice.m_filterEnvelope.OnPianoSustain(pedalFalloff, pedalReleaseMul);

						for (auto& voiceOp : voice.m_operators)
							if (true == voiceOp.enabled && true == voiceOp.isCarrier)
							{
								voiceOp.envelope.OnPianoSustain(pedalFalloff, pedalReleaseMul);
							}

						Log("Voice sustained (CP): " + std::to_string(iVoice));
					}
				}
			}
			else
			{
				// Release all sustained (playing) voices
				for (unsigned iVoice = 0; iVoice < m_curPolyphony; ++iVoice)
				{
					Voice &voice = m_voices[iVoice];
					if (false == voice.IsIdle() && true == voice.IsSustained())
					{
						voice.m_sustained = false;
						Log("Voice no longer sustained (CP): " + std::to_string(iVoice));
					}
				}
			}
		}
	}

	/* ----------------------------------------------------------------------------------------------------

		Voice render.

		This function was lifted straight out of Render() so it has a few dependencies on class members
		instead of just the context(s). I've made the function constant as a precaution but it's not
		unthinkable that something slips through the cracks. 
		
		If you spot anything fishy please fix it ASAP.

	 ------------------------------------------------------------------------------------------------------ */

	/* static */ void Bison::VoiceRenderThread(Bison *pInst, VoiceThreadContext *pContext)
	{
		SFM_ASSERT(nullptr != pInst);
		SFM_ASSERT(nullptr != pContext);
		pInst->RenderVoices(pContext->parameters, pContext->voiceIndices, pContext->numSamples, pContext->pDestL, pContext->pDestR);
	}

	// Renders a set of voices
	// - Stick to variables supplied through a context *or* make very sure you read only!
	// - Assumes that each voice is active
	void Bison::RenderVoices(const VoiceRenderParameters &context, const std::vector<unsigned> &voiceIndices, unsigned numSamples, float *pDestL, float *pDestR) const
	{
		SFM_ASSERT(nullptr != pDestL && nullptr != pDestR);

		for (auto iVoice : voiceIndices)
		{
			Voice &voice = const_cast<Voice&>(m_voices[iVoice]);
			SFM_ASSERT(false == voice.IsIdle());

			// Update LFO frequencies
			float frequency = m_globalLFO->GetFrequency(), modFrequency;
			CalcLFOFreq(frequency, modFrequency, m_patch.LFOModSpeed);
			
			voice.m_LFO1.SetFrequency(frequency);
			voice.m_LFO2.SetFrequency(frequency);
			voice.m_modLFO.SetFrequency(modFrequency);
			
			// Update LFO S&H parameters
			const float slewRate = m_patch.SandHSlewRate;
			voice.m_LFO1.SetSampleAndHoldSlewRate(slewRate);
			voice.m_LFO2.SetSampleAndHoldSlewRate(slewRate);
			voice.m_modLFO.SetSampleAndHoldSlewRate(slewRate);

			if (true == m_resetPhaseBPM)
			{
				// If resetting BPM sync. phase initiate a fade in
				voice.m_globalAmp.SetRate(m_sampleRate, kGlobalAmpCutTime);
				voice.m_globalAmp.Set(0.f);
				voice.m_globalAmp.SetTarget(kVoiceGain);
			}
	
			if (true == context.resetFilter)
			{
				// Reset
				voice.m_filterSVF.resetState();
			}

			// LFO
			auto curLFOBlend    = m_curLFOBlend;
			auto curLFOModDepth = m_curLFOModDepth;

			// Reset to initial cutoff & Q
			auto curCutoff = m_curCutoff;
			auto curQ      = m_curQ;

			// Reset to initial pitch/amp. bend & modulation
			auto curPitchBend  = m_curPitchBend;
			auto curAmpBend    = m_curAmpBend;
			auto curModulation = m_curModulation;

			// Reset to initial aftertouch
			auto curAftertouch = m_curAftertouch;

			const bool noFilter = SvfLinearTrapOptimised2::NO_FLT_TYPE == context.filterType;
			auto& filterEG      = voice.m_filterEnvelope;
			
			// FIXME: split up in passes, so as to trade memory bandwidth for reduced read cache (misses)?
			for (unsigned iSample = 0; iSample < numSamples; ++iSample)
			{
				const float sampAftertouch = curAftertouch.Sample();

				const float sampMod = std::min<float>(1.f, curModulation.Sample() + context.modulationAftertouch*sampAftertouch);
						
				// Render dry voice
				float left, right;
				voice.Sample(
					left, right, 
					curPitchBend.Sample(),
					curAmpBend.Sample(),
					sampMod,
					curLFOBlend.Sample(), 
					curLFOModDepth.Sample());

				// Sample filter envelope
				float filterEnv = filterEG.Sample();
				if (true == m_patch.filterEnvInvert)
					filterEnv = 1.f-filterEnv;

				// SVF cutoff aftertouch (curved towards zero if pressed)
				const float cutAfter = context.mainFilterAftertouch*sampAftertouch;
				SFM_ASSERT_NORM(cutAfter);
			
#if !defined(SFM_DISABLE_FX)						

				// Apply & mix filter (FIXME: move to sequential loop?)
				if (false == noFilter)
				{	
					float filteredL = left;
					float filteredR = right;
							
					// Cutoff & Q, finally, for *this* sample
					const float nonEnvCutoffHz = curCutoff.Sample()*(1.f - cutAfter*kMainCutoffAftertouchRange); // More pressure -> lower cutoff freq.
					const float cutoffHz = lerpf<float>(context.fullCutoff, nonEnvCutoffHz, filterEnv);
					const float sampQ = curQ.Sample();

					// Ref.: https://github.com/FredAntonCorvest/Common-DSP/blob/master/Filter/SvfLinearTrapOptimised2Demo.cpp
					voice.m_filterSVF.updateCoefficients(cutoffHz, sampQ, context.filterType, m_sampleRate);
					voice.m_filterSVF.tick(filteredL, filteredR);
							
					left  = filteredL;
					right = filteredR;
				}

#endif

				// Add to mix
				pDestL[iSample] += left;
				pDestR[iSample] += right;
			}
		}
	}

	/* ----------------------------------------------------------------------------------------------------

		Block renderer; basically takes care of all there is to it in the right order.
		Currently tailored to play nice with (JUCE) VST.

	 ------------------------------------------------------------------------------------------------------ */
	
	void Bison::Render(unsigned numSamples, float bendWheel, float modulation, float aftertouch, float *pLeft, float *pRight)
	{
		SFM_ASSERT_BINORM(bendWheel); 
		SFM_ASSERT_NORM(modulation);
		SFM_ASSERT_NORM(aftertouch); 
		
		SFM_ASSERT(nullptr != pLeft && nullptr != pRight);
		SFM_ASSERT(nullptr != m_pBufL[0] && nullptr != m_pBufR[0]);
		SFM_ASSERT(nullptr != m_pBufL[1] && nullptr != m_pBufR[1]);

		if (numSamples > m_samplesPerBlock)
		{
			// According to JUCE's documentation this *can* occur, but I don't think it ever does (nor should)
			SFM_ASSERT(false);
			return;
		}

#if SFM_KILL_DENORMALS
		// Disable denormals
		DisableDenormals disableDEN;
#endif

		const bool monophonic = Patch::VoiceMode::kMono == m_curVoiceMode;

		// Reset voices if polyphony changes
		const unsigned maxVoices = (false == monophonic) ? m_patch.maxPolyVoices : 1;
		if (m_curPolyphony != maxVoices)
		{
			m_resetVoices = true;
			m_curPolyphony = maxVoices;
		}

		// Modulation override?
		if (0.f != m_patch.modulationOverride)
		{
			SFM_ASSERT(m_patch.modulationOverride > 0.f && m_patch.modulationOverride <= 1.f);
			modulation = m_patch.modulationOverride;
		}

		// Calculate current BPM freq.
		unsigned overrideDelayBit = 0;
		if (true == m_patch.beatSync && 0.f != m_BPM)
		{
			const float ratio = m_patch.beatSyncRatio; // Note ratio
			SFM_ASSERT(ratio >= 0);

			const float BPM = m_BPM;
			const float BPS = BPM/60.f;    // Beats per sec.
			m_freqBPM = BPS/ratio;         // Sync. freq.
			
			// If can't fit delay within it's line, revert to manual setting
			if (1.f/m_freqBPM >= kMainDelayInSec)
				overrideDelayBit = kFlagOverrideDelay;
		}
		else
			// None: interpret this as a cue to use user controlled rate(s)
			m_freqBPM = 0.f;

		// Calculate LFO freq.
		float freqLFO = 0.f;

		const bool overrideLFO = m_patch.syncOverride & kFlagOverrideLFO;
		if (false == m_patch.beatSync || m_freqBPM == 0.f || true == overrideLFO)
		{
			// Set LFO speed in (DX7) range
			freqLFO = MIDI_To_DX7_LFO_Hz(m_patch.LFORate);
			m_globalLFO->SetFrequency(freqLFO); // FIXME: LPF?
		}
		else
		{
			// Adapt BPM freq.
			freqLFO = m_freqBPM;

			if (false == m_resetPhaseBPM)
			{
				m_globalLFO->SetFrequency(freqLFO); // FIXME: LPF?
			}
			else
			{
				// Full reset; likely to be used when (re)starting a track
				// This *must* be done prior to UpdateVoicesPreRender()
				m_globalLFO->Initialize(freqLFO, m_sampleRate);

				// FIXME: this is where one would reinitialize possible interpolation of LFO rate (removed along with ParameterSlew @ 1/11/2021)
			}
		}
		
		// Set (interpolated) LFO parameters
		m_curLFOBlend.SetTarget(m_patch.LFOBlend);
		m_curLFOModDepth.SetTarget(m_patch.LFOModDepth);

		// Update voice logic (PRE)
		UpdateVoicesPreRender();

		// Update filter type & state
		//

		SvfLinearTrapOptimised2::FLT_TYPE filterType;
		
		// Set target cutoff (Hz) & Q
		const float normCutoff = m_patch.cutoff;
		const float resonance = m_patch.resonance;

		// Using smoothstepf() to add a little curvature, chiefly intended to appease basic MIDI controls
		const float cutoff = SVF_CutoffToHz(smoothstepf(normCutoff), m_Nyquist);
		m_curCutoff.SetTarget(cutoff);
		
		// Set filter type & parameters
		float normQ = resonance;

		const float fullCutoff = SVF_CutoffToHz(1.f, m_Nyquist); // Full cutoff used to apply DCF

		SFM_ASSERT_NORM(normCutoff);
		SFM_ASSERT_NORM(normQ);

		float Q;
		switch (m_patch.filterType)
		{
		default:
		case Patch::kNoFilter:
			filterType = SvfLinearTrapOptimised2::NO_FLT_TYPE;
			Q = SVF_ResoToQ(normQ*m_patch.resonanceLimit);
			break;

		case Patch::kLowpassFilter:
			// Screams and yells
			filterType = SvfLinearTrapOptimised2::LOW_PASS_FILTER;
			Q = SVF_ResoToQ(normQ*m_patch.resonanceLimit);
			break;

		case Patch::kHighpassFilter:
			filterType = SvfLinearTrapOptimised2::HIGH_PASS_FILTER;
			Q = SVF_ResoToQ(normQ*m_patch.resonanceLimit);
			break;

		case Patch::kBandpassFilter:
			filterType = SvfLinearTrapOptimised2::BAND_PASS_FILTER;
			Q = SVF_ResoToQ(0.25f*normQ); // Lifts
			break;

		case Patch::kNotchFilter:
			filterType = SvfLinearTrapOptimised2::NOTCH_FILTER;
			Q = SVF_ResoToQ(0.25f - 0.25f*normQ); // Dents
			break;
		}
		
		// Set correct Q
		SFM_ASSERT(Q >= kSVFMinFilterQ);
		m_curQ.SetTarget(Q);

		// Switched filter type?
		const bool resetFilter = m_curFilterType != filterType;
		m_curFilterType = filterType;

		// Set pitch & amp. wheel & modulation target values
		const float bendWheelFiltered = bendWheel;
		if (false == m_patch.pitchIsAmpMod)
		{
			// Wheel modulates pitch
			m_curPitchBend.SetTarget(bendWheelFiltered);
			m_curAmpBend.SetTarget(1.f /* 0dB */);
		}
		else
		{
			// Wheel modulates amplitude
			m_curAmpBend.SetTarget(dB2Lin(bendWheelFiltered*kAmpBendRange));
			m_curPitchBend.SetTarget(0.f);
		}

		// Set modulation & aftertouch target values
		m_curModulation.SetTarget(modulation);

		const float aftertouchFiltered = aftertouch; // FIXME: LPF?
		m_curAftertouch.SetTarget(aftertouchFiltered);

		// Clear L/R buffers
		memset(m_pBufL[0], 0, m_samplesPerBlock*sizeof(float));
		memset(m_pBufR[0], 0, m_samplesPerBlock*sizeof(float));

		// Start rendering voices, if necessary
		const unsigned numVoices = m_voiceCount;

		if (0 != numVoices)
		{
			// Swap 2 branches for multiplications later on
			const float mainFilterAftertouch = (Patch::kMainFilter == m_patch.aftertouchMod) ? 1.f : 0.f;
			const float modulationAftertouch = (Patch::kModulation == m_patch.aftertouchMod) ? 1.f : 0.f;

			VoiceRenderParameters parameters;
			parameters.freqLFO = freqLFO;
			parameters.filterType = filterType;
			parameters.resetFilter = resetFilter;
			parameters.fullCutoff = fullCutoff;
			parameters.modulationAftertouch = modulationAftertouch;
			parameters.mainFilterAftertouch = mainFilterAftertouch;

			// Build array of voices to render
			std::vector<unsigned> voiceIndices;
			for (int iVoice = 0; iVoice < kMaxPolyVoices /* Actual voice count can be > m_curPolyphony */; ++iVoice)
			{
				if (false == m_voices[iVoice].IsIdle())
					voiceIndices.push_back(iVoice);
			}

#if defined(SFM_DISABLE_VOICE_THREAD)
			if (true)
#else
			if (voiceIndices.size() <= kSingleThreadMaxVoices || numSamples < kMultiThreadMinSamples)
#endif
			{
				// Render all voices on current thread
				VoiceThreadContext context(parameters);
				context.voiceIndices = std::move(voiceIndices);
				context.numSamples = numSamples;
				context.pDestL = m_pBufL[0];
				context.pDestR = m_pBufR[0];
			
				VoiceRenderThread(this, &context);
			}
			else
			{
				// Clear L/R buffers
				memset(m_pBufL[1], 0, m_samplesPerBlock*sizeof(float));
				memset(m_pBufR[1], 0, m_samplesPerBlock*sizeof(float));
				
				// Use 2 (current plus one extra) threads to render voices
				VoiceThreadContext contexts[2] = { parameters, parameters };
				
				// Split voices up 50/50
				const size_t half = voiceIndices.size() / 2;
				contexts[0].voiceIndices = std::vector<unsigned>(voiceIndices.begin(), voiceIndices.begin() + half);
				contexts[1].voiceIndices = std::vector<unsigned>(voiceIndices.begin() + half, voiceIndices.end());

				contexts[0].numSamples = contexts[1].numSamples = numSamples;

				contexts[0].pDestL = m_pBufL[0];
				contexts[0].pDestR = m_pBufR[0];
				contexts[1].pDestL = m_pBufL[1];
				contexts[1].pDestR = m_pBufR[1];

				std::thread thread(VoiceRenderThread, this, &contexts[1]);

				VoiceRenderThread(this, &contexts[0]); // Process our part
				thread.join();                         // Wait for thread

				// Mix samples (FIXME: could move to PostPass but if all is well we've already won at least *some* CPU if necessary)
				for (unsigned iSample = 0; iSample < numSamples; ++iSample)
				{
					m_pBufL[0][iSample] += m_pBufL[1][iSample];
					m_pBufR[0][iSample] += m_pBufR[1][iSample];
				}
			}
		}

		// Keep *all* supersaw oscillators running; I could move this loop to RenderVoices(), but that would clutter up the function a bit,
		// and here it's easy to follow and easy to extend
		// FIXME: review this (see Github issue: https://github.com/bipolaraudio/FM-BISON/issues/235)

//		const bool monophonic = Patch::VoiceMode::kMono == m_patch.voiceMode;
		for (auto &voice : m_voices)
		{	
			const bool isIdle = voice.IsIdle() && !monophonic;

			for (auto &voiceOp : voice.m_operators)
			{
				// Only update if *not* in use
				if (true == isIdle || false == voiceOp.enabled)
				{
					auto &saw = voiceOp.oscillator.GetSupersaw();
					saw.Skip(numSamples);
				}
			}
		}

		// Advance global LFO phase (free running)
		m_globalLFO->Skip(numSamples);
				
		// Update voice logic (post)
		UpdateVoicesPostRender();

		// Update sustain state
		UpdateSustain();

		// Calc. post filter wetness
		float postWet = m_patch.postWet;
		if (Patch::kPostFilter == m_patch.aftertouchMod)
			postWet = std::min<float>(1.f, postWet+aftertouchFiltered); // More pressure -> more wetness

		// Apply post-processing (FIXME: pass structure?)
		m_postPass->Apply(numSamples,
			/* BPM sync. */
			m_freqBPM, m_patch.syncOverride | overrideDelayBit,
			/* Auto-wah (FIXME: use more ParameterSlew if necessary) */
			m_patch.wahResonance,
			m_patch.wahAttack,
			m_patch.wahHold,
			m_patch.wahRate,
			m_patch.wahDrivedB,
			m_patch.wahSpeak,
			m_patch.wahSpeakVowel,
			m_patch.wahSpeakVowelMod,
			m_patch.wahSpeakGhost,
			m_patch.wahSpeakCut,
			m_patch.wahSpeakResonance,
			m_patch.wahCut,
			m_patch.wahWet * ( (Patch::kWahPedal == m_patch.sustainType) ? m_sustain : 1.f ), // FIXME: this ain't great, will probably be noisy without some sort of LPF
			/* Chorus/Phaser */
			m_patch.cpRate,
			m_patch.cpWet,
			false == m_patch.cpIsPhaser,
			/* Delay */
			m_patch.delayInSec,
			m_patch.delayWet,
			m_patch.delayDrivedB,
			m_patch.delayFeedback,
			m_patch.delayFeedbackCutoff,
			m_patch.delayTapeWow,
			/* MOOG-style 24dB filter + Tube distort */
			m_patch.postCutoff,
			m_patch.postResonance,
			m_patch.postDrivedB,
			postWet,
			m_patch.tubeDistort,
			m_patch.tubeDrive,
			m_patch.tubeOffset,
			m_patch.tubeTone,
			m_patch.tubeToneReso,
			/* Reverb */
			m_patch.reverbWet,
			m_patch.reverbRoomSize,
			m_patch.reverbDampening,
			m_patch.reverbWidth,
			m_patch.reverbBassTuningdB,
			m_patch.reverbTrebleTuningdB,
			m_patch.reverbPreDelay,
			/* Compressor */
			m_patch.compThresholddB,
			m_patch.compKneedB,
			m_patch.compRatio,
			m_patch.compGaindB,
			m_patch.compAttack,
			m_patch.compRelease,
			m_patch.compLookahead,
			m_patch.compAutoGain,
			m_patch.compRMSToPeak,
			/* Tuning (post-EQ) */
			m_patch.bassTuningdB,
			m_patch.trebleTuningdB,
			m_patch.midTuningdB,
			/* Master volume */
			m_patch.masterVoldB,
			/* Buffers */
			m_pBufL[0], m_pBufR[0], pLeft, pRight);

		// Of all these, copies were used per voice, so skip numSamples to keep up	
		m_curLFOBlend.Skip(numSamples);
		m_curLFOModDepth.Skip(numSamples);
		m_curCutoff.Skip(numSamples);
		m_curQ.Skip(numSamples);
		m_curPitchBend.Skip(numSamples);
		m_curAmpBend.Skip(numSamples);
		m_curModulation.Skip(numSamples);
		m_curAftertouch.Skip(numSamples);

		// This has been done by now
		m_resetVoices   = false;
		m_resetPhaseBPM = false;

		//
		// Primitive visualization aid(s)
		//

		// Calculate peak ([0..1]) for each operator
		for (float &peak : m_opPeaks)
			peak = 0.f;

		if (numVoices > 0)
		{
			for (unsigned iVoice = 0; iVoice < m_curPolyphony; ++iVoice)
			{
				Voice &voice = m_voices[iVoice];

				if (false == voice.IsIdle())
				{
					for (unsigned iOp = 0; iOp < kNumOperators; ++iOp)
					{
						Voice::Operator &voiceOp = voice.m_operators[iOp];

						if (true == voiceOp.enabled)
						{
							const float curGain = voiceOp.envGain.Get();
							
							// New maximum?
							if (curGain > m_opPeaks[iOp])
								m_opPeaks[iOp] = curGain;
						}
					}
				}
			}
		}
	}

}; // namespace SFM

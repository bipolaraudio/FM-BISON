
/*
	FM. BISON hybrid FM synthesis -- Interpolated (linear or multiplicative) parameter.
	(C) visualizers.nl & bipolaraudio.nl
	MIT license applies, please see https://en.wikipedia.org/wiki/MIT_License or LICENSE in the project root!

	This object is used to interpolate parameters that need per-sample interpolation in the time domain so that it 
	will always reproduce the same effect regardless of the number of samples processed per block or the sample rate.
	Alternatively a fixed number of samples can be set.

	Do *always* call Set() and SetTarget() after calling SetRate() during interpolation to restore the current value
	and set the new target. This is a small design flaw in juce::SmoothedValue I feel.
	
	So what you do is:

	'
		const float curValue = interpolator.Get();
		interpolator.SetRate(sampleRate, timeInSec);
		interpolator.Set(curValue);
		interpolator.SetTarget(targetValue);
	'

	I'm not fixing this as long was use juce::SmoothedValue.
	
	FIXME: 
		- Replace JUCE implementation (also in other places where juce::SmoothedValue is used)
		- I'm not entirely happy with the latter 2 constructors, the top one becomes ambiguous when I set the last
		  parameter to a default value (kDefParameterLatency), which is the case 99% of the time
		- I've had it interpolating past the target, which in turn resulted in invalid parameters; maybe that's fixed in JUCE 6?
*/

#pragma once

// Include JUCE (for juce::SmoothedValue)
#include "../FM-BISON-internal-plug-in/JuceLibraryCode/JuceHeader.h"

#include "synth-global.h"

namespace SFM
{
	typedef ValueSmoothingTypes::Linear kLinInterpolate;
	typedef ValueSmoothingTypes::Multiplicative kMulInterpolate;

	template <typename T> class InterpolatedParameter
	{
	public:
		// Default: zero
		// If you comment this constructor it's easier to spot forgotten initializations
		InterpolatedParameter()
		{
			SetRate(0);
			Set(0.f);
		}

		// Initialize at value and initialize rate & time
		InterpolatedParameter(float value, unsigned sampleRate, float timeInSec) :
			m_value(value)
		{
			SFM_ASSERT(timeInSec >= 0.f);
			SetRate(sampleRate, timeInSec);
			Set(value);
		}

		// Initialize at value and initialize rate & time
		InterpolatedParameter(float value, unsigned numSamples) :
			m_value(value)
		{
			SFM_ASSERT(numSamples > 0);
			SetRate(numSamples);
			Set(value);
		}

		SFM_INLINE float Sample()
		{
			const float result = m_value.getNextValue();
//			const float target = m_value.getTargetValue();

			return result;
		}

		SFM_INLINE float Get() const
		{
			return m_value.getCurrentValue();
		}

		// Set current & target
		SFM_INLINE void Set(float value)
		{
			m_value.setCurrentAndTargetValue(value);
		}

		// Set target
		SFM_INLINE void SetTarget(float value)
		{
			m_value.setTargetValue(value);
		}
		
		// Get target
		SFM_INLINE float GetTarget() const
		{
			return m_value.getTargetValue();
		}

		// Skip over N samples towards target value
		SFM_INLINE void Skip(unsigned numSamples)
		{
			m_value.skip(numSamples);
		}

		// Set rate in seconds
		SFM_INLINE void SetRate(unsigned sampleRate, float time)
		{
			m_value.reset(double(sampleRate), double(time));
		}
		
		// Set rate in samples
		SFM_INLINE void SetRate(unsigned numSamples)
		{
			m_value.reset(numSamples);
		}

		// Is no longer interpolating
		SFM_INLINE bool IsDone()
		{
			return false == m_value.isSmoothing();
		}

	private:
		juce::SmoothedValue<float, T> m_value;
	};
};

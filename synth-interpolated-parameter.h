
/*
	FM. BISON hybrid FM synthesis -- Interpolated (linear or multiplicative) parameter.
	(C) njdewit technologies (visualizers.nl) & bipolaraudio.nl
	MIT license applies, please see https://en.wikipedia.org/wiki/MIT_License or LICENSE in the project root!

	This object is used to interpolate parameters that need per-sample interpolation in the time domain so that it 
	will always reproduce the same effect regardless of the number of samples processed per block or the sample rate.
	Alternatively a fixed number of samples can be set.

	When using kMulInterpolate the target value may never be zero!

	Do *always* call Set() and SetTarget() after calling SetRate() during interpolation to restore the current value
	and set the new target. This is a small design flaw in juce::SmoothedValue if you ask me.

	Like so:
		const float curValue = interpolator.Get();
		interpolator.SetRate(sampleRate, timeInSec);
		interpolator.Set(curValue);
		interpolator.SetTarget(targetValue);

	I'm not fixing this as long as we use juce::SmoothedValue.

	IMPORTANT: use the clamp feature for values that should *not* go out of range; if a small under- or overshoot is
	           no problem, please set it to false and save yourself a few branches
 
 	06/04/2022: Adapt Alara version?
	
	FIXME: 
	- Replace JUCE implementation (also in other places where juce::SmoothedValue is used)
	- Model own implementation after this one
*/

#pragma once

// Include JUCE (for juce::SmoothedValue)
#include <JuceHeader.h>

#include "synth-global.h"

namespace SFM
{
	typedef juce::ValueSmoothingTypes::Linear kLinInterpolate;
	typedef juce::ValueSmoothingTypes::Multiplicative kMulInterpolate; // Target value may *never* be zero!

	template <typename T, bool clamp>
	class InterpolatedParameter
	{
	public:
		// Default: zero
		// If you comment this constructor it's easier to spot forgotten initializations
		// FIXME: as of 04/2022, fixing the minimum and maximum for compiler compliance, I've seen the above happening more than a few times!
		InterpolatedParameter() :
			m_minimum(0.f), m_maximum(1.f)
		{
			SetRate(0);
			Set(0.f);
		}

		// Initialize at value and initialize rate & time
		InterpolatedParameter(float value, unsigned sampleRate, float timeInSec, float minimum, float maximum) :
			m_minimum(minimum), m_maximum(maximum),
			m_value(value)
		{
			SFM_ASSERT(timeInSec >= 0.f);
			SetRate(sampleRate, timeInSec);
			Set(value);
		}

		// Initialize at value and initialize rate & time
		InterpolatedParameter(float value, unsigned numSamples, float minimum, float maximum) :
			m_minimum(minimum), m_maximum(maximum),
			m_value(value)
		{
			SFM_ASSERT(numSamples > 0);
			SetRate(numSamples);
			Set(value);
		}
		
		SFM_INLINE float Sample()
		{
			const float result = m_value.getNextValue();
			return (clamp) ? jlimit<float>(m_minimum, m_maximum, result) : result;
		}

		SFM_INLINE float Get() const
		{
			const float result = m_value.getCurrentValue();
			return (clamp) ? jlimit<float>(m_minimum, m_maximum, result) : result;		}

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
			return (float) m_value.getTargetValue();
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
		// As of 2022 I still can't use floating point template arguments; this sucks because now we'll have to keep 2 constants
		// The 'need to clamp' branch however should be eliminated at compile time, so it's not that bad
		float m_minimum;
		float m_maximum;
		
		juce::SmoothedValue<float, T> m_value;
	};
};

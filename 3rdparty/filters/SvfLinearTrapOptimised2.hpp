
// ----------------------------------------------------------------------------------------------------
//
// Modified for FM. BISON
//
// - No external dependencies
// - Stereo support (monaural remains, see tickMono())
// - Added specific setup functions
// - Added getFilterType()
// - Ported to single precision (comments not modified)
// 
// - Stable Q range of [0.025..40] is gauranteed, but for stability using the default Q of 0.5
//   seems to be the safe bet, certainly without oversampling; I've had situations where this
//   filter ran continously for a longer time and eventually blew up
//
// - Using cheaper trig. approximation functions may seem like a good idea but we're on thin
//   ice as it is
// 
// - I've refactored this for work (FD), maybe a I can adapt that version at some point!
//
// I can't say I'm a big fan of how most DSP code is written but I'll try to keep it as-is.
//
// ----------------------------------------------------------------------------------------------------

//  SvfLinearTrapOptimised2.hpp
//
//  Created by Fred Anton Corvest (FAC) on 26/11/2016.
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in all
//  copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//  SOFTWARE.

#ifndef SvfLinearTrapOptimised2_hpp
#define SvfLinearTrapOptimised2_hpp

#define _USE_MATH_DEFINES // for M_PI
#include <math.h>         //

#include "../../synth-global.h" // for SFM_INLINE

/*!
 @class SvfLinearTrapOptimised2
 @brief A ready to use C++ port of Andy Simper's implementation of State Variable Filters described in the technical paper 
 provided at http://www.cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf Thank you Andy for the time you spent on making those technical papers.
 */
class SvfLinearTrapOptimised2 {
public:
	/*!
	 @class SvfLinearTrapOptimised2
	 @enum	FILTER_TYPE
	 @brief The different states of the filter.
	 */
	enum FLT_TYPE {LOW_PASS_FILTER, BAND_PASS_FILTER, HIGH_PASS_FILTER, NOTCH_FILTER, PEAK_FILTER, ALL_PASS_FILTER, BELL_FILTER, LOW_SHELF_FILTER, HIGH_SHELF_FILTER, NO_FLT_TYPE};
	
	SvfLinearTrapOptimised2() {
		_ic1eq_left = _ic2eq_left = _v1_left = _v2_left = _v3_left = 0.f;
		_ic1eq_right = _ic2eq_right = _v1_right = _v2_right = _v3_right = 0.f;
	}
	
	/*!
	 @class SvfLinearTrapOptimised2
	 @param gainDb
		Gain in dB to boost or cut the cutoff point of the shelf & bell filters
	 */
	void setGain(float gainDb) {
		_coef.setGain(gainDb);
	}
	
	/*!
	 @class FacAbstractFilter
	 @brief Updates the coefficients of the filter for the given cutoff, q, type and sample rate
	 @param cutoff
		cutoff frequency in Hz that should be clamped into the range [16hz, NYQUIST].
	 @param Q
		Q factor that should be clamped into the range [0.025, 40.0]. Default value is 0.5
	 @param sampleRate
		Sample rate. Default value is 44100hz. Do not forget to call resetState before changing the sample rate
	 */
	SFM_INLINE void updateCoefficients(float cutoff, float q = 0.5f, FLT_TYPE type = LOW_PASS_FILTER, unsigned sampleRate = 44100) {
		_coef.update(cutoff, q, type, sampleRate);
	}

	// A few shorthand update functions
	SFM_INLINE void updateAllpassCoeff(float cutoff, float q, unsigned sampleRate) {
		_coef.updateAllpass(cutoff, q, sampleRate);
	}

	SFM_INLINE void updateLowpassCoeff(float cutoff, float q, unsigned sampleRate) {
		_coef.updateLowpass(cutoff, q, sampleRate);
	}

	SFM_INLINE void updateHighpassCoeff(float cutoff, float q, unsigned sampleRate) {
		_coef.updateHighpass(cutoff, q, sampleRate);
	}

	SFM_INLINE void updateNone() {
		_coef.updateNone();
	}
	
	// This copies *only* the coefficients of the specified filter, use at your own risk
	SFM_INLINE void updateCopy(const SvfLinearTrapOptimised2 &filter)
	{
		_coef = filter._coef;
	}
	
	/*!
	 @class FacAbstractFilter
	 @brief Resets the state of the filter
			Do not forget to call resetState before changing the sample rate
	 */
	void resetState() {
		_ic1eq_left = _ic2eq_left = _ic1eq_right = _ic2eq_right = 0.;
	}

private:
	/*!
	 @class FacAbstractFilter
	 @brief Tick method (apply the filter on the provided sample & state)
	 */
	SFM_INLINE float tickImpl(float v0, float &_v1, float &_v2, float &_v3, float &_ic1eq, float &_ic2eq) {
		_v3 = v0 - _ic2eq;
		_v1 = _coef._a1*_ic1eq + _coef._a2*_v3;
		_v2 = _ic2eq + _coef._a2*_ic1eq + _coef._a3*_v3;
		_ic1eq = 2.f*_v1 - _ic1eq;
		_ic2eq = 2.f*_v2 - _ic2eq;
		
		return _coef._m0*v0 + _coef._m1*_v1 + _coef._m2*_v2;
	}

public:    
	/*!
	 @class FacAbstractFilter
	 @brief Tick method (apply the filter on the provided stereo material)
	 */
	SFM_INLINE void tick(float &left, float &right) {
		const float v0_left  = tickImpl(left,  _v1_left,  _v2_left,  _v3_left,  _ic1eq_left,  _ic2eq_left);
		const float v0_right = tickImpl(right, _v1_right, _v2_right, _v3_right, _ic1eq_right, _ic2eq_right);
		left  = v0_left;
		right = v0_right;
	}
	
	// Do *not* mix with stereo tick() call without first calling resetState()
	SFM_INLINE void tickMono(float &sample) {
		const float filtered = tickImpl(sample, _v1_left, _v2_left, _v3_left, _ic1eq_left, _ic2eq_left);
		sample = filtered;
	}

	// Returns (latest) filter type
	SFM_INLINE FLT_TYPE getFilterType() const
	{
		return _coef._type;
	}

private:
	struct Coefficients {
		Coefficients() {
			_a1 = _a2 = _a3 = _m0 = _m1 = _m2 = 0;
			_A = _ASqrt = 1;
		}

		SFM_INLINE void updateAllpass(float cutoff, float q, unsigned sampleRate)
		{
			const float g = tanf((cutoff / sampleRate) * SFM::kPI);
			const float k = computeK(q, false);

			computeA(g, k);
			_m0 = 1;
			_m1 = -2*k;
			_m2 = 0;

			_type = SvfLinearTrapOptimised2::ALL_PASS_FILTER;
		}

		SFM_INLINE void updateLowpass(float cutoff, float q, unsigned sampleRate)
		{
			float g = tanf((cutoff / sampleRate) * SFM::kPI);
			const float k = computeK(q, false);

			computeA(g, k);
			_m0 = 0;
			_m1 = 0;
			_m2 = 1;

			_type = SvfLinearTrapOptimised2::LOW_PASS_FILTER;
		}

		SFM_INLINE void updateHighpass(float cutoff, float q, unsigned sampleRate)
		{
			const float g = tan((cutoff / sampleRate) * SFM::kPI);
			const float k = computeK(q, false);

			computeA(g, k);
			_m0 = 1;
			_m1 = -k;
			_m2 = -1;

			_type = SvfLinearTrapOptimised2::HIGH_PASS_FILTER;
		}

		SFM_INLINE void updateNone()
		{
			_type = SvfLinearTrapOptimised2::NO_FLT_TYPE;
		}
		
		SFM_INLINE void update(float cutoff, float q = 0.5f, SvfLinearTrapOptimised2::FLT_TYPE type = LOW_PASS_FILTER, unsigned sampleRate = 44100) {
			if (type != NO_FLT_TYPE)
			{
				float g = tan((cutoff / sampleRate) * SFM::kPI);
				const float k = computeK(q, type == BELL_FILTER /* Use gain for bell (peak) filter only */);
			
				switch (type) {
					case LOW_PASS_FILTER:
						computeA(g, k);
						_m0 = 0;
						_m1 = 0;
						_m2 = 1;
						break;
					case BAND_PASS_FILTER:
						computeA(g, k);
						_m0 = 0;
						_m1 = 1;
						_m2 = 0;
						break;
					case HIGH_PASS_FILTER:
						computeA(g, k);
						_m0 = 1;
						_m1 = -k;
						_m2 = -1;
						break;
					case NOTCH_FILTER:
						computeA(g, k);
						_m0 = 1;
						_m1 = -k;
						_m2 = 0;
						break;
					case PEAK_FILTER:
						computeA(g, k);
						_m0 = 1;
						_m1 = -k;
						_m2 = -2;
						break;
					case ALL_PASS_FILTER:
						computeA(g, k);
						_m0 = 1;
						_m1 = -2*k;
						_m2 = 0;
						break;
					case BELL_FILTER:
						computeA(g, k);
						_m0 = 1;
						_m1 = k*(_A*_A - 1);
						_m2 = 0;
						break;
					case LOW_SHELF_FILTER:
						computeA(g /= _ASqrt, k);
						_m0 = 1;
						_m1 = k*(_A-1);
						_m2 = _A*_A - 1;
						break;
					case HIGH_SHELF_FILTER:
						computeA(g *= _ASqrt, k);
						_m0 = _A*_A;
						_m1 = k*(1-_A)*_A;
						_m2 = 1-_A*_A;
						break;

					case NO_FLT_TYPE:
					default:
						SFM_ASSERT(false);
				}
			}

			_type = type;
		}
		
		void setGain(float gainDb) {
			_A = powf(10.f, gainDb / 40.f);
			_ASqrt = sqrtf(_A);
		}
		
		SFM_INLINE float computeK(float q, bool useGain=false) {
			return 1.f / (useGain ? (q*_A) : q);
		}
		
		SFM_INLINE void computeA(float g, float k) {
			_a1 = 1/(1 + g*(g + k));
			_a2 = g*_a1;
			_a3 = g*_a2;
		}
		
		float _a1, _a2, _a3;
		float _m0, _m1, _m2;
		
		float _A;
		float _ASqrt;
		
		FLT_TYPE _type = NO_FLT_TYPE;
	} _coef;
	
	float _ic1eq_left;
	float _ic2eq_left;
	float _v1_left, _v2_left, _v3_left;

	float _ic1eq_right;
	float _ic2eq_right;
	float _v1_right, _v2_right, _v3_right;
};

#endif /* SvfLinearTrapOptimised2_hpp */

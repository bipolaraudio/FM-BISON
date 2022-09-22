
// ----------------------------------------------------------------------------------------------------
//
// Modified for FM. BISON
//
// This filter is the basic building block of filters; it does *not* respond well to rapid changes
// of the frequency; this filter is rather versatile (well suited for an EQ for example) and a
// building block for other (higher order) filters
//
// - No external dependencies
// - Added reset() function
// - Ported to single precision (32-bit floating point is sufficient for audio range)
// - Added stereo support
// - Removed useless functions (like setQ() et cetera)
// - Added 'none' type (user's responsibility to *not* call process() or processMono())
// - Minor optimizations
// - Bug fix: swapped shelf filter coeff. calculations
//
// IMPORTANT:
// - Do not mix process() and processMono()
// - processMono() returns filtered sample instead of writing to ref.
// - Certain filters are relatively expensive to 'set', see setBiquad() impl.
//
// - Useful (graphical) design tool @ https://www.earlevel.com/main/2013/10/13/biquad-calculator-v3/
// - Like above, but to "design" coefficients @ https://www.earlevel.com/main/2013/10/28/pole-zero-placement-v2/
// - As for Q, 0.707 is a nice default value, and [0.01..20.0] a decent range (just don't feed it zero!)
//
// I can't say I'm a big fan of how most DSP code is written but I'll try to keep it as-is.
//
// ----------------------------------------------------------------------------------------------------

#include "../../synth-global.h" // for SFM_INLINE, SFM_ASSERT, SFM::kPI

//
//  Biquad.h
//
//  Created by Nigel Redmon on 11/24/12
//  EarLevel Engineering: earlevel.com
//  Copyright 2012 Nigel Redmon
//
//  For a complete explanation of the Biquad code:
//  http://www.earlevel.com/main/2012/11/26/biquad-c-source-code/
//
//  License:
//
//  This source code is provided as is, without warranty.
//  You may copy and distribute verbatim copies of this document.
//  You may modify and use this source code to create binary code
//  for your own purposes, free or commercial.
//

#ifndef Biquad_h
#define Biquad_h

#define _USE_MATH_DEFINES
#include <math.h>

enum {
	bq_type_none = 0,
	bq_type_lowpass,
	bq_type_highpass,
	bq_type_bandpass,
	bq_type_notch,
	bq_type_peak,
	bq_type_lowshelf,
	bq_type_highshelf
};

class Biquad {
public:
	Biquad()
	{
		reset();
	}

	Biquad(int type, float Fc, float Q, float peakGainDB)
	{
		setBiquad(type, Fc, Q, peakGainDB);
	}

	~Biquad() {}

	void reset();
	SFM_INLINE void setBiquad(int type, float Fc, float Q, float peakGaindB);

	SFM_INLINE void process(float &sampleL, float &sampleR);
	SFM_INLINE void processMono(float &sample);

	SFM_INLINE int getType() const
	{
		return m_type;
	}

protected:
	void calcBiquad(void);

	int m_type;
	float m_Fc, m_Q, m_peakGain, m_FcK, m_peakGainV;

	float a0, a1, a2, b1, b2;
	float z1l, z2l, z1r, z2r;
};

SFM_INLINE void Biquad::process(float &sampleL, float &sampleR) { 

	SFM_ASSERT(m_type != bq_type_none);

	const float outL = sampleL * a0 + z1l;
	z1l = sampleL * a1 + z2l - b1 * outL;
	z2l = sampleL * a2 - b2 * outL;
	
	const float outR = sampleR * a0 + z1r;
	z1r = sampleR * a1 + z2r - b1 * outR;
	z2r = sampleR * a2 - b2 * outR;

	sampleL = outL;
	sampleR = outR;
}

SFM_INLINE void Biquad::processMono(float &sample) {

	SFM_ASSERT(m_type != bq_type_none);

	const float out = sample * a0 + z1l; // Just use left Zs
	z1l = sample * a1 + z2l - b1 * out;  //
	z2l = sample * a2 - b2 * out;        //

	sample = out;
}

SFM_INLINE void Biquad::setBiquad(int type, float Fc, float Q, float peakGaindB)
{
	m_type = type;

	if (bq_type_none == type)
		return;

	// This worked for sh*t @ Fall Damage, does it here?
	// z1l = z2l = 0.f; // Stereo (L)
	// z1r = z2r = 0.f; // (R)

	m_Q = Q;
	m_Fc = Fc;
	m_FcK = tanf(SFM::kPI*m_Fc);

	if (m_peakGain != peakGaindB) // Looks like it'll be worth the branch
	{
		m_peakGain = peakGaindB;
		m_peakGainV = (0.f != m_peakGain) ? powf(10.f, fabsf(m_peakGain) / 20.f) : 1.f/20.f;
	}
	
	calcBiquad();
}

#endif // Biquad_h

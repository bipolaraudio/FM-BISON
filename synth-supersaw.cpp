
/*
	FM. BISON hybrid FM synthesis -- Self-contained JP-8000 style supersaw oscillator.
	(C) visualizers.nl & bipolaraudio.nl
	MIT license applies, please see https://en.wikipedia.org/wiki/MIT_License or LICENSE in the project root!
*/

#include "synth-supersaw.h"

static double SampleDetuneCurve(double detune)
{
	SFM_ASSERT_NORM(detune);

	// "Since the Roland JP-8000 is a hardware synthesizer, it uses MIDI protocol to transfer control data.
	//  MIDI values are from a scale of 0 to 127 (128 in total). The detune of the Super Saw is therefore
	//  divided into 128 steps. If the detune is sampled at every 8th interval, it will result in a total of 17
	//  (value 0 also included) data points."
	//
	// Polynomial generated by Adam Szabo using Matlab

	return 
		(10028.7312891634*pow(detune, 11.0)) - (50818.8652045924*pow(detune, 10.0)) + (111363.4808729368*pow(detune, 9.0)) -
		(138150.6761080548*pow(detune, 8.0)) + (106649.6679158292*pow(detune, 7.0)) - (53046.9642751875*pow(detune, 6.0))  + 
		(17019.9518580080*pow(detune, 5.0))  - (3425.0836591318*pow(detune, 4.0))   + (404.2703938388*pow(detune, 3.0))    - 
		(24.1878824391*pow(detune, 2.0))     + (0.6717417634*detune)                + 0.0030115596;		
}

static void CalculateMix(float mix, float &mainMix, float &sideMix)
{
	SFM_ASSERT_NORM(mix);

	mainMix = -0.55366f*mix + 0.99785f;
	sideMix = -0.73764f*powf(mix, 2.f) + 1.2841f*mix + 0.044372f;
}

namespace SFM
{
	void Supersaw::SetDetune(float detune /* [0..1] */)
	{
		m_curDetune = (float) SampleDetuneCurve(detune);
		SFM_ASSERT(m_curDetune >= 0.f && m_curDetune <= 1.f);
	}
	
	void Supersaw::SetMix(float mix /* [0..1] */)
	{
		CalculateMix(mix, m_mainMix, m_sideMix);
	}
}

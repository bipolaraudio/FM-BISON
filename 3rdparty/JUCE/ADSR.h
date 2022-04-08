
// ----------------------------------------------------------------------------------------------------
//
// Modified for FM. BISON
//
// This is a modified version of the JUCE ADSR class; we'll, likely, be using it temporarily,
// just had to get rid of the ADSR by Nigel Redmon which didn't play too nice with FM synthesis
//
// - No external dependencies
// - Added various functions as used by SFM::Envelope (synth-envelope.h)
// - Removed buffer processing function
// - Curves added by Paul
// 
// FIXME:
//   - Uses 'jassert' (which happens to be available since FM. BISON uses it too)
//
// ----------------------------------------------------------------------------------------------------

/*
==============================================================================

This file is part of the JUCE library.
Copyright (c) 2020 - Raw Material Software Limited

JUCE is an open source library subject to commercial or open-source
licensing.

The code included in this file is provided under the terms of the ISC license
http://www.isc.org/downloads/software-support-policy/isc-license. Permission
To use, copy, modify, and/or distribute this software for any purpose with or
without fee is hereby granted provided that the above copyright notice and
this permission notice appear in all copies.

JUCE IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES, WHETHER
EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE
DISCLAIMED.

==============================================================================
*/

#include "../../synth-global.h"

namespace SFM // So that it will not collide with juce::ADSR
{

    //==============================================================================
    /**
    A very simple ADSR envelope class.

    To use it, call setSampleRate() with the current sample rate and give it some parameters
    with setParameters() then call getNextSample() to get the envelope value to be applied
    to each audio sample or applyEnvelopeToBuffer() to apply the envelope to a whole buffer.

    @tags{Audio}
    */
    class /* JUCE_API */  ADSR
    {
    public:
        //==============================================================================
        ADSR()
        {
            recalculateRates();
        }

        //==============================================================================
        /**
        Holds the parameters being used by an ADSR object.

        @tags{Audio}
        */
        struct /* JUCE_API */  Parameters
        {
            Parameters() = default;

            Parameters (
				float attackTimeSeconds,
				float attackCurve,
				float decayTimeSeconds,
				float decayCurve,
				float sustainLevel,
				float releaseTimeSeconds,
				float releaseCurveY) :
			attack (attackTimeSeconds)
,			attackCurve (attackCurve)
,			decay (decayTimeSeconds)
,			decayCurve(decayCurve)
,			sustain (sustainLevel)
,			release (releaseTimeSeconds)
,			releaseCurve(releaseCurveY) {}

            float attack = 0.1f, attackCurve = 0.5f, decay = 0.1f, decayCurve = 0.5f, sustain = 1.0f, release = 0.1f, releaseCurve = 0.5f;
        };

        enum class State { idle, attack, decay, sustain, pianosustain, release };

        /** Sets the parameters that will be used by an ADSR object.

        You must have called setSampleRate() with the correct sample rate before
        this otherwise the values may be incorrect!

        @see getParameters
        */
        void setParameters (const Parameters& newParameters)
        {
            // need to call setSampleRate() first!
            jassert (sampleRate > 0.0);

            parameters = newParameters;
            recalculateRates();
        }

        /** Returns the parameters currently being used by an ADSR object.

        @see setParameters
        */
        const Parameters& getParameters() const noexcept  { return parameters; }

        /** Returns true if the envelope is in its attack, decay, sustain or release stage. */
        bool isActive() const noexcept                    { return state != State::idle; }

        //==============================================================================
        /** Sets the sample rate that will be used for the envelope.

        This must be called before the getNextSample() or setParameters() methods.
        */
        void setSampleRate (double newSampleRate) noexcept
        {
            jassert (newSampleRate > 0.0);
            sampleRate = newSampleRate;
        }

        //==============================================================================
        /** Resets the envelope to an idle state. */
        void reset() noexcept
        {
            m_offset = 0.f;
            envelopeVal = 0.0f;
            state = State::idle;
        }

        /** Starts the attack phase of the envelope. */
        void noteOn() noexcept
        {
            m_offset = 0.f;

            if (attackRate > 0.0f)
            {
                state = State::attack;
            }
            else if (decayRate > 0.0f)
            {
                envelopeVal = 1.0f;
                state = State::decay;
            }
            else if (parameters.sustain > 0.f)
            {
                state = State::sustain;
            }

        }

        /** Starts the release phase of the envelope. */
        void noteOff() noexcept
        {
            if (state != State::idle)
            {
                if (parameters.release > 0.0f)
                {
                    m_offset = 0.f;
                    releaseRate = (float) (1.f / (parameters.release * sampleRate));
                    releaseLevel = envelopeVal;
                    state = State::release;
                }
                else
                {
                    reset();
                }
            }
        }

        bool isReleasing() const noexcept
        {
            return State::release == state;
        } 

        void sustain() 
        {
            // Sustain at current level immediately and indefinitely
            parameters.sustain = envelopeVal;
            state = State::sustain;
        }

        void pianoSustain(float falloff) noexcept
        {
            jassert(falloff >= 0.f);

            parameters.sustain = envelopeVal; // Adapt last/current output

            auto getRate = [] (float distance, float timeInSeconds, double sr)
            {
                return timeInSeconds > 0.0f ? (float) (distance / (timeInSeconds * sr)) : -1.0f;
            };

            // Elongate decay phase
            const float decay = parameters.decay;
            const float invFalloff = 1.f-falloff;
            pianoSustainRate = getRate(envelopeVal, SFM::kEpsilon + decay + decay*invFalloff, sampleRate);

            // Set state
            state = State::pianosustain;
        }

        void scaleReleaseRate(float scale) noexcept
        {
            jassert(scale >= 0.f);
            parameters.release *= scale;
        }

        State getState() const noexcept
        {
            return state;
        }

        //==============================================================================
        /** Returns the next sample value for an ADSR object.

        @see applyEnvelopeToBuffer
        */
        
        // Quadratic Bézier curve
        float getQuadraticCurve(float start, float end, float control, float offset)
        {
            const float startControl = lerpf(start, control, offset);
            const float controlEnd = lerpf(control, end, offset);
            
            return lerpf(startControl, controlEnd, offset);
        }

        // Cubic Bézier curve
        float getCubicCurve(float start, float end, float controlA, float controlB, float offset)
        {
            const float a = lerpf(start, controlA, offset);
            const float b = lerpf(controlA, controlB, offset);
            const float c = lerpf(controlB, end, offset);

            const float ab = lerpf(a, b, offset);
            const float bc = lerpf(b, c, offset);

            return lerpf(ab, bc, offset);
        }

        float getCurve(float start, float end, float control, float offset)
        {
            const bool isOut = start > end;
            float high = sqrtf(control);
            float low = control * control * control;

            // Sustain limit
            if (state == State::decay)
            {
                if (isOut && high < parameters.sustain)
                    high = end;
                
                if (isOut && low < parameters.sustain)
                    low = end;
            }

            if (state == State::release)
            {
                if (isOut && high > parameters.sustain)
                    high = start;

                if (isOut && low > parameters.sustain)
                    low = start;
            }

			// Let's trust that the compiler figures out this can be collapsed into a single branch, and otherwise
			// the branch predictor will take care of it (FIXME: check generated assembly)
			const float controlA = isOut ? high : low;
			const float controlB = isOut ? low : high;

            return getCubicCurve(start, end, controlA, controlB, offset);
        }
        
        float getNextSample() noexcept
        {
            
            if (state == State::idle)
                return 0.0f;

            if (state == State::attack)
            {
                m_offset += attackRate;

                envelopeVal = getCurve(0.f, 1.f, attackCurve, m_offset);

                if (m_offset >= 1.0f)
                {
                    envelopeVal = 1.f;
                    goToNextState();
                }
            }
            else if (state == State::decay)
            {
                m_offset += decayRate;
                
                envelopeVal = getCurve(1.f, parameters.sustain, decayCurve, m_offset);

                if (m_offset >= 1.0f || envelopeVal <= parameters.sustain)
                {
                    envelopeVal = parameters.sustain;
                    goToNextState();
                }

            }
            else if (state == State::sustain)
            {
                envelopeVal = parameters.sustain;
            }
            else if (state == State::pianosustain)
            {
                envelopeVal -= pianoSustainRate;

                if (envelopeVal < 0.0f)
                    envelopeVal = 0.f;
            }
            else if (state == State::release)
            {
                envelopeVal = getCurve(releaseLevel, 0.f, releaseCurve, m_offset);
                
                m_offset += releaseRate;

                if (m_offset >= 1.0f || envelopeVal <= 0.f)
                {
                    envelopeVal = 0.f;
                    goToNextState();
                }
            }
            return envelopeVal;
        }

        float getSample() const noexcept
        {
            return envelopeVal;
        }

    private:
        //==============================================================================
        void recalculateRates() noexcept
        {
            auto getRate = [] (float distance, float timeInSeconds, double sr)
            {
                return timeInSeconds > 0.0f ? (float) (distance / (timeInSeconds * sr)) : -1.0f;
            };

			attackRate  = getRate (1.0f, parameters.attack, sampleRate);
			decayRate   = getRate (1.0f, parameters.decay, sampleRate);
			releaseRate = getRate (1.0f, parameters.release, sampleRate);
            
            releaseLevel = -1.f;

            attackCurve = parameters.attackCurve;
            decayCurve = parameters.decayCurve;
            releaseCurve = parameters.releaseCurve;

            if ((state == State::attack && attackRate <= 0.0f)
                || (state == State::decay && (decayRate <= 0.0f || envelopeVal <= parameters.sustain))
                || (state == State::release && releaseRate <= 0.0f))
            {
                goToNextState();
            }
        }

        void goToNextState() noexcept
        {
            m_offset = 0.f;

            if (state == State::attack)
                state = (decayRate > 0.0f ? State::decay : State::sustain);
            else if (state == State::decay)
                state = State::sustain; 
            else if (state == State::release)
            {
                releaseLevel = envelopeVal;
                reset();
            }
        }

        //==============================================================================
        State state = State::idle;
        Parameters parameters;

        double sampleRate = 44100.0;
        float envelopeVal = 0.0f, attackRate = 0.0f, decayRate = 0.0f, releaseRate = 0.0f, pianoSustainRate = 0.0f
            , attackCurve = 0.5f, decayCurve = 0.5f, releaseCurve = 0.5f, releaseLevel = 0.f;
        float m_offset = 0.f;
    };

} // namespace SFM


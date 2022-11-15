
// Fall Damage DSP common -- Simple Biquad (12dB) based *serial* parametric EQ (stereo).
// (C) http://www.falldamagestudio.com

// Now what does serial mean in this context?
// - It means that the output of each band is used as input for the next, which results in this EQ being more of a sculpting tool than a general purpose EQ.
// - In case all bands are disabled (no filter type set) the signal passes through unchanged.

// Good visual tool to design/inspect different filter settings: https://www.earlevel.com/main/2013/10/13/biquad-calculator-v3/

#pragma once

namespace FDDSP {

	template<unsigned NumBands>
	class BiquadEQ
	{
	public:
		// See Biquad.h for details
		struct Band
		{
			int   type;      // Biquad type 
			float gain;      // Optional; does not apply to all filter types, this is *not* a pre- or postgain
			float frequency; // Cutoff freq. [kBiquad_MinCutoffInHz..kBiquad_MaxCutoffInHz]
			float Q;         // Quality factor
		};

		BiquadEQ()
		{
			for (auto& band : m_bands)
				band.type = bq_type_none;
		}

		void Reset()
		{
			for (unsigned iBand = 0; iBand < NumBands; ++iBand)
			{
				m_bands[iBand].type = bq_type_none;
				m_biquads[iBand].reset();
			}
		}

		// Process a single stereo sample
		FDDSP_INLINE void Apply(float& sampleL, float& sampleR)
		{
			float processedL = sampleL, processedR = sampleR;

			for (auto& biquad : m_biquads)
			{
				if (bq_type_none != biquad.getType())
				{
					// Filtered signal becomes new current signal		
					biquad.process(processedL, processedR);
				}
			}

			// Set result
			sampleL = processedL;
			sampleR = processedR;
		}
		
		// Process a single monaural sample (do not mix Apply() and ApplyMono() calls)
		FDDSP_INLINE void ApplyMono(float& sample)
		{
			float processed = sample;

			for (auto& biquad : m_biquads)
			{
				if (bq_type_none != biquad.getType())
				{
					// Filtered signal becomes new current signal		
					biquad.processMono(processed);
				}
			}

			// Set result
			sample = processed;
		}

		// Use to set up bands
		Band& GetBand(unsigned iBand)
		{
			FDDSP_ASSERT(iBand < NumBands);
			return m_bands[iBand];
		}

		// When done modifying bands call this function to update the (internal) filter settings
		//
		// This is a costly call; currently we've decided that we only update once before each block of samples to process so long as this
		// keeps working out for us, since interpolating these parameters and recalculating filter coefficients per sample is easy but expensive
		//
		// If we however get to that point, I'd suggest implementing interpolation *inside* of this class instead of tacking it on from the outside
		void UpdateBands(unsigned sampleRate)
		{
			FDDSP_ASSERT(sampleRate > 0);

			for (unsigned iBand = 0; iBand < NumBands; ++iBand)
			{
				const auto& band = m_bands[iBand];
				auto& biquad = m_biquads[iBand];

				if (bq_type_none != band.type)
				{
					const float freqHz = band.frequency;
					FDDSP_ASSERT_RANGE(freqHz, FDDSP::kBiquad_MinCutoffInHz, FDDSP::kBiquad_MaxCutoffInHz);

					const float Q = band.Q;
					FDDSP_ASSERT(Q > 0.f);
					
					biquad.setBiquad(band.type, freqHz/sampleRate, Q, band.gain);
				}
				else
				{
					biquad.setBiquad(FDDSP::bq_type_none, 0.f, 0.f, 0.f);
				}
			}
		}
	
	private:
		Band m_bands[NumBands];
		Biquad m_biquads[NumBands];
	};

}
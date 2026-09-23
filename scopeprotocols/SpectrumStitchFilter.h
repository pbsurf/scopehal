/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2026 Andrew D. Zonenberg and contributors                                                         *
* All rights reserved.                                                                                                 *
*                                                                                                                      *
* Redistribution and use in source and binary forms, with or without modification, are permitted provided that the     *
* following conditions are met:                                                                                        *
*                                                                                                                      *
*    * Redistributions of source code must retain the above copyright notice, this list of conditions, and the         *
*      following disclaimer.                                                                                           *
*                                                                                                                      *
*    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the       *
*      following disclaimer in the documentation and/or other materials provided with the distribution.                *
*                                                                                                                      *
*    * Neither the name of the author nor the names of any contributors may be used to endorse or promote products     *
*      derived from this software without specific prior written permission.                                           *
*                                                                                                                      *
* THIS SOFTWARE IS PROVIDED BY THE AUTHORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED   *
* TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL *
* THE AUTHORS BE HELD LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES        *
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR       *
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT *
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE       *
* POSSIBILITY OF SUCH DAMAGE.                                                                                          *
*                                                                                                                      *
***********************************************************************************************************************/


/**
	@file
	@author ngscopeclient contributors
	@brief Declaration of SpectrumStitchFilter
 */
#ifndef SpectrumStitchFilter_h
#define SpectrumStitchFilter_h

#include "../scopehal/PeakDetectionFilter.h"

/**
	@brief Puts together spectra captured at different center frequencies, to make one wider spectrum

	This is for software defined radios which sweep their LO across a span that is wider than they can capture at
	once, with one capture per LO frequency. Feed it the output of a Complex FFT. Each input spectrum is placed at its
	own frequency, and the output keeps the pieces from every step of the sweep.

	The LO is assumed to sweep upwards, so an input centered at or below the previous one starts a new sweep. When a
	sweep is complete, anything outside of it (such as left over from before the span was changed) is dropped.

	Only the middle part of each input is used ("Usable Bandwidth"), since the analog filter of the radio rolls off at
	the edges. Where captures overlap, each output bin comes from the capture whose center is closest to it. There is
	often a spike at the LO frequency from DC offset and LO leakage, which can be interpolated over ("DC Notch").

	A stitched span can have far more bins than there are pixels, so like the FFT filter there is a "Detector" setting
	which picks how the bins in each pixel are drawn.

	If all the inputs are at the same center frequency, each one is a new sweep, so this just passes through the
	usable part of the spectrum.
 */
class SpectrumStitchFilter : public PeakDetectionFilter
{
public:
	SpectrumStitchFilter(const std::string& color);

	virtual void Refresh(vk::raii::CommandBuffer& cmdBuf, std::shared_ptr<QueueHandle> queue) override;

	virtual void ClearSweeps() override;

	static std::string GetProtocolName();

	PROTOCOL_DECODER_INITPROC(SpectrumStitchFilter)

protected:
	void Reset();
	void FinishSweep();
	void Grow(int64_t start, int64_t end, int64_t gridPhase);
	void Crop(int64_t start, int64_t end);

	FilterParameter& m_usableBandwidth;
	FilterParameter& m_dcNotch;
	FilterParameter& m_detector;

	///@brief Width of a frequency bin, in X axis units (zero if we have no data)
	int64_t m_binWidth;

	///@brief Frequency of the first bin in m_values
	int64_t m_origin;

	///@brief Stitched spectrum
	std::vector<float> m_values;

	///@brief For each bin, how far it was from the center of the capture it came from
	std::vector<int64_t> m_distances;

	///@brief For each bin, which sweep it came from (zero if nothing has been put there yet)
	std::vector<uint64_t> m_sweeps;

	///@brief Current sweep number, starting from 1
	uint64_t m_sweep;

	///@brief Center frequency of the last input
	int64_t m_lastCenter;

	///@brief Lowest and highest center frequency seen in the current sweep
	int64_t m_sweepMinCenter;
	int64_t m_sweepMaxCenter;

	///@brief Number of captures in the current sweep
	size_t m_sweepCaptures;

	///@brief Half the width of the usable part of each capture in the current sweep
	int64_t m_sweepHalfWidth;

	///@brief How far past the outermost centers the last complete sweep went (zero if there hasn't been one)
	int64_t m_edgeWidth;

	///@brief Timestamp of the last input, so the same one isn't added twice if we're refreshed without new data
	time_t m_lastTimestamp;
	int64_t m_lastFemtoseconds;
};

#endif

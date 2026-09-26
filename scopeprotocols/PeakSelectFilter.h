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
	@brief Declaration of PeakSelectFilter
 */
#ifndef PeakSelectFilter_h
#define PeakSelectFilter_h

#include "../scopehal/PeakDetectionFilter.h"

/**
	@brief Match if the input is the first output of a PeakDetector (FFT, Peaks, spectrum analyzer channel, etc.)
 */
class InputConstraintPeakDetector : public InputConstraint
{
public:
	InputConstraintPeakDetector(FlowGraphNode* sink)
		: InputConstraint(sink)
	{}

	virtual bool Check(StreamDescriptor source) override
	{
		return (source.m_stream == 0) &&
			(source.GetType() == Stream::STREAM_TYPE_ANALOG) &&
			(dynamic_cast<PeakDetector*>(source.m_channel) != nullptr);
	}

	virtual std::string ToString() override
	{ return "Output of a peak detector (FFT, Peaks, Peak Hold, ...)"; }
};

/**
	@brief Selects one peak found by an upstream PeakDetector and outputs its position, magnitude and FWHM as scalars
 */
class PeakSelectFilter : public Filter
{
public:
	PeakSelectFilter(const std::string& color);

	virtual void Refresh(vk::raii::CommandBuffer& cmdBuf, std::shared_ptr<QueueHandle> queue) override;

	virtual bool ConsumesUpstreamPeaks() override
	{ return true; }

	static std::string GetProtocolName();

	PROTOCOL_DECODER_INITPROC(PeakSelectFilter)

protected:
	void OnModeChanged();
	void OutputNoPeak();

	///@brief How the peak is chosen
	FilterParameter& m_mode;

	///@brief 1-based rank of the peak by magnitude (MODE_RANK)
	FilterParameter& m_rank;

	///@brief Target X position (MODE_HIGHEST_IN_RANGE, MODE_NEAREST)
	FilterParameter& m_target;

	///@brief Maximum distance from m_target, 0 for unlimited (MODE_HIGHEST_IN_RANGE, MODE_NEAREST)
	FilterParameter& m_range;

	///@brief Values for m_mode
	enum Mode
	{
		MODE_RANK,
		MODE_HIGHEST_IN_RANGE,
		MODE_NEAREST
	};
};

#endif

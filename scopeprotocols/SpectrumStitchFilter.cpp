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
	@brief Implementation of SpectrumStitchFilter
 */

#include "../scopehal/scopehal.h"
#include "SpectrumStitchFilter.h"

using namespace std;

//Integer division rounding towards negative / positive infinity
static int64_t FloorDiv(int64_t a, int64_t b)
{
	int64_t q = a / b;
	if( (a % b != 0) && ( (a < 0) != (b < 0) ) )
		q --;
	return q;
}

static int64_t CeilDiv(int64_t a, int64_t b)
{
	return -FloorDiv(-a, b);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SpectrumStitchFilter::SpectrumStitchFilter(const string& color)
	: PeakDetectionFilter(color, CAT_RF)
	, m_usableBandwidth(m_parameters["Usable Bandwidth"])
	, m_dcNotch(m_parameters["DC Notch"])
{
	AddStream(Unit(Unit::UNIT_DBM), "data", Stream::STREAM_TYPE_ANALOG);
	CreateInput<InputConstraintStreamType>("din", Stream::STREAM_TYPE_ANALOG);

	//A little more than the LO step of IIOSDR, so adjacent captures overlap
	m_usableBandwidth = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_PERCENT));
	m_usableBandwidth.SetFloatVal(0.9);

	m_dcNotch = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_HZ));
	m_dcNotch.SetFloatVal(0);

	Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string SpectrumStitchFilter::GetProtocolName()
{
	return "Spectrum Stitch";
}

void SpectrumStitchFilter::ClearSweeps()
{
	Reset();
	SetData(nullptr, 0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

///@brief Throws away everything we've stitched together so far
void SpectrumStitchFilter::Reset()
{
	m_binWidth = 0;
	m_origin = 0;
	m_values.clear();
	m_distances.clear();
	m_sweeps.clear();
	m_sweep = 0;
	m_lastCenter = 0;
	m_sweepMinCenter = 0;
	m_sweepMaxCenter = 0;
	m_sweepCaptures = 0;
	m_sweepHalfWidth = 0;
	m_edgeWidth = 0;
	m_lastTimestamp = 0;
	m_lastFemtoseconds = 0;
}

/**
	@brief Called at the end of a sweep, to drop anything outside of it
 */
void SpectrumStitchFilter::FinishSweep()
{
	if(m_sweepCaptures == 0)
		return;

	//Don't go further past the outermost captures than the captures in the middle go (half the step between them),
	//since that's where the analog filter rolls off
	int64_t edge = m_sweepHalfWidth;
	if(m_sweepCaptures > 1)
	{
		int64_t step = (m_sweepMaxCenter - m_sweepMinCenter) / static_cast<int64_t>(m_sweepCaptures - 1);
		edge = min(edge, step/2 + m_binWidth);
	}

	m_edgeWidth = edge;
	Crop(m_sweepMinCenter - edge, m_sweepMaxCenter + edge);
}

/**
	@brief Extends the stitched spectrum so it covers a range of frequencies

	@param start		Lowest frequency to cover
	@param end			Highest frequency to cover
	@param gridPhase	Frequency of any bin of the input, to line the bins up with it if we don't have any yet
 */
void SpectrumStitchFilter::Grow(int64_t start, int64_t end, int64_t gridPhase)
{
	if(m_values.empty())
		m_origin = gridPhase + FloorDiv(start - gridPhase, m_binWidth) * m_binWidth;

	if(start < m_origin)
	{
		int64_t n = CeilDiv(m_origin - start, m_binWidth);
		m_values.insert(m_values.begin(), n, 0);
		m_distances.insert(m_distances.begin(), n, 0);
		m_sweeps.insert(m_sweeps.begin(), n, 0);
		m_origin -= n * m_binWidth;
	}

	int64_t pastEnd = m_origin + static_cast<int64_t>(m_values.size()) * m_binWidth;
	if(end >= pastEnd)
	{
		size_t n = m_values.size() + FloorDiv(end - pastEnd, m_binWidth) + 1;
		m_values.resize(n, 0);
		m_distances.resize(n, 0);
		m_sweeps.resize(n, 0);
	}
}

/**
	@brief Drops everything from the stitched spectrum outside of a range of frequencies

	@param start	Lowest frequency to keep
	@param end		Highest frequency to keep
 */
void SpectrumStitchFilter::Crop(int64_t start, int64_t end)
{
	if(m_values.empty())
		return;

	int64_t first = max<int64_t>(0, CeilDiv(start - m_origin, m_binWidth));
	int64_t last = min<int64_t>(m_values.size() - 1, FloorDiv(end - m_origin, m_binWidth));
	if(first > last)
	{
		m_values.clear();
		m_distances.clear();
		m_sweeps.clear();
		return;
	}

	m_values.resize(last + 1);
	m_distances.resize(last + 1);
	m_sweeps.resize(last + 1);
	m_values.erase(m_values.begin(), m_values.begin() + first);
	m_distances.erase(m_distances.begin(), m_distances.begin() + first);
	m_sweeps.erase(m_sweeps.begin(), m_sweeps.begin() + first);
	m_origin += first * m_binWidth;
}

void SpectrumStitchFilter::Refresh(vk::raii::CommandBuffer& cmdBuf, shared_ptr<QueueHandle> queue)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("SpectrumStitchFilter::Refresh");
	#endif
	ClearMessages();

	//Make sure we've got valid inputs
	if(!VerifyAllInputsOK())
	{
		AddErrorMessage("Missing input", "One or more inputs are unconnected");
		SetData(nullptr, 0);
		return;
	}

	auto din = dynamic_cast<UniformAnalogWaveform*>(GetInputWaveform(0));
	if(!din || (din->size() < 2) || (din->m_timescale <= 0) )
	{
		AddErrorMessage("Invalid input", "The input must be a spectrum, such as the output of a Complex FFT");
		SetData(nullptr, 0);
		return;
	}

	//If we're refreshed without new data, don't add the same capture again
	if(GetData(0) && (din->m_startTimestamp == m_lastTimestamp) && (din->m_startFemtoseconds == m_lastFemtoseconds) )
		return;
	m_lastTimestamp = din->m_startTimestamp;
	m_lastFemtoseconds = din->m_startFemtoseconds;

	//If the bins or units changed, what we have doesn't line up with the new data, so start over
	auto xunit = m_inputs[0]->GetXAxisUnits();
	auto yunit = m_inputs[0]->GetYAxisUnits();
	if( (din->m_timescale != m_binWidth) ||
		(xunit.GetType() != m_xAxisUnit.GetType()) ||
		(yunit.GetType() != GetYAxisUnits(0).GetType()) )
	{
		Reset();
	}
	m_binWidth = din->m_timescale;
	m_xAxisUnit = xunit;
	SetYAxisUnits(yunit, 0);

	din->PrepareForCpuAccess();
	size_t len = din->size();
	int64_t bin = m_binWidth;
	int64_t center = din->m_triggerPhase + static_cast<int64_t>(len / 2) * bin;
	int64_t half = llround(static_cast<double>(len) * bin * m_usableBandwidth.GetFloatVal() / 2);
	half = min(max(half, bin), static_cast<int64_t>(len / 2) * bin);

	//The LO sweeps upwards, so if it went back down (or didn't move) this is a new sweep
	if( (m_sweep == 0) || (center <= m_lastCenter) )
	{
		FinishSweep();
		m_sweep ++;
		m_sweepMinCenter = center;
		m_sweepMaxCenter = center;
		m_sweepCaptures = 0;
	}
	m_lastCenter = center;
	m_sweepMinCenter = min(m_sweepMinCenter, center);
	m_sweepMaxCenter = max(m_sweepMaxCenter, center);
	m_sweepCaptures ++;
	m_sweepHalfWidth = half;

	//Make room for this capture. If the spectrum is getting wider, only go as far past the capture as the last sweep
	//went past its ends, so the edge of the output doesn't move back and forth every sweep.
	int64_t edge = (m_edgeWidth > 0) ? min(half, m_edgeWidth) : half;
	Grow(center - edge, center + edge, din->m_triggerPhase);

	//Interpolate over the DC notch, if we have one
	vector<float> spectrum(din->m_samples.GetCpuPointer(), din->m_samples.GetCpuPointer() + len);
	double notch = m_dcNotch.GetFloatVal();
	if(xunit.GetType() == Unit::UNIT_MICROHZ)
		notch *= 1e6;
	if(notch > 0)
	{
		size_t mid = len / 2;
		size_t radius = floor(notch / 2 / bin);
		if( (radius < mid) && (mid + radius + 1 < len) )
		{
			size_t left = mid - radius - 1;
			size_t right = mid + radius + 1;
			for(size_t i=left+1; i<right; i++)
			{
				float frac = static_cast<float>(i - left) / (right - left);
				spectrum[i] = spectrum[left] + (spectrum[right] - spectrum[left]) * frac;
			}
		}
	}

	//Copy in each bin, unless another capture in this sweep was closer to it
	for(size_t i=0; i<len; i++)
	{
		int64_t f = din->m_triggerPhase + static_cast<int64_t>(i) * bin;
		int64_t dist = llabs(f - center);
		if(dist > half)
			continue;

		int64_t j = FloorDiv(f - m_origin + bin/2, bin);
		if( (j < 0) || (j >= static_cast<int64_t>(m_values.size())) )
			continue;

		if( (m_sweeps[j] != m_sweep) || (dist < m_distances[j]) )
		{
			m_values[j] = spectrum[i];
			m_distances[j] = dist;
			m_sweeps[j] = m_sweep;
		}
	}

	//Make the output
	auto cap = dynamic_cast<UniformAnalogWaveform*>(GetData(0));
	if(!cap)
	{
		cap = new UniformAnalogWaveform;
		SetData(cap, 0);
	}
	cap->m_timescale = bin;
	cap->m_triggerPhase = m_origin;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = din->m_startFemtoseconds;
	size_t nout = m_values.size();
	cap->Resize(nout);
	cap->PrepareForCpuAccess();

	//Anything we haven't got yet (if the captures don't overlap) is filled in from the next bin down
	float fill = 0;
	for(size_t j=0; j<nout; j++)
	{
		if(m_sweeps[j])
		{
			fill = m_values[j];
			break;
		}
	}
	for(size_t j=0; j<nout; j++)
	{
		if(m_sweeps[j])
			fill = m_values[j];
		cap->m_samples[j] = fill;
	}
	cap->MarkModifiedFromCpu();
	cap->m_revision ++;

	FindPeaks(cap, cmdBuf, queue);
}

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
	@brief Implementation of PeakSelectFilter
 */

#include "../scopehal/scopehal.h"
#include "PeakSelectFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

PeakSelectFilter::PeakSelectFilter(const string& color)
	: Filter(color, CAT_MEASUREMENT)
	, m_mode(m_parameters["Mode"])
	, m_rank(m_parameters["Rank"])
	, m_target(m_parameters["Target"])
	, m_range(m_parameters["Range"])
{
	AddStream(Unit(Unit::UNIT_HZ), "x", Stream::STREAM_TYPE_ANALOG_SCALAR);
	AddStream(Unit(Unit::UNIT_DBM), "y", Stream::STREAM_TYPE_ANALOG_SCALAR);
	AddStream(Unit(Unit::UNIT_HZ), "fwhm", Stream::STREAM_TYPE_ANALOG_SCALAR);

	CreateInput<InputConstraintPeakDetector>("din");

	m_mode = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_mode.AddEnumValue("Rank", MODE_RANK);
	m_mode.AddEnumValue("Highest in range", MODE_HIGHEST_IN_RANGE);
	m_mode.AddEnumValue("Nearest to target", MODE_NEAREST);
	m_mode.SetIntVal(MODE_RANK);
	m_mode.signal_changed().connect(sigc::mem_fun(*this, &PeakSelectFilter::OnModeChanged));

	m_rank = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_COUNTS));
	m_rank.SetIntVal(1);

	//Target and range are in the input's X axis units, updated on each refresh
	m_target = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_HZ));
	m_target.SetIntVal(0);

	m_range = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_HZ));
	m_range.SetIntVal(0);

	OnModeChanged();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string PeakSelectFilter::GetProtocolName()
{
	return "Peak Select";
}

/**
	@brief Show only the parameters used by the current mode
 */
void PeakSelectFilter::OnModeChanged()
{
	bool rank = (m_mode.GetIntVal() == MODE_RANK);
	m_rank.MarkHidden(!rank);
	m_target.MarkHidden(rank);
	m_range.MarkHidden(rank);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void PeakSelectFilter::OutputNoPeak()
{
	for(size_t i=0; i<3; i++)
		m_streams[i].m_value = NAN;
}

void PeakSelectFilter::Refresh(
	[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
	[[maybe_unused]] shared_ptr<QueueHandle> queue
	)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("PeakSelectFilter::Refresh");
	#endif
	ClearMessages();

	if(!VerifyAllInputsOK())
	{
		AddErrorMessage("Missing input", "One or more inputs are unconnected");
		OutputNoPeak();
		return;
	}

	auto din = GetInput(0);
	auto detector = dynamic_cast<PeakDetector*>(din.m_channel);
	if(!detector)
	{
		AddErrorMessage("Invalid input", "Input is not a peak detector");
		OutputNoPeak();
		return;
	}

	//Output units track the input
	auto xunit = din.GetXAxisUnits();
	SetYAxisUnits(xunit, 0);
	SetYAxisUnits(din.GetYAxisUnits(), 1);
	SetYAxisUnits(xunit, 2);
	m_target.SetUnit(xunit);
	m_range.SetUnit(xunit);

	//Peaks are sorted by decreasing magnitude
	auto& peaks = detector->GetPeaks();
	const Peak* found = nullptr;
	switch(m_mode.GetIntVal())
	{
		case MODE_RANK:
			{
				auto rank = m_rank.GetIntVal();
				if( (rank >= 1) && (rank <= (int64_t)peaks.size()) )
					found = &peaks[rank - 1];
			}
			break;

		case MODE_HIGHEST_IN_RANGE:
		case MODE_NEAREST:
			{
				int64_t target = m_target.GetIntVal();
				int64_t range = m_range.GetIntVal();
				bool nearest = (m_mode.GetIntVal() == MODE_NEAREST);
				int64_t bestDist = 0;
				for(auto& p : peaks)
				{
					int64_t dist = llabs(p.m_x - target);
					if( (range > 0) && (dist > range) )
						continue;

					//The first peak in range is the highest
					if(!nearest)
					{
						found = &p;
						break;
					}

					if(!found || (dist < bestDist))
					{
						found = &p;
						bestDist = dist;
					}
				}
			}
			break;

		default:
			break;
	}

	if(!found)
	{
		AddErrorMessage("No peak", "No peak matches the selection (is the upstream Peak Window too wide?)");
		OutputNoPeak();
		return;
	}

	m_streams[0].m_value = found->m_x;
	m_streams[1].m_value = found->m_y;
	m_streams[2].m_value = found->m_fwhm;
}

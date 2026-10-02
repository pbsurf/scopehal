/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
*                                                                                                                      *
* Copyright (c) 2012-2025 Andrew D. Zonenberg and contributors                                                         *
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
	@brief Implementation of SDRTransmitChannel
	@ingroup sdrdrivers
 */

#include "scopehal.h"
#include "SCPISDR.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Input processing

bool SDRTransmitChannel::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if( (stream.m_channel == nullptr) || (i != INPUT_LO) || (GetInputCount() == 0) )
		return false;

	return (stream.GetType() == Stream::STREAM_TYPE_ANALOG_SCALAR);
}

void SDRTransmitChannel::OnInputChanged(size_t /*i*/)
{
	//Send whatever the new input says, even if it's the same as the last one
	m_lastLo = NAN;
}

void SDRTransmitChannel::Refresh(vk::raii::CommandBuffer& /*cmdBuf*/, shared_ptr<QueueHandle> /*queue*/)
{
	if(GetInputCount() == 0)
		return;

	auto loIn = GetInput(INPUT_LO);
	if(!loIn || (loIn.GetYAxisUnits() != Unit(Unit::UNIT_HZ)))
		return;

	//We're refreshed every time the filter graph runs (every capture), so only retune when the input changes.
	//Compare against what we last sent rather than what the radio reports, since the synthesizer may round it.
	double lo = loIn.GetScalarValue();
	if(lo == m_lastLo)
		return;
	m_lastLo = lo;

	auto sdr = dynamic_cast<SCPISDR*>(m_instrument);
	if(sdr)
		sdr->SetTxLOFrequency(llround(lo));
}

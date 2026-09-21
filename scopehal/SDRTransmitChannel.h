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
	@brief Declaration of SDRTransmitChannel
	@ingroup sdrdrivers
 */

#ifndef SDRTransmitChannel_h
#define SDRTransmitChannel_h

/**
	@brief A transmit path of a software defined radio

	This has no data streams, since the samples being transmitted come from the radio itself (for example a DDS core)
	rather than from ngscopeclient. It exists so that the transmitter shows up in the instrument's channel list and can
	be configured with the SCPISDR transmit control methods.

	@ingroup sdrdrivers
 */
class SDRTransmitChannel : public InstrumentChannel
{
public:

	/**
		@brief Creates a transmit channel

		@param sdr		The radio this channel is part of
		@param hwname	Internal hardware name of the channel
		@param color	Display color
		@param index	Position of this channel within m_channels of the parent instrument
		@param txIndex	Zero-based index of this transmit path, as used by the SCPISDR transmit control methods
	 */
	SDRTransmitChannel(
		Instrument* sdr,
		const std::string& hwname,
		const std::string& color,
		size_t index,
		size_t txIndex)
		: InstrumentChannel(sdr, hwname, color, Unit(Unit::UNIT_HZ), index)
		, m_txIndex(txIndex)
	{
		ClearStreams();

		//There's nothing to connect to a filter graph
		m_visibilityMode = VIS_HIDE;
	}

	///@brief Gets the zero-based index of this path among the radio's transmit paths
	size_t GetTxIndex() const
	{ return m_txIndex; }

	virtual PhysicalConnector GetPhysicalConnector() override
	{ return InstrumentChannel::CONNECTOR_SMA; }

protected:
	size_t m_txIndex;
};

#endif

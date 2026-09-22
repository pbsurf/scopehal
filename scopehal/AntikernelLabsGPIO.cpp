/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
*                                                                                                                      *
* Copyright (c) 2012-2024 Andrew D. Zonenberg and contributors                                                         *
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
	@author Andrew D. Zonenberg
	@brief Implementation of AntikernelLabsGPIO
	@ingroup miscdrivers
 */

#include "scopehal.h"
#include "AntikernelLabsGPIO.h"
#include "VectorGPIOChannel.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

/**
	@brief Initialize the driver

	@param transport	SCPITransport pointing at the instrument
 */
AntikernelLabsGPIO::AntikernelLabsGPIO(SCPITransport* transport)
	: SCPIDevice(transport, true)
	, SCPIInstrument(transport, true)
	, m_cachedConfigValid(false)
	, m_cachedTris(0)
	, m_cachedOut(0)
{
	//Create initial stream
	m_channels.push_back(new VectorGPIOChannel(
		"GPIO",
		this,
		"#808080",
		0,
		32));

	//needs to run *before* the Oscilloscope class implementation
	m_preloaders.push_front(sigc::mem_fun(*this, &AntikernelLabsGPIO::DoPreLoadConfiguration));
}

AntikernelLabsGPIO::~AntikernelLabsGPIO()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Instantiation

uint32_t AntikernelLabsGPIO::GetInstrumentTypes() const
{
	return INST_MISC;
}

uint32_t AntikernelLabsGPIO::GetInstrumentTypesForChannel(size_t /*i*/) const
{
	return INST_MISC;
}

///@brief Returns the constant driver name
string AntikernelLabsGPIO::GetDriverNameInternal()
{
	return "akl.gpio";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Serialization

void AntikernelLabsGPIO::DoPreLoadConfiguration(
	[[maybe_unused]] int version,
	[[maybe_unused]] const YAML::Node& node,
	[[maybe_unused]] IDTable& idmap,
	[[maybe_unused]] ConfigWarningList& list)
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Acquisition

bool AntikernelLabsGPIO::AcquireData()
{
	auto chan = dynamic_cast<VectorGPIOChannel*>(m_channels[0]);
	if(!chan)
		return false;

	//Get the input value
	auto inval = Trim(m_transport->SendCommandQueuedWithReply("GPIO:INVAL?"));
	uint32_t hexinval = 0;
	sscanf(inval.c_str(), "%x", &hexinval);

	//Push to output streams
	for(size_t i=0; i<chan->GetStreamCount(); i++)
	{
		uint32_t mask = (1 << i);
		if( (hexinval & mask) == mask)
			chan->SetDigitalScalarValue(i, 1);
		else
			chan->SetDigitalScalarValue(i, 0);
	}

	//Generate output register values
	uint32_t tris = 0;
	uint32_t outval = 0;
	for(size_t i=0; i<chan->GetStreamCount(); i++)
	{
		//If we have no input connected, set the tristate to 1
		uint32_t mask = (1 << i);
		auto in = chan->GetInput(i);
		if(!in)
		{
			tris |= mask;
			continue;
		}

		//If we have an input, set the output to its scalar value
		if(in.GetDigitalScalarValue())
			outval |= mask;
	}

	//Push tristate and output values
	//Skip if we didn't change anything
	if(m_cachedConfigValid && (m_cachedTris == tris) )
	{}
	else
	{
		m_transport->SendCommandQueued(string("GPIO:TRIS ") + to_string_hex(tris));
		m_cachedTris = tris;
	}

	if(m_cachedConfigValid && (m_cachedOut == outval) )
	{}
	else
	{
		m_transport->SendCommandQueued(string("GPIO:OUTVAL ") + to_string_hex(outval));
		m_cachedOut = outval;
	}

	m_cachedConfigValid = true;

	return true;
}

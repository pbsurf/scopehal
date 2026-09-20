/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
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
	@brief Implementation of SCPIIIOTransport
	@ingroup transports
 */

#ifdef HAS_IIO

#include "scopehal.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SCPIIIOTransport::SCPIIIOTransport(const string& args)
	: m_uri(args)
{
	LogDebug("Connecting to IIO device at %s\n", args.c_str());

	m_context = IIOContext::Open(args);
}

SCPIIIOTransport::~SCPIIIOTransport()
{
}

bool SCPIIIOTransport::IsConnected()
{
	return m_context != nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual transport code

string SCPIIIOTransport::GetTransportName()
{
	return "iio";
}

string SCPIIIOTransport::GetConnectionString()
{
	return m_uri;
}

/**
	@brief Builds a *IDN? style reply (vendor,model,serial,version) from the IIO context attributes
 */
string SCPIIIOTransport::MakeIdentity()
{
	if(!m_context)
		return "";

	auto attrs = m_context->GetAttributes();

	//Attributes are set by the firmware, for example:
	//  hw_model="Analog Devices PlutoSDR Rev.B (Z7010-AD9363A)" hw_serial="104473..." fw_version="v0.38"
	//Other IIO devices may not have any of these.
	string vendor = "libiio";
	string model = attrs["hw_model"];
	if(model.empty())
		model = m_context->GetDescription();

	const string adi = "Analog Devices ";
	if(model.find(adi) == 0)
	{
		vendor = "Analog Devices";
		model = model.substr(adi.length());
	}

	string serial = attrs["hw_serial"];
	string version = attrs["fw_version"];
	if(serial.empty())
		serial = "unknown";
	if(version.empty())
		version = "unknown";

	//Commas delimit fields and the parser reads the version with %s (no whitespace)
	for(auto& c : vendor)
	{
		if(c == ',')
			c = ' ';
	}
	for(auto& c : model)
	{
		if(c == ',')
			c = ' ';
	}
	for(auto& c : serial)
	{
		if( (c == ',') || isspace(c) )
			c = '_';
	}
	for(auto& c : version)
	{
		if( (c == ',') || isspace(c) )
			c = '_';
	}

	return vendor + "," + model + "," + serial + "," + version;
}

bool SCPIIIOTransport::SendCommand(const string& cmd)
{
	if(cmd == "*IDN?")
	{
		m_pendingReply = MakeIdentity();
		return true;
	}

	LogDebug("IIO transport does not support SCPI command \"%s\"\n", cmd.c_str());
	return false;
}

string SCPIIIOTransport::ReadReply(bool /*endOnSemicolon*/, [[maybe_unused]] function<void(float)> progress)
{
	string ret = m_pendingReply;
	m_pendingReply = "";
	return ret;
}

void SCPIIIOTransport::FlushRXBuffer()
{
	m_pendingReply = "";
}

void SCPIIIOTransport::SendRawData(size_t /*len*/, const unsigned char* /*buf*/)
{
}

size_t SCPIIIOTransport::ReadRawData(size_t /*len*/, unsigned char* /*buf*/, std::function<void(float)> /*progress*/)
{
	return 0;
}

bool SCPIIIOTransport::IsCommandBatchingSupported()
{
	return false;
}

#endif

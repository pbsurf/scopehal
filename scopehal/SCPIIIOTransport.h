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
	@brief Declaration of SCPIIIOTransport
	@ingroup transports
 */

#ifdef HAS_IIO

#ifndef SCPIIIOTransport_h
#define SCPIIIOTransport_h

#include "IIOContext.h"

/**
	@brief Transport for Industrial I/O (IIO) devices such as the ADALM-PLUTO, via libiio

	IIO is not a SCPI protocol, so there is no command/response traffic on this transport. It exists to give IIO
	drivers a connection object that fits into the normal transport / session save / add-instrument flow. The
	"path" is a libiio context URI (usb:1.5.5, ip:192.168.2.1, local:, ...) or a "mock:" URI for simulated hardware.

	The only SCPI command implemented is *IDN?, which is synthesized from the IIO context attributes so that
	SCPIDevice can identify the instrument. Drivers access the hardware through GetContext().

	@ingroup transports
 */
class SCPIIIOTransport : public SCPITransport
{
public:
	SCPIIIOTransport(const std::string& args);
	virtual ~SCPIIIOTransport();

	//not copyable or assignable
	SCPIIIOTransport(const SCPIIIOTransport&) =delete;
	SCPIIIOTransport& operator=(const SCPIIIOTransport&) =delete;

	virtual std::string GetConnectionString() override;
	static std::string GetTransportName();

	virtual bool SendCommand(const std::string& cmd) override;
	virtual std::string ReadReply(bool endOnSemicolon = true, std::function<void(float)> progress = nullptr) override;
	virtual size_t ReadRawData(size_t len, unsigned char* buf, std::function<void(float)> progress = nullptr) override;
	virtual void SendRawData(size_t len, const unsigned char* buf) override;

	virtual bool IsCommandBatchingSupported() override;
	virtual bool IsConnected() override;

	virtual void FlushRXBuffer() override;

	static std::vector<TransportEndpoint> EnumTransportEndpoints();

	///@brief Gets the IIO context (nullptr if we failed to connect)
	IIOContext* GetContext()
	{ return m_context.get(); }

	TRANSPORT_INITPROC(SCPIIIOTransport)

protected:
	std::string MakeIdentity();

	std::string m_uri;
	std::unique_ptr<IIOContext> m_context;

	///@brief Reply waiting to be read
	std::string m_pendingReply;
};

#endif

#endif

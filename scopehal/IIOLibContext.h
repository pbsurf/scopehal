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
	@brief Declaration of IIOLibContext
	@ingroup sdrdrivers
 */

#ifdef HAS_IIO

#ifndef IIOLibContext_h
#define IIOLibContext_h

#include "IIOContext.h"

struct iio_context;

/**
	@brief IIOContext implementation backed by libiio (0.x API)

	@ingroup sdrdrivers
 */
class IIOLibContext : public IIOContext
{
public:
	virtual ~IIOLibContext();

	//not copyable or assignable
	IIOLibContext(const IIOLibContext&) =delete;
	IIOLibContext& operator=(const IIOLibContext&) =delete;

	static std::unique_ptr<IIOContext> Create(const std::string& uri);

	virtual std::string GetUri() override;
	virtual std::string GetDescription() override;
	virtual std::map<std::string, std::string> GetAttributes() override;

	virtual std::vector<std::string> GetDeviceNames() override;
	virtual bool HasDevice(const std::string& dev) override;
	virtual bool HasChannel(const std::string& dev, const std::string& chan, bool output) override;

	virtual bool ReadDeviceAttr(const std::string& dev, const std::string& attr, std::string& value) override;
	virtual bool WriteDeviceAttr(const std::string& dev, const std::string& attr, const std::string& value) override;
	virtual bool ReadChannelAttr(
		const std::string& dev, const std::string& chan, bool output, const std::string& attr,
		std::string& value) override;
	virtual bool WriteChannelAttr(
		const std::string& dev, const std::string& chan, bool output, const std::string& attr,
		const std::string& value) override;

protected:
	IIOLibContext(const std::string& uri, iio_context* ctx);

	std::string m_uri;
	iio_context* m_ctx;

	///@brief Serializes all access to the libiio context
	std::recursive_mutex m_mutex;
};

#endif

#endif

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
	@brief Declaration of IIOMockContext
	@ingroup sdrdrivers
 */

#ifdef HAS_IIO

#ifndef IIOMockContext_h
#define IIOMockContext_h

#include "IIOContext.h"

/**
	@brief Simulated IIO context modeling an AD936x SDR, for development and testing without hardware

	URIs: "mock:" or "mock:ad9363" (1R1T, Pluto-like), "mock:ad9361" (2R2T)

	The attribute names, channel layout, and device names follow the Linux ad9361 driver as used by pyadi-iio.
	Value ranges, clamping vs rejection of out-of-range writes, and default values are approximations chosen to
	be plausible, and have NOT been validated against real hardware.

	@ingroup sdrdrivers
 */
class IIOMockContext : public IIOContext
{
public:
	virtual ~IIOMockContext();

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
	///@brief Model-specific parameters
	struct Variant
	{
		std::string name;
		std::string hwModel;
		size_t numChannels;
		int64_t minLoHz;
		int64_t maxLoHz;
		int64_t maxBandwidthHz;
	};

	IIOMockContext(const std::string& uri, const Variant& variant);

	void AddChannel(const std::string& dev, bool output, const std::string& id, const std::string& name = "");
	void AddAttr(const std::string& dev, const std::string& chan, bool output, const std::string& attr,
		const std::string& value, bool writable = true);

	static std::string MakeKey(
		const std::string& dev, const std::string& chan, bool output, const std::string& attr);
	std::string ResolveChannel(const std::string& dev, const std::string& chan, bool output);

	bool WriteAttr(const std::string& dev, const std::string& chan, bool output, const std::string& attr,
		const std::string& value);
	bool ReadAttr(const std::string& dev, const std::string& chan, bool output, const std::string& attr,
		std::string& value);

	std::string m_uri;
	Variant m_variant;

	std::map<std::string, std::string> m_ctxAttrs;
	std::vector<std::string> m_devices;

	///@brief Maps "dev|dir|id-or-name" to the canonical channel ID
	std::map<std::string, std::string> m_channels;

	struct Attr
	{
		std::string value;
		bool writable;
	};
	std::map<std::string, Attr> m_attrs;

	std::recursive_mutex m_mutex;
};

#endif

#endif

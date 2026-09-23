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
	@brief Implementation of IIOLibContext
	@ingroup sdrdrivers
 */

#ifdef HAS_IIO

#include "scopehal.h"
#include "IIOLibContext.h"
#include <iio.h>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

IIOLibContext::IIOLibContext(const string& uri, iio_context* ctx)
	: m_uri(uri)
	, m_ctx(ctx)
	, m_rxBuf(nullptr)
	, m_rxDepth(0)
	, m_rxKernelBuffers(0)
{
}

IIOLibContext::~IIOLibContext()
{
	//The buffer belongs to the context, so has to go first
	StopCapture();
	iio_context_destroy(m_ctx);
}

unique_ptr<IIOContext> IIOLibContext::Create(const string& uri)
{
	LogDebug("Opening IIO context %s\n", uri.c_str());

	errno = 0;
	auto ctx = iio_create_context_from_uri(uri.c_str());
	if(!ctx)
	{
		LogError("Failed to open IIO context \"%s\": %s\n", uri.c_str(), strerror(errno));
		return nullptr;
	}

	//Can't use make_unique because the constructor is protected
	return unique_ptr<IIOContext>(new IIOLibContext(uri, ctx));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Context info

string IIOLibContext::GetUri()
{
	return m_uri;
}

string IIOLibContext::GetDescription()
{
	lock_guard<recursive_mutex> lock(m_mutex);

	auto desc = iio_context_get_description(m_ctx);
	return desc ? desc : "";
}

map<string, string> IIOLibContext::GetAttributes()
{
	lock_guard<recursive_mutex> lock(m_mutex);

	map<string, string> ret;
	unsigned int n = iio_context_get_attrs_count(m_ctx);
	for(unsigned int i=0; i<n; i++)
	{
		const char* name = nullptr;
		const char* value = nullptr;
		if( (iio_context_get_attr(m_ctx, i, &name, &value) == 0) && name)
			ret[name] = value ? value : "";
	}
	return ret;
}

vector<string> IIOLibContext::GetDeviceNames()
{
	lock_guard<recursive_mutex> lock(m_mutex);

	vector<string> ret;
	unsigned int n = iio_context_get_devices_count(m_ctx);
	for(unsigned int i=0; i<n; i++)
	{
		auto dev = iio_context_get_device(m_ctx, i);

		//Devices might not have a name, fall back to the ID if so
		auto name = iio_device_get_name(dev);
		ret.push_back(name ? name : iio_device_get_id(dev));
	}
	return ret;
}

bool IIOLibContext::HasDevice(const string& dev)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	return iio_context_find_device(m_ctx, dev.c_str()) != nullptr;
}

bool IIOLibContext::HasChannel(const string& dev, const string& chan, bool output)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	auto d = iio_context_find_device(m_ctx, dev.c_str());
	if(!d)
		return false;
	return iio_device_find_channel(d, chan.c_str(), output) != nullptr;
}

bool IIOLibContext::HasChannelAttr(const string& dev, const string& chan, bool output, const string& attr)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	auto d = iio_context_find_device(m_ctx, dev.c_str());
	auto c = d ? iio_device_find_channel(d, chan.c_str(), output) : nullptr;
	return c && iio_channel_find_attr(c, attr.c_str());
}

/**
	@brief Finds IIO devices attached by USB

	Network devices are not scanned, they need DNS-SD support and can take a long time to time out.
 */
vector<pair<string, string> > IIOLibContext::Scan()
{
	vector<pair<string, string> > ret;

	if(!iio_has_backend("usb"))
		return ret;

	auto scan = iio_create_scan_context("usb", 0);
	if(!scan)
		return ret;

	struct iio_context_info** info = nullptr;
	ssize_t n = iio_scan_context_get_info_list(scan, &info);
	for(ssize_t i=0; i<n; i++)
	{
		auto uri = iio_context_info_get_uri(info[i]);
		auto desc = iio_context_info_get_description(info[i]);
		if(uri)
			ret.push_back(pair<string, string>(uri, desc ? desc : ""));
	}

	if(info)
		iio_context_info_list_free(info);
	iio_scan_context_destroy(scan);

	return ret;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Attribute access

/**
	@brief Converts the result of a libiio attribute read into a string

	@param ret		Return value of iio_*_attr_read()
	@param buf		Buffer passed to iio_*_attr_read()
	@param len		Size of buf
	@param what		Description of the attribute (for logging)
	@param value	Output string
 */
static bool FinishRead(ssize_t ret, char* buf, size_t len, const string& what, string& value)
{
	if(ret < 0)
	{
		LogError("Failed to read IIO attribute %s: %s\n", what.c_str(), strerror(-ret));
		return false;
	}

	//Make sure we're terminated no matter what the backend did, then trim trailing whitespace
	buf[len-1] = '\0';
	value = buf;
	while(!value.empty() && isspace(value.back()))
		value.pop_back();
	return true;
}

static bool FinishWrite(ssize_t ret, const string& what, const string& value)
{
	if(ret < 0)
	{
		LogError("Failed to write IIO attribute %s = \"%s\": %s\n", what.c_str(), value.c_str(), strerror(-ret));
		return false;
	}
	return true;
}

bool IIOLibContext::ReadDeviceAttr(const string& dev, const string& attr, string& value)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	auto d = iio_context_find_device(m_ctx, dev.c_str());
	if(!d)
	{
		LogError("IIO device \"%s\" not found\n", dev.c_str());
		return false;
	}

	char buf[8192] = {0};
	return FinishRead(iio_device_attr_read(d, attr.c_str(), buf, sizeof(buf)), buf, sizeof(buf),
		dev + "/" + attr, value);
}

bool IIOLibContext::WriteDeviceAttr(const string& dev, const string& attr, const string& value)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	auto d = iio_context_find_device(m_ctx, dev.c_str());
	if(!d)
	{
		LogError("IIO device \"%s\" not found\n", dev.c_str());
		return false;
	}

	return FinishWrite(iio_device_attr_write(d, attr.c_str(), value.c_str()), dev + "/" + attr, value);
}

bool IIOLibContext::ReadChannelAttr(
	const string& dev, const string& chan, bool output, const string& attr, string& value)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	auto d = iio_context_find_device(m_ctx, dev.c_str());
	auto c = d ? iio_device_find_channel(d, chan.c_str(), output) : nullptr;
	if(!c)
	{
		LogError("IIO channel \"%s/%s\" (%s) not found\n", dev.c_str(), chan.c_str(), output ? "out" : "in");
		return false;
	}

	char buf[8192] = {0};
	return FinishRead(iio_channel_attr_read(c, attr.c_str(), buf, sizeof(buf)), buf, sizeof(buf),
		dev + "/" + chan + "/" + attr, value);
}

bool IIOLibContext::WriteChannelAttr(
	const string& dev, const string& chan, bool output, const string& attr, const string& value)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	auto d = iio_context_find_device(m_ctx, dev.c_str());
	auto c = d ? iio_device_find_channel(d, chan.c_str(), output) : nullptr;
	if(!c)
	{
		LogError("IIO channel \"%s/%s\" (%s) not found\n", dev.c_str(), chan.c_str(), output ? "out" : "in");
		return false;
	}

	return FinishWrite(iio_channel_attr_write(c, attr.c_str(), value.c_str()),
		dev + "/" + chan + "/" + attr, value);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Streaming

bool IIOLibContext::CaptureBlock(
	const string& dev,
	const vector<string>& channels,
	size_t depth,
	size_t kernelBuffers,
	size_t discard,
	vector<vector<int16_t> >& data)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	//Set up a new buffer if we don't have one for this capture already.
	//(a new buffer has nothing out of date in it, so there's nothing to discard)
	if( !m_rxBuf || (dev != m_rxDevName) || (channels != m_rxChannelNames) || (depth != m_rxDepth) ||
		(kernelBuffers != m_rxKernelBuffers) )
	{
		StopCapture();
		if(!StartCapture(dev, channels, depth, kernelBuffers))
			return false;
		discard = 0;
	}

	//Throw away anything that's out of date, then keep the last block
	for(size_t i=0; i<=discard; i++)
	{
		auto ret = iio_buffer_refill(m_rxBuf);
		if(ret < 0)
		{
			//Start over with a new buffer next time, in case this one is broken
			LogError("Failed to read IIO buffer from %s: %s\n", dev.c_str(), strerror(-ret));
			StopCapture();
			return false;
		}
	}

	data.clear();
	data.resize(m_rxChannels.size());
	for(size_t i=0; i<m_rxChannels.size(); i++)
	{
		data[i].resize(depth);
		size_t expected = depth * sizeof(int16_t);
		size_t got = iio_channel_read(m_rxChannels[i], m_rxBuf, data[i].data(), expected);
		if(got != expected)
		{
			LogError("Short read from IIO channel %s/%s (got %zu of %zu bytes)\n",
				dev.c_str(), channels[i].c_str(), got, expected);
			return false;
		}
	}

	return true;
}

/**
	@brief Enables the channels to capture and opens a buffer for them

	@param dev				Device to capture from
	@param channels			Channel IDs to capture
	@param depth			Number of samples per block
	@param kernelBuffers	Number of blocks the kernel can queue up

	@return					True on success, false on failure (details are logged)
 */
bool IIOLibContext::StartCapture(
	const string& dev,
	const vector<string>& channels,
	size_t depth,
	size_t kernelBuffers)
{
	auto d = iio_context_find_device(m_ctx, dev.c_str());
	if(!d)
	{
		LogError("IIO device \"%s\" not found\n", dev.c_str());
		return false;
	}

	//Start with everything off, then turn on only what we were asked for
	unsigned int nchans = iio_device_get_channels_count(d);
	for(unsigned int i=0; i<nchans; i++)
		iio_channel_disable(iio_device_get_channel(d, i));

	vector<iio_channel*> chans;
	for(auto& name : channels)
	{
		auto c = iio_device_find_channel(d, name.c_str(), false);
		if(!c || !iio_channel_is_scan_element(c))
		{
			LogError("IIO device \"%s\" has no input scan element \"%s\"\n", dev.c_str(), name.c_str());
			return false;
		}

		//We only handle 16 bit samples (12 bit ADCs like the AD936x are stored in 16 bits)
		auto fmt = iio_channel_get_data_format(c);
		if( (fmt->length != 16) || (fmt->repeat > 1) )
		{
			LogError("IIO channel %s/%s has an unsupported sample format (%u bits, repeat %u)\n",
				dev.c_str(), name.c_str(), fmt->length, fmt->repeat);
			return false;
		}

		chans.push_back(c);
	}
	for(auto c : chans)
		iio_channel_enable(c);

	//Not fatal if the device won't take this many, it'll just use what it has
	auto ret = iio_device_set_kernel_buffers_count(d, kernelBuffers);
	if(ret < 0)
		LogWarning("Couldn't use %zu kernel buffers for %s: %s\n", kernelBuffers, dev.c_str(), strerror(-ret));

	errno = 0;
	auto buf = iio_device_create_buffer(d, depth, false);
	if(!buf)
	{
		LogError("Failed to create IIO buffer for %s (%zu samples): %s\n", dev.c_str(), depth, strerror(errno));
		for(auto c : chans)
			iio_channel_disable(c);
		return false;
	}

	m_rxBuf = buf;
	m_rxDevName = dev;
	m_rxChannelNames = channels;
	m_rxChannels = chans;
	m_rxDepth = depth;
	m_rxKernelBuffers = kernelBuffers;
	return true;
}

void IIOLibContext::StopCapture()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	if(!m_rxBuf)
		return;

	iio_buffer_destroy(m_rxBuf);
	for(auto c : m_rxChannels)
		iio_channel_disable(c);

	m_rxBuf = nullptr;
	m_rxDevName.clear();
	m_rxChannelNames.clear();
	m_rxChannels.clear();
	m_rxDepth = 0;
	m_rxKernelBuffers = 0;
}

#endif

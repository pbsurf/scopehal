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
	@brief Declaration of IIOContext
	@ingroup sdrdrivers
 */

#ifdef HAS_IIO

#ifndef IIOContext_h
#define IIOContext_h

/**
	@brief Backend-independent view of an IIO (Industrial I/O) context

	This is a thin wrapper around the subset of libiio we use. It exists so that

	* the libiio headers do not leak into the rest of libscopehal
	* the driver can be developed and tested without hardware, using IIOMockContext ("mock:" URIs)

	Devices are identified by name (or ID), channels by ID (or name) plus direction. Note that in IIO terminology
	"output" means TX / host-to-device, so the RX local oscillator of an AD936x is an *output* channel (altvoltage0).

	All methods are thread safe. Attribute accessors return false on failure (details are logged).

	@ingroup sdrdrivers
 */
class IIOContext
{
public:
	virtual ~IIOContext() {}

	/**
		@brief Opens a context

		Supported URIs:
		* "mock:" or "mock:ad9363" - simulated Pluto-like 1R1T AD9363
		* "mock:ad9361"            - simulated 2R2T AD9361
		* Anything libiio understands (usb:1.5.5, ip:192.168.2.1, local:, xml:file.xml, ...)
		* A bare hostname or IP address, which is assumed to be "ip:<host>"

		@param uri	Context URI

		@return		The new context, or nullptr if connection failed
	 */
	static std::unique_ptr<IIOContext> Open(const std::string& uri);

	/**
		@brief Lists IIO contexts that can be found automatically (currently USB devices only)

		This never lists "mock:" devices. It is reasonably fast, but not free, so don't call it every frame.

		@return		List of (URI, description) pairs
	 */
	static std::vector<std::pair<std::string, std::string> > Scan();

	///@brief Gets the URI we were opened with
	virtual std::string GetUri() =0;

	///@brief Gets a human-readable description of the context
	virtual std::string GetDescription() =0;

	///@brief Gets the context attributes (hw_model, hw_serial, fw_version, etc)
	virtual std::map<std::string, std::string> GetAttributes() =0;

	//Topology
	virtual std::vector<std::string> GetDeviceNames() =0;
	virtual bool HasDevice(const std::string& dev) =0;
	virtual bool HasChannel(const std::string& dev, const std::string& chan, bool output) =0;

	///@brief Checks if a channel has an attribute, without logging an error if it doesn't
	virtual bool HasChannelAttr(
		const std::string& dev, const std::string& chan, bool output, const std::string& attr) =0;

	//Attribute access (raw strings)
	virtual bool ReadDeviceAttr(const std::string& dev, const std::string& attr, std::string& value) =0;
	virtual bool WriteDeviceAttr(const std::string& dev, const std::string& attr, const std::string& value) =0;
	virtual bool ReadChannelAttr(
		const std::string& dev, const std::string& chan, bool output, const std::string& attr, std::string& value) =0;
	virtual bool WriteChannelAttr(
		const std::string& dev, const std::string& chan, bool output, const std::string& attr,
		const std::string& value) =0;

	/**
		@brief Captures a block of samples from a buffer-capable device

		This blocks until the samples have been received. Channels are enabled for the duration of the call and
		then disabled again, and a fresh buffer is used each time so the data is always newly acquired.

		@param dev			Device to capture from (e.g. cf-ad9361-lpc)
		@param channels		Channel IDs to capture (e.g. voltage0, voltage1). All must be 16 bit input channels.
		@param depth		Number of samples to capture from each channel
		@param data			Output samples, one vector per channel in the same order as channels. These are converted
							to host format, so for a 12 bit ADC they range from -2048 to 2047.

		@return				True on success, false on failure (details are logged)
	 */
	virtual bool CaptureBlock(
		const std::string& dev,
		const std::vector<std::string>& channels,
		size_t depth,
		std::vector<std::vector<int16_t> >& data) =0;

	//Attribute access (typed convenience wrappers)
	bool ReadChannelAttrInt(
		const std::string& dev, const std::string& chan, bool output, const std::string& attr, int64_t& value);
	bool WriteChannelAttrInt(
		const std::string& dev, const std::string& chan, bool output, const std::string& attr, int64_t value);
	bool ReadChannelAttrDouble(
		const std::string& dev, const std::string& chan, bool output, const std::string& attr, double& value);
	bool WriteChannelAttrDouble(
		const std::string& dev, const std::string& chan, bool output, const std::string& attr, double value);
};

#endif

#endif

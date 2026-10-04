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
#include <random>

/**
	@brief Simulated IIO context modeling an AD936x SDR, for development and testing without hardware

	URIs: "mock:" or "mock:ad9363" (1R1T, Pluto-like), "mock:ad9361" (2R2T)

	Like the real driver, it publishes the supported ranges as "<attr>_available" attributes.

	CaptureBlock() synthesizes a handful of fixed-frequency RF tones (-60 to -40 dBm) plus noise. These are simulated
	at their real RF frequencies, so they move around or disappear as you retune the LO, change the sample rate, or
	narrow the RF bandwidth. It also takes as long as a real capture would (up to a limit).
	In manual gain mode the signal in the samples scales with the RX gain (clipping at full scale), while the AGC
	modes always settle at 20 dB.

	Each TX path is also looped back to the RX path with the same number, as if connected with a cable and a 30 dB
	attenuator. The DDS tones are synthesized from their frequency, scale, and phase settings and shifted by the
	difference between the TX and RX LOs, so a tone with the wrong sign or I/Q phases shows up as its image. A full
	scale tone at 0 dB transmit attenuation is +7 dBm. Levels at the RX input use the driver's units (the Complex FFT
	shows the power that went in), and they scale with the RX gain like a real radio's, clipping included. The same
	goes for the environment's tones.

	The noise is thermal noise at the input (-174 dBm/Hz plus a 3 dB noise figure, over the sample rate) amplified by
	the RX gain, plus 0.3 counts of ADC noise. Above about 50 dB of gain (at 2.5 MS/s, less at higher sample rates) the
	thermal noise dominates, so the noise floor referred to the input stays put; below that the ADC noise takes over
	and it rises as the gain goes down.

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
	virtual bool HasChannelAttr(
		const std::string& dev, const std::string& chan, bool output, const std::string& attr) override;

	virtual bool ReadDeviceAttr(const std::string& dev, const std::string& attr, std::string& value) override;
	virtual bool WriteDeviceAttr(const std::string& dev, const std::string& attr, const std::string& value) override;
	virtual bool ReadChannelAttr(
		const std::string& dev, const std::string& chan, bool output, const std::string& attr,
		std::string& value) override;
	virtual bool WriteChannelAttr(
		const std::string& dev, const std::string& chan, bool output, const std::string& attr,
		const std::string& value) override;

	virtual bool CaptureBlock(
		const std::string& dev,
		const std::vector<std::string>& channels,
		size_t depth,
		size_t kernelBuffers,
		size_t discard,
		std::vector<std::vector<int16_t> >& data) override;
	virtual void StopCapture() override;

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
		double minGainDb;
		double maxGainDb;
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

	///@brief Total number of samples generated so far, used to keep the simulated signals phase continuous
	uint64_t m_sampleIndex;

	std::mt19937 m_rng;
};

#endif

#endif

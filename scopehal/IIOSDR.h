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
	@brief Declaration of IIOSDR
	@ingroup sdrdrivers
 */

#ifdef HAS_IIO

#ifndef IIOSDR_h
#define IIOSDR_h

#include "SCPISDR.h"
#include "SCPIIIOTransport.h"

/**
	@brief IIOSDR - driver for AD936x based software defined radios (ADALM-PLUTO etc) using libiio

	Currently only the receive path is supported.

	Configuration changes are cached and pushed to the hardware by BackgroundProcessing(), which runs on the
	instrument thread between acquisitions. This means the GUI thread never blocks on the radio, and never contends
	with a capture that is in progress. The values reported by the getters are the requested settings, clamped to the
	limits we know about, until the hardware has been updated and read back.

	@ingroup sdrdrivers
 */
class IIOSDR
	: public virtual SCPISDR
{
public:
	IIOSDR(SCPITransport* transport);
	virtual ~IIOSDR();

	//not copyable or assignable
	IIOSDR(const IIOSDR& rhs) =delete;
	IIOSDR& operator=(const IIOSDR& rhs) =delete;

public:

	virtual void BackgroundProcessing() override;

	//Channel configuration
	virtual bool IsChannelEnabled(size_t i) override;
	virtual void EnableChannel(size_t i) override;
	virtual void DisableChannel(size_t i) override;
	virtual OscilloscopeChannel::CouplingType GetChannelCoupling(size_t i) override;
	virtual std::vector<OscilloscopeChannel::CouplingType> GetAvailableCouplings(size_t i) override;

	//Baseband timebase
	virtual uint64_t GetSampleRate() override;
	virtual void SetSampleRate(uint64_t rate) override;
	virtual uint64_t GetSampleDepth() override;
	virtual void SetSampleDepth(uint64_t depth) override;
	virtual std::vector<uint64_t> GetSampleRatesNonInterleaved() override;
	virtual std::vector<uint64_t> GetSampleDepthsNonInterleaved() override;
	virtual bool CanInterleave() override;

	//LO configuration
	virtual void SetSpan(int64_t span) override;
	virtual int64_t GetSpan() override;
	virtual void SetCenterFrequency(size_t channel, int64_t freq) override;
	virtual int64_t GetCenterFrequency(size_t channel) override;

	//Instrument settings
	virtual bool HasTimebaseControls() override;
	virtual bool HasFrequencyControls() override;
	virtual bool HasResolutionBandwidth() override;

	//Triggering
	virtual Oscilloscope::TriggerMode PollTrigger() override;
	virtual bool AcquireData() override;
	virtual void Start() override;
	virtual void StartSingleTrigger() override;
	virtual void Stop() override;
	virtual void ForceTrigger() override;
	virtual bool IsTriggerArmed() override;
	virtual void PushTrigger() override;
	virtual void PullTrigger() override;
	virtual OscilloscopeChannel* GetExternalTrigger() override;

protected:
	std::string GetChannelColor(size_t i);
	void ApplyConfiguration();
	void ReadHardwareConfiguration();

	///@brief The IIO context (owned by the transport), or nullptr if we failed to find a supported device
	IIOContext* m_ctx;

	///@brief Number of receive paths
	size_t m_numRx;

	//Requested configuration. Protected by m_cacheMutex
	int64_t m_centerFreq;
	int64_t m_span;
	uint64_t m_sampleRate;
	uint64_t m_sampleDepth;
	std::vector<bool> m_channelEnabled;
	bool m_centerFreqDirty;
	bool m_spanDirty;
	bool m_sampleRateDirty;

	///@brief Configuration currently active in the hardware. Only touched by the instrument thread.
	int64_t m_hwCenterFreq;
	uint64_t m_hwSampleRate;

public:
	static std::string GetDriverNameInternal();

	//This is intentionally not virtual since it's a static method used by enumeration
	//cppcheck-suppress duplInheritedMember
	static std::vector<SCPIInstrumentModel> GetDriverSupportedModels()
	{
		return {
			{"ADALM-PLUTO", {{ SCPITransportType::TRANSPORT_IIO, "ip:192.168.2.1" }}}
		};
	}

	SDR_INITPROC(IIOSDR)
};

#endif

#endif

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

	The receive paths are ComplexChannels streaming I/Q samples. If the radio has the usual DDS core in the FPGA, each
	transmit path is also exposed as an SDRTransmitChannel which can generate up to two tones.

	Configuration changes are cached and pushed to the hardware by BackgroundProcessing(), which runs on the
	instrument thread between acquisitions. This means the GUI thread never blocks on the radio, and never contends
	with a capture that is in progress. The values reported by the getters are the requested settings, clamped to the
	limits we know about, until the hardware has been updated and read back.

	If the span is wider than can be captured in one go (the smaller of the sample rate and the widest analog
	bandwidth), the radio sweeps: each acquisition steps the RX LO to the next frequency across the span, and reports
	it as the center frequency of that capture. The steps are a fraction of the capture bandwidth apart, so the edges
	of each capture (where the analog filter rolls off) overlap the next one, and are a whole number of FFT bins so
	the spectra line up. Use the Spectrum Stitch filter on the output of a Complex FFT to put the pieces together.
	In single trigger mode, the radio stays armed until it has been through the whole sweep.

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

	//RX gain
	virtual bool HasGainControl(size_t i) override;
	virtual std::vector<std::string> GetGainModes(size_t i) override;
	virtual std::string GetGainMode(size_t i) override;
	virtual void SetGainMode(size_t i, const std::string& mode) override;
	virtual bool IsGainAdjustable(size_t i) override;
	virtual std::pair<float, float> GetGainRange(size_t i) override;
	virtual float GetGain(size_t i) override;
	virtual void SetGain(size_t i, float gain) override;

	//Transmit control
	virtual size_t GetTxChannelCount() override;
	virtual size_t GetTxToneCount(size_t tx) override;
	virtual int64_t GetTxLOFrequency() override;
	virtual void SetTxLOFrequency(int64_t freq) override;
	virtual std::pair<int64_t, int64_t> GetTxLOFrequencyRange() override;
	virtual int64_t GetTxToneFrequency(size_t tx, size_t tone) override;
	virtual void SetTxToneFrequency(size_t tx, size_t tone, int64_t freq) override;
	virtual std::pair<int64_t, int64_t> GetTxToneFrequencyRange(size_t tx) override;
	virtual float GetTxAttenuation(size_t tx) override;
	virtual void SetTxAttenuation(size_t tx, float atten) override;
	virtual std::pair<float, float> GetTxAttenuationRange(size_t tx) override;
	virtual float GetTxToneAmplitude(size_t tx, size_t tone) override;
	virtual void SetTxToneAmplitude(size_t tx, size_t tone, float amplitude) override;

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
	///@brief Limits of the radio, used to clamp requests
	struct Limits
	{
		int64_t minCenterFreq;
		int64_t maxCenterFreq;
		int64_t minBandwidth;
		int64_t maxBandwidth;
		uint64_t minSampleRate;
		uint64_t maxSampleRate;
		float minGain;
		float maxGain;
	};

	///@brief Configuration of one tone from a DDS
	struct TxTone
	{
		///@brief Frequency relative to the TX LO in Hz, negative if below the LO
		int64_t freq;

		///@brief Amplitude as a fraction of full scale
		float amplitude;

		///@brief True if we have a change that hasn't been sent to the radio yet
		bool dirty;
	};

	std::string GetChannelColor(size_t i);
	void DetectLimits();
	void DetectTransmitter();
	void ApplyConfiguration();
	void ReadHardwareConfiguration();
	bool ReadTone(size_t tx, size_t tone, TxTone& out);
	void WriteTone(size_t tx, size_t tone, const TxTone& in);
	std::string GetToneChannelName(size_t tx, size_t tone, bool q);
	int64_t GetCaptureBandwidth(uint64_t rate);
	bool IsSweeping(int64_t span, uint64_t rate);
	std::vector<int64_t> GetSweepFrequencies(int64_t center, int64_t span, uint64_t rate, size_t depth);
	void Retune(int64_t freq);

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
	std::vector<float> m_gain;
	std::vector<std::string> m_gainMode;
	std::vector<bool> m_gainDirty;
	std::vector<bool> m_gainModeDirty;
	bool m_centerFreqDirty;
	bool m_spanDirty;
	bool m_sampleRateDirty;

	///@brief Number of transmit paths with a DDS (zero if the radio doesn't have one)
	size_t m_numTx;

	///@brief Number of tones per transmit path
	size_t m_numTones;

	///@brief Tone configuration, indexed by [transmit path][tone]. Protected by m_cacheMutex
	std::vector<std::vector<TxTone> > m_txTones;

	///@brief Attenuation of each transmit path in dB, and whether it needs to be sent to the radio.
	///Protected by m_cacheMutex
	std::vector<float> m_txAtten;
	std::vector<bool> m_txAttenDirty;

	///@brief Range of transmit attenuation in dB. Set at startup and never changes.
	float m_txMinAtten;
	float m_txMaxAtten;

	///@brief TX LO frequency and range. Protected by m_cacheMutex
	int64_t m_txLoFreq;
	bool m_txLoDirty;
	int64_t m_txLoMin;
	int64_t m_txLoMax;

	///@brief Largest tone frequency (half the transmit sample rate). Protected by m_cacheMutex
	int64_t m_txMaxToneFreq;

	///@brief Supported gain control modes (same for all channels)
	std::vector<std::string> m_gainModes;

	///@brief Limits of the radio. Set at startup and never changes.
	Limits m_limits;

	///@brief Configuration currently active in the hardware. Only touched by the instrument thread.
	int64_t m_hwCenterFreq;
	uint64_t m_hwSampleRate;

	///@brief LO frequencies of the sweep in progress (empty if not sweeping). Only touched by the instrument thread.
	std::vector<int64_t> m_sweepFreqs;

	///@brief Index into m_sweepFreqs of the next capture. Only touched by the instrument thread.
	size_t m_sweepStep;

	///@brief Set when the trigger is armed, to start the next capture at the beginning of the sweep
	std::atomic<bool> m_sweepRestart;

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

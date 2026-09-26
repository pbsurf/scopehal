/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
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
	@brief Declaration of HTTPExportFilter and HTTPExportServer
 */
#ifndef HTTPExportFilter_h
#define HTTPExportFilter_h

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace httplib
{
	class Server;
	struct Request;
	struct Response;
}

/**
	@brief Snapshot of an analog waveform, owned by HTTPExportServer and never modified after publication

	Request handlers serialize it without holding any lock, so it must never refer to live waveform data.
 */
struct HTTPExportWaveform
{
	///@brief Time (or other X axis) units per offset tick, in m_xUnit
	int64_t m_timescale = 0;

	///@brief Offset of the first sample from the trigger, in m_xUnit
	int64_t m_triggerPhase = 0;

	///@brief Start of the acquisition, Unix time
	time_t m_startTimestamp = 0;

	///@brief Fractional part of the start time, in fs
	int64_t m_startFemtoseconds = 0;

	///@brief X axis unit of offsets, durations, timescale and trigger phase, ASCII (e.g. "fs", "uHz")
	std::string m_xUnit;

	///@brief Unit of m_xScale * X values (e.g. "s")
	std::string m_xUnitScaled;

	///@brief Multiplier converting X values from m_xUnit to m_xUnitScaled (e.g. 1e-15)
	double m_xScale = 1;

	///@brief Unit of the samples, ASCII (converted to SI like scalars, e.g. mV to V)
	std::string m_yUnit;

	///@brief True if the waveform has explicit offsets and durations
	bool m_sparse = false;

	///@brief Sample values
	std::vector<float> m_samples;

	///@brief Sample offsets in timescale units (sparse only)
	std::vector<int64_t> m_offsets;

	///@brief Sample durations in timescale units (sparse only)
	std::vector<int64_t> m_durations;

	///@brief X axis value of sample i, in m_xUnit
	int64_t GetX(size_t i) const
	{ return (m_sparse ? m_offsets[i] : static_cast<int64_t>(i)) * m_timescale + m_triggerPhase; }
};

/**
	@brief Application-wide HTTP server publishing the latest values of all HTTPExportFilter instances

	The listener is started when the first value is registered and stopped when the last one is removed.
	Configure() is called by the application (from preferences) to set the listen address and port.
 */
class HTTPExportServer
{
public:
	static HTTPExportServer& Get();

	~HTTPExportServer();

	//not copyable or assignable
	HTTPExportServer(const HTTPExportServer& rhs) =delete;
	HTTPExportServer& operator=(const HTTPExportServer& rhs) =delete;

	///@brief Acquisition requested by a client, in increasing priority
	enum TriggerRequest
	{
		TRIGGER_NONE,
		TRIGGER_SINGLE,
		TRIGGER_FORCE
	};

	void Configure(const std::string& host, uint16_t port, bool allowTrigger);
	TriggerRequest TakeTriggerRequest();

	std::string GetBaseURL();
	std::string GetError();

	bool Register(const std::string& key);
	void Unregister(const std::string& key);
	void Update(const std::string& key, double value, const std::string& text, const std::string& unit);
	void UpdateWaveform(const std::string& key, std::shared_ptr<const HTTPExportWaveform> wfm);
	bool IsWaveformWanted(const std::string& key);

protected:
	HTTPExportServer();

	void Start();
	void Stop();

	std::string ValueToJson(const std::string& key);
	std::string AllValuesToJson();
	std::string ValuesToPrometheus();

	bool ParseFreshParams(
		const httplib::Request& req, httplib::Response& res, double& freshTimeout, TriggerRequest& trigger);
	int WaitForUpdate(
		std::unique_lock<std::mutex>& lock, const std::string& key, double freshTimeout, TriggerRequest trigger);
	int GetWaveform(
		const std::string& key,
		double freshTimeout,
		TriggerRequest trigger,
		std::shared_ptr<const HTTPExportWaveform>& wfm,
		uint64_t& seq,
		std::chrono::system_clock::time_point& updated);
	void HandleWaveformRequest(const httplib::Request& req, httplib::Response& res, const std::string& format);

	///@brief A single published value
	struct Entry
	{
		///@brief Numeric value
		double m_value = 0;

		///@brief Value formatted for output (empty if no value yet)
		std::string m_text;

		///@brief Unit of the value
		std::string m_unit;

		///@brief Number of updates so far
		uint64_t m_seq = 0;

		///@brief Wall clock time of the last update
		std::chrono::system_clock::time_point m_updated;

		///@brief Latest waveform, or null if the value is a scalar
		std::shared_ptr<const HTTPExportWaveform> m_wfm;

		///@brief True if a client asked for the waveform since the last copy
		bool m_wfmWanted = false;
	};

	///@brief Mutex protecting m_entries and m_stopping. Request handlers take only this mutex.
	std::mutex m_entriesMutex;

	///@brief Signalled when an entry is updated or removed, or the listener is stopping
	std::condition_variable m_entriesChanged;

	///@brief True while the listener is being stopped, so handlers waiting for a fresh waveform give up
	bool m_stopping = false;

	///@brief Acquisition requested by a client and not yet taken by the application (protected by m_entriesMutex)
	TriggerRequest m_triggerRequest = TRIGGER_NONE;

	///@brief True if clients may request acquisitions (read by request handlers, so atomic)
	std::atomic<bool> m_allowTrigger;

	///@brief Published values, by key
	std::map<std::string, Entry> m_entries;

	///@brief Mutex protecting the listener state and configuration below (always taken before m_entriesMutex)
	std::mutex m_serverMutex;

	///@brief The listener, if running
	std::unique_ptr<httplib::Server> m_server;

	///@brief Thread running the listener
	std::thread m_thread;

	///@brief Address to listen on
	std::string m_host;

	///@brief Port to listen on
	uint16_t m_port;

	///@brief Error from the last attempt to start the listener
	std::string m_error;

	///@brief Time of the last attempt to start the listener, for retrying after a failure
	std::chrono::steady_clock::time_point m_lastStartAttempt;
};

/**
	@brief Publishes a scalar value or an analog waveform over HTTP, keyed by the filter's display name
 */
class HTTPExportFilter : public Filter
{
public:
	HTTPExportFilter(const std::string& color);
	virtual ~HTTPExportFilter();

	virtual void Refresh(vk::raii::CommandBuffer& cmdBuf, std::shared_ptr<QueueHandle> queue) override;
	virtual void SetDisplayName(std::string name) override;

	static std::string GetProtocolName();

	std::string GetEndpointURL();

	void ReleaseSelfReference();

	PROTOCOL_DECODER_INITPROC(HTTPExportFilter)

protected:
	void UpdateRegistration();
	void PublishWaveform(StreamDescriptor& din);

	///@brief Mutex protecting m_key and m_registered
	std::mutex m_keyMutex;

	///@brief Key we are (or want to be) published under
	std::string m_key;

	///@brief True if m_key is registered with the server
	bool m_registered;

	///@brief True if we still hold the reference to ourself taken in the constructor
	bool m_holdingSelfReference;
};

#endif

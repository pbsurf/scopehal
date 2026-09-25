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

#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace httplib
{
	class Server;
}

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

	void Configure(const std::string& host, uint16_t port);

	std::string GetBaseURL();
	std::string GetError();

	bool Register(const std::string& key);
	void Unregister(const std::string& key);
	void Update(const std::string& key, double value, const std::string& text, const std::string& unit);

protected:
	HTTPExportServer();

	void Start();
	void Stop();

	std::string ValueToJson(const std::string& key);
	std::string AllValuesToJson();
	std::string ValuesToPrometheus();

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
	};

	///@brief Mutex protecting m_entries. Request handlers take only this mutex.
	std::mutex m_entriesMutex;

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
	@brief Publishes a scalar value over HTTP, keyed by the filter's display name
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

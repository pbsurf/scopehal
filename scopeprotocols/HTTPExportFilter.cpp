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
	@brief Implementation of HTTPExportFilter and HTTPExportServer
 */

//Must come before scopehal.h so winsock2.h is included before windows.h
#include "../third_party/cpp-httplib/httplib.h"

#include "../scopehal/scopehal.h"
#include "HTTPExportFilter.h"

#include <algorithm>
#include <cctype>
#include <cinttypes>
#include <cmath>
#include <cstring>
#include <ctime>

#ifndef _WIN32
#include <unistd.h>
#endif

using namespace std;

//Only characters allowed in keys (so they can be used verbatim in URLs and never need escaping)
static const char* g_keyPattern = "[A-Za-z0-9_-]+";

//Largest waveform served as JSON or CSV (about 15 bytes per sample); bigger ones must use .npy
static const size_t g_maxTextSamples = 1000000;

//Longest a client may wait for a fresh waveform, in seconds
static const double g_defaultFreshTimeout = 10;
static const double g_maxFreshTimeout = 60;

static string SanitizeKey(const string& name);
static string AsciiUnitName(Unit unit);
static void GetExportUnit(Unit unit, string& name, double& scale);
static string JsonEscape(const string& s);
static string FormatTimestamp(chrono::system_clock::time_point t);
static string FormatAge(chrono::system_clock::time_point t, chrono::system_clock::time_point now = chrono::system_clock::now());
static void SetErrorResponse(httplib::Response& res, int status, const char* what);
static vector<pair<string, string>> WaveformMetadata(const HTTPExportWaveform& wfm);
static string WaveformToJson(const HTTPExportWaveform& wfm);
static string WaveformToCsv(const HTTPExportWaveform& wfm);
static string NpyHeader(const HTTPExportWaveform& wfm);

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// HTTPExportServer

HTTPExportServer::HTTPExportServer()
	: m_allowTrigger(true)
	, m_host("127.0.0.1")
	, m_port(8080)
{
}

HTTPExportServer::~HTTPExportServer()
{
	lock_guard<mutex> lock(m_serverMutex);
	Stop();
}

/**
	@brief Gets the application-wide server instance
 */
HTTPExportServer& HTTPExportServer::Get()
{
	static HTTPExportServer server;
	return server;
}

/**
	@brief Sets the address and port to listen on

	Cheap if nothing changed, so it may be called every frame. Also periodically retries starting the listener if a
	previous attempt failed.

	@param host			Address to listen on
	@param port			Port to listen on
	@param allowTrigger	True if clients may request acquisitions (trigger= query parameter)
 */
void HTTPExportServer::Configure(const string& host, uint16_t port, bool allowTrigger)
{
	m_allowTrigger = allowTrigger;

	lock_guard<mutex> lock(m_serverMutex);

	bool wanted;
	{
		lock_guard<mutex> lock2(m_entriesMutex);
		wanted = !m_entries.empty();
	}

	if( (host != m_host) || (port != m_port) )
	{
		m_host = host;
		m_port = port;
		m_error = "";

		Stop();
		if(wanted)
			Start();
	}

	//Not running even though we have values to publish, so the last start failed. Retry now and then.
	else if(wanted && !m_server && (chrono::steady_clock::now() - m_lastStartAttempt > chrono::seconds(5)) )
		Start();
}

/**
	@brief Gets the URL clients should use to reach the server, or an empty string if it's not listening
 */
string HTTPExportServer::GetBaseURL()
{
	lock_guard<mutex> lock(m_serverMutex);
	if(!m_server)
		return "";

	string host = m_host;
	if(host == "0.0.0.0")
	{
		char name[256] = {0};
		if(gethostname(name, sizeof(name) - 1) == 0)
			host = name;
	}

	return string("http://") + host + ":" + to_string(m_port);
}

/**
	@brief Gets the error from the last attempt to start the listener, or an empty string if there was none
 */
string HTTPExportServer::GetError()
{
	lock_guard<mutex> lock(m_serverMutex);
	return m_error;
}

/**
	@brief Adds a new key, starting the listener if it's the first one

	@return False if the key is already in use
 */
bool HTTPExportServer::Register(const string& key)
{
	lock_guard<mutex> lock(m_serverMutex);
	{
		lock_guard<mutex> lock2(m_entriesMutex);
		if(m_entries.find(key) != m_entries.end())
			return false;
		m_entries[key] = Entry();
	}

	if(!m_server)
		Start();
	return true;
}

/**
	@brief Removes a key, stopping the listener if it was the last one
 */
void HTTPExportServer::Unregister(const string& key)
{
	lock_guard<mutex> lock(m_serverMutex);

	bool empty;
	{
		lock_guard<mutex> lock2(m_entriesMutex);
		m_entries.erase(key);
		empty = m_entries.empty();
	}
	m_entriesChanged.notify_all();

	if(empty)
		Stop();
}

/**
	@brief Publishes a new value for a registered key

	@param key		Key to update
	@param value	Numeric value
	@param text		Value formatted for output
	@param unit		Unit of the value
 */
void HTTPExportServer::Update(const string& key, double value, const string& text, const string& unit)
{
	lock_guard<mutex> lock(m_entriesMutex);
	auto it = m_entries.find(key);
	if(it == m_entries.end())
		return;

	auto& e = it->second;
	e.m_value = value;
	e.m_text = text;
	e.m_unit = unit;
	e.m_wfm = nullptr;
	e.m_seq ++;
	e.m_updated = chrono::system_clock::now();
	m_entriesChanged.notify_all();
}

/**
	@brief Publishes a new waveform for a registered key

	@param key		Key to update
	@param wfm		Snapshot of the waveform, must not be modified afterwards
 */
void HTTPExportServer::UpdateWaveform(const string& key, shared_ptr<const HTTPExportWaveform> wfm)
{
	lock_guard<mutex> lock(m_entriesMutex);
	auto it = m_entries.find(key);
	if(it == m_entries.end())
		return;

	auto& e = it->second;
	e.m_value = 0;
	e.m_text = "";
	e.m_unit = wfm->m_yUnit;
	e.m_wfm = wfm;
	e.m_wfmWanted = false;
	e.m_seq ++;
	e.m_updated = chrono::system_clock::now();
	m_entriesChanged.notify_all();
}

/**
	@brief Checks whether a waveform should be copied for a key: a client asked for it, or there is no copy yet

	Copying every acquisition would be wasteful when clients poll rarely, so the copy is made on demand. The first
	request after a gap therefore returns an older acquisition, unless the client asks for a fresh one.
 */
bool HTTPExportServer::IsWaveformWanted(const string& key)
{
	lock_guard<mutex> lock(m_entriesMutex);
	auto it = m_entries.find(key);
	if(it == m_entries.end())
		return false;
	return it->second.m_wfmWanted || !it->second.m_wfm;
}

/**
	@brief Gets and clears the acquisition requested by clients since the last call

	Called by the application every frame. It decides what to do: nothing if the trigger is already running, since
	the next acquisition will satisfy the clients anyway.
 */
HTTPExportServer::TriggerRequest HTTPExportServer::TakeTriggerRequest()
{
	lock_guard<mutex> lock(m_entriesMutex);
	auto ret = m_triggerRequest;
	m_triggerRequest = TRIGGER_NONE;
	return ret;
}

/**
	@brief Starts the listener. Must be called with m_serverMutex held.
 */
void HTTPExportServer::Start()
{
	m_lastStartAttempt = chrono::steady_clock::now();

	auto server = make_unique<httplib::Server>();

	//We only expect occasional polling, no need for a big pool
	server->new_task_queue = [] { return new httplib::ThreadPool(2, 16); };

	auto allValues = [this](const httplib::Request&, httplib::Response& res)
		{ res.set_content(AllValuesToJson(), "application/json"); };
	server->Get("/", allValues);
	server->Get("/values", allValues);

	server->Get(string("/values/(") + g_keyPattern + ")\\.txt",
		[this](const httplib::Request& req, httplib::Response& res)
		{
			double freshTimeout;
			TriggerRequest trigger;
			if(!ParseFreshParams(req, res, freshTimeout, trigger))
				return;

			unique_lock<mutex> lock(m_entriesMutex);
			int status = WaitForUpdate(lock, req.matches[1], freshTimeout, trigger);
			auto it = m_entries.find(req.matches[1]);
			if( (status == httplib::StatusCode::OK_200) && it->second.m_text.empty() )
				status = httplib::StatusCode::NotFound_404;

			if(status != httplib::StatusCode::OK_200)
				SetErrorResponse(res, status, "value");
			else
				res.set_content(it->second.m_text + "\n", "text/plain");
		});

	server->Get(string("/values/(") + g_keyPattern + ")",
		[this](const httplib::Request& req, httplib::Response& res)
		{
			double freshTimeout;
			TriggerRequest trigger;
			if(!ParseFreshParams(req, res, freshTimeout, trigger))
				return;

			int status;
			{
				unique_lock<mutex> lock(m_entriesMutex);
				status = WaitForUpdate(lock, req.matches[1], freshTimeout, trigger);
			}

			//Might have been removed since we checked
			string json;
			if(status == httplib::StatusCode::OK_200)
				json = ValueToJson(req.matches[1]);
			if(json.empty() && (status == httplib::StatusCode::OK_200))
				status = httplib::StatusCode::NotFound_404;

			if(status != httplib::StatusCode::OK_200)
				SetErrorResponse(res, status, "value");
			else
				res.set_content(json + "\n", "application/json");
		});

	server->Get("/metrics", [this](const httplib::Request&, httplib::Response& res)
		{ res.set_content(ValuesToPrometheus(), "text/plain; version=0.0.4"); });

	server->Get(string("/waveforms/(") + g_keyPattern + ")(?:\\.(json|csv|npy))?",
		[this](const httplib::Request& req, httplib::Response& res)
		{
			string format = req.matches[2];
			HandleWaveformRequest(req, res, format.empty() ? "json" : format);
		});

	if(!server->bind_to_port(m_host, m_port))
	{
		string err = string("Unable to listen on ") + m_host + ":" + to_string(m_port);

		//Don't repeat the same error on every retry
		if(err != m_error)
			LogError("HTTP export: %s\n", err.c_str());
		m_error = err;
		return;
	}

	m_error = "";
	LogNotice("HTTP export: listening on %s:%d\n", m_host.c_str(), m_port);

	auto p = server.get();
	m_thread = thread([p] { p->listen_after_bind(); });
	m_server = std::move(server);
}

/**
	@brief Stops the listener, if running. Must be called with m_serverMutex held.
 */
void HTTPExportServer::Stop()
{
	if(!m_server)
		return;

	//Wake up handlers waiting for a fresh waveform, or stop() would wait for them to time out
	{
		lock_guard<mutex> lock(m_entriesMutex);
		m_stopping = true;
	}
	m_entriesChanged.notify_all();

	m_server->stop();
	if(m_thread.joinable())
		m_thread.join();
	m_server = nullptr;

	{
		lock_guard<mutex> lock(m_entriesMutex);
		m_stopping = false;
	}

	LogNotice("HTTP export: stopped listening\n");
}

/**
	@brief Formats a single value as a JSON object, or returns an empty string if the key doesn't exist
 */
string HTTPExportServer::ValueToJson(const string& key)
{
	lock_guard<mutex> lock(m_entriesMutex);
	auto it = m_entries.find(key);
	if(it == m_entries.end())
		return "";

	auto& e = it->second;

	string ret;
	if(e.m_wfm)
	{
		//Waveforms are only listed here, the samples are served by /waveforms/<key>
		ret = "{\"type\":\"waveform\"";
		for(auto& field : WaveformMetadata(*e.m_wfm))
			ret += ",\"" + field.first + "\":" + field.second;
	}
	else
	{
		//JSON has no representation for NaN or infinity
		string value = "null";
		if(!e.m_text.empty() && isfinite(e.m_value))
			value = e.m_text;

		ret = string("{");
		if(e.m_seq != 0)
			ret += "\"type\":\"scalar\",";
		ret += string("\"value\":") + value + ",\"unit\":\"" + JsonEscape(e.m_unit) + "\"";
	}

	ret += ",\"seq\":" + to_string(e.m_seq);
	if(e.m_seq != 0)
		ret += string(",\"updated\":\"") + FormatTimestamp(e.m_updated) + "\",\"age_s\":" + FormatAge(e.m_updated);
	return ret + "}";
}

/**
	@brief Parses the fresh, timeout and trigger query parameters

	trigger=force (or 1) / single asks the application for an acquisition if the trigger is stopped, and implies
	fresh=1. fresh=1 waits for the next update, up to timeout=<seconds> (default 10, at most 60).

	@return False if the parameters are invalid or triggering is disabled; the error response is already set
 */
bool HTTPExportServer::ParseFreshParams(
	const httplib::Request& req, httplib::Response& res, double& freshTimeout, TriggerRequest& trigger)
{
	freshTimeout = 0;
	trigger = TRIGGER_NONE;

	if(req.has_param("trigger"))
	{
		auto value = req.get_param_value("trigger");
		if( (value == "force") || (value == "1") )
			trigger = TRIGGER_FORCE;
		else if(value == "single")
			trigger = TRIGGER_SINGLE;
		else if(value != "0")
		{
			res.status = httplib::StatusCode::BadRequest_400;
			res.set_content("trigger must be force (or 1), single, or 0\n", "text/plain");
			return false;
		}

		if( (trigger != TRIGGER_NONE) && !m_allowTrigger)
		{
			res.status = httplib::StatusCode::Forbidden_403;
			res.set_content(
				"Triggering acquisitions over HTTP is disabled in ngscopeclient's preferences "
				"(Network > HTTP Export)\n",
				"text/plain");
			return false;
		}
	}

	if( (trigger != TRIGGER_NONE) || (req.has_param("fresh") && (req.get_param_value("fresh") != "0")) )
	{
		freshTimeout = g_defaultFreshTimeout;
		if(req.has_param("timeout"))
			freshTimeout = min(atof(req.get_param_value("timeout").c_str()), g_maxFreshTimeout);
	}
	return true;
}

/**
	@brief Flags an entry's waveform as wanted and optionally waits for its next update. Must be called with lock held.

	@param lock			Lock on m_entriesMutex
	@param key			Key to look up
	@param freshTimeout	If positive, wait up to this many seconds for the next update
	@param trigger		Acquisition to request from the application while waiting

	@return HTTP status: 200, 404 if the key doesn't exist (or was removed while waiting), 504 on timeout
 */
int HTTPExportServer::WaitForUpdate(
	unique_lock<mutex>& lock, const string& key, double freshTimeout, TriggerRequest trigger)
{
	auto it = m_entries.find(key);
	if(it == m_entries.end())
		return httplib::StatusCode::NotFound_404;

	//Waveforms are only copied when wanted (harmless for scalars)
	it->second.m_wfmWanted = true;

	if(freshTimeout <= 0)
		return httplib::StatusCode::OK_200;

	//Requested under the same lock as we read seq0, so the update it causes can't be missed
	auto seq0 = it->second.m_seq;
	if(trigger > m_triggerRequest)
		m_triggerRequest = trigger;

	bool updatedInTime = m_entriesChanged.wait_for(
		lock,
		chrono::duration<double>(freshTimeout),
		[&]
		{
			//The entry may have been removed while we waited
			it = m_entries.find(key);
			return m_stopping || (it == m_entries.end()) || (it->second.m_seq != seq0);
		});

	if(it == m_entries.end())
		return httplib::StatusCode::NotFound_404;
	if(!updatedInTime || m_stopping)
		return httplib::StatusCode::GatewayTimeout_504;
	return httplib::StatusCode::OK_200;
}

/**
	@brief Gets the latest waveform for a key and flags it as wanted, so the next acquisition is copied

	@param key			Key to look up
	@param freshTimeout	If positive, wait up to this many seconds for the next update instead of returning the
						current copy
	@param trigger		Acquisition to request from the application while waiting
	@param wfm			Set to the waveform
	@param seq			Set to the update count of the waveform
	@param updated		Set to the time the waveform was published

	@return HTTP status: 200, 404 if the key doesn't exist or has no waveform (yet), 504 if no fresh waveform arrived in time
 */
int HTTPExportServer::GetWaveform(
	const string& key,
	double freshTimeout,
	TriggerRequest trigger,
	shared_ptr<const HTTPExportWaveform>& wfm,
	uint64_t& seq,
	chrono::system_clock::time_point& updated)
{
	unique_lock<mutex> lock(m_entriesMutex);
	auto it = m_entries.find(key);
	if(it == m_entries.end())
		return httplib::StatusCode::NotFound_404;

	//Only waveforms (or entries without a value yet, which may become one) can be requested
	if(!it->second.m_wfm && (it->second.m_seq != 0))
		return httplib::StatusCode::NotFound_404;

	int status = WaitForUpdate(lock, key, freshTimeout, trigger);
	if(status != httplib::StatusCode::OK_200)
		return status;

	//WaitForUpdate() checked it still exists, and we've held the lock since
	it = m_entries.find(key);
	if(!it->second.m_wfm)
		return httplib::StatusCode::NotFound_404;

	wfm = it->second.m_wfm;
	seq = it->second.m_seq;
	updated = it->second.m_updated;
	return httplib::StatusCode::OK_200;
}

/**
	@brief Serves /waveforms/<key>[.json|.csv|.npy]

	Query parameters: see ParseFreshParams().
 */
void HTTPExportServer::HandleWaveformRequest(const httplib::Request& req, httplib::Response& res, const string& format)
{
	double freshTimeout;
	TriggerRequest trigger;
	if(!ParseFreshParams(req, res, freshTimeout, trigger))
		return;

	shared_ptr<const HTTPExportWaveform> wfm;
	uint64_t seq = 0;
	chrono::system_clock::time_point updated;
	int status = GetWaveform(req.matches[1], freshTimeout, trigger, wfm, seq, updated);
	if(status != httplib::StatusCode::OK_200)
	{
		SetErrorResponse(res, status, "waveform");
		return;
	}

	//From here on we only use our own snapshot, without holding any lock

	if( (format != "npy") && (wfm->m_samples.size() > g_maxTextSamples) )
	{
		res.status = httplib::StatusCode::PayloadTooLarge_413;
		res.set_content(
			"Waveform has " + to_string(wfm->m_samples.size()) + " samples, more than " +
				to_string(g_maxTextSamples) + " can be served as JSON or CSV. Use .npy instead.\n",
			"text/plain");
		return;
	}

	if(format == "json")
	{
		res.set_content(
			"{\"type\":\"waveform\",\"seq\":" + to_string(seq) + ",\"updated\":\"" + FormatTimestamp(updated) +
				"\",\"age_s\":" + FormatAge(updated) + "," + WaveformToJson(*wfm) + "}\n",
			"application/json");
		return;
	}

	//CSV and NumPy carry the metadata in headers
	res.set_header("X-Seq", to_string(seq));
	res.set_header("X-Updated", FormatTimestamp(updated));
	res.set_header("X-Age-S", FormatAge(updated));
	for(auto& field : WaveformMetadata(*wfm))
	{
		//x_unit -> X-X-Unit
		string name = "X";
		bool upper = true;
		for(auto c : "-" + field.first)
		{
			if( (c == '_') || (c == '-') )
			{
				name += '-';
				upper = true;
			}
			else
			{
				name += upper ? static_cast<char>(toupper(c)) : c;
				upper = false;
			}
		}

		//String values without the JSON quotes (units never contain quotes or backslashes that need escaping)
		auto value = field.second;
		if( (value.size() >= 2) && (value[0] == '"') )
			value = value.substr(1, value.size() - 2);
		res.set_header(name, value);
	}

	if(format == "csv")
	{
		res.set_content(WaveformToCsv(*wfm), "text/csv");
		return;
	}

	//NumPy: stream the header and then the data straight from our snapshot.
	//Uniform waveforms are a float32 array, sparse ones a structured array of (offset, duration, value) records.
	//Both assume a little endian host, as the header says.
	auto header = make_shared<string>(NpyHeader(*wfm));
	auto body = make_shared<string>();
	const char* data = reinterpret_cast<const char*>(wfm->m_samples.data());
	size_t datalen = wfm->m_samples.size() * sizeof(float);
	if(wfm->m_sparse)
	{
		const size_t recsize = 2*sizeof(int64_t) + sizeof(float);
		body->resize(wfm->m_samples.size() * recsize);
		for(size_t i=0; i<wfm->m_samples.size(); i++)
		{
			char* p = &(*body)[i * recsize];
			memcpy(p, &wfm->m_offsets[i], sizeof(int64_t));
			memcpy(p + sizeof(int64_t), &wfm->m_durations[i], sizeof(int64_t));
			memcpy(p + 2*sizeof(int64_t), &wfm->m_samples[i], sizeof(float));
		}
		data = body->data();
		datalen = body->size();
	}

	res.set_content_provider(
		header->size() + datalen,
		"application/octet-stream",
		[wfm, header, body, data](size_t offset, size_t length, httplib::DataSink& sink)
		{
			//Captures keep the snapshot alive until the transfer is done
			if(offset < header->size())
			{
				size_t n = min(length, header->size() - offset);
				return sink.write(header->data() + offset, n);
			}
			return sink.write(data + (offset - header->size()), length);
		});
	res.set_header("Content-Disposition", string("attachment; filename=\"") + string(req.matches[1]) + ".npy\"");
}

/**
	@brief Formats all values as a JSON object, keyed by name
 */
string HTTPExportServer::AllValuesToJson()
{
	vector<string> keys;
	{
		lock_guard<mutex> lock(m_entriesMutex);
		for(auto& it : m_entries)
			keys.push_back(it.first);
	}

	string ret = "{";
	bool first = true;
	for(auto& k : keys)
	{
		//might have been removed since we made the list
		auto json = ValueToJson(k);
		if(json.empty())
			continue;

		if(!first)
			ret += ",";
		first = false;
		ret += string("\"") + k + "\":" + json;
	}
	return ret + "}\n";
}

/**
	@brief Formats all values in the Prometheus text exposition format
 */
string HTTPExportServer::ValuesToPrometheus()
{
	lock_guard<mutex> lock(m_entriesMutex);
	auto now = chrono::system_clock::now();

	string values = "# TYPE ngscopeclient_value gauge\n";
	string ages = "# TYPE ngscopeclient_value_age_seconds gauge\n";
	for(auto& it : m_entries)
	{
		auto& e = it.second;
		if( (e.m_seq == 0) || e.m_wfm)
			continue;

		//Prometheus label values use the same escaping as JSON strings, near enough
		values += string("ngscopeclient_value{name=\"") + it.first + "\",unit=\"" + JsonEscape(e.m_unit) + "\"} " +
			e.m_text + "\n";

		ages += string("ngscopeclient_value_age_seconds{name=\"") + it.first + "\"} " + FormatAge(e.m_updated, now) + "\n";
	}
	return values + ages;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helpers

/**
	@brief Converts a display name to a key by replacing disallowed characters with underscores
 */
static string SanitizeKey(const string& name)
{
	string ret;
	for(auto c : name)
	{
		if(isalnum(static_cast<unsigned char>(c)) || (c == '_') || (c == '-'))
			ret += c;
		else
			ret += '_';
	}
	return ret;
}

/**
	@brief Gets the name of a unit in ASCII (some of ours use Greek letters or superscripts), for all HTTP output
 */
static string AsciiUnitName(Unit unit)
{
	string name = unit.ToString();
	name = str_replace("μ", "u", name);
	name = str_replace("Ω", "Ohm", name);
	name = str_replace("°", "deg", name);
	name = str_replace("ρ", "rho", name);
	name = str_replace("²", "^2", name);

	//Anything we didn't anticipate
	for(auto& c : name)
	{
		if(static_cast<unsigned char>(c) >= 0x80)
			c = '?';
	}
	return name;
}

/**
	@brief Gets the unit values are published in, and the factor converting values in our internal unit to it

	Clients shouldn't need to know our internal units, so values are published in SI units where we use a scaled one
	(fs, uHz etc.) and percentages as such (we store them as fractions). Unit names are ASCII only.
 */
static void GetExportUnit(Unit unit, string& name, double& scale)
{
	scale = 1;
	switch(unit.GetType())
	{
		case Unit::UNIT_FS:
			name = "s";
			scale = 1e-15;
			break;

		case Unit::UNIT_PM:
			name = "m";
			scale = 1e-12;
			break;

		case Unit::UNIT_MICROHZ:
			name = "Hz";
			scale = 1e-6;
			break;

		case Unit::UNIT_MILLIVOLTS:
			name = "V";
			scale = 1e-3;
			break;

		case Unit::UNIT_MICROVOLTS:
			name = "V";
			scale = 1e-6;
			break;

		case Unit::UNIT_MICROAMPS:
			name = "A";
			scale = 1e-6;
			break;

		case Unit::UNIT_PERCENT:
			name = "%";
			scale = 100;
			break;

		//These names describe how to display a dimensionless value, not a unit
		case Unit::UNIT_COUNTS:
		case Unit::UNIT_COUNTS_SCI:
		case Unit::UNIT_RATIO_SCI:
			name = "";
			break;

		default:
			name = AsciiUnitName(unit);
			break;
	}
}

static string JsonEscape(const string& s)
{
	string ret;
	for(auto c : s)
	{
		if( (c == '"') || (c == '\\') )
		{
			ret += '\\';
			ret += c;
		}
		else if(static_cast<unsigned char>(c) < 0x20)
		{
			char buf[8];
			snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(c));
			ret += buf;
		}
		else
			ret += c;
	}
	return ret;
}

/**
	@brief Formats a wall clock time as ISO 8601 UTC with millisecond resolution
 */
static string FormatTimestamp(chrono::system_clock::time_point t)
{
	auto tt = chrono::system_clock::to_time_t(t);
	int ms = static_cast<int>(chrono::duration_cast<chrono::milliseconds>(t.time_since_epoch()).count() % 1000);

	tm tmv;
#ifdef _WIN32
	gmtime_s(&tmv, &tt);
#else
	gmtime_r(&tt, &tmv);
#endif

	char buf[64];
	strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tmv);
	char buf2[80];
	snprintf(buf2, sizeof(buf2), "%s.%03dZ", buf, ms);
	return buf2;
}

/**
	@brief Sets a 404 or 504 response from WaitForUpdate() and friends, with a plain text explanation

	@param what		"value" or "waveform"
 */
static void SetErrorResponse(httplib::Response& res, int status, const char* what)
{
	res.status = status;
	if(status == httplib::StatusCode::GatewayTimeout_504)
		res.set_content(string("No new ") + what + " before the timeout (is the scope triggering?)\n", "text/plain");
	else
		res.set_content(string("No such ") + what + ", or none acquired yet\n", "text/plain");
}

/**
	@brief Formats the time since t in seconds with millisecond resolution
 */
static string FormatAge(chrono::system_clock::time_point t, chrono::system_clock::time_point now)
{
	char buf[32];
	snprintf(buf, sizeof(buf), "%.3f", chrono::duration<double>(now - t).count());
	return buf;
}

/**
	@brief Formats a sample for JSON or CSV. Floats need 9 significant digits to round-trip.
 */
static void AppendSample(string& out, float v, const char* nonFinite)
{
	if(!isfinite(v))
	{
		out += nonFinite;
		return;
	}

	char buf[32];
	snprintf(buf, sizeof(buf), "%.9g", v);
	out += buf;
}

/**
	@brief Gets the metadata of a waveform as (name, JSON value) pairs, in the order they're output
 */
static vector<pair<string, string>> WaveformMetadata(const HTTPExportWaveform& wfm)
{
	char scale[32];
	snprintf(scale, sizeof(scale), "%.15g", wfm.m_xScale);

	return
	{
		{"sparse", wfm.m_sparse ? "true" : "false"},
		{"length", to_string(wfm.m_samples.size())},
		{"y_unit", "\"" + JsonEscape(wfm.m_yUnit) + "\""},
		{"x_unit", "\"" + JsonEscape(wfm.m_xUnit) + "\""},
		{"timescale", to_string(wfm.m_timescale)},
		{"trigger_phase", to_string(wfm.m_triggerPhase)},
		{"x_scale", scale},
		{"x_scaled_unit", "\"" + JsonEscape(wfm.m_xUnitScaled) + "\""},
		{"start_timestamp", to_string(static_cast<int64_t>(wfm.m_startTimestamp))},
		{"start_femtoseconds", to_string(wfm.m_startFemtoseconds)}
	};
}

/**
	@brief Formats the metadata and samples of a waveform as JSON object members (without the braces)

	Offsets and durations are raw, in timescale units, so no precision is lost:
	X of sample i = (offset[i] (or i if uniform) * timescale + trigger_phase) * x_scale, in x_scaled_unit.
 */
static string WaveformToJson(const HTTPExportWaveform& wfm)
{
	string ret;
	for(auto& field : WaveformMetadata(wfm))
		ret += "\"" + field.first + "\":" + field.second + ",";

	ret.reserve(ret.size() + wfm.m_samples.size() * (wfm.m_sparse ? 40 : 14));

	ret += "\"samples\":[";
	for(size_t i=0; i<wfm.m_samples.size(); i++)
	{
		if(i)
			ret += ',';
		AppendSample(ret, wfm.m_samples[i], "null");
	}
	ret += "]";

	if(wfm.m_sparse)
	{
		ret += ",\"offsets\":[";
		for(size_t i=0; i<wfm.m_offsets.size(); i++)
		{
			if(i)
				ret += ',';
			ret += to_string(wfm.m_offsets[i]);
		}
		ret += "],\"durations\":[";
		for(size_t i=0; i<wfm.m_durations.size(); i++)
		{
			if(i)
				ret += ',';
			ret += to_string(wfm.m_durations[i]);
		}
		ret += "]";
	}
	return ret;
}

/**
	@brief Formats a waveform as CSV, like CSVExportFilter: X in seconds (or Hz) and the value
 */
static string WaveformToCsv(const HTTPExportWaveform& wfm)
{
	string ret;
	if(wfm.m_xUnitScaled == "s")
		ret = "Time (s)";
	else if(wfm.m_xUnitScaled == "Hz")
		ret = "Frequency (Hz)";
	else
		ret = "X (" + wfm.m_xUnitScaled + ")";
	ret += ",Value (" + wfm.m_yUnit + ")\n";

	ret.reserve(ret.size() + wfm.m_samples.size() * 32);
	for(size_t i=0; i<wfm.m_samples.size(); i++)
	{
		char buf[32];
		snprintf(buf, sizeof(buf), "%.15g,", wfm.GetX(i) * wfm.m_xScale);
		ret += buf;
		AppendSample(ret, wfm.m_samples[i], "NaN");
		ret += '\n';
	}
	return ret;
}

/**
	@brief Builds a NumPy .npy (format version 1.0) header for a waveform
 */
static string NpyHeader(const HTTPExportWaveform& wfm)
{
	string dict = string("{'descr': ") +
		(wfm.m_sparse ? "[('offset', '<i8'), ('duration', '<i8'), ('value', '<f4')]" : "'<f4'") +
		", 'fortran_order': False, 'shape': (" + to_string(wfm.m_samples.size()) + ",), }";

	//Magic, version and length take 10 bytes; the whole header is padded with spaces to a multiple of 64,
	//ending in a newline
	size_t total = (10 + dict.size() + 1 + 63) / 64 * 64;
	dict.append(total - 10 - dict.size() - 1, ' ');
	dict += '\n';

	string ret("\x93NUMPY\x01\x00", 8);
	ret += static_cast<char>(dict.size() & 0xff);
	ret += static_cast<char>(dict.size() >> 8);
	return ret + dict;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// HTTPExportFilter construction / destruction

HTTPExportFilter::HTTPExportFilter(const string& color)
	: Filter(color, CAT_EXPORT)
	, m_registered(false)
	, m_holdingSelfReference(true)
{
	//No output stream

	CreateInput<InputConstraintStreamTypes>(
		"din",
		initializer_list<Stream::StreamType>
		{
			Stream::STREAM_TYPE_ANALOG_SCALAR,
			Stream::STREAM_TYPE_DIGITAL_SCALAR,
			Stream::STREAM_TYPE_ANALOG
		});

	//Like ExportFilter, keep a reference to ourself since nothing downstream will.
	//Released by ReleaseSelfReference() when the session is cleared or the filter is deleted from the graph editor.
	AddRef();
}

HTTPExportFilter::~HTTPExportFilter()
{
	lock_guard<mutex> lock(m_keyMutex);
	if(m_registered)
		HTTPExportServer::Get().Unregister(m_key);
}

/**
	@brief Drops the reference to ourself taken in the constructor, if we still hold it

	Safe to call more than once, e.g. if a deletion attempt fails because something else still holds a reference.
	May delete the filter.
 */
void HTTPExportFilter::ReleaseSelfReference()
{
	if(!m_holdingSelfReference)
		return;
	m_holdingSelfReference = false;
	Release();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string HTTPExportFilter::GetProtocolName()
{
	return "HTTP Export";
}

void HTTPExportFilter::SetDisplayName(string name)
{
	Filter::SetDisplayName(name);
	UpdateRegistration();
}

/**
	@brief Gets the URL of our value, or an error message if it's not available
 */
string HTTPExportFilter::GetEndpointURL()
{
	//The default name is assigned without going through SetDisplayName(), so we might not have registered yet
	UpdateRegistration();

	lock_guard<mutex> lock(m_keyMutex);
	auto& server = HTTPExportServer::Get();

	if(m_key.empty())
		return "";
	if(!m_registered)
		return string("(name \"") + m_key + "\" is already in use by another HTTP Export filter)";

	auto base = server.GetBaseURL();
	if(base.empty())
		return string("(") + server.GetError() + ")";

	auto din = GetInput(0);
	if(din && (din.GetType() == Stream::STREAM_TYPE_ANALOG))
		return base + "/waveforms/" + m_key;
	return base + "/values/" + m_key;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

/**
	@brief Registers our key with the server, or re-registers it if the display name changed
 */
void HTTPExportFilter::UpdateRegistration()
{
	lock_guard<mutex> lock(m_keyMutex);

	auto key = SanitizeKey(GetDisplayName());
	if(key.empty())
		return;
	if( (key == m_key) && m_registered)
		return;

	//Register the new key before dropping the old one, so a rename never leaves the server without keys
	//(which would stop and restart the listener).
	//If this fails because the key is taken, we try again next time.
	auto& server = HTTPExportServer::Get();
	bool registered = server.Register(key);
	if(m_registered)
		server.Unregister(m_key);

	m_key = key;
	m_registered = registered;
}

void HTTPExportFilter::Refresh(
	[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
	[[maybe_unused]] shared_ptr<QueueHandle> queue)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("HTTPExportFilter::Refresh");
	#endif

	ClearMessages();
	UpdateRegistration();

	auto din = GetInput(0);
	if(!din)
	{
		AddErrorMessage("Missing input", "The input is not connected");
		return;
	}

	lock_guard<mutex> lock(m_keyMutex);
	if(!m_registered)
	{
		AddErrorMessage(
			"Duplicate name",
			string("Another HTTP Export filter is already using the name \"") + m_key + "\"");
		return;
	}

	auto& server = HTTPExportServer::Get();

	if(din.GetType() == Stream::STREAM_TYPE_ANALOG)
	{
		PublishWaveform(din);
		return;
	}

	string unit;
	double scale;
	GetExportUnit(din.GetYAxisUnits(), unit, scale);

	double value;
	char text[32];
	if(din.GetType() == Stream::STREAM_TYPE_DIGITAL_SCALAR)
	{
		//Integers are published as-is
		auto v = din.GetDigitalScalarValue();
		value = v;
		snprintf(text, sizeof(text), "%" PRIu64, v);
	}
	else
	{
		value = din.GetScalarValue() * scale;

		//Scalars are doubles; 15 significant digits is the most that round-trips any decimal value.
		//Spell non-finite values the way Prometheus expects.
		if(isnan(value))
			snprintf(text, sizeof(text), "NaN");
		else if(isinf(value))
			snprintf(text, sizeof(text), "%sInf", (value > 0) ? "+" : "-");
		else
			snprintf(text, sizeof(text), "%.15g", value);
	}

	server.Update(m_key, value, text, unit);

	auto err = server.GetError();
	if(!err.empty())
		AddErrorMessage("Server not running", err);
}

/**
	@brief Copies the input waveform to the server, if a client asked for it since the last copy

	The server thread must never see the live waveform (the filter graph reuses buffers), so we give it a snapshot.
	Must be called with m_keyMutex held.
 */
void HTTPExportFilter::PublishWaveform(StreamDescriptor& din)
{
	auto& server = HTTPExportServer::Get();

	auto err = server.GetError();
	if(!err.empty())
		AddErrorMessage("Server not running", err);

	auto data = din.GetData();
	if(!data)
	{
		AddErrorMessage("No data", "The input has no waveform");
		return;
	}

	auto sa = dynamic_cast<SparseAnalogWaveform*>(data);
	auto ua = dynamic_cast<UniformAnalogWaveform*>(data);
	if(!sa && !ua)
	{
		AddErrorMessage("Unsupported waveform", "The input waveform is neither uniform nor sparse analog");
		return;
	}

	if(!server.IsWaveformWanted(m_key))
		return;

	data->PrepareForCpuAccess();

	auto wfm = make_shared<HTTPExportWaveform>();
	wfm->m_timescale = data->m_timescale;
	wfm->m_triggerPhase = data->m_triggerPhase;
	wfm->m_startTimestamp = data->m_startTimestamp;
	wfm->m_startFemtoseconds = data->m_startFemtoseconds;

	//Offsets stay in our internal X unit (integers, no precision lost), clients apply m_xScale
	auto xunit = din.GetXAxisUnits();
	wfm->m_xUnit = AsciiUnitName(xunit);
	GetExportUnit(xunit, wfm->m_xUnitScaled, wfm->m_xScale);

	//Samples are converted like scalars
	double yscale;
	GetExportUnit(din.GetYAxisUnits(), wfm->m_yUnit, yscale);

	size_t len = data->size();
	if(sa)
	{
		wfm->m_sparse = true;
		wfm->m_samples.assign(sa->m_samples.GetCpuPointer(), sa->m_samples.GetCpuPointer() + len);
		wfm->m_offsets.assign(sa->m_offsets.GetCpuPointer(), sa->m_offsets.GetCpuPointer() + len);
		wfm->m_durations.assign(sa->m_durations.GetCpuPointer(), sa->m_durations.GetCpuPointer() + len);
	}
	else
		wfm->m_samples.assign(ua->m_samples.GetCpuPointer(), ua->m_samples.GetCpuPointer() + len);

	if(yscale != 1)
	{
		for(auto& v : wfm->m_samples)
			v *= yscale;
	}

	server.UpdateWaveform(m_key, wfm);
}

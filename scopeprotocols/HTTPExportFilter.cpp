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

#include <cctype>
#include <cinttypes>
#include <cmath>
#include <ctime>

#ifndef _WIN32
#include <unistd.h>
#endif

using namespace std;

//Only characters allowed in keys (so they can be used verbatim in URLs and never need escaping)
static const char* g_keyPattern = "[A-Za-z0-9_-]+";

static string SanitizeKey(const string& name);
static string JsonEscape(const string& s);
static string FormatTimestamp(chrono::system_clock::time_point t);

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// HTTPExportServer

HTTPExportServer::HTTPExportServer()
	: m_host("127.0.0.1")
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
 */
void HTTPExportServer::Configure(const string& host, uint16_t port)
{
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
	e.m_seq ++;
	e.m_updated = chrono::system_clock::now();
}

/**
	@brief Starts the listener. Must be called with m_serverMutex held.
 */
void HTTPExportServer::Start()
{
	m_lastStartAttempt = chrono::steady_clock::now();

	auto server = make_unique<httplib::Server>();

	//We only expect occasional polling, no need for a big pool
	server->new_task_queue = [] { return new httplib::ThreadPool(2, 4); };

	auto allValues = [this](const httplib::Request&, httplib::Response& res)
		{ res.set_content(AllValuesToJson(), "application/json"); };
	server->Get("/", allValues);
	server->Get("/values", allValues);

	server->Get(string("/values/(") + g_keyPattern + ")\\.txt",
		[this](const httplib::Request& req, httplib::Response& res)
		{
			lock_guard<mutex> lock(m_entriesMutex);
			auto it = m_entries.find(req.matches[1]);
			if( (it == m_entries.end()) || it->second.m_text.empty() )
				res.status = httplib::StatusCode::NotFound_404;
			else
				res.set_content(it->second.m_text + "\n", "text/plain");
		});

	server->Get(string("/values/(") + g_keyPattern + ")",
		[this](const httplib::Request& req, httplib::Response& res)
		{
			auto json = ValueToJson(req.matches[1]);
			if(json.empty())
				res.status = httplib::StatusCode::NotFound_404;
			else
				res.set_content(json + "\n", "application/json");
		});

	server->Get("/metrics", [this](const httplib::Request&, httplib::Response& res)
		{ res.set_content(ValuesToPrometheus(), "text/plain; version=0.0.4"); });

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

	m_server->stop();
	if(m_thread.joinable())
		m_thread.join();
	m_server = nullptr;

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

	//JSON has no representation for NaN or infinity
	string value = "null";
	if(!e.m_text.empty() && isfinite(e.m_value))
		value = e.m_text;

	string ret = string("{\"value\":") + value + ",\"unit\":\"" + JsonEscape(e.m_unit) + "\",\"seq\":" + to_string(e.m_seq);
	if(e.m_seq != 0)
	{
		double age = chrono::duration<double>(chrono::system_clock::now() - e.m_updated).count();
		char buf[32];
		snprintf(buf, sizeof(buf), "%.3f", age);
		ret += string(",\"updated\":\"") + FormatTimestamp(e.m_updated) + "\",\"age_s\":" + buf;
	}
	return ret + "}";
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
		if(e.m_seq == 0)
			continue;

		//Prometheus label values use the same escaping as JSON strings, near enough
		values += string("ngscopeclient_value{name=\"") + it.first + "\",unit=\"" + JsonEscape(e.m_unit) + "\"} " +
			e.m_text + "\n";

		char buf[32];
		snprintf(buf, sizeof(buf), "%.3f", chrono::duration<double>(now - e.m_updated).count());
		ages += string("ngscopeclient_value_age_seconds{name=\"") + it.first + "\"} " + buf + "\n";
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
			Stream::STREAM_TYPE_DIGITAL_SCALAR
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

	double value;
	char text[32];
	if(din.GetType() == Stream::STREAM_TYPE_DIGITAL_SCALAR)
	{
		auto v = din.GetDigitalScalarValue();
		value = v;
		snprintf(text, sizeof(text), "%" PRIu64, v);
	}
	else
	{
		value = din.GetScalarValue();

		//Scalars are doubles; 15 significant digits is the most that round-trips any decimal value.
		//Spell non-finite values the way Prometheus expects.
		if(isnan(value))
			snprintf(text, sizeof(text), "NaN");
		else if(isinf(value))
			snprintf(text, sizeof(text), "%sInf", (value > 0) ? "+" : "-");
		else
			snprintf(text, sizeof(text), "%.15g", value);
	}

	auto& server = HTTPExportServer::Get();
	server.Update(m_key, value, text, din.GetYAxisUnits().ToString());

	auto err = server.GetError();
	if(!err.empty())
		AddErrorMessage("Server not running", err);
}

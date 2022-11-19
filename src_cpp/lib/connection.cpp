#include "connection.h"
#include "logger.h"
#include "utils.h" // strtrim
#include <curl/curl.h>
#include <pthread.h>
#include <iostream>
#include <sstream>
#include <string.h>

// Init on program start, destruct on close
struct curl_init {
	curl_init()
	{
		curl_global_init(CURL_GLOBAL_ALL);
	}
	~curl_init()
	{
		curl_global_cleanup();
	}
} CURL_INIT;

Connection *Connection::createHTTP(cstr_t &method, cstr_t &url)
{
	Connection *con = new Connection();
	curl_easy_setopt(con->m_curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
	curl_easy_setopt(con->m_curl, CURLOPT_URL, url.c_str());

	if (method == "GET")
		curl_easy_setopt(con->m_curl, CURLOPT_HTTPGET, 1L);
	else if (method == "POST")
		curl_easy_setopt(con->m_curl, CURLOPT_POST, 1L);
	else if (method == "PUT") {
		curl_easy_setopt(con->m_curl, CURLOPT_CUSTOMREQUEST, method.c_str());
		curl_easy_setopt(con->m_curl, CURLOPT_UPLOAD, 1L);
	} else
		curl_easy_setopt(con->m_curl, CURLOPT_CUSTOMREQUEST, method.c_str());

	curl_easy_setopt(con->m_curl, CURLOPT_NOBODY, 0L);
	curl_easy_setopt(con->m_curl, CURLOPT_USERAGENT, "curl/" LIBCURL_VERSION);
	curl_easy_setopt(con->m_curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(con->m_curl, CURLOPT_PIPEWAIT, 1L);

	curl_easy_setopt(con->m_curl, CURLOPT_WRITEFUNCTION, recvAsyncHTTP);
	curl_easy_setopt(con->m_curl, CURLOPT_WRITEDATA, con);
	curl_easy_setopt(con->m_curl, CURLOPT_READFUNCTION, sendAsyncHTTP);
	curl_easy_setopt(con->m_curl, CURLOPT_READDATA, con);
	//curl_easy_setopt(con->m_curl, CURLOPT_VERBOSE, 1L);
	return con;
}

Connection::Connection()
{
	// Open connection
	m_curl = curl_easy_init();
	ASSERT(m_curl, "CURL init failed");

	curl_easy_setopt(m_curl, CURLOPT_TIMEOUT, CURL_TIMEOUT_MS);
}

Connection::~Connection()
{
	m_connected = false;

	if (m_thread) {
		pthread_join(m_thread, nullptr);
		m_thread = 0;
	}

	if (m_http_headers)
		curl_slist_free_all(m_http_headers);

	// Disconnect
	curl_easy_cleanup(m_curl);
}

void Connection::setHTTP_URL(cstr_t &url)
{
	curl_easy_setopt(m_curl, CURLOPT_URL, url.c_str());
}

void Connection::addHTTP_Header(cstr_t &what)
{
	m_http_headers = curl_slist_append(m_http_headers, what.c_str());
}

void Connection::enqueueHTTP_Send(std::string && data)
{
	MutexLock _(m_send_queue_lock);
	m_send_queue.push(std::move(data));

	/*
	curl_easy_setopt(m_curl, CURLOPT_POSTFIELDSIZE, data.size());
	curl_easy_setopt(m_curl, CURLOPT_POSTFIELDS, data.c_str()); // not copied!
	*/
}

bool Connection::connect()
{
	curl_easy_setopt(m_curl, CURLOPT_HTTPHEADER, m_http_headers);
	CURLcode res = curl_easy_perform(m_curl);

	m_connected = (res == CURLE_OK);
	// CURLE_PARTIAL_FILE is triggered by "HEAD" requests
	if (!m_connected && res != CURLE_PARTIAL_FILE) {
		ERROR("CURL failed: " << curl_easy_strerror(res));
		return false;
	}

	// HTTP connection: the request is already done now.

	// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=67791
	//m_thread = new std::thread(recvAsync, this);
	return m_connected;
}

bool Connection::send(cstr_t &data) const
{
	if (!m_connected) {
		WARN("Connection is dead.");
		return false;
	}

	if (data.size() < 255)
		VERBOSE("<< Sending: " << strtrim(data));
	else
		VERBOSE("<< Sending " << data.size() << " bytes");

	CURLcode res;
	int retries = 0;
	for (size_t sent_total = 0; sent_total < data.size();) {
		if (retries == MAX_SEND_RETRIES) {
			WARN(curl_easy_strerror(res));
			return false;
		}

		size_t nsent = 0;
		res = curl_easy_send(m_curl, &data[sent_total], data.size() - sent_total, &nsent);
		sent_total += nsent;

		if (res == CURLE_OK)
			continue;

		retries++;

		// Try waiting for socket
		if (res == CURLE_AGAIN)
			SLEEP_MS(10);
	}
	return true;
}

long Connection::getHTTP_Status() const
{
	long http_code = 0;
	curl_easy_getinfo(m_curl, CURLINFO_RESPONSE_CODE, &http_code);
	return http_code;
}

std::string Connection::getHTTP_URL() const
{
	char *url = nullptr;
	curl_easy_getinfo(m_curl, CURLINFO_EFFECTIVE_URL, &url);
	return std::string(url);
}

std::string *Connection::popRecv()
{
	MutexLock _(m_recv_queue_lock);
	if (m_recv_queue.size() == 0)
		return nullptr;

	// std::swap to avoid memory copy
	std::string *data = new std::string();
	std::swap(m_recv_queue.front(), *data);
	m_recv_queue.pop();
	return data;
}

std::string *Connection::popAll()
{
	MutexLock _(m_recv_queue_lock);
	if (m_recv_queue.size() == 0)
		return nullptr;

	std::string *data = new std::string();
	data->reserve(m_recv_queue.front().size() * m_recv_queue.size());

	while (!m_recv_queue.empty()) {
		data->append(m_recv_queue.front());
		m_recv_queue.pop();
	}
	return data;
}

size_t Connection::recv(std::string &data)
{
	// Re-use memory wherever possible
	thread_local char buf[RECEIVE_BUFSIZE];

	size_t nread = 0;
	CURLcode res = curl_easy_recv(m_curl, buf, sizeof(buf), &nread);

	if (res != CURLE_OK && res != CURLE_AGAIN)
		WARN(curl_easy_strerror(res));

	if (nread == 0)
		return nread;

	data.append(buf, nread);

	return nread;
}

size_t Connection::recvAsyncHTTP(void *buffer, size_t size, size_t nitems, void *con_p)
{
	Connection *con = (Connection *)con_p;

	MutexLock _(con->m_recv_queue_lock);
	con->m_recv_queue.push(std::string());
	auto &pkt = con->m_recv_queue.back();
	pkt.resize(nitems * size);
	memcpy(&pkt[0], buffer, pkt.size());

	return pkt.size();
}

size_t Connection::sendAsyncHTTP(void *buffer, size_t size, size_t nitems, void *con_p)
{
	Connection *con = (Connection *)con_p;

	MutexLock _(con->m_send_queue_lock);
	if (con->m_send_queue.empty())
		return 0;

	std::string &what = con->m_send_queue.front();

	size_t to_send = std::min(nitems * size, what.size() - con->m_send_index);
	memcpy(buffer, &what[con->m_send_index], to_send);

	con->m_send_index += to_send;
	if (con->m_send_index >= what.size()) {
		con->m_send_queue.pop();
		con->m_send_index = 0;
	}

	return to_send;
}

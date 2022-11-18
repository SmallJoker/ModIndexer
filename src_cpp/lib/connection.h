#pragma once

#include "types.h"
#include <queue>

struct curl_slist;

class Connection {
public:
	static Connection *createHTTP(cstr_t &method, cstr_t &url);

	~Connection();
	DISABLE_COPY(Connection);

	void setHTTP_URL(cstr_t &url);
	void addHTTP_Header(cstr_t &what);
	void enqueueHTTP_Send(std::string && data);
	bool connect();

	bool send(cstr_t &data) const;
	std::string *popRecv();
	std::string *popAll();

private:
	Connection();

	static const unsigned MAX_SEND_RETRIES = 5;
	static const long CURL_TIMEOUT_MS = 5000;
	static const unsigned RECEIVE_BUFSIZE = 1024;

	size_t recv(std::string &data);
	static size_t recvAsyncHTTP(void *buffer, size_t size, size_t nitems, void *con_p);
	static size_t sendAsyncHTTP(void *buffer, size_t size, size_t nitems, void *con_p);

	void *m_curl;
	curl_slist *m_http_headers = nullptr;
	bool m_connected = false;

	// Receive thread
	pthread_t m_thread = 0;
	size_t m_send_index = 0;
	std::queue<std::string> m_recv_queue;
	std::queue<std::string> m_send_queue;
	mutable std::mutex m_recv_queue_lock;
	mutable std::mutex m_send_queue_lock;
};

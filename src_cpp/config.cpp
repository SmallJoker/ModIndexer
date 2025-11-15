#include <fstream>
#include <map>
#include <json/json.h>
#include <queue>

#include "lib/connection.h"
#include "misc.h"
#include "lib/logger.h"

// Processes the queue for upload, leaving an empty queue upon completion.
void upload_changes(std::queue<TopicData> &data)
{
	std::ifstream is("config_url.txt");
	ASSERT(is.good(), "Configuration file not found");
	char url_c[256];
	char key_c[256];
	is.getline(url_c, sizeof(url_c));
	is.getline(key_c, sizeof(key_c));

	Json::Value root;
	root["secure"] = key_c;

	int i = 0;
	std::map<int, std::string> topic_id_to_title;
	while (!data.empty()) {
		auto topic = data.front();

		Json::Value entry;
		entry["topicId"] = topic.topic_id;
		entry["title"] = topic.title;
		entry["userId"] = topic.author_id;
		entry["userName"] = topic.author;
		entry["type"] = (int)topic.type;
		entry["link"] = topic.link;

		root["data"][i++] = entry;

		topic_id_to_title.emplace(topic.topic_id, topic.title);
		data.pop();
	}

	std::string serialized;
	{
		Json::FastWriter wr;
		serialized = wr.write(root);
	}
	VERBOSE("SEND entries. count=" << topic_id_to_title.size());

	std::unique_ptr<Connection> con(Connection::createHTTP("POST", url_c));
	con->send(serialized);
	if (!con->connect(5))
		exit(5);

	// Updated posts
	std::unique_ptr<std::string> out(con->popAll());
	VERBOSE("RECV: " << *out);

	root.clear();
	{
		Json::Reader rd;
		rd.parse(*out, root);
	}

	for (auto v : root)
		LOG("Updated " << topic_id_to_title[v.asInt()]);
}

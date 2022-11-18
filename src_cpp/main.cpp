#include <assert.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <pugixml.hpp>
#include <regex>
#include <string>

#include "misc.h"
#include "lib/connection.h"
#include "lib/logger.h"

Subforum subforum = Subforum::INVALID;


std::string parse_page(pugi::xpath_node_set &nodes, std::string url, std::string xpath)
{
	std::unique_ptr<Connection> con(Connection::createHTTP("GET", url));
	con->connect();

	std::unique_ptr<std::string> text(con->popAll());
	if (!text)
		return "File download failed";

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_buffer_inplace(&(*text)[0], text->size());

	if (!result)
		return std::string("XML parser failed: ") + result.description();

	nodes = doc.select_nodes(xpath.c_str());
	if (nodes.empty())
		return "Cannot find any nodes in " + xpath;

	// Good
	return "";
}

// Remove useless tags from the forum titles
const std::string MODNAME_ALLOWED_CHARS = "abcdefghijklmnopqrstuvwxyz0123456789_";
// Content of [tags]
const std::vector<std::string> bad_content = { "wip", "beta", "test", "code", "indev", "git", "github", "-" };
// Beginnings of [mod-my_doors5] for wrong formatted titles
const std::vector<std::string> bad_prefix = { "minetest", "mod", "mods" };

std::string parse_title(std::string title, std::string *mod_name, DbType *mod_tag)
{
	*mod_tag = DbType::INVALID;
	mod_name->clear();

	size_t pos = 0,
		open_pos = 0;
	bool opened = false,
		delete_tag = false;

	for (; pos < title.size(); pos++) {
		char cur = title[pos];

		if (cur == '[' || cur == '{') {
			opened = true;
			open_pos = pos;
			continue;
		}
		if (cur == ']' || cur == '}') {
			// Tag closed
			opened = false;

			int len = pos - open_pos + 1;
			std::string content = strtrim(title.substr(open_pos + 1, len - 2));
			for (char &c : content)
				c = tolower(c);

			double num = 0.0f;
			bool is_number = sscanf(content.c_str(), "%lf", &num);
			DbType tag = tag_to_rel_dbtype(content);

			if (!is_number && *mod_tag == DbType::INVALID
					&& tag != DbType::INVALID) {
				// Mod tag detected
				*mod_tag = tag;
				delete_tag = true;
			}

			bool is_bad_content = std::find(bad_content.begin(), bad_content.end(), content) != bad_content.end();
			if (delete_tag || is_number
					|| is_bad_content
					|| tag != DbType::INVALID) {

				// Remove this tag
				title = title.erase(open_pos, len);
				pos -= len;
				delete_tag = false;
				continue;
			}

			int start_substr = 0;

			for (auto prefix : bad_prefix) {
				if (content.size() <= start_substr + prefix.size() + 1)
					break;

				// Strip "minetest" prefixes
				if (content.substr(start_substr, prefix.size()) == prefix)
					start_substr += prefix.size();

				// Strip leftover '-', '_'
				char first = content[start_substr];
				if (first == '_' || first == '-')
					start_substr++;
			}

			if (start_substr == 0) {
				// Everything fine, nothing to replace
				*mod_name = content;
			} else {
				// Replace this tag with the proper name
				*mod_name = content.substr(start_substr);
				title = title.erase(open_pos + 1, start_substr);
				pos -= start_substr;
			}

			delete_tag = false;
			continue;
		}

		// Mark [v1.0] tags for deletion
		if (opened && MODNAME_ALLOWED_CHARS.rfind(cur) == std::string::npos) {
			delete_tag = true;
		}
	}

	// Trim double whitespaces
	bool was_space = false;
	pos = 0;

	for (size_t i = 0; i < title.size(); i++) {
		char cur = title[i];
		bool is_space = std::isspace(cur);
		if (was_space && is_space)
			continue;

		if (is_space && i == title.size() - 1)
			continue; // Tailing space

		was_space = is_space;
		title[pos] = cur;
		pos++;
	}

	title.resize(pos);
	return title;
}

void fetch_topic_list(int page)
{
	LOG("Forum " << (int)subforum << " - Page " << page);

	std::stringstream ss;
	ss << "https://forum.minetest.net/viewforum.php?f=" << (int)subforum;
	ss << "&start=" << ((page - 1) * 30);

	pugi::xpath_node_set body_node;
	std::string err = parse_page(body_node,
		 ss.str(), "//div[@class='forumbg']/.//li[contains(@class, 'row')]");

	if (!err.empty()) {
		WARN(err);
		return;
	}

	std::regex regex_topic("t=(\\d+)");
	std::regex regex_author("u=(\\d+)");

	for (auto &topic_node : body_node) {
		{
			// Ignore sticky posts and announcements
			std::string classes = topic_node.node().attribute("class").as_string();
			if (classes.rfind("sticky") != std::string::npos
					|| classes.rfind("announce") != std::string::npos)
				continue;
		}

		TopicData topic;

		{
			// Topic title
			auto title_node = topic_node.node().select_node(".//a[@class='topictitle']");
			topic.title = title_node.node().text().as_string();

			// Topic ID
			std::string link = title_node.node().attribute("href").as_string();
			std::smatch match;
			if (std::regex_match(link, match, regex_topic))
				topic.topic_id = std::atoi(match.str(1).c_str());
		}

		{
			// Author name
			// Links may have the class "username" or "username-colored"
			auto author_node = topic_node.node().select_node(".//div[contains(@class, 'topic-poster')]/a");
			topic.author = author_node.node().text().as_string();

			// Author ID
			std::string link = author_node.node().attribute("href").as_string();
			std::smatch match;
			if (std::regex_match(link, match, regex_author))
				topic.author_id = std::atoi(match.str(1).c_str());
		}

		if (topic.title.size() < 10)
			continue;

		DbType type;
		std::string mod_name;
		topic.title = parse_title(topic.title, &mod_name, &type);

		switch (type) {
		case DbType::REL_MOD:
			switch (subforum) {
			case Subforum::REL_MODS:
				// Ok.
				break;
			case Subforum::WIP_MODS:
				type = DbType::WIP_MOD;
				break;
			case Subforum::OLD_MODS:
				type = DbType::OLD_MOD;
				break;
			default:
				LOG("Found a mod in the wrong forum");
				break;
			}
			break;
		case DbType::REL_MP:
			switch (subforum) {
			case Subforum::REL_MODS:
				// Ok.
				break;
			case Subforum::WIP_MODS:
				type = DbType::WIP_MP;
				break;
			case Subforum::OLD_MODS:
				type = DbType::OLD_MOD;
				break;
			default:
				LOG("Found a modpack in the wrong forum");
				break;
			}
			break;
		case DbType::REL_GAME:
			switch (subforum) {
			case Subforum::REL_GAMES:
				// Ok.
				break;
			case Subforum::WIP_GAMES:
			case Subforum::WIP_MODS:
				type = DbType::WIP_GAME;
				break;
			//case Subforum::OLD_GAMES: // TODO
			case Subforum::OLD_MODS:
				type = DbType::OLD_MOD;
				break;
			default:
				LOG("Found a game in the wrong forum");
				break;
			}
			break;
		case DbType::REL_CSM:
			switch (subforum) {
			case Subforum::CSM_MODS:
				// Ok.
				break;
			case Subforum::WIP_MODS:
				type = DbType::WIP_CSM;
				break;
			case Subforum::OLD_MODS:
				type = DbType::OLD_MOD;
				break;
			default:
				LOG("Found a CSM in the wrong place");
				break;
			}
			break;
		default:
				// OK
				break;
		}

		if (topic.topic_id == 0 || topic.author_id == 0 || topic.author.empty()) {
			ERROR("Invalid topic data");
			return;
		}

		topic.title = escape_xml(topic.title);

		if (type != DbType::INVALID && subforum != Subforum::OLD_MODS) {
			// Fetch topics, get download/source links
			//FetchSingleTopic(mod_name, ref info);
		}

		topic.dump(&std::cout);

		//update_data.Add(info);
	}

}

void unittest()
{
	std::string mod_name;
	DbType type;
	parse_title("[Mod] Farming Redo [1.47] [farming]", &mod_name, &type);
	assert(mod_name == "farming");
	assert(type == DbType::REL_MOD);

	LOG("Unittests passed!");
}


void exit_main()
{
	LOG("Shutting down...");
}

int main()
{
	atexit(exit_main);
	g_logger = new Logger();

	unittest();
    return 0;
}

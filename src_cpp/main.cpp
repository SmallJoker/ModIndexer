#include <assert.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <regex>
#include <string>

#include "misc.h"
#include "lib/connection.h"
#include "lib/html.h"
#include "lib/logger.h"

std::queue<TopicData> new_topic_data;


std::string download_page(HTML &html, std::string url)
{
	std::ifstream is(url); // If file exists
	std::string *text_ptr;
	if (is.good()) {
		VERBOSE("Reading file " << url);
		is.seekg(0, is.end);
		size_t length = is.tellg();
		is.seekg(0, is.beg);

		text_ptr = new std::string(length, '\0');
		is.read(&(*text_ptr)[0], length);
		is.close();
	} else {
		// Download from web
		VERBOSE("Downloading " << url);
		std::unique_ptr<Connection> con(Connection::createHTTP("GET", url));
		con->connect();
		text_ptr = con->popAll();
	}

	std::unique_ptr<std::string> text(text_ptr);
	if (!text)
		return "File download failed";

	//VERBOSE("Got " << *text);

	mystatus_t ok = myhtml_parse(*html, MyENCODING_UTF_8, text->c_str(), text->size());

	if (MyHTML_FAILED(ok))
		return std::string("XML parser failed: ") + std::to_string(ok);

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

void fetch_topic_list(Subforum subforum, int page)
{
	LOG("Forum " << (int)subforum << " - Page " << page);

	std::stringstream ss;
	ss << "https://forum.minetest.net/viewforum.php?f=" << (int)subforum;
	ss << "&start=" << ((page - 1) * 30);

	HTML html;
	std::string err = download_page(html, ss.str());
	// "//div[@class='forumbg']/.//li[contains(@class, 'row')]"

	if (!err.empty()) {
		WARN(err);
		return;
	}

	std::regex regex_topic(".*t=([0-9]+).*");
	std::regex regex_author(".*u=([0-9]+).*");

	auto dl_nodes = html.getNodes(NULL, "dl");
	ASSERT(dl_nodes->length >= 1, "no <dl> found");

	FOR_COLLECTION(dl_nodes, node, {
		if (!html.nodeKeyContains(node, "class", "row-item")
				|| html.nodeKeyContains(node, "class", "global", false)
				|| html.nodeKeyContains(node, "class", "sticky", false)) {
			// Ignore announcements and sticky topics
			
			LOG("Ignored " << html.getAttribute(node, "class"));
			continue;
		}

		TopicData topic;

		{
			// Topic title
			auto title_nodes = html.getNodesByKeyValue(node, "class", "topictitle");
			if (title_nodes->length < 1)
				continue;
	
			auto title_node = title_nodes->list[0];
			topic.title = html.getText(title_node);

			VERBOSE("Title: " << topic.title);

			// Topic ID
			std::string link = html.getAttribute(title_node, "href");
			std::smatch match;
			if (std::regex_match(link, match, regex_topic))
				topic.topic_id = std::atoi(match.str(1).c_str());
			else
				WARN("No match");

			VERBOSE("Link: " << link << " -> id=" << topic.topic_id);
		}

		{
			// Author name
			// Links may have the class "username" or "username-colored"
			auto poster_node = html.getNodesByKeyValue(node, "class", "username", false)->list[0];
			topic.author = html.getText(poster_node);

			// Author ID
			std::string link = html.getAttribute(poster_node, "href");
			std::smatch match;
			if (std::regex_match(link, match, regex_author))
				topic.author_id = std::atoi(match.str(1).c_str());
			else
				WARN("No match");

			VERBOSE("Autor: " << topic.author << " -> id=" << topic.author_id);
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
		// "link" is yet not filled in

		topic.title = escape_xml(topic.title);

		if (type != DbType::INVALID && subforum != Subforum::OLD_MODS) {
			// Fetch topics, get download/source links
			//fetch_single_topic(mod_name, *topic);
		}

		topic.dump(&std::cout);

		new_topic_data.push(topic);
	})
	LOG("Done");
}

void unittest()
{
	g_logger->setLogLevels(LL_VERBOSE);
	{
		// parse_title()
		std::string mod_name;
		DbType type;
		parse_title("[Mod] Farming Redo [1.47] [farming]", &mod_name, &type);
		assert(mod_name == "farming");
		assert(type == DbType::REL_MOD);
	
		parse_title("[Game] Inside The Box SE [0.0.4] (WIP)", &mod_name, &type);
		assert(mod_name.empty());
		assert(type == DbType::REL_GAME);
	}

	if (0) {
		// download_page()
		HTML html;
		std::string err = download_page(html, "https://example.com");
		ASSERT(err.empty(), err);
		auto nodes = html.getNodes(NULL, "head");
		ASSERT(nodes->length == 1, "No heads");
		auto viewports = html.getNodesByKeyValue(nodes->list[0], "name", "viewport");
		ASSERT(viewports->length == 1, "No nodes");
		//html.dump(&std::cout, nodes);
	}

	{
		HTML html;
		std::string err = download_page(html, "viewforum_50.html");
		ASSERT(err.empty(), err);

		// "//div[@class='forumbg']/.//li[contains(@class, 'row')]"
		auto dl_nodes = html.getNodes(NULL, "dl");
		ASSERT(dl_nodes->length >= 1, "no <dl> found");
	
		FOR_COLLECTION(dl_nodes, node, {
			if (html.nodeKeyContains(node, "class", "row-item")) {
				auto topic = html.getNodesByKeyValue(node, "class", "topictitle");
				html.dump(&std::cout, topic);
				auto poster = html.getNodesByKeyValue(node, "class", "username");
				html.dump(&std::cout, poster);
			}
		})
		
		//ASSERT(nodes->length >= 1, "No forumbg");
	}
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
	g_logger->setLogLevels(LL_NORMAL);

	//unittest();
	fetch_topic_list(Subforum::REL_GAMES, 1);
    return 0;
}

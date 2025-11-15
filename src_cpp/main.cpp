#include <assert.h>
#include <fstream>
#include <iostream>
#include <json/json.h>
#include <memory>
#include <regex>

#include "misc.h"
#include "stringops.h"
#include "lib/connection.h"
#include "lib/html.h"
#include "lib/logger.h"

std::queue<TopicData> new_topic_data;
void upload_changes(std::queue<TopicData> &data);


const char *DBTYPE2STR[(int)DbType::INVALID_MAX] = {
	"invalid0",
	"REL mod",
	"REL modpack",
	"WIP mod",
	"WIP modpack",
	"old mod",
	"REL game",
	"WIP game",
	"REL CSM",
	"WIP CSM"
};

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
		if (!con->connect(3))
			return "Page unreachable";
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

bool is_link_reachable(std::string *url)
{
	VERBOSE("Checking " << *url);
	std::unique_ptr<Connection> con(Connection::createHTTP("HEAD", *url));
	if (!con->connect(1))
		return false;

	long status_code = con->getHTTP_Status();
	*url = con->getHTTP_URL();
	VERBOSE("status=" << status_code << ", url=" << *url);

	return status_code == 200;
}

// Analyze topic contents and get link
void fetch_single_topic(TopicData *data)
{
	LOG("Parse tid=" << data->topic_id << ": " << data->title);
	sleep_ms(200);

	std::stringstream ss;
	ss << "https://forum.luanti.org/viewtopic.php?t=" << data->topic_id;

	HTML html;
	std::string err = download_page(html, ss.str());

	if (!err.empty()) {
		// Topic is dead. Remove mod.
		data->type = DbType::INVALID;
		WARN("Dead mod: " << data->modname);
		return;
	}

	// "//div[@class='postbody']"
	// ".//a[@class='postlink']"

	auto post_nodes = html.getNodesByKeyValue(NULL, "class", "postbody");
	auto link_nodes = html.getNodesByKeyValue(post_nodes->list[0], "class", "postlink");

	if (link_nodes->length == 0) {
		LOG("No download links found.");
		return;
	}

	std::string author_l = data->author;
	for (char &c : author_l)
		c = tolower(c);
	std::string title_l = data->title;
	for (char &c : title_l)
		c = tolower(c);


	std::string link;
	int quality = 0; // 0 to 10

	FOR_COLLECTION(link_nodes, link_node, {
		std::string url_raw = html.getAttribute(link_node, "href");
		std::string text = html.getText(link_node);

		if (url_raw.size() < 10 || url_raw == link)
			continue;
		if (!link.empty() && url_raw.rfind(link) != std::string::npos)
			continue; // Already checked

		if (url_raw[url_raw.size() - 1] == '/')
			url_raw = url_raw.erase(url_raw.size() - 1);

		std::string url_new;
		int priority = check_link_pattern(url_raw, &url_new);

		// Weight the link to find the best matching
		std::string url_l = url_new;
		for (char &c : url_l) {
			if (c == '-')
				c = '_';
			else
				c = towlower(c);
		}

		if (!data->modname.empty() && url_l.rfind(data->modname) != std::string::npos)
			priority += 3;
		if (!author_l.empty() && url_l.rfind(author_l) != std::string::npos)
			priority++;
		if (!title_l.empty() && url_l.rfind(title_l) != std::string::npos)
			priority += 10;

		const char *reachable = "unknown";
		if (priority > quality) {
			if (is_link_reachable(&url_new)) {
				// Best link so far. Take it.
				link = url_new;
				quality = priority;
				reachable = "yes";
			} else {
				reachable = "no";
			}
		} else if (priority == quality) {
			link.clear(); // No clear match
		}

		VERBOSE("Link prio="  << priority << ", url=" << url_new << ", reachable=" << reachable);
	})

	// Can't be worse than empty
	data->link = link;
}

void get_dbtype_from_subforum(DbType &type, Subforum subforum)
{
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
}

Subforum get_forum_from_url(cstr_t &url)
{
	const std::string needle = "viewforum.php?f=";
	int forum_id = 0;
	auto pos = url.rfind(needle);
	if (pos == std::string::npos)
		return Subforum::INVALID;

	if (sscanf(url.substr(pos + needle.size()).c_str(), "%i", &forum_id) != 1)
		return Subforum::INVALID;

	return (Subforum)forum_id;
}

void fetch_any_list(Subforum subforum_in, cstr_t &list_url);

void fetch_topic_list(Subforum subforum_in, int page)
{
	LOG("Forum " << (int)subforum_in << " - Page " << page);

	std::stringstream ss;
	ss << "https://forum.luanti.org/viewforum.php?f=" << (int)subforum_in;
	ss << "&start=" << ((page - 1) * 30);
	fetch_any_list(subforum_in, ss.str());
}

void fetch_any_list(Subforum subforum_in, cstr_t &list_url)
{
	HTML html;
	std::string err = download_page(html, list_url);
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
				|| html.nodeKeyContains(node, "class", "sticky", false)
				|| html.nodeKeyContains(node, "class", "announce", false)) {
			// Ignore announcements and sticky topics

			//LOG("Ignored " << html.getAttribute(node, "class"));
			continue;
		}

		Subforum subforum = subforum_in;
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
			// Sometimes there's "display: none" tag floating around....
			auto poster_div_nodes = html.getNodesByKeyValue(node, "class", "topic-poster");
			if (poster_div_nodes->length == 0) {
				// Search result page
				poster_div_nodes = html.getNodesByKeyValue(node, "class", "responsive-hide left-box");
			}

			// Links may have the class "username" or "username-colored"
			auto poster_node = html.getNodesByKeyValue(poster_div_nodes->list[0], "class", "username", false)->list[0];
			topic.author = html.getText(poster_node);

			// Author ID
			std::string link = html.getAttribute(poster_node, "href");
			std::smatch match;
			if (std::regex_match(link, match, regex_author))
				topic.author_id = std::atoi(match.str(1).c_str());
			else
				WARN("No match");

			VERBOSE("Author: " << topic.author << " -> id=" << topic.author_id);

			// Find out the subforum ID if not yet known
			if (subforum == Subforum::INVALID) {
				// Only present in search result pages
				auto link_nodes = html.getNodes(poster_div_nodes->list[0], "a");
				if (link_nodes->length == 2) {
					std::string link = html.getAttribute(link_nodes->list[1], "href");
					subforum = get_forum_from_url(link);
					VERBOSE("Subforum id=" << (int)subforum);
				}
			}
		}

		if (subforum == Subforum::INVALID) {
			ERROR("Unknown forum type!");
			continue;
		}

		if (topic.title.size() < 10)
			continue;

		DbType type;
		topic.title = parse_title(topic.title, &topic.modname, &type);

		get_dbtype_from_subforum(type, subforum);

		if (topic.topic_id == 0 || topic.author_id == 0 || topic.author.empty()) {
			ERROR("Invalid topic data");
			return;
		}

		// "link" is yet not filled in

		topic.type = type;
		topic.title = escape_xml(topic.title);

		if (type != DbType::INVALID && subforum != Subforum::OLD_MODS) {
			// Fetch topics, get download/source links
			fetch_single_topic(&topic);
		}

		// Processing is done. Add to queue.
		if (type == DbType::INVALID) {
			LOG("Ignored: Invalid format");
			continue;
		}

		{
			std::stringstream dump;
			topic.dump(&dump);
			LoggerAssistant(LL_VERBOSE) << dump.str();
		}

		new_topic_data.push(topic);
	})
	LOG("Done");
}

void unittest()
{
	g_logger->setLogLevels(LL_VERBOSE, LL_INVALID);
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

	if (0) {
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

	if (0) {
		std::string url = "https://example.com";
		bool ok = is_link_reachable(&url);
		ASSERT(ok, "Cannot connect");

		// Redirect check
		url = "https://tinyurl.com/phjec9xk";
		ok = is_link_reachable(&url);
		ASSERT(ok, "Cannot connect");
		ASSERT(url == "https://www.youtube.com/watch?v=dQw4w9WgXcQ", "broken link?");

		// Unreachable websites
		url = "https://looolcathost/";
		ok = is_link_reachable(&url);
		ASSERT(!ok, "Website is reachable");
	}

	if (0) {
		TopicData topic;
		topic.topic_id = 9019;
		topic.modname = "farming";
		fetch_single_topic(&topic);
		ASSERT(topic.link == "https://notabug.org/TenPlus1/Farming", "wrong link matched");

		topic.topic_id = 9196;
		topic.author = "VanessaE";
		topic.title = "Dreambuilder";
		topic.modname = "";
		fetch_single_topic( &topic);
		ASSERT(topic.link.empty() || topic.link == "https://github.com/mt-mods/dreambuilder_game", "wrong link matched");
	}

	if (0) {
		TopicData topic;
		topic.topic_id = 13700;
		topic.modname = "aftermath";
		fetch_single_topic(&topic);
		LOG("Found link: " << topic.link);
		ASSERT(topic.link == "https://github.com/maikerumine/aftermath", "wrong link matched");
	}

	if (0) {
		// Stray link with no "href" text
		TopicData topic;
		topic.topic_id = 25597;
		topic.modname = "stripped_tree";
		fetch_single_topic(&topic);
	}

	LOG("Unittests passed!");
}

void exit_main()
{
	LOG("Shutting down...");
}

int main(int argc, char *argv[])
{
	atexit(exit_main);
	g_logger = new Logger();
	g_logger->setLogLevels(LL_NORMAL, LL_NORMAL);

	if (argc >= 2 && strcmp(argv[1], "--unittest") == 0) {
		unittest();

		//fetch_topic_list(Subforum::REL_GAMES, 1);
		//upload_changes(new_topic_data);
		return 0;
	}

	if (argc >= 2 && strequalsi(argv[1], "auto")) {
		// Automatic parsing
		const struct {
			Subforum subforum;
			int page_start;
			int page_end;
		} subforums[] = {
			{ Subforum::REL_MODS,  1, 5 },
			{ Subforum::WIP_MODS,  1, 7 },
			{ Subforum::REL_GAMES, 1, 2 },
			{ Subforum::WIP_GAMES, 1, 2 },
			{ Subforum::OLD_MODS,  1, 1 },
			{ Subforum::CSM_MODS,  1, 1 },
		};
		for (auto &e : subforums) {
			LOG("Parsing " << (int)e.subforum << " pages " << e.page_start << " -> " << e.page_end);
			for (int i = e.page_start; i <= e.page_end; ++i) {
				LOG("Page " << i << " / " << e.page_end);
				fetch_topic_list(e.subforum, i);
			}
			upload_changes(new_topic_data);
		}
		return 0;
	}

	if (argc >= 3 && strequalsi(argv[1], "fromlisting")) {
		LOG("Parsing " << argv[2]);
		fetch_any_list(Subforum::INVALID, argv[2]);
		upload_changes(new_topic_data);
		return 0;
	}

	if (argc >= 4) {
		// 1: subforum
		// 2: page start
		// 3: page end

		const struct {
			Subforum subforum;
			const char *name;
		} subforums[] = {
			{ Subforum::REL_MODS,  "rel mods" },
			{ Subforum::WIP_MODS,  "wip mods" },
			{ Subforum::REL_GAMES, "rel games" },
			{ Subforum::WIP_GAMES, "wip games" },
			{ Subforum::OLD_MODS,  "old mods" },
			{ Subforum::CSM_MODS,  "csm" },
		};

		Subforum subforum = Subforum::INVALID;
		for (auto v : subforums) {
			if (strequalsi(argv[1], v.name)) {
				subforum = v.subforum;
				break;
			}
		}
		ASSERT(subforum != Subforum::INVALID, "Unknown subforum name: " << argv[1]);

		int page_start = 0;
		sscanf(argv[2], "%i", &page_start);
		page_start = std::max(1, page_start);

		int page_end = page_start;
		sscanf(argv[3], "%i", &page_end);
		page_end = std::max(page_start, page_end);

		LOG("Parsing " << (int)subforum << " pages " << page_start << " -> " << page_end);
		for (int i = page_start; i <= page_end; ++i) {
			LOG("Page " << i << " / " << page_end);
			fetch_topic_list(subforum, i);
		}
		upload_changes(new_topic_data);
	}

    return 0;
}

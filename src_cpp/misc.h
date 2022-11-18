#pragma once

#include <sstream>
#include "lib/utils.h"


enum class Subforum : int {
	INVALID = 0,
	REL_MODS  = 11,
	WIP_MODS  =  9,
	REL_GAMES = 15,
	WIP_GAMES = 50,
	OLD_MODS  = 13,
	CSM_MODS  = 53
};

enum class DbType : int {
	INVALID  = 0,
	REL_MOD  = 1,
	REL_MP   = 2,
	WIP_MOD  = 3,
	WIP_MP   = 4,
	OLD_MOD  = 5,
	REL_GAME = 6,
	WIP_GAME = 7,
	REL_CSM  = 8,
	WIP_CSM  = 9
};

struct TopicData {
	int topic_id = 0, author_id = 0;
	DbType type = DbType::INVALID;
	std::string title, author, link;

	void dump(std::ostream *os)
	{
		*os
			<< " * Topic (tid=" << topic_id << "): \"" << title << "\" by " << author << " (uid=" << author_id << ")"
			<< "\n   Link: " << link
			<< "\n   Type: " << (int)type
			<< std::endl;
	}
};

static DbType tag_to_rel_dbtype(std::string text)
{
	const struct Entry {
		const char *text;
		const DbType type;
	} LUT[] = {
		{ "mod", DbType::REL_MOD },
		{ "mod pack", DbType::REL_MP },
		{ "modpack", DbType::REL_MP },
		{ "game", DbType::REL_GAME },
		{ "csm", DbType::REL_CSM },
		{ "clientmod", DbType::REL_CSM },
		{ "client mod", DbType::REL_CSM },
		{ "(client)mod", DbType::REL_CSM },
		{ "client-side mod", DbType::REL_CSM }
	};
	for (char &c : text)
		c = tolower(c);

	// old* -> mark as old mod
	if (text.rfind("old") == 0)
		return DbType::OLD_MOD;

	for (auto e : LUT) {
		if (text == e.text)
			return e.type;
	}

	return DbType::INVALID;
}

// Convert special characters to HTML code
static std::string escape_xml(std::string text)
{
	const char      fromChr[] = { '"', '\'', '\\', '{', '}', '|', '%', ':', '<', '>' };
	const std::string toStr[] = { "&quot;", "&#39;", "&#92;", "&#123;", "&#125;", "&#124;", "&#37;", "&#58;", "&lt;", "&gt;" };
	std::stringstream ss;

	bool was_space = true;
	for (char cur : text) {
		bool is_space = std::isspace(cur);
		if (was_space && is_space)
			continue; // trim repeated spaces

		// Cut off non-ASCII
		if ((int)cur > 0xFF)
			continue;

		bool found = false;
		for (size_t k = 0; k < sizeof(fromChr); k++) {
			if (cur == fromChr[k]) {
				ss << toStr[k];
				found = true;
				break;
			}
		}

		was_space = is_space;
		if (!found)
			ss << cur;
	}

	return ss.str();
}

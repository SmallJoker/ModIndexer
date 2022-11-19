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
	WIP_CSM  = 9,
	INVALID_MAX
};

extern const char *DBTYPE2STR[(int)DbType::INVALID_MAX];

struct TopicData {
	int topic_id = 0, author_id = 0;
	DbType type = DbType::INVALID;
	std::string title, author, link;

	void dump(std::ostream *os)
	{
		*os
			<< " * Topic (tid=" << topic_id << "): \"" << title << "\" by " << author << " (uid=" << author_id << ")"
			<< "\n   Link: " << link
			<< "\n   Type: " << DBTYPE2STR[(int)type]
			<< std::endl;
	}
};

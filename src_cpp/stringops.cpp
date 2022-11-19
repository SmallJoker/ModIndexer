#include "stringops.h"
#include <vector>
#include <regex>

DbType tag_to_rel_dbtype(std::string text)
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
	bool was_space = true;
	pos = 0;

	for (size_t i = 0; i < title.size(); i++) {
		char cur = title[i];
		bool is_space = std::isspace(cur);
		if (was_space && is_space)
			continue;

		was_space = is_space;
		title[pos] = cur;
		pos++;
	}

	title.resize(pos);
	return strtrim(title); // Get rid of tailing spaces
}


int check_link_pattern(cstr_t &url_raw, std::string *url_new)
{
	static const std::regex patterns[] = {
		// GitHub / NotABug / CodeBerg
		std::regex(R"(^(https?://(www\.)?(github\.com|notabug\.org|codeberg\.org)(/[\w_.-]*){2})(/?$|\.git$|/archive/*|/zipball/*))"),
		// GitLab
		std::regex(R"(^(https?://(www\.)?gitlab\.com(/[\w_.-]*){2})(/?$|\.git$|/repository/*))"),
		// BitBucket
		std::regex(R"(^(https?://(www\.)?bitbucket.org(/[\w_.-]*){2})(/?$|\.git$|/get/*|/downloads/*))"),
		// repo.or.cz
		std::regex(R"(^(https?://repo\.or\.cz/[\w_.-]*\.git)(/?$|/snapshot/*))"),
		// git.gpcf.eu (why can't you just be normal?)
		std::regex(R"(^(https?://git\.gpcf\.eu/\?p=[\w_.-]*.git)(\;a=snapshot*)?)"),
		// git.minetest.land
		std::regex(R"(^(https?://git\.minetest\.land(/[\w_.-]*){2})(/?$|\.git$|/archive/*))")
	};

	// Convert attachment links to proper ones
	// Ignore forum attachments. They're evil and hard to check.
	//if (url_raw.StartsWith("./download/file.php?id="))
	//	url_raw = url_raw.Replace(".", "https://forum.minetest.net");

	for (const std::regex &regex : patterns) {
		std::smatch match;
		if (!std::regex_match(url_raw, match, regex))
			continue;

		*url_new = match.str(1);
		return 10;
	}

	*url_new = url_raw;
	return -10; // None matches
}


// Convert special characters to HTML code
std::string escape_xml(std::string text)
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

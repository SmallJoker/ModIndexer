#pragma once

#include "lib/types.h"
#include "misc.h"

std::string parse_title(std::string title, std::string *mod_name, DbType *mod_tag);

// Search for Git URLs and return a weight value
int check_link_pattern(cstr_t &url_raw, std::string *url_new);

std::string escape_xml(std::string text);

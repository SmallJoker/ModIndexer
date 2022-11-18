#pragma once

#include <string>
#include <vector>

std::string strtrim(const std::string &str);

// Case-insentitive string compare
bool strequalsi(const std::string &a, const std::string &b);
// Case-insensitive string search
size_t strfindi(std::string haystack, std::string needle);


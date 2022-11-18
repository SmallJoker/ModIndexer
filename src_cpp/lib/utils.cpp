#include "utils.h"
#include <string.h>

// Shameless copy
std::string strtrim(const std::string &str)
{
	size_t front = 0;

	while (std::isspace(str[front]))
		++front;

	size_t back = str.size();
	while (back > front && std::isspace(str[back - 1]))
		--back;

	return str.substr(front, back - front);
}

bool strequalsi(const std::string &a, const std::string &b)
{
	if (a.size() != b.size())
		return false;
	for (size_t i = 0; i < a.size(); ++i)
		if (tolower(a[i]) != tolower(b[i]))
			return false;
	return true;
}

size_t strfindi(std::string haystack, std::string needle)
{
	for (char &c : haystack)
		c = tolower(c);
	for (char &c : needle)
		c = tolower(c);
	return haystack.rfind(needle);
}

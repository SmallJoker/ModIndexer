#include "html.h"
#include "logger.h"

HTML::HTML()
{
	m_html = myhtml_create();
	myhtml_init(m_html, MyHTML_OPTIONS_DEFAULT, 1, 0);

	m_tree = myhtml_tree_create();
	myhtml_tree_init(m_tree, m_html);
}

HTML::~HTML()
{
	for (auto nodes : collections)
		myhtml_collection_destroy(nodes);

	myhtml_tree_destroy(m_tree);
	myhtml_destroy(m_html);
}

#define WARN_ON_NOK(status) \
	if ((status) != myhtml_status::MyHTML_STATUS_OK) { \
		WARN("Status code: " << (status)); \
	}

myhtml_collection_t *HTML::getNodes(myhtml_tree_node_t *parent, cstr_t &name)
{
	mystatus_t status = 0;
	myhtml_collection_t *nodes;
	if (!parent)
		parent = getRoot();

	// This function is recursive. The other non-scope is not..
	nodes = myhtml_get_nodes_by_name_in_scope(
		m_tree, NULL, parent, name.c_str(), name.size(), &status);

	WARN_ON_NOK(status)

	collections.push_back(nodes);
	return nodes;
}

myhtml_collection_t *HTML::getNodesByKeyValue(myhtml_tree_node_t *parent, cstr_t &key, cstr_t &val, bool separated)
{
	mystatus_t status = 0;
	myhtml_collection_t *nodes;

	nodes = (separated
		? myhtml_get_nodes_by_attribute_value_whitespace_separated
		: myhtml_get_nodes_by_attribute_value_contain)
		(m_tree, NULL, parent, false, key.c_str(), key.size(), val.c_str(), val.size(), &status);

	WARN_ON_NOK(status)

	collections.push_back(nodes);
	return nodes;
}


// Unfortunately it is not exposed
extern "C" {
bool myhtml_get_nodes_by_attribute_value_recursion_whitespace_separated(mycore_string_t* str, const char* value, size_t value_len);
}

bool HTML::nodeKeyContains(myhtml_tree_node_t *node, cstr_t &key, cstr_t &val, bool separated)
{
	auto value = myhtml_attribute_by_key(node, key.c_str(), key.size());
	if (!value)
		return false;

	auto str = myhtml_attribute_value_string(value);
	if (!str)
		return false;

	if (separated)
		return myhtml_get_nodes_by_attribute_value_recursion_whitespace_separated(
			str, val.c_str(), val.size());

	// Fuzzy string contains
	return strstr(str->data, val.c_str());
}


std::string HTML::getAttribute(myhtml_tree_node_t *node, cstr_t &key)
{
	std::string out;
	
	auto value = myhtml_attribute_by_key(node, key.c_str(), key.size());
	if (!value)
		return out;

	auto str = myhtml_attribute_value_string(value);
	if (str && str->data)
		out = str->data;

	return out;
}


std::string HTML::getText(myhtml_tree_node_t *node)
{
	if (!node)
		return "";

	auto str = myhtml_node_string(node);
	if (str && str->data)
		return str->data;

	// "-text" child
	return getText(myhtml_node_child(node));
}


void HTML::dump_node(std::ostream *os, myhtml_tree_node_t *node, int depth)
{
	for (int i = 0; i < depth; i++)
		*os << "\t";

	myhtml_tag_id_t tag_id = myhtml_node_tag_id(node);
	const char *tag = myhtml_tag_name_by_id(m_tree, tag_id, NULL);

	*os << '<' << (tag ? tag : "ERROR");

	// Attribute dump
	myhtml_tree_attr_t *attr = myhtml_node_attribute_first(node);
	while (attr) {
		const char *name = myhtml_attribute_key(attr, NULL);
		if (name) {
			*os << ' ' << name;

			const char *value = myhtml_attribute_value(attr, NULL);
			if (value)
				*os << "=\"" << value << '"';
		}
		attr = myhtml_attribute_next(attr);
	}

	if (myhtml_node_is_close_self(node))
		*os << " /";
	*os << '>';

	const char *text = myhtml_node_text(node, NULL);
	if (text)
		*os << " Text=" << text;

	*os << '\n';

	dump(os, myhtml_node_child(node), depth + 1);
}


void HTML::dump(std::ostream *os, myhtml_collection_t *nodes, int depth)
{
	if (depth == 1)
		*os << "Dump start collection " << nodes->length << "\n";

	for (size_t i = 0; i < nodes->length; ++i) {
		dump_node(os, nodes->list[i], depth);
	}

	if (depth == 1)
		*os << "Dump end" << std::endl;
}

void HTML::dump(std::ostream *os, myhtml_tree_node_t *node, int depth)
{
	if (depth == 1)
		*os << "Dump start nodes\n";

	if (node)
		dump_node(os, node, depth);

	if (depth == 1)
		*os << "Dump end" << std::endl;
}

myhtml_tree_node_t *HTML::getRoot()
{
	return myhtml_tree_get_document(m_tree);
}

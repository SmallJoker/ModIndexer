#pragma once

#include <list>
#include "types.h"
#include "myhtml/api.h"

#define FOR_COLLECTION(mylist, var, mycode) \
	for (size_t i_##var = 0; i_##var < (mylist)->length; ++i_##var) { \
		auto var = (mylist)->list[i_##var]; \
		mycode \
	}

class HTML {
public:
	HTML();
	~HTML();

	myhtml_collection_t *getNodes(myhtml_tree_node_t *parent, cstr_t &name);
	myhtml_collection_t *getNodesByKeyValue(myhtml_tree_node_t *parent, cstr_t &tag, cstr_t &val, bool separated = true);
	bool nodeKeyContains(myhtml_tree_node_t *node, cstr_t &key, cstr_t &val, bool separated = true);
	std::string getAttribute(myhtml_tree_node_t *node, cstr_t &key);
	std::string getText(myhtml_tree_node_t *node);

	void dump(std::ostream *os, myhtml_collection_t *nodes, int depth = 1);
	void dump(std::ostream *os, myhtml_tree_node_t *node, int depth = 1);
	void track(myhtml_collection_t *ptr) { collections.push_back(ptr); }

	myhtml_tree_t *operator*() { return m_tree; }
	myhtml_tree_node_t *getRoot();
private:
	void dump_node(std::ostream *os, myhtml_tree_node_t *node, int depth);

	myhtml_t *m_html;
	myhtml_tree_t *m_tree;
	std::list<myhtml_collection_t *> collections;
};

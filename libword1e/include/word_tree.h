#pragma once

#include <word.h>

typedef struct word_node {
	char letter;
	struct word_node *right, *down;
} WordNode;

WordNode *word_tree_from_list(void);
int word_tree_count(WordNode *tree, const Know *know);

#include <word_tree.h>

#include <stdlib.h>
#include <string.h>

static int
preempt_node_count(const Word *words, int num_words, int *offset, int pos, char *pfx)
{
	if (pos >= 5) {
		++(*offset);
		return 0;
	}

	int res = 0;
	while (*offset < num_words) {
		bool pfx_correct = true;
		for (int i = 0; i < pos; ++i) {
			if (words[*offset].letters[i] != pfx[i]) {
				pfx_correct = false;
				break;
			}
		}

		if (!pfx_correct)
			break;

		pfx[pos] = words[*offset].letters[pos];
		res += 1 + preempt_node_count(words, num_words, offset, pos + 1, pfx);
	}

	return res;
}

static WordNode *
assign_nodes(WordNode **pool, const Word *words, int num_words, int *offset, int pos, char *pfx)
{
	if (pos >= 5) {
		++(*offset);
		return NULL;
	}

	WordNode *res = NULL, **prev_next = &res;
	while (*offset < num_words) {
		bool pfx_correct = true;
		for (int i = 0; i < pos; ++i) {
			if (words[*offset].letters[i] != pfx[i]) {
				pfx_correct = false;
				break;
			}
		}

		if (!pfx_correct)
			break;

		WordNode *node = (*pool)++;
		node->letter = pfx[pos] = words[*offset].letters[pos];
		node->down = NULL;
		node->right = assign_nodes(pool, words, num_words, offset, pos + 1, pfx);
		*prev_next = node;
		prev_next = &node->down;
	}

	return res;
}

int
word_cmp(const void *a, const void *b)
{
	char w_a[6] = { 0 };
	char w_b[6] = { 0 };

	memcpy(w_a, ((Word *)a)->letters, 5);
	memcpy(w_b, ((Word *)b)->letters, 5);

	return strcmp(w_a, w_b);
}

WordNode *
word_tree_from_list(void)
{
	extern Word *opts;
	extern int num_opts;

	size_t s = sizeof(Word) * num_opts;
	Word *sorted_words = malloc(s);
	memcpy(sorted_words, opts, s);
	qsort(sorted_words, num_opts, sizeof(Word), word_cmp);

	char pfx[5];
	int dummy = 0;
	memset(pfx, 0, sizeof(pfx));
	int node_count = preempt_node_count(sorted_words, num_opts, &dummy, 0, pfx);
	WordNode *pool = malloc(sizeof(WordNode) * node_count);

	dummy = 0;
	memset(pfx, 0, sizeof(pfx));
	return assign_nodes(&pool, sorted_words, num_opts, &dummy, 0, pfx);
}

static int
counter(WordNode *tree, Know know, int pos)
{
	if (tree == NULL)
		return know.hist[0] == 0 && know.hist[1] == 0;

	int left_to_use = __builtin_popcount(know.hist[0]) + __builtin_popcount(know.hist[1]);
	int pos_left = 5 - pos;
	if (left_to_use > pos_left)
		return 0;

	int res = 0;
	for (; tree != NULL; tree = tree->down) {
		char l = tree->letter;
		if (0 != (know.exclude[pos] & letter_bit(l)))
			continue;

		Know k = know;
		hist_remove_letter(k.hist, l);
		res += counter(tree->right, k, pos + 1);
	}

	return res;
}

int
word_tree_count(WordNode *tree, const Know *know)
{
	return counter(tree, *know, 0);
}

/*static WordNode *
filter(WordNode *node, Know know, int pos)
{
	int left_to_use = __builtin_popcount(know.hist[0]) + __builtin_popcount(know.hist[1]);
	int pos_left = 5 - pos;
	if (left_to_use > pos_left)
		return NULL;

	WordNode **node_p = &node;
	for (; node != NULL; node = *node_p) {
		if (0 != (know.exclude[pos] & letter_bit(node->letter))) {
			*node_p = node->down;
			continue;
		}

		char l = node->letter;
		hist_remove_letter(know.hist, l);
		WordNode *new_right = filter(node->right, know, pos + 1);
		if (new_right == NULL) {
			*node_p = node->down;
		} else {
			node->right = new_right;
			node_p = &node->down;
		}
	}

	return node;
}

static void
filter(WordNode **node_p, Know know, int pos)
{
	WordNode *node = *node_p;
	if (node == NULL) {
		if (know.hist[0] != 0 || know.hist[1] != 0)
			*node_p = (*node_p)->down;

		return;
	}

	int left_to_use = __builtin_popcount(know.hist[0]) + __builtin_popcount(know.hist[1]);
	int pos_left = 5 - pos;
	if (left_to_use > pos_left)
		return next;

	WordNode **node_ref = &node, *tree;
	while ((tree = *node_ref) != NULL) {
		char l = tree->letter;
		if (0 != (know.exclude[pos] & letter_bit(l)))
			continue;

		Know k = know;
		hist_remove_letter(k.hist, l);
		*node_ref = filter(tree, tree->down, k, pos + 1);
		node_ref = &tree->down;
	}

	return node;
}

void
filter_word_tree(WordNode **tree, const Know *know)
{
	*tree = filter(*tree, (*tree)->down, *know, 0);
}*/

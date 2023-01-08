#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

typedef char Word[5];
typedef uint32_t Word32[5];
typedef struct {
	Word32 maybe;
	/* position unknown, but must contain */
	Word musthave;
} Know;

typedef struct {
	enum { GREEN, YELLOW, BLACK } colors[5];
} WordColor;

typedef struct guess_node {
	Word guess;
	struct guess_node *left, *down;
} GuessTreeNode;

typedef struct {
	char fst, snd, repr;
} Digraph;

int scan_word(FILE *f, Word out);
void load_words(FILE *f);
Know no_knowledge(void);

static inline uint32_t
letter_bit(char letter)
{
	return 1 << (letter - 'A');
}

static inline char
bit_letter(uint32_t bit)
{
	return __builtin_ctz(bit) + 'A';
}

bool word_matches(Word word, Know know);
int num_opts_with_knowledge(Know know);
void filter_opts(Know know);
Know gather_knowledge_col(Word guess, Word word, WordColor *out);
Know gather_knowledge(Word guess, Word word);
Know combine_knowledge(Know a, Know b);
void print_know(Know k);
void print_wordch(FILE *f, char ch, char nxt);
void print_word(FILE *f, Word word);
double score_guess(Word guess, Know know, double break_at);
double best_guesses(Word *top, int max_out, int *num_out, Know know);

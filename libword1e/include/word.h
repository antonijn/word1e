#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#include <hist.h>

typedef struct {
	char letters[5];
	Histogram hist;
} Word;

typedef struct {
	uint32_t exclude[5];
	Histogram hist;
} Know;

typedef struct {
	double starting_score;
	enum {
		WA_TARGET   = 0x1, /* may be a target word */
		WA_EXPLICIT = 0x2, /* word may be considered explicit */
		WA_SLUR     = 0x4, /* word is a slur */
	} flags;
} WordAttr;

#define no_knowledge() ((Know){ 0 })

#define DARK_COLOR   0
#define GREEN_COLOR  1
#define YELLOW_COLOR 2

typedef uint8_t WordColor[5];

typedef struct {
	char fst, snd, repr;
} Digraph;

enum option_catalog {
	OC_NONE,
	OC_TARGET,
	OC_ALL,
};

extern Word *all_words, *opts;
extern Digraph *digraphs;
extern WordAttr *word_attrs;
extern int num_opts, num_words, verbosity, num_digraphs;
extern enum option_catalog opt_catalog;

int scan_word(FILE *f, Word *out);
ssize_t load_words(FILE *f, Word **words_out);
int load_index(FILE *f);

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

int index_of_word(const Word *word);
bool has_no_knowledge(const Know *know);
bool word_matches(const Word *word, const Know *know);
void filter_opts(const Know *know);
int update_opts(const Know *know);
bool all_green(WordColor wc);
void compare_to_target(WordColor out, const Word *guess, const Word *target);
int knowledge_from_colors(Know *know, const Word *guess, WordColor colors);
int absorb_knowledge(Know *know, const Know *other);
void print_know(const Know *k);
void print_wordch(FILE *f, char ch, char nxt);
void print_word(FILE *f, const Word *word);

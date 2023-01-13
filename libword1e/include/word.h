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

#define no_knowledge() ((Know){ 0 })

#define DARK_COLOR   0
#define GREEN_COLOR  1
#define YELLOW_COLOR 2

typedef uint8_t WordColor[5];

typedef struct {
	char fst, snd, repr;
} Digraph;

extern Word *all_words, *opts;
extern Digraph *digraphs;
extern double *initial_scores;
extern int num_opts, num_words, verbosity, num_digraphs;

int scan_word(FILE *f, Word *out);
void load_words(FILE *f);

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

bool word_matches(const Word *word, const Know *know);
void filter_opts(const Know *know);
void compare_to_target(WordColor out, const Word *guess, const Word *target);
int knowledge_from_colors(Know *know, const Word *guess, WordColor colors);
int absorb_knowledge(Know *know, const Know *other);
void print_know(const Know *k);
void print_wordch(FILE *f, char ch, char nxt);
void print_word(FILE *f, const Word *word);

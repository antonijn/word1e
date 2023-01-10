#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

typedef char Word[5];
typedef uint64_t Histogram[2];
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

int scan_word(FILE *f, Word out);
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

//void get_word_hist(Histogram hist, Word word);
bool word_matches(Word word, const Know *know);
int count_opts(const Know *know);
void filter_opts(const Know *know);
void compare_to_target(WordColor out, Word guess, Word target);
int knowledge_from_colors(Know *know, Word guess, WordColor colors);
int absorb_knowledge(Know *know, const Know *other);
void print_know(const Know *k);
void print_wordch(FILE *f, char ch, char nxt);
void print_word(FILE *f, Word word);
double score_guess(Word guess, const Know *know, double break_at);
double best_guesses(Word *top, int max_out, int *num_out, const Know *know);

/*
 * Tools for making educated word1e guesses.
 * Copyright (C) {year} {fullname}
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
 * USA
 */

#define _GNU_SOURCE

#include "guess.h"
#include "threadpool.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include <sched.h>

#define MAX_WORKERS 64

int
hist_count(const Histogram hist, char letter)
{
	uint64_t l = letter - 'A';
	int idx = l >> 4;
	uint64_t shift = (l & 0xf) * 4;
	uint64_t mask = (uint64_t)0xf << shift;
	return __builtin_popcountll(hist[idx] & mask);
}

static inline void
hist_add_letter(Histogram hist, char letter)
{
	uint64_t l = letter - 'A';
	int idx = l >> 4;
	uint64_t shift = (l & 0xf) * 4;
	uint64_t mask = (uint64_t)0xf << shift;
	hist[idx] |= (((hist[idx] & mask) << 1) & mask) | ((uint64_t)1 << shift);
}

static int
scan_letter(FILE *f)
{
	extern Digraph *digraphs;
	extern int num_digraphs;

	int ch;
	while ((ch = fgetc(f)) == '-')
		;

	if (ch == EOF)
		return EOF;

	ch = toupper(ch);

	for (int i = 0; i < num_digraphs; ++i) {
		if (digraphs[i].fst == ch) {
			int snd = fgetc(f);
			if (snd != EOF)
				snd = toupper(snd);

			if (snd == digraphs[i].snd)
				ch = digraphs[i].repr;
			else
				ungetc(snd, f);
			break;
		}
	}

	return ch;
}

int
scan_word(FILE *f, Word *out)
{
	memset(out, 0, sizeof(Word));
	for (int i = 0; i < 5; ++i) {
		int ch = scan_letter(f);
		if (ch < 0)
			return -1;
		out->letters[i] = ch;
		hist_add_letter(out->hist, ch);
	}

	return 0;
}

void
load_words(FILE *f)
{
	extern Word *all_words, *opts;
	extern double *initial_scores;
	extern Digraph *digraphs;
	extern int num_words, num_opts, num_digraphs, verbosity;

	int line = 1;

	if (fscanf(f, "%d\n", &num_words) != 1) {
		fprintf(stderr, "error: expected word count on line 1\n");
		exit(1);
	}
	++line;

	int ch;
	while ((ch = fgetc(f)) == '#') {
		char lnbuf[256];
		if (!fgets(lnbuf, sizeof(lnbuf), f)) {
			fprintf(stderr, "error: unexpected eof on line %d\n", line);
			exit(1);
		}

		if (!strcmp(lnbuf, "INDEXED\n")) {
			initial_scores = malloc(sizeof(initial_scores[0]) * num_words);
		} else if (!strncmp(lnbuf, "DIGRAPH ", 8)) {
			if (num_digraphs >= 32 - 26) {
				fprintf(stderr, "error: too many digraphs\n");
				exit(1);
			}

			char fst, snd;
			if (sscanf(lnbuf + 8, "%c%c\n", &fst, &snd) != 2 || !isalpha(fst) || !isalpha(snd)) {
				fprintf(stderr, "error: expected two characters after #DIGRAPH\n");
				exit(1);
			}

			++num_digraphs;
			digraphs = realloc(digraphs, sizeof(digraphs[0]) * num_digraphs);
			Digraph *di = &digraphs[num_digraphs - 1];
			di->fst = toupper(fst);
			di->snd = toupper(snd);
			di->repr = 'Z' + num_digraphs;
		} else {
			fprintf(stderr, "error: line %d\n", line);
			exit(1);
		}

		++line;
	}
	ungetc(ch, f);

	if (verbosity > 0)
		fprintf(stderr, "reading %d words...\n", num_words);

	size_t all_words_size = sizeof(Word) * num_words;
	all_words = malloc(all_words_size);

	double last_score = 1.0;
	for (int i = 0; i < num_words; ++i) {
		if (scan_word(f, &all_words[i]) < 0) {
			fprintf(stderr, "error: line %d\n", line);
			exit(1);
		}

		if (initial_scores) {
			int iscore;
			if (fscanf(f, " %6d\n", &iscore) != 1) {
				fprintf(stderr, "error: wrong index on line %d\n", line);
				exit(1);
			}

			double score = iscore / 1000000.0;
			if (score > last_score) {
				fprintf(stderr, "error: words must be given in decreasing scoring order (line %d)\n", line);
				exit(1);
			}

			initial_scores[i] = last_score = score;
		} else if (fgetc(f) != '\n') {
			fprintf(stderr, "error: expected newline on line %d\n", line);
			exit(1);
		}

		++line;
	}

	opts = memcpy(malloc(all_words_size), all_words, all_words_size);
	num_opts = num_words;
}

bool
word_matches(const Word *word, const Know *know)
{
	for (int i = 0; i < 5; ++i) {
		/* word contains ruled-out letter */
		if (0 != (know->exclude[i] & letter_bit(word->letters[i])))
			return false;
	}

	for (int i = 0; i < sizeof(word->hist) / sizeof(word->hist[0]); ++i)
		if ((word->hist[i] & know->hist[i]) != know->hist[i])
			return false;

	return true;
}

int
count_opts(const Know *know)
{
	extern Word *opts;
	extern int num_opts;

	int res = 0;
	for (int i = 0; i < num_opts; ++i)
		if (word_matches(&opts[i], know))
			++res;

	return res;
}


void
filter_opts(const Know *know)
{
	extern Word *opts;
	extern int num_opts;

	int j = 0;
	for (int i = 0; i < num_opts; ++i)
		if (word_matches(&opts[i], know))
			memmove(&opts[j++], &opts[i], sizeof(Word));

	num_opts = j;
}

void
compare_to_target(WordColor out, const Word *guess, const Word *target)
{
	int8_t target_hist[32] = { 0 };

	for (int i = 0; i < 5; ++i)
		if (guess->letters[i] != target->letters[i])
			++target_hist[target->letters[i] - 'A'];

	for (int i = 0; i < 5; ++i) {
		uint8_t color = DARK_COLOR;

		if (guess->letters[i] == target->letters[i]) {
			color = GREEN_COLOR;
		} else if (target_hist[guess->letters[i] - 'A'] > 0) {
			color = YELLOW_COLOR;
			--target_hist[guess->letters[i] - 'A'];
		}

		out[i] = color;
	}
}

int
knowledge_from_colors(Know *know, const Word *guess, WordColor colors)
{
	memset(know, 0, sizeof(Know));
	Histogram yellow = { 0 };

	for (int i = 0; i < 5; ++i) {
		char letter = guess->letters[i];
		switch (colors[i]) {
		case GREEN_COLOR:
			hist_add_letter(know->hist, letter);
			know->exclude[i] |= ~letter_bit(letter);
			break;

		case YELLOW_COLOR:
			hist_add_letter(yellow, letter);
			hist_add_letter(know->hist, letter);
			know->exclude[i] |= letter_bit(letter);
			break;

		case DARK_COLOR:
			know->exclude[i] |= letter_bit(letter);
			break;
		}
	}

	for (int i = 0; i < 5; ++i) {
		char letter = guess->letters[i];
		if (colors[i] != DARK_COLOR || hist_count(yellow, letter) > 0)
			continue;

		for (int j = 0; j < 5; ++j)
			if (guess->letters[j] != letter)
				know->exclude[j] |= letter_bit(letter);
	}

	return 0;
}

int
absorb_knowledge(Know *restrict know, const Know *other)
{
	for (int i = 0; i < 5; ++i)
		know->exclude[i] |= other->exclude[i];

	for (int i = 0; i < sizeof(know->hist) / sizeof(know->hist[0]); ++i)
		know->hist[i] |= other->hist[i];

	return 0;
}

void
print_wordch(FILE *f, char ch, char nxt)
{
	extern Digraph *digraphs;
	extern int num_digraphs;

	if (ch > 'Z') {
		int dgidx = ch - 'Z' - 1;
		if (dgidx >= num_digraphs) {
			fprintf(stderr, "invalid digraph %02x\n", (int)ch);
			fputc('?', f);
			return;
		}

		Digraph di = digraphs[dgidx];
		fputc(di.fst, f);
		fputc(di.snd, f);
		return;
	}

	fputc(ch, f);
	for (int j = 0; j < num_digraphs; ++j) {
		if (ch == digraphs[j].fst && nxt == digraphs[j].snd) {
			fputc('-', f);
			break;
		}
	}
}

void
print_word(FILE *f, const Word *word)
{
	extern Digraph *digraphs;
	extern int num_digraphs;

	for (int i = 0; i < 4; ++i)
		print_wordch(f, word->letters[i], word->letters[i + 1]);
	print_wordch(f, word->letters[4], 0);
}

void
print_know(const Know *k)
{
	for (int i = 0; i < 5; ++i) {
		if (__builtin_popcount(k->exclude[i]) == 31) {
			putchar(bit_letter(~k->exclude[i]));
			continue;
		}

		printf("[^");
		for (char l = 'A'; l <= 'Z'; ++l)
			if (k->exclude[i] & letter_bit(l))
				putchar(l);

		putchar(']');
	}

	for (char l = 'A'; l <= 'Z'; ++l) {
		int count = hist_count(k->hist, l);
		if (count > 0)
			printf(" %c: %d", l, count);
	}
	putchar('\n');
}

static int
cpu_count(void)
{
	cpu_set_t cs;
	CPU_ZERO(&cs);
	sched_getaffinity(0, sizeof(cs), &cs);

	int count = 0;
	for (int i = 0; i < 64; i++) {
		if (!CPU_ISSET(i, &cs))
			break;
		count++;
	}
	return count;
}

typedef struct {
	int offset, stride;
	const Know *know;
	const Word *guess;
	double score_part;
} ScoreTask;

static void
score_guess_worker(void *info)
{
	extern Word *opts;
	extern int num_opts;

	ScoreTask *st = info;

	Know know = *st->know;
	const Word *guess = st->guess;
	const int stride = st->stride;

	double score_part = 0.0;
	double norm = (1.0 / num_opts) * (1.0 / num_opts);

	for (int j = st->offset; j < num_opts; j += stride) {
		WordColor wc;
		compare_to_target(wc, guess, &opts[j]);

		Know new;
		knowledge_from_colors(&new, guess, wc);

		Know sim_know = know;
		absorb_knowledge(&sim_know, &new);

		int sim_opts = count_opts(&sim_know);
		score_part -= sim_opts * norm;
	}

	st->score_part = score_part;
}

double
score_guess(const Word *guess, const Know *know)
{
	extern Word *all_words;
	extern int num_opts, num_words;
	extern double *initial_scores;

	Know nothing = no_knowledge();
	if (initial_scores != NULL && memcmp(&nothing, know, sizeof(*know)) == 0) {
		for (int i = 0; i < num_words; ++i) {
			if (memcmp(all_words[i].letters, guess->letters, 5) == 0)
				return initial_scores[i];
		}
	}

	ScoreTask tasks[MAX_WORKERS];

	int num_workers = 1 + num_opts / 100;
	int max_workers = cpu_count();
	if (max_workers > MAX_WORKERS)
		max_workers = MAX_WORKERS;

	if (num_workers > max_workers)
		num_workers = max_workers;

	for (int i = 0; i < num_workers; ++i) {
		tasks[i].offset = i;
		tasks[i].stride = num_workers;
		tasks[i].know = know;
		tasks[i].guess = guess;
	}

	threadpool_t *pool = threadpool_create(num_workers, num_workers * 2, 0);
	for (int i = 1; i < num_workers; ++i)
		threadpool_add(pool, score_guess_worker, &tasks[i], 0);

	score_guess_worker(&tasks[0]);

	threadpool_destroy(pool, threadpool_graceful);

	double score = 1.0;

	if (word_matches(guess, know))
		score += (1.0 / num_opts) * (1.0 / num_opts);

	for (int i = 0; i < num_workers; ++i)
		score += tasks[i].score_part;

	return score;
}

double
score_guess_st(const Word *guess, const Know *know, double break_at)
{
	extern Word *opts;
	extern int num_opts;

	double guess_score = 1.0;
	double norm = (1.0 / num_opts) * (1.0 / num_opts);

	if (word_matches(guess, know))
		guess_score += norm;

	for (int j = 0; j < num_opts; ++j) {
		WordColor wc;
		compare_to_target(wc, guess, &opts[j]);

		Know new;
		knowledge_from_colors(&new, guess, wc);

		Know sim_know = *know;
		absorb_knowledge(&sim_know, &new);

		int sim_opts = count_opts(&sim_know);
		guess_score -= sim_opts * norm;

		if (guess_score < break_at)
			break;
	}

	return guess_score;
}

typedef struct {
	pthread_mutex_t lock;
	double best_score;
	int num_out, max_out;
	Word *top;
} BestTaskOutput;

typedef struct {
	int offset, stride;
	BestTaskOutput *out;
	Know know;
} BestTask;

static void
best_guess_worker(void *info)
{
	extern Word *opts, *all_words;
	extern int num_opts, num_words, verbosity;

	BestTask *task = info;
	BestTaskOutput *out = task->out;
	
	const int stride = task->stride;

	double best_local_score = 0.0;

	for (int i = task->offset; i < num_words; i += stride) {
		double guess_score = score_guess_st(&all_words[i], &task->know, best_local_score);

		pthread_mutex_lock(&out->lock);
		if (guess_score > out->best_score) {
			memcpy(&out->top[0], &opts[i], sizeof(Word));
			out->best_score = guess_score;
			out->num_out = 1;
		} else if (guess_score == out->best_score) {
			int j = out->num_out;
			if (j < out->max_out)
				memcpy(&out->top[j], &opts[i], sizeof(Word));
			++out->num_out;
		}

		best_local_score = out->best_score;
		pthread_mutex_unlock(&out->lock);
	}
}

double
best_guesses(Word *top, int max_out, int *num_out, const Know *know)
{
	extern Word *all_words;
	extern int num_opts, num_words, verbosity;
	extern double *initial_scores;

	Know nothing = no_knowledge();
	if (initial_scores != NULL && memcmp(&nothing, know, sizeof(*know)) == 0) {
		memcpy(&top[0], &all_words[0], sizeof(Word));
		*num_out = 1;
		return initial_scores[0];
	}

	BestTaskOutput out = {
		.best_score = 0.0,
		.max_out = max_out,
		.top = top,
		.num_out = 0,
	};

	pthread_mutex_init(&out.lock, NULL);

	BestTask tasks[MAX_WORKERS];

	int max_workers = cpu_count();
	if (max_workers > MAX_WORKERS)
		max_workers = MAX_WORKERS;

	int num_workers = max_workers;

	for (int i = 0; i < num_workers; ++i) {
		tasks[i].offset = i;
		tasks[i].stride = num_workers;
		tasks[i].know = *know;
		tasks[i].out = &out;
	}

	threadpool_t *pool = threadpool_create(num_workers, num_workers * 2, 0);
	for (int i = 1; i < num_workers; ++i)
		threadpool_add(pool, best_guess_worker, &tasks[i], 0);

	best_guess_worker(&tasks[0]);

	threadpool_destroy(pool, threadpool_graceful);
	pthread_mutex_destroy(&out.lock);

	*num_out = out.num_out;
	return out.best_score;
}

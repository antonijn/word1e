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

#include "guess.h"
#include "threadpool.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include <sched.h>

#define MAX_WORKERS 64

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
scan_word(FILE *f, Word out)
{
	for (int i = 0; i < 5; ++i) {
		int ch = scan_letter(f);
		if (ch < 0)
			return -1;
		out[i] = ch;
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
		if (scan_word(f, all_words[i]) < 0) {
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

Know
no_knowledge(void)
{
	extern int num_digraphs;

	uint32_t all_bits = 0x7FFFFFF;
	for (int i = 0; i < num_digraphs; ++i)
		all_bits = (all_bits << 1) | 1;

	Know know;
	memset(&know, 0, sizeof(know));
	for (int i = 0; i < 5; ++i)
		know.maybe[i] = all_bits;
	memset(know.musthave, 0, sizeof(know.musthave));
	return know;
}

bool
word_matches(Word word, Know know)
{
	for (int i = 0; i < 5; ++i) {
		/* word contains ruled-out letter */
		if (0 == (know.maybe[i] & letter_bit(word[i])))
			return false;
	}

	Word word_cpy;
	memcpy(word_cpy, word, sizeof(Word));

	for (int i = 0; i < 5; ++i) {
		char ltr = know.musthave[i];
		if (!ltr)
			continue;

		bool contains = false;
		for (int j = 0; j < 5; ++j) {
			if (word_cpy[j] == ltr) {
				contains = true;
				word_cpy[j] = 0;
				break;
			}
		}

		if (!contains)
			return false;
	}

	return true;
}

int
num_opts_with_knowledge(Know know)
{
	extern Word *opts;
	extern int num_opts;

	int res = 0;
	for (int i = 0; i < num_opts; ++i)
		if (word_matches(opts[i], know))
			++res;

	return res;
}

void
filter_opts(Know know)
{
	extern Word *opts;
	extern int num_opts;

	int j = 0;
	for (int i = 0; i < num_opts; ++i)
		if (word_matches(opts[i], know))
			memmove(opts[j++], opts[i], 5);

	num_opts = j;
}

Know
gather_knowledge_col(Word guess, Word word, WordColor *out)
{
	Know new = no_knowledge();

	Word incor = { 0 };
	Word rem = { 0 };

	for (int i = 0; i < 5; ++i) {
		if (guess[i] == word[i]) {
			new.maybe[i] = letter_bit(word[i]);
			out->colors[i] = GREEN;
		} else {
			incor[i] = guess[i];
			rem[i] = word[i];
			new.maybe[i] &= ~letter_bit(guess[i]);
		}
	}

	for (int i = 0; i < 5; ++i) {
		char incor_ltr = incor[i];
		if (!incor_ltr)
			continue;

		out->colors[i] = BLACK;
		bool contains = false;
		for (int j = 0; j < 5; ++j) {
			if (word[j] == incor_ltr)
				contains = true;

			if (rem[j] == incor_ltr) {
				new.musthave[i] = incor_ltr;
				rem[j] = 0;
				out->colors[i] = YELLOW;
				break;
			}
		}

		if (contains)
			continue;

		for (int j = 0; j < 5; ++j)
			new.maybe[j] &= ~letter_bit(incor_ltr);
	}

	return new;
}

Know
gather_knowledge(Word guess, Word word)
{
	WordColor dummy;
	return gather_knowledge_col(guess, word, &dummy);
}

static void
rm_musthave(Know *k, char ltr)
{
	for (int i = 0; i < 5; ++i) {
		if (k->musthave[i] == ltr) {
			k->musthave[i] = 0;
			break;
		}
	}
}

static int
musthave_count(Know k, char ltr)
{
	int res = 0;
	for (int i = 0; i < 5; ++i)
		if (k.musthave[i] == ltr)
			++res;
	return res;
}

Know
combine_knowledge(Know a, Know b)
{
	Know res;
	for (int i = 0; i < 5; ++i) {
		res.maybe[i] = a.maybe[i] & b.maybe[i];
		if (__builtin_popcount(a.maybe[i]) > 1 && __builtin_popcount(res.maybe[i]) == 1)
			rm_musthave(&a, bit_letter(res.maybe[i]));
		if (__builtin_popcount(b.maybe[i]) > 1 && __builtin_popcount(res.maybe[i]) == 1)
			rm_musthave(&b, bit_letter(res.maybe[i]));
	}

	memset(res.musthave, 0, sizeof(Word));

	int nmh = 0;
	for (char ch = 'A'; ch <= 'Z'; ++ch) {
		int amh = musthave_count(a, ch);
		int bmh = musthave_count(b, ch);
		int newmh = (amh > bmh) ? amh : bmh;
		for (int i = 0; i < newmh; ++i)
			res.musthave[nmh++] = ch;
	}

	return res;
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
print_word(FILE *f, Word word)
{
	extern Digraph *digraphs;
	extern int num_digraphs;

	for (int i = 0; i < 4; ++i)
		print_wordch(f, word[i], word[i + 1]);
	print_wordch(f, word[4], 0);
}

void
print_know(Know k)
{
	extern int num_digraphs;

	uint32_t all_bits = 0x7FFFFFF;
	for (int i = 0; i < num_digraphs; ++i)
		all_bits = (all_bits << 1) | 1;

	uint32_t black = 0;
	for (int i = 0; i < 5; ++i)
		black |= k.maybe[i];
	black ^= all_bits;
	int nblack = __builtin_popcount(black);

	printf("I know: /");

	for (int i = 0; i < 5; ++i) {
		int maybe = k.maybe[i];
		int pop = __builtin_popcount(maybe);
		if (pop > 26 + num_digraphs - nblack) {
			putchar('.');
			continue;
		}

		if (pop == 1) {
			putchar(bit_letter(maybe));
			continue;
		}

		bool neg = pop > (26 + num_digraphs - nblack) / 2;
		putchar('[');
		if (neg)
			putchar('~');
		for (char ch = 'A'; ch <= 'Z' + num_digraphs; ++ch) {
			int ltr_bit = letter_bit(ch);
			if ((ltr_bit & black) == 0 && (!neg == ((maybe & ltr_bit) == ltr_bit)))
				print_wordch(stdout, ch, 0);
		}
		putchar(']');
	}
	printf("/; must contain /");
	for (int i = 0; i < 5; ++i)
		if (k.musthave[i])
			print_wordch(stdout, k.musthave[i], 0);

	printf("/, not /");
	for (char ch = 'A'; ch <= 'Z' + num_digraphs; ++ch)
		if (black & letter_bit(ch))
			print_wordch(stdout, ch, 0);

	puts("/");
}

double
score_guess(Word guess, Know know, double break_at)
{
	extern Word *opts;
	extern int num_opts;

	double guess_score = 1.0;
	double norm = (1.0 / num_opts) * (1.0 / num_opts);

	for (int j = 0; j < num_opts; ++j) {
		if (memcmp(guess, opts[j], 5) == 0)
			continue;

		Know new = gather_knowledge(guess, opts[j]);
		Know sim_know = combine_knowledge(know, new);
		int sim_opts = num_opts_with_knowledge(sim_know);
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
} TaskOutput;

typedef struct {
	int offset, stride;
	TaskOutput *out;
	Know know;
} Task;

static void
best_guess_worker(void *info)
{
	extern Word *opts, *all_words;
	extern int num_opts, num_words, verbosity;

	Task *task = info;
	TaskOutput *out = task->out;
	
	const int stride = task->stride;

	double best_local_score = 0.0;

	for (int i = task->offset; i < num_opts; i += stride) {
		Word guess;
		memcpy(guess, &opts[i], sizeof(Word));

		double guess_score = score_guess(guess, task->know, best_local_score);

		pthread_mutex_lock(&out->lock);
		if (guess_score > out->best_score) {
			memcpy(out->top[0], guess, sizeof(Word));
			out->best_score = guess_score;
			out->num_out = 1;
		} else if (guess_score == out->best_score) {
			int j = out->num_out;
			if (j < out->max_out)
				memcpy(out->top[j], guess, sizeof(Word));
			++out->num_out;
		}

		best_local_score = out->best_score;
		pthread_mutex_unlock(&out->lock);
	}
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

double
best_guesses(Word *top, int max_out, int *num_out, Know know)
{
	extern Word *all_words;
	extern int num_opts, num_words, verbosity;
	extern double *initial_scores;

	Know nothing = no_knowledge();
	if (initial_scores != NULL && memcmp(&nothing, &know, sizeof(know)) == 0) {
		memcpy(top[0], all_words[0], sizeof(Word));
		*num_out = 1;
		return initial_scores[0];
	}

	TaskOutput out = {
		.best_score = 0.0,
		.max_out = max_out,
		.top = top,
		.num_out = 0,
	};

	pthread_mutex_init(&out.lock, NULL);

	Task tasks[MAX_WORKERS];

	int num_workers = 1 + num_opts / 50;
	int max_workers = cpu_count();
	if (max_workers > MAX_WORKERS)
		max_workers = MAX_WORKERS;

	if (num_workers > max_workers)
		num_workers = max_workers;

	for (int i = 0; i < num_workers; ++i) {
		tasks[i].offset = i;
		tasks[i].stride = num_workers;
		tasks[i].know = know;
		tasks[i].out = &out;
	}

	threadpool_t *pool = threadpool_create(num_workers, num_workers * 2, 0);
	for (int i = 0; i < num_workers; ++i)
		threadpool_add(pool, best_guess_worker, &tasks[i], 0);

	threadpool_destroy(pool, threadpool_graceful);
	pthread_mutex_destroy(&out.lock);

	*num_out = out.num_out;
	return out.best_score;
}

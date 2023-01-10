/*
 * Make word1e index.
 * Copyright (C) 2023  Antonie Blom
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "guess.h"
#include "threadpool.h"
#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	Word guess;
	double score;
} InitialGuess;

static InitialGuess *output;

typedef struct {
	int from, until;
} Range;

Word *all_words, *opts;
Digraph *digraphs;
double *initial_scores;
int num_opts, num_words, verbosity = 0, num_digraphs;
static const char *word_list;
static char *out_path;
static char *cmd;

static int
ig_compar(const void *lptr, const void *rptr)
{
	InitialGuess l = *(InitialGuess *)lptr;
	InitialGuess r = *(InitialGuess *)rptr;
	return (l.score < r.score) - (l.score > r.score);
}

static void
build_index(void *info)
{
	Range *r = info;
	int from = r->from, until = r->until;

	Know k = no_knowledge();
	for (int i = from; i < until; ++i) {
		InitialGuess *ig = output + i;
		memcpy(ig->guess, all_words[i], sizeof(Word));
		ig->score = score_guess(all_words[i], &k, 0.0);
		if (verbosity > 0) {
			int iscore = ig->score * 1000000.0;
			print_word(stderr, ig->guess);
			fprintf(stderr, " %06d\n", iscore);
		}
	}
}

static void
compile_index(void)
{
	fprintf(stderr, "sorting output...");
	qsort(output, num_words, sizeof(InitialGuess), ig_compar);
	fprintf(stderr, " done!\n");

	fprintf(stderr, "writing output...");
	FILE *fout = stdout;
	if (out_path) {
		fout = fopen(out_path, "w");
		if (!fout) {
			perror(cmd);
			exit(1);
		}
	}

	fprintf(fout, "%d\n", num_words);
	fprintf(fout, "#INDEXED\n");
	for (int i = 0; i < num_digraphs; ++i) {
		fprintf(fout, "#DIGRAPH %c%c\n", digraphs[i].fst, digraphs[i].snd);
	}

	for (int i = 0; i < num_words; ++i) {
		int iscore = output[i].score * 1000000.0;
		print_word(fout, output[i].guess);
		fprintf(fout, " %06d\n", iscore);
	}

	if (out_path)
		fclose(fout);
	fprintf(stderr, " done!\n");
}

static void
print_usage(void)
{
	printf("Usage: %s [OPTION]... [PATH]\n"
	       "Make Worlde-solver index.\n\n"
	       "Options:\n"
	       "  -o PATH               Output index.\n"
	       "  -v                    Verbose output.\n"
	       "  --help                Show this message.\n\n", cmd);
}

static int
handle_string_option(const char *arg, int *arg_idx, int argc, char **argv)
{
	if (0 == strcmp(arg, "--help")) {
		print_usage();
		exit(0);
	}

	fprintf(stderr, "unknown option `%s'\n", arg);
	return -1;
}
static int
handle_option(char opt, int *arg_idx, int argc, char **argv)
{
	switch (opt) {
	case 'v':
		++verbosity;
		break;
	case 'o':
		if (argc <= *arg_idx + 1) {
			fprintf(stderr, "expected argument after -o\n");
			print_usage();
			return -1;
		}

		out_path = argv[++*arg_idx];
		break;
	default:
		fprintf(stderr, "unknown option '%c'\n", opt);
		print_usage();
		return -1;
	}
	return 0;
}
static int
handle_arg(const char *arg, int *arg_idx, int argc, char **argv)
{
	if (arg[0] != '-') {
		if (word_list) {
			fprintf(stderr, "more than one word list file given\n");
			print_usage();
			exit(1);
		}

		word_list = arg;
		return 0;
	}

	if (arg[1] == '-')
		return handle_string_option(arg, arg_idx, argc, argv);

	for (int i = 1; arg[i]; ++i)
		if (handle_option(arg[i], arg_idx, argc, argv))
			return -1;

	return 0;
}
static int
handle_args(int argc, char **argv)
{
	cmd = argv[0];

	for (int i = 1; i < argc; ++i)
		if (handle_arg(argv[i], &i, argc, argv))
			return -1;

	return 0;
}

int
main(int argc, char **argv)
{
	if (handle_args(argc, argv) < 0)
		exit(1);

	FILE *f = stdin;
	if (word_list) {
		f = fopen(word_list, "r");
		if (!f) {
			perror(cmd);
			exit(1);
		}
	}
	load_words(f);
	if (word_list)
		fclose(f);

	Range ranges[8];
	int last_word = 0;
	for (int i = 0; i < 8; ++i) {
		ranges[i].from = last_word;
		last_word += (num_words - last_word) / (8 - i);
		ranges[i].until = last_word;

		fprintf(stderr, "task %d handling ", i);
		print_word(stderr, all_words[ranges[i].from]);
		fprintf(stderr, "..");
		print_word(stderr, all_words[last_word - 1]);
		fprintf(stderr, "\n");
	}

	output = malloc(sizeof(InitialGuess) * num_words);

	size_t nthread = 8, nqueue = 16;
	threadpool_t *pool = threadpool_create(nthread, nqueue, 0);
	for (int i = 0; i < 8; ++i)
		threadpool_add(pool, build_index, &ranges[i], 0);

	threadpool_destroy(pool, threadpool_graceful);
	fprintf(stderr, "tasks done!\n");
	return compile_index(), 0;
}

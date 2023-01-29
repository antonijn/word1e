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

#include <score.h>
#include <threadpool.h>
#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	Word *guess;
	double score;
	WordAttr attr;
} InitialGuess;

static InitialGuess *output;

typedef struct {
	int from, until;
} Range;

static const char *word_list, *target_path;
static char *out_path;
static char *cmd;

static int
ig_compar(const void *lptr, const void *rptr)
{
	InitialGuess l = *(InitialGuess *)lptr;
	InitialGuess r = *(InitialGuess *)rptr;
	return (l.score < r.score) - (l.score > r.score);
}

static int
w_compar(const void *lptr, const void *rptr)
{
	Word l = *(Word *)lptr;
	Word r = *(Word *)rptr;

	char lbuf[6], rbuf[6];
	memcpy(lbuf, l.letters, 5);
	memcpy(rbuf, r.letters, 5);
	lbuf[5] = rbuf[5] = '\0';

	return strcmp(lbuf, rbuf);
}

static void
build_index(void *info)
{
	Range *r = info;
	int from = r->from, until = r->until;

	Know k = no_knowledge();
	for (int i = from; i < until; ++i) {
		InitialGuess *ig = output + i;
		ig->guess = &all_words[i];
		ig->score = score_guess_st(&all_words[i], &k, 0.0);
		if (verbosity > 0) {
			int iscore = ig->score * 1000000.0;
			print_word(stderr, ig->guess);
			fprintf(stderr, " %06d\n", iscore);
		}
	}
}

static int
calc_attrs(const Word *word, Word *sorted_opts)
{
	int res = 0;
	if (bsearch(word, sorted_opts, num_opts, sizeof(Word), w_compar))
		res |= WA_TARGET;

	return res;
}

static void
print_attrs(FILE *fout, int attr)
{
	if (attr == 0)
		return;

	fputc(' ', fout);

	if (attr & WA_TARGET)
		fputc('t', fout);
	if (attr & WA_EXPLICIT)
		fputc('x', fout);
	if (attr & WA_SLUR)
		fputc('s', fout);
}

static void
compile_index(void)
{
	fprintf(stderr, "sorting output...");
	qsort(output, num_words, sizeof(InitialGuess), ig_compar);
	fprintf(stderr, " done!\n");

	fprintf(stderr, "sorting target list...");
	Word *sorted_opts = malloc(num_opts * sizeof(Word));
	if (sorted_opts == NULL) {
		fprintf(stderr, "out of memory!\n");
		exit(1);
	}

	memcpy(sorted_opts, opts, num_opts * sizeof(Word));
	qsort(sorted_opts, num_opts, sizeof(Word), w_compar);

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
	for (int i = 0; i < num_digraphs; ++i)
		fprintf(fout, "#DIGRAPH %c%c\n", digraphs[i].fst, digraphs[i].snd);

	for (int i = 0; i < num_words; ++i) {
		int iscore = output[i].score * 1000000.0;
		print_word(fout, output[i].guess);
		fprintf(fout, " %06d", iscore);

		int attr = calc_attrs(output[i].guess, sorted_opts);
		print_attrs(fout, attr);

		fputc('\n', fout);
	}

	if (out_path)
		fclose(fout);

	free(sorted_opts);

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
	       "  --target PATH         Path to file of possible target words.\n"
	       "  --help                Show this message.\n\n", cmd);
}

static int
handle_string_option(const char *arg, int *arg_idx, int argc, char **argv)
{
	if (0 == strcmp(arg, "--help")) {
		print_usage();
		exit(0);
	}

	if (0 == strcmp(arg, "--target")) {
		if (argc <= *arg_idx + 1) {
			fprintf(stderr, "expected argument after --target\n");
			print_usage();
			return -1;
		}

		target_path = argv[++*arg_idx];
		return 0;
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

static void
read_word_list(void)
{
	FILE *f = stdin;
	if (word_list) {
		f = fopen(word_list, "r");
		if (!f) {
			perror(cmd);
			exit(1);
		}
	}

	ssize_t snum_words = load_words(f, &all_words);

	if (word_list)
		fclose(f);

	if (snum_words < 0)
		exit(1);

	num_words = snum_words;
}

static void
read_target_list(void)
{
	if (target_path == NULL) {
		/* we can do this, since filter_opts() is never called */
		opts = all_words;
		num_opts = num_words;
		return;
	}

	FILE *f = fopen(target_path, "r");
	if (!f) {
		perror(cmd);
		exit(1);
	}

	ssize_t snum_words = load_words(f, &opts);

	fclose(f);

	if (snum_words < 0)
		exit(1);

	num_opts = snum_words;
}

int
main(int argc, char **argv)
{
	if (handle_args(argc, argv) < 0)
		exit(1);

	read_word_list();
	read_target_list();

	Range ranges[8];
	int last_word = 0;
	for (int i = 0; i < 8; ++i) {
		ranges[i].from = last_word;
		last_word += (num_words - last_word) / (8 - i);
		ranges[i].until = last_word;

		fprintf(stderr, "task %d handling ", i);
		print_word(stderr, &all_words[ranges[i].from]);
		fprintf(stderr, "..");
		print_word(stderr, &all_words[last_word - 1]);
		fprintf(stderr, "\n");
	}

	output = malloc(sizeof(InitialGuess) * num_words);

	size_t nthread = 8, nqueue = 16;
	threadpool_t *pool = threadpool_create(nthread, nqueue, 0);
	for (int i = 0; i < 8; ++i)
		threadpool_add(pool, build_index, &ranges[i], 0);

	threadpool_destroy(pool, THREADPOOL_GRACEFUL);
	fprintf(stderr, "tasks done!\n");
	return compile_index(), 0;
}

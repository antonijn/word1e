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
#include <stdatomic.h>

typedef struct {
	Word *guess;
	WordAttr attr;
} InitialGuess;

static InitialGuess *output;

typedef struct {
	int from, until;
} Range;

static const char *word_list, *target_path, *slur_path, *out_path;
static char *cmd;

static Word *slurs;
static int num_slurs;

static int
ig_compar(const void *lptr, const void *rptr)
{
	InitialGuess l = *(InitialGuess *)lptr;
	InitialGuess r = *(InitialGuess *)rptr;
	return (l.attr.starting_score < r.attr.starting_score)
	     - (l.attr.starting_score > r.attr.starting_score);
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

static int
calc_attrs(const Word *word)
{
	int res = 0;

	/* requires opts be alphabetically sorted (which it should be) */
	if (bsearch(word, opts, num_opts, sizeof(Word), w_compar))
		res |= WA_TARGET;
	if (bsearch(word, slurs, num_slurs, sizeof(Word), w_compar))
		res |= WA_SLUR;

	return res;
}

static void
build_index(void *info)
{
	static atomic_int progress = 0;

	Range *r = info;
	int from = r->from, until = r->until;

	Know k = no_knowledge();
	for (int i = from; i < until; ++i) {
		InitialGuess *ig = output + i;
		ig->guess = &all_words[i];
		ig->attr.starting_score = score_guess_st(&all_words[i], NULL, &k, 0.0);
		if (verbosity > 0) {
			int iscore = ig->attr.starting_score * 1000000.0;
			print_word(stderr, ig->guess);

			/* spaces are so simultaneous writes don't
			 * leave permanent marks. */
			fprintf(stderr, " 0.%06d [%5d / %5d]        \r", iscore, ++progress, num_words);
		}

		ig->attr.flags = calc_attrs(ig->guess);
	}
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
		int iscore = output[i].attr.starting_score * 1000000.0;
		print_word(fout, output[i].guess);
		fprintf(fout, " 0.%06d", iscore);
		print_attrs(fout, output[i].attr.flags);
		fputc('\n', fout);
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
	       "  --target PATH         Path to file of possible target words.\n"
	       "  --slur PATH           Path to file of slurs.\n"
	       "  --help                Show this message.\n\n", cmd);
}

static int
handle_path_option(int *arg_idx,
                   int argc,
                   char **argv,
                   const char *opt_name,
                   const char **target)
{
	if (argc <= *arg_idx + 1) {
		fprintf(stderr, "expected argument after %s\n", opt_name);
		print_usage();
		return -1;
	}

	*target = argv[++*arg_idx];
	return 0;
}

static int
handle_string_option(const char *arg, int *arg_idx, int argc, char **argv)
{
	if (0 == strcmp(arg, "--help")) {
		print_usage();
		exit(0);
	}

	if (0 == strcmp(arg, "--target"))
		return handle_path_option(arg_idx, argc, argv, "--target", &target_path);

	if (0 == strcmp(arg, "--slur"))
		return handle_path_option(arg_idx, argc, argv, "--slur", &slur_path);

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
		return handle_path_option(arg_idx, argc, argv, "-o", &out_path);
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
		f = fopen(word_list, "rb");
		if (!f) {
			perror(word_list);
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
read_special_list(Word **list, int *count, Word *fb, int num_fb, const char *path)
{
	if (path == NULL & num_fb <= 0)
		return;

	if (path != NULL) {
		FILE *f = fopen(path, "rb");
		if (!f) {
			perror(path);
			exit(1);
		}

		ssize_t snum_words = load_words(f, list);

		fclose(f);

		if (snum_words < 0)
			exit(1);

		*count = snum_words;
	} else {
		*list = malloc(sizeof(Word) * num_fb);
		if (*list == NULL) {
			fprintf(stderr, "out of memory\n");
			exit(1);
		}

		*count = num_fb;
		memcpy(*list, fb, sizeof(Word) * num_fb);
	}

	qsort(*list, *count, sizeof(Word), w_compar);
}

int
main(int argc, char **argv)
{
	if (handle_args(argc, argv) < 0)
		exit(1);

	read_word_list();

	/* opts will be alphabetically sorted */
	read_special_list(&opts, &num_opts, all_words, num_words, target_path);
	read_special_list(&slurs, &num_slurs, NULL, 0, slur_path);

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
	fprintf(stderr, "\ntasks done!\n");

	compile_index();

	free(all_words);
	free(opts);
	free(slurs);
	return 0;
}

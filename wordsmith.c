/*
 * JSON API for making educated word1e guesses.
 * Copyright (C) 2023 Antonie Blom
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

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <word.h>
#include <score.h>
#include "json.h"

static char *cmd;
static Word target, *guesses, *top_words_buf;
static int num_guesses, max_top_words;
static JSONWriter *json;

static int
load_word(char *word_str, Word *word)
{
	FILE *f = fmemopen((void *)word_str, strlen(word_str), "r");
	int c = scan_word(f, word);
	fclose(f);

	if (c < 0) {
		fprintf(stderr, "invalid word\n");
		return -1;
	}

	return 0;
}

static int
handle_string_option(const char *arg, int *arg_idx, int argc, char **argv)
{
	fprintf(stderr, "unknown option `%s'\n", arg);
	return -1;
}
static int
handle_option(char opt, int *arg_idx, int argc, char **argv)
{
	switch (opt) {
	case 't':
		if (argc <= *arg_idx + 1) {
			fprintf(stderr, "expected argument after -t\n");
			return -1;
		}

		return load_word(argv[++*arg_idx], &target);
	default:
		fprintf(stderr, "unknown option `%c'\n", opt);
		return -1;
	}
}
static int
handle_arg(const char *arg, int *arg_idx, int argc, char **argv)
{
	if (arg[0] != '-')
		return load_word((char *)arg, &guesses[num_guesses++]);

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

	for (int i = 2; i < argc; ++i)
		if (handle_arg(argv[i], &i, argc, argv))
			return -1;

	return 0;
}

static int
check_target_loaded(void)
{
	if (target.letters[0] == '\0') {
		fprintf(stderr, "target not loaded\n");
		return -1;
	}

	return 0;
}

static void
prep_guesses(Know *k, int n)
{
	memset(k, 0, sizeof(*k));
	for (int i = 0; i < n; ++i) {
		WordColor wc;
		compare_to_target(wc, &guesses[i], &target);

		Know new;
		knowledge_from_colors(&new, &guesses[i], wc);

		absorb_knowledge(k, &new);
	}

	if (update_opts(k) < 0)
		exit(1);
}

static void
jsonify_word(const Word *word)
{
	char word_string[6];
	memcpy(word_string, word->letters, 5);
	word_string[5] = '\0';

	json_string(json, word_string);
}

static void
report_word(const Word *word, double score)
{
	json_enter_dict(json);

	json_enter_assoc(json, "word");
	jsonify_word(word);
	json_leave_assoc(json);

	json_enter_assoc(json, "score");
	json_double(json, score);
	json_leave_assoc(json);

	json_leave_dict(json);
}

static int
report(const Word *user,
       double user_score,
       WordColor user_wc,
       const Word *best,
       int num_best,
       double best_score,
       int eliminated)
{
	json_enter_dict(json);

	json_enter_assoc(json, "user");
	report_word(user, user_score);
	json_leave_assoc(json);

	json_enter_assoc(json, "colors");
	char str[6];
	for (int i = 0; i < 5; ++i) {
		switch (user_wc[i]) {
		case DARK_COLOR:   str[i] = 'B'; break;
		case GREEN_COLOR:  str[i] = 'G'; break;
		case YELLOW_COLOR: str[i] = 'Y'; break;
		}
	}
	str[5] = '\0';
	json_string(json, str);
	json_leave_assoc(json);

	if (best != NULL) {
		json_enter_assoc(json, "best");
		json_enter_list(json);

		for (int i = 0; i < num_best; ++i)
			report_word(&best[i], best_score);

		json_leave_list(json);
		json_leave_assoc(json);
	}

	json_enter_assoc(json, "optionsLeft");
	json_enter_list(json);

	for (int i = 0; i < num_opts; ++i)
		jsonify_word(&opts[i]);

	json_leave_list(json);
	json_leave_assoc(json);

	json_enter_assoc(json, "eliminated");
	json_int(json, eliminated);
	json_leave_assoc(json);

	json_leave_dict(json);
	return 0;
}

static void
select_guess(const Word **guess_out, const Word *top_words_buf, int num_top, int guess_idx)
{
	if (guess_idx > 0) {
		*guess_out = &top_words_buf[0];
	} else {
		int max = num_words / 50;
		if (max < 100)
			max = 100;
		if (max >= num_words)
			max = num_words;

		*guess_out = &all_words[rand() % max];
	}
}

static int
solve(int argc, char **argv)
{
	if (handle_args(argc, argv) < 0)
		return 1;
	if (check_target_loaded() < 0)
		return 1;

	Know k;
	prep_guesses(&k, num_guesses);

	json_enter_list(json);
	for (int i = num_guesses; num_opts > 0; ++i) {
		int n;
		double best_score = best_guesses(top_words_buf, max_top_words, &n, &k);

		/* shouldn't happen, but let's be safe */
		if (n <= 0)
			break;

		const Word *guess;
		select_guess(&guess, top_words_buf, n, i);

		WordColor wc;
		compare_to_target(wc, guess, &target);

		Know new;
		knowledge_from_colors(&new, guess, wc);

		absorb_knowledge(&k, &new);

		int elim = update_opts(&k);
		if (elim < 0)
			return 1;

		report(guess, best_score, wc, top_words_buf, n, best_score, elim);

		if (all_green(wc))
			break;
	}
	json_leave_list(json);

	return 0;
}

static int
coach(int argc, char **argv)
{
	if (handle_args(argc, argv) < 0)
		return 1;
	if (check_target_loaded() < 0)
		return 1;

	if (num_guesses < 1) {
		fprintf(stderr, "not enough guesses\n");
		return 1;
	}

	Know k;
	prep_guesses(&k, num_guesses - 1);

	Word *user_guess = &guesses[num_guesses - 1];
	double user_score = score_guess(user_guess, &k);

	int n;
	double best_score = best_guesses(top_words_buf, max_top_words, &n, &k);

	WordColor wc;
	compare_to_target(wc, user_guess, &target);

	Know new;
	knowledge_from_colors(&new, user_guess, wc);

	absorb_knowledge(&k, &new);

	int elim = update_opts(&k);
	if (elim < 0)
		return 1;

	report(user_guess, user_score, wc, top_words_buf, n, best_score, elim);
	return 0;
}

static int
list_flagged(int argc, char **argv, int flag)
{
	bool noflag = (flag == 0);

	json_enter_list(json);
	for (int i = 0; i < num_words; ++i)
		if (noflag || (word_attrs[i].flags & flag))
			jsonify_word(&all_words[i]);
	json_leave_list(json);

	return 0;
}

static int
list(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "list mode expected\n");
		return 1;
	}

	if (argc > 3) {
		fprintf(stderr, "too many arguments\n");
		return 1;
	}

	char *mode = argv[2];
	int flag = -1;

	struct { char *s; int flag; } assoc[] = {
		{ "all",      0           },
		{ "targets",  WA_TARGET   },
		{ "explicit", WA_EXPLICIT },
		{ "slurs",    WA_SLUR     },
	};

	for (int i = 0; i < sizeof(assoc) / sizeof(assoc[0]); ++i) {
		if (0 == strcmp(mode, assoc[i].s)) {
			flag = assoc[i].flag;
			break;
		}
	}

	if (flag == -1) {
		fprintf(stderr, "unsupported list mode\n");
		return 1;
	}

	return list_flagged(argc, argv, flag);
}

int
main(int argc, char **argv)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	srand(ts.tv_nsec);

	if (argc < 2) {
		fprintf(stderr, "mode expected\n");
		return 1;
	}

	char *mode = argv[1];
	int (*cb)(int, char **) = NULL;

	if (0 == strcmp(mode, "solve"))
		cb = solve;
	else if (0 == strcmp(mode, "coach"))
		cb = coach;
	else if (0 == strcmp(mode, "list"))
		cb = list;

	if (cb == NULL) {
		fprintf(stderr, "invalid mode\n");
		return 1;
	}

	char *index_file = getenv("WORDSMITH_INDEX");
	if (index_file == NULL) {
		fprintf(stderr, "expected WORDSMITH_INDEX\n");
		return 1;
	}

	FILE *f = fopen(index_file, "rb");
	if (f == NULL) {
		perror(index_file);
		return 1;
	}

	int idx_rc = load_index(f);

	fclose(f);

	if (idx_rc < 0)
		return 1;

	num_guesses = 0;
	guesses = malloc(argc * sizeof(Word));
	max_top_words = num_words;
	top_words_buf = malloc(sizeof(Word) * max_top_words);

	if (guesses == NULL || top_words_buf == NULL) {
		free(guesses);
		fprintf(stderr, "out of memory\n");
		return 1;
	}

	JSONWriter writer = { 0 };
	json = &writer;

	json_writer_init(json, stdout);

	int rc = cb(argc, argv);

	putchar('\n');

	json_writer_destroy(json);
	free(top_words_buf);
	free(guesses);
	free(opts);
	free(all_words);

	return rc;
}

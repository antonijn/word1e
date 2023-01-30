/*
 * Word1e bot.
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

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <score.h>
#include "word.h"

static char *dict_path = "words-index.txt";
static const char *target_str;

static enum { FIXED_TARGET, PUZZLE_TARGET, RANDOM_TARGET, } target_mode = -1;
static enum { BOT_GUESS, USER_GUESS, } guess_mode = -1;

static void
set_mode(int *out, int mode)
{
	if (*out != -1) {
		fprintf(stderr, "mode set twice\n");
		exit(1);
	}

	*out = mode;
}

static bool fixed_first_guess = true, random_word = false, secret = false;
static int initial_options = 1;
static enum { AUTO_COLOR, YES_COLOR, NO_COLOR } color = AUTO_COLOR;

static Word target;

typedef struct {
	Word guess;
	double score;
} GuessReport;

typedef Know (*Oracle)(const Word *guess, WordColor colors);
typedef bool (*Guesser)(const Know *k, GuessReport *guess, GuessReport **best, int *num_best);

static void
load_target(char *target_str)
{
	FILE *f = fmemopen((void *)target_str, strlen(target_str), "r");
	if (scan_word(f, &target) < 0) {
		fprintf(stderr, "invalid word given\n");
		exit(1);
	}
}

static char *cmd;

static void
print_opt(int i, int cols, int count, Word word)
{
	if ((i % cols) == 0)
		putchar(' ');
	putchar(' ');
	print_word(stdout, &word);
	if ((i % cols) == (cols - 1) || i == count - 1)
		putchar('\n');
}

static void
print_opts(int cols, int count)
{
	if (num_opts <= count) {
		for (int i = 0; i < num_opts; ++i)
			print_opt(i, cols, num_opts, opts[i]);
	} else {
		for (int i = 0; i + 1 < count; ++i)
			print_opt(i, cols, count, opts[i]);
		printf(" ...\n");
	}
}

static void
print_guesses(Word *top, int max, int n, double score)
{
	int m = (n < max) ? n : max;
	for (int i = 0; i < m; ++i) {
		print_word(stdout, &top[i]);
		if (i == m - 1) {
			if (n > m)
				printf("... +%d", n - m);
		} else {
			putchar('/');
		}
	}

	double exp_opts = num_opts * (1.0 - score);
	printf(" (score %.1f%%, exp %.2f)\n", score * 100.0, exp_opts);
}

static void
best_reports(const Know *know, GuessReport **best, int *num_best)
{
	const int max_top_guesses = 16;
	Word *top_words = malloc(sizeof(Word) * max_top_guesses);

	int n;
	double best_score = best_guesses(top_words, max_top_guesses, &n, know);

	int m = (n < max_top_guesses) ? n : max_top_guesses;
	GuessReport *reports = malloc(sizeof(GuessReport) * m);
	*best = reports;
	*num_best = m;

	for (int i = 0; i < m; ++i) {
		memcpy(&reports[i].guess, &top_words[i], sizeof(Word));
		reports[i].score = best_score;
	}

	free(top_words);
}

static bool
bot_guesser(const Know *know, GuessReport *guess, GuessReport **best, int *num_best)
{
	best_reports(know, best, num_best);

	if (has_no_knowledge(know) && initial_options > 1) {
		int mod = (initial_options > num_words) ? num_words : initial_options;
		int idx = rand() % mod;
		memcpy(&guess->guess, &all_words[idx], sizeof(Word));
		guess->score = word_attrs[idx].starting_score;
	} else {
		*guess = (*best)[0];
	}

	/*
	if (verbosity > -1) {
		printf("%s", "bot guess ");
		print_guesses(top5, MAX_GUESS, n, score);
	}
	*/

	return true;
}

static bool
user_guesser(const Know *k, GuessReport *guess, GuessReport **best, int *num_best)
{
	Word word;
	bool should_prompt = isatty(STDOUT_FILENO) && isatty(STDIN_FILENO);
	for (;;) {
		if (should_prompt)
			printf("> ");

		if (scan_word(stdin, &word) == 0) {
			// flush newline
			int ch;
			while ((ch = getchar()) != '\n')
				;
			break;
		}
		if (feof(stdin))
			return false;
	}

	best_reports(k, best, num_best);

	memcpy(&guess->guess, &word, sizeof(Word));
	guess->score = score_guess(&word, k);
	return true;

	/*
	printf("got score %.1f%% (%+.1f%%), exp %.2f\n",
	       guess_score * 100.0,
	       (guess_score - best_score) * 100.0,
	       num_opts * (1.0 - guess_score));
	printf("best was ");
	print_guesses(best, nbest, n, best_score);

	*score = guess_score;
	return true;
	*/
}

static void
print_playing(const Word *guess, WordColor colors)
{
	if (secret)
		return;

	printf("Playing ");
	for (int i = 0; i < 5; ++i) {
		if (color == YES_COLOR) {
			switch (colors[i]) {
			case GREEN_COLOR:
				printf("\e[1;30m\e[42m");
				break;
			case YELLOW_COLOR:
				printf("\e[1;30m\e[43m");
				break;
			case DARK_COLOR:
				printf("\e[1m");
				break;
			}
		}

		print_wordch(stdout, guess->letters[i], (i < 4) ? guess->letters[i + 1] : 0);

		if (color == YES_COLOR)
			printf("\e[0m");
	}
}

static void
print_emojis(WordColor colors)
{
	if (color == NO_COLOR) {
		if (!secret)
			putchar(' ');

		for (int i = 0; i < 5; ++i) {
			switch (colors[i]) {
			case GREEN_COLOR:
				printf("\U0001F7E9");
				break;
			case YELLOW_COLOR:
				printf("\U0001F7E8");
				break;
			case DARK_COLOR:
				printf("\U00002B1B");
				break;
			}
		}
	}
}

static Know
fixed_target_oracle(const Word *guess, WordColor wc_out)
{
	compare_to_target(wc_out, guess, &target);

	Know new;
	knowledge_from_colors(&new, guess, wc_out);

	print_playing(guess, wc_out);
	print_emojis(wc_out);
	putchar('\n');

	return new;
}

static bool
feedback_valid(char *fbstr)
{
	int i;
	for (i = 0; i < 5; ++i)
		if (!strchr(".-+", fbstr[i]))
			return false;
	return fbstr[i] == '\0';
}

static int
color_prompt(WordColor colors)
{
	char fbbuf[6];
	Know new = no_knowledge();
	do {
		if (feof(stdin))
			return EOF;

		printf("? ");
	} while (scanf("%5s", fbbuf) != 1 || !feedback_valid(fbbuf));

	for (int i = 0; i < 5; ++i) {
		switch (fbbuf[i]) {
		case '.':
			colors[i] = DARK_COLOR;
			break;
		case '-':
			colors[i] = YELLOW_COLOR;
			break;
		case '+':
			colors[i] = GREEN_COLOR;
			break;
		}
	}

	return 0;
}

static Know
puzzle_target_oracle(const Word *guess, WordColor wc_out)
{
	printf("Play ");
	print_word(stdout, guess);
	puts(".");

	WordColor wc;
	if (color_prompt(wc) < 0)
		exit(1);

	Know know;
	knowledge_from_colors(&know, guess, wc_out);
	return know;
}

static void
print_opts_left()
{
	if (verbosity <= -1)
		return;

	printf("options left: %d\n", num_opts);
	print_opts(4, 20);
}

static void
run(Guesser guesser, Oracle oracle, Know k)
{
	int guess_count = 0;
	while (num_opts > 0) {
		GuessReport guess, *best = NULL;
		int num_best = 0;

		memset(&guess, 0, sizeof(guess));

		bool cont = guesser(&k, &guess, &best, &num_best);

		if (!cont) {
			free(best);
			break;
		}

		++guess_count;

		WordColor wc = { 0 };
		Know new = oracle(&guess.guess, wc);
		absorb_knowledge(&k, &new);

		int prev_num_opts = num_opts;
		filter_opts(&k);

		int eliminated = prev_num_opts - num_opts;

		print_opts_left();

		free(best);

		if (memcmp(&target, &guess.guess, 5) == 0)
			break;
	}

	putchar('\n');
	if (num_opts) {
		printf("Got ");
		print_word(stdout, &target);
		printf(" in %d guesses.\n", guess_count);
	} else {
		printf("Didn't get ");
		print_word(stdout, &target);
		printf(" in %d guesses.\n", guess_count);
	}
}

static void
print_usage(void)
{
	static const char usage[] =
		"Usage: ./word1e-solve [OPTION]... WORD\n\n"
		"Options:\n"
		"  -c                    Coaching mode.\n"
		"  --color=<auto|yes|no> Enable/disable coloor.\n"
		"  --help                Show this message.\n"
		"  -i PATH               Use index file at PATH.\n"
		"  -j                    JSON output.\n"
		"  -q                    Quiet output.\n"
		"  -r                    Select random word.\n"
		"  -s                    Keep the target word a secret.\n"
		"  -v                    Verbose output.\n"
		"  -x                    Extended initial word selection.\n";

	puts(usage);
}

static int
handle_string_option(const char *arg, int *arg_idx, int argc, char **argv)
{
	if (0 == strcmp(arg, "--help")) {
		print_usage();
		exit(0);
	}

	if (0 == strcmp(arg, "--color=auto")) {
		color = AUTO_COLOR;
		return 0;
	}
	if (0 == strcmp(arg, "--color=yes")) {
		color = YES_COLOR;
		return 0;
	}
	if (0 == strcmp(arg, "--color=no")) {
		color = NO_COLOR;
		return 0;
	}

	fprintf(stderr, "unknown option `%s'\n", arg);
	return -1;
}
static int
handle_option(char opt, int *arg_idx, int argc, char **argv)
{
	switch (opt) {
	case 'c':
		set_mode((int *)&guess_mode, USER_GUESS);
		break;
	case 'q':
		--verbosity;
		break;
	case 's':
		secret = true;
		break;
	case 'r':
		set_mode((int *)&target_mode, RANDOM_TARGET);
		break;
	case 'v':
		++verbosity;
		break;
	case 'x':
		initial_options = 100;
		break;
	case 'i':
		if (argc <= *arg_idx + 1) {
			fprintf(stderr, "expected argument after -i\n");
			print_usage();
			return -1;
		}

		dict_path = argv[++*arg_idx];
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
		set_mode((int *)&target_mode, FIXED_TARGET);
		load_target((char *)arg);
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

	if (color == AUTO_COLOR)
		color = isatty(STDOUT_FILENO) ? YES_COLOR : NO_COLOR;

	return 0;
}

int
main(int argc, char **argv)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	srand(ts.tv_nsec);

	if (handle_args(argc, argv) < 0)
		exit(1);

	FILE *f = fopen(dict_path, "rb");
	if (f == NULL) {
		perror(cmd);
		exit(1);
	}

	if (load_index(f) < 0)
		exit(1);

	fclose(f);

	if (target_mode == RANDOM_TARGET) {
		int idx = random() % num_opts;
		memcpy(&target, &opts[idx], sizeof(Word));
		target_mode = FIXED_TARGET;
	}

	if (target_mode == -1)
		target_mode = PUZZLE_TARGET;

	if (guess_mode == -1)
		guess_mode = BOT_GUESS;

	Guesser guesser;
	Oracle oracle;
	Know k_init = no_knowledge();

	switch (target_mode) {
	case FIXED_TARGET:
		oracle = fixed_target_oracle;
		break;
	case PUZZLE_TARGET:
		oracle = puzzle_target_oracle;
		break;
	}

	switch (guess_mode) {
	case BOT_GUESS:
		guesser = bot_guesser;
		break;
	case USER_GUESS:
		guesser = user_guesser;
		break;
	}

	run(guesser, oracle, k_init);
	return 0;
}

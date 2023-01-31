/*
 * Tools for making educated word1e guesses.
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

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#include <stdbool.h>
#endif

#include <word.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Word *all_words, *opts;
Digraph *digraphs;
WordAttr *word_attrs;
int num_opts, num_words, verbosity = 0, num_digraphs;

int
index_of_word(const Word *word)
{
	for (int i = 0; i < num_words; ++i) {
		if (memcmp(all_words[i].letters, word->letters, 5) == 0)
			return i;
	}

	return -1;
}

static int
scan_letter(FILE *f)
{
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

ssize_t
load_words(FILE *f, Word **words_out)
{
	if (f == NULL)
		return -1;

	int line = 1;

	long init_pos = ftell(f);
	if (init_pos < 0)
		return -1;

	if (fseek(f, 0, SEEK_END) < 0)
		return -1;

	long final_pos = ftell(f);
	if (final_pos < 0)
		return -1;

	if (fseek(f, init_pos, init_pos) < 0)
		return -1;

	long size = final_pos - init_pos;
	size_t max_num_words = (size + 1) / 6;

	if (verbosity > 0)
		fprintf(stderr, "max %zd words...\n", max_num_words);

	Word *words = malloc(sizeof(Word) * max_num_words);

	if (words == NULL) {
		fprintf(stderr, "out of memory\n");
		return -1;
	}

	ssize_t i;
	for (i = 0; i < max_num_words; ++i) {
		int ch;
		while (isspace(ch = fgetc(f)) && ch != EOF)
			if (ch == '\n')
				++line;

		if (ch == EOF)
			break;

		ungetc(ch, f);
		if (scan_word(f, &words[i]) < 0) {
			fprintf(stderr, "error: line %d\n", line);
			goto error;
		}

		++line;
	}

	Word *resized_words = realloc(words, sizeof(Word) * i);
	if (resized_words != NULL)
		words = resized_words;

	if (verbosity > 0)
		fprintf(stderr, "read %zd words...\n", i);

	*words_out = words;
	return i;

error:
	free(words);
	return -1;
}

static int
read_attrs(FILE *f, int line)
{
	int ch = fgetc(f);
	if (ch == '\n')
		return 0;

	if (ch != ' ') {
		fprintf(stderr, "error: expected whitespace (line %d)\n", line);
		return -1;
	}

	int res = 0;
	bool should_break = false;
	while (!should_break) {
		switch (fgetc(f)) {
		case 't':
			res |= WA_TARGET;
			break;
		case 'x':
			res |= WA_EXPLICIT;
			break;
		case 's':
			res |= WA_SLUR;
			break;
		case '\n':
			should_break = true;
			break;
		default:
			fprintf(stderr, "error: unexpected attribute character (line %d)\n", line);
			return -1;
		}
	}

	return res;
}

int
load_index(FILE *f)
{
	if (f == NULL)
		return -1;

	int line = 1;

	if (fscanf(f, "%d\n", &num_words) != 1) {
		fprintf(stderr, "error: expected word count on line 1\n");
		return -1;
	}
	++line;

	int ch;
	while ((ch = fgetc(f)) == '#') {
		char lnbuf[256];
		if (!fgets(lnbuf, sizeof(lnbuf), f)) {
			fprintf(stderr, "error: unexpected eof on line %d\n", line);
			return -1;
		}

		if (!strncmp(lnbuf, "DIGRAPH ", 8)) {
			if (num_digraphs >= 32 - 26) {
				fprintf(stderr, "error: too many digraphs\n");
				return -1;
			}

			char fst, snd;
			if (sscanf(lnbuf + 8, "%c%c\n", &fst, &snd) != 2 || !isalpha(fst) || !isalpha(snd)) {
				fprintf(stderr, "error: expected two characters after #DIGRAPH\n");
				return -1;
			}

			++num_digraphs;
			digraphs = realloc(digraphs, sizeof(digraphs[0]) * num_digraphs);
			Digraph *di = &digraphs[num_digraphs - 1];
			di->fst = toupper(fst);
			di->snd = toupper(snd);
			di->repr = 'Z' + num_digraphs;
		} else {
			fprintf(stderr, "error: line %d\n", line);
			return -1;
		}

		++line;
	}
	ungetc(ch, f);

	if (verbosity > 0)
		fprintf(stderr, "reading %d words...\n", num_words);

	all_words  = malloc(sizeof(all_words[0])  * num_words);
	word_attrs = malloc(sizeof(word_attrs[0]) * num_words);
	opts       = malloc(sizeof(opts[0])       * num_words);

	if (all_words == NULL || word_attrs == NULL || opts == NULL) {
		fprintf(stderr, "out of memory\n");

		free(all_words);
		free(word_attrs);
		free(opts);
		return -1;
	}

	num_opts = 0;
	double last_score = 1.0;
	for (int i = 0; i < num_words; ++i) {
		if (scan_word(f, &all_words[i]) < 0) {
			fprintf(stderr, "error: line %d\n", line);
			return -1;
		}

		int iscore;
		if (fscanf(f, " %6d", &iscore) != 1) {
			fprintf(stderr, "error: wrong index on line %d\n", line);
			return -1;
		}

		double score = iscore / 1000000.0;
		if (score > last_score) {
			fprintf(stderr, "error: words must be given in decreasing scoring order (line %d)\n", line);
			return -1;
		}

		word_attrs[i].starting_score = last_score = score;
		int attr = read_attrs(f, line);
		if (attr < 0)
			return -1;

		word_attrs[i].flags = attr;

		if (attr == WA_TARGET) {
			memcpy(&opts[num_opts], &all_words[i], sizeof(Word));
			++num_opts;
		}

		++line;
	}

	return 0;
}

bool
has_no_knowledge(const Know *know)
{
	const Know k = no_knowledge();
	if (memcmp(know->exclude, k.exclude, sizeof(k.exclude)) != 0)
		return false;

	if (memcmp(know->hist, k.hist, sizeof(k.hist)) != 0)
		return false;

	return true;
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


void
filter_opts(const Know *know)
{
	int j = 0;
	for (int i = 0; i < num_opts; ++i)
		if (word_matches(&opts[i], know))
			memmove(&opts[j++], &opts[i], sizeof(Word));

	num_opts = j;

	Word *new_opts = realloc(opts, num_opts * sizeof(Word));
	if (new_opts != NULL)
		opts = new_opts;
}

bool
all_green(WordColor wc)
{
	for (int i = 0; i < 5; ++i)
		if (wc[i] != GREEN_COLOR)
			return false;
	return true;
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

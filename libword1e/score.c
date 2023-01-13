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
#endif

#include <word.h>
#include <score.h>
#include <threadpool.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include <sched.h>

#define MAX_WORKERS 64

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

	threadpool_destroy(pool, THREADPOOL_GRACEFUL);

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

	threadpool_destroy(pool, THREADPOOL_GRACEFUL);
	pthread_mutex_destroy(&out.lock);

	*num_out = out.num_out;
	return out.best_score;
}

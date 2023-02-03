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

#define MIN_WORK_SIZE 128
#define MAX_TASKS     256

int
count_opts(const Know *know)
{
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
	int from, to;
	const Know *know;
	const Word *guess;
	double score_part;
} ScoreTask;

static void
score_guess_worker(void *info)
{
	ScoreTask *st = info;

	Know know = *st->know;
	const Word *guess = st->guess;

	double score_part = 0.0;
	double norm = (1.0 / num_opts) * (1.0 / num_opts);

	int from = st->from, to = st->to;
	for (int j = from; j < to; ++j) {
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
score_guess_with_attr(const Word *guess, const WordAttr *attr, const Know *know)
{
	if (attr != NULL && has_no_knowledge(know))
		return attr->starting_score;

	ScoreTask tasks[MAX_TASKS];

	int num_tasks = 1 + (num_opts - 1) / MIN_WORK_SIZE;
	if (num_tasks > MAX_TASKS)
		num_tasks = MAX_TASKS;

	threadpool_t *pool = threadpool_create(cpu_count(), num_tasks, 0);
	for (int i = 0; i < num_tasks; ++i) {
		tasks[i].from = i * num_opts / num_tasks;
		tasks[i].to = (i + 1) * num_opts / num_tasks;
		tasks[i].know = know;
		tasks[i].guess = guess;

		threadpool_add(pool, score_guess_worker, &tasks[i], 0);
	}

	threadpool_destroy(pool, THREADPOOL_GRACEFUL);

	double score = 1.0;

	if ((attr == NULL || (attr->flags & WA_TARGET)) && word_matches(guess, know))
		score += (1.0 / num_opts) * (1.0 / num_opts);

	for (int i = 0; i < num_tasks; ++i)
		score += tasks[i].score_part;

	return score;
}

double
score_guess(const Word *guess, const Know *know)
{
	int i = index_of_word(guess);
	const WordAttr *attr = NULL;
	if (i >= 0 && word_attrs != NULL)
		attr = &word_attrs[i];
	return score_guess_with_attr(guess, attr, know);
}

double
score_guess_st(const Word *guess, const WordAttr *attr, const Know *know, double break_at)
{
	double guess_score = 1.0;
	double norm = (1.0 / num_opts) * (1.0 / num_opts);

	if ((attr == NULL || (attr->flags & WA_TARGET)) && word_matches(guess, know))
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
	int from, to;
	BestTaskOutput *out;
	Know know;
} BestTask;

static void
suggest(BestTaskOutput *out, int guess_idx, double guess_score)
{
	if (!suggest_slurs && (word_attrs[guess_idx].flags & WA_SLUR))
		return;

	if (guess_score > out->best_score) {
		out->num_out = 0;
		out->best_score = guess_score;
	}

	if (out->num_out < out->max_out)
		out->top[out->num_out] = all_words[guess_idx];

	++out->num_out;
}

static void
best_guess_worker(void *info)
{
	BestTask *task = info;
	BestTaskOutput *out = task->out;

	double best_local_score = 0.0;

	int from = task->from, to = task->to;
	for (int i = from; i < to; ++i) {
		double guess_score = score_guess_st(&all_words[i],
		                                    &word_attrs[i],
		                                    &task->know,
		                                    best_local_score);

		pthread_mutex_lock(&out->lock);
		if (guess_score >= out->best_score)
			suggest(out, i, guess_score);

		best_local_score = out->best_score;
		pthread_mutex_unlock(&out->lock);
	}
}

double
best_guesses(Word *top, int max_out, int *num_out, const Know *know)
{
	if (word_attrs != NULL && has_no_knowledge(know)) {
		top[0] = all_words[0];
		*num_out = 1;
		return word_attrs[0].starting_score;
	}

	if (num_opts > 0 && num_opts <= 2) {
		memcpy(&top[0], &opts[0], num_opts * sizeof(Word));
		*num_out = num_opts;
		return (5 - num_opts) * 0.25;
	}

	BestTaskOutput out = {
		.best_score = 0.0,
		.max_out = max_out,
		.top = top,
		.num_out = 0,
	};

	pthread_mutex_init(&out.lock, NULL);

	BestTask tasks[MAX_TASKS];

	int num_tasks = 1 + (num_words - 1) / MIN_WORK_SIZE;
	if (num_tasks > MAX_TASKS)
		num_tasks = MAX_TASKS;

	threadpool_t *pool = threadpool_create(cpu_count(), num_tasks, 0);
	for (int i = 0; i < num_tasks; ++i) {
		tasks[i].from = i * num_words / num_tasks;
		tasks[i].to = (i + 1) * num_words / num_tasks;
		tasks[i].know = *know;
		tasks[i].out = &out;

		threadpool_add(pool, best_guess_worker, &tasks[i], 0);
	}

	threadpool_destroy(pool, THREADPOOL_GRACEFUL);
	pthread_mutex_destroy(&out.lock);

	*num_out = out.num_out;
	return out.best_score;
}

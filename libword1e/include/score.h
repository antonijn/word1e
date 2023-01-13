#pragma once

#include <word.h>

int count_opts(const Know *know);
double score_guess(const Word *guess, const Know *know);
double score_guess_st(const Word *guess, const Know *know, double break_at);
double best_guesses(Word *top, int max_out, int *num_out, const Know *know);

// Copyright (c) 2015 MIT License by 6.172 Staff

#ifndef EVAL_H
#define EVAL_H

#include <stdbool.h>

#include "./move_gen.h"
#include "./search.h"

#define EV_SCORE_RATIO 100   // Ratio of ev_score_t values to score_t values

// ev_score_t values
#define PAWN_EV_VALUE (PAWN_VALUE*EV_SCORE_RATIO)

inline void mark_laser_path(position_t *p, color_t c, char *laser_map,
				   char mark_mask);

void mark_laser_path_with_heuristics(
    position_t *p, color_t c, char *laser_map, char mark_mask,
    int* h_squares_attackable,
    int* num_enemy_pinned_pawns);


score_t eval(position_t *p, bool verbose);

#endif  // EVAL_H

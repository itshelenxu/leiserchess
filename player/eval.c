// Copyright (c) 2015 MIT License by 6.172 Staff

#include "./eval.h"

#include <float.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "./move_gen.h"
#include "./tbassert.h"

// The reason for this constant is to handle floating point errors.
// For example, casting 12.000000 to an int is sometimes 11 and sometimes 12.
// Used in the h_squares_attackable heuristic.
#define EPSILON 1e-7

// -----------------------------------------------------------------------------
// Evaluation
// -----------------------------------------------------------------------------

typedef int32_t ev_score_t;     // Static evaluator uses "hi res" values

int RANDOMIZE;

int PCENTRAL;
int HATTACK;
int PBETWEEN;
int PCENTRAL;
int KFACE;
int KAGGRESSIVE;
int MOBILITY;
int PAWNPIN;

// Heuristics for static evaluation - described in the google doc
// mentioned in the handout.

// Table for the pcentral heuristic below.
ev_score_t pcentral_table[BOARD_WIDTH][BOARD_WIDTH] = {
  {125, 181, 220, 234, 234, 220, 181, 125},
  {181, 249, 302, 323, 323, 302, 249, 181},
  {220, 302, 375, 411, 411, 375, 302, 220},
  {234, 323, 411, 500, 500, 411, 323, 234},
  {234, 323, 411, 500, 500, 411, 323, 234},
  {220, 302, 375, 411, 411, 375, 302, 220},
  {181, 249, 302, 323, 323, 302, 249, 181},
  {125, 181, 220, 234, 234, 220, 181, 125}
};

// PCENTRAL heuristic: Bonus for Pawn near center of board
ev_score_t pcentral(fil_t f, rnk_t r) {
  double df = BOARD_WIDTH / 2 - f - 1;
  if (df < 0) {
    df = f - BOARD_WIDTH / 2;
  }
  double dr = BOARD_WIDTH / 2 - r - 1;
  if (dr < 0) {
    dr = r - BOARD_WIDTH / 2;
  }
  double bonus = 1 - sqrt(df * df + dr * dr) / BOARD_WIDTH * sqrt(2);
  return PCENTRAL * bonus;
}

// returns true if c lies on or between a and b, which are not ordered
bool between(int c, int a, int b) {
  bool x = ((c >= a) && (c <= b)) || ((c <= a) && (c >= b));
  return x;
}

// PBETWEEN heuristic: Bonus for Pawn at (f, r) in rectangle defined by Kings at the corners
ev_score_t pbetween(position_t * p, fil_t f, rnk_t r) {
  bool is_between =
    between(f, fil_of(p->kloc[WHITE]), fil_of(p->kloc[BLACK])) &&
    between(r, rnk_of(p->kloc[WHITE]), rnk_of(p->kloc[BLACK]));
  return is_between ? PBETWEEN : 0;
}


// KFACE heuristic: bonus (or penalty) for King facing toward the other King
ev_score_t kface(position_t * p, fil_t f, rnk_t r) {
  square_t sq = square_table[f][r];
  piece_t x = p->board[sq];
  color_t c = color_of(x);
  square_t opp_sq = p->kloc[opp_color(c)];
  int delta_fil = fil_of(opp_sq) - f;
  int delta_rnk = rnk_of(opp_sq) - r;
  int bonus;

  switch (ori_of(x)) {
    case NN:
      bonus = delta_rnk;
      break;

    case EE:
      bonus = delta_fil;
      break;

    case SS:
      bonus = -delta_rnk;
      break;

    case WW:
      bonus = -delta_fil;
      break;

    default:
      bonus = 0;
      tbassert(false, "Illegal King orientation.\n");
  }

  return (bonus * KFACE) / (abs(delta_rnk) + abs(delta_fil));
}

// KAGGRESSIVE heuristic: bonus for King with more space to back
ev_score_t kaggressive(position_t * p, fil_t f, rnk_t r) {
  square_t sq = square_table[f][r];
  piece_t x = p->board[sq];
  color_t c = color_of(x);
  tbassert(ptype_of(x) == KING, "ptype_of(x) = %d, sq = %d\n", ptype_of(x), sq);

  square_t opp_sq = p->kloc[opp_color(c)];
  fil_t of = fil_of(opp_sq);
  rnk_t _or = (rnk_t) rnk_of(opp_sq);

  int delta_fil = of - f;
  int delta_rnk = _or - r;

  int bonus = 0;

  if (delta_fil >= 0 && delta_rnk >= 0) {
    bonus = (f + 1) * (r + 1);
  } else if (delta_fil <= 0 && delta_rnk >= 0) {
    bonus = (BOARD_WIDTH - f) * (r + 1);
  } else if (delta_fil <= 0 && delta_rnk <= 0) {
    bonus = (BOARD_WIDTH - f) * (BOARD_WIDTH - r);
  } else if (delta_fil >= 0 && delta_rnk <= 0) {
    bonus = (f + 1) * (BOARD_WIDTH - r);
  }

  return (KAGGRESSIVE * bonus) / (BOARD_WIDTH * BOARD_WIDTH);
}

// Harmonic-ish distance: 1/(|dx|+1) + 1/(|dy|+1)
double h_dist(square_t a, square_t b) {
  //  printf("a = %d, FIL(a) = %d, RNK(a) = %d\n", a, FIL(a), RNK(a));
  //  printf("b = %d, FIL(b) = %d, RNK(b) = %d\n", b, FIL(b), RNK(b));
  int delta_fil = abs(fil_of(a) - fil_of(b));
  int delta_rnk = abs(rnk_of(a) - rnk_of(b));
  double x = (1.0 / (delta_fil + 1)) + (1.0 / (delta_rnk + 1));
  //  printf("max_dist = %d\n\n", x);
  return x;
}

// Same as mark_laser_path(), except also computes heuristics.
void mark_laser_path_with_heuristics(position_t * p, color_t c, char *laser_map,
                                     char mark_mask, double *squares_attackable,
                                     int *num_enemy_pinned_pawns) {

  square_t sq = p->kloc[c];
  square_t o_king_sq = p->kloc[opp_color(c)];
  int bdir = ori_of(p->board[sq]);

  tbassert(ptype_of(p->board[sq]) == KING,
           "ptype: %d\n", ptype_of(p->board[sq]));
  tbassert(ptype_of(p->board[o_king_sq]) == KING,
           "ptype: %d\n", ptype_of(p->board[o_king_sq]));
  tbassert(color_of(p->board[o_king_sq]) != c,
           "color: %d\n", color_of(p->board[o_king_sq]));

  laser_map[sq] |= mark_mask;
  *squares_attackable = h_dist(sq, o_king_sq) + EPSILON;

  char prev_state;
  while (true) {
    sq += beam[bdir];           //beam_of(bdir);
    prev_state = laser_map[sq];
    laser_map[sq] |= mark_mask;
    tbassert(sq < ARR_SIZE && sq >= 0, "sq: %d\n", sq);

    switch (ptype_of(p->board[sq])) {
      case EMPTY:              // empty square
        if (prev_state == 0) {
          *squares_attackable += h_dist(sq, o_king_sq);
        }
        break;
      case PAWN:               // Pawn
        if (prev_state == 0) {
          *squares_attackable += h_dist(sq, o_king_sq);
        }
        if (c != color_of(p->board[sq])) {
          // pinned an enemy pawn!
          ++(*num_enemy_pinned_pawns);
        }
        bdir = reflect_of(bdir, ori_of(p->board[sq]));
        if (bdir < 0) {         // Hit back of Pawn
          return;
        }
        break;
      case KING:               // King
        if (prev_state == 0) {
          *squares_attackable += h_dist(sq, o_king_sq);
        }
        return;                 // sorry, game over my friend!
        break;
      case INVALID:            // Ran off edge of board
        return;
        break;
      default:                 // Shouldna happen, man!
        tbassert(false, "Not cool, man.  Not cool.\n");
        break;
    }
  }
}

// Marks the path/line-of-sight of the laser until it hits a piece or goes off
// the board.
//
// p : Current board state.
// c : Color of king shooting laser.
// laser_map : End result will be stored here. Every square on the
//             path of the laser is marked with mark_mask.
// mark_mask : What each square is marked with.
extern inline void mark_laser_path(position_t * p, color_t c, char *laser_map,
                                   char mark_mask) {

  square_t sq = p->kloc[c];
  int bdir = ori_of(p->board[sq]);

  tbassert(ptype_of(p->board[sq]) == KING,
           "ptype: %d\n", ptype_of(p->board[sq]));
  laser_map[sq] |= mark_mask;

  while (true) {
    sq += beam[bdir];           // beam_of(bdir);
    laser_map[sq] |= mark_mask;
    tbassert(sq < ARR_SIZE && sq >= 0, "sq: %d\n", sq);

    switch (ptype_of(p->board[sq])) {
      case EMPTY:              // empty square
        break;
      case PAWN:               // Pawn
        bdir = reflect_of(bdir, ori_of(p->board[sq]));
        if (bdir < 0) {         // Hit back of Pawn
          return;
        }
        break;
      case KING:               // King
        return;                 // sorry, game over my friend!
        break;
      case INVALID:            // Ran off edge of board
        return;
        break;
      default:                 // Shouldna happen, man!
        tbassert(false, "Not cool, man.  Not cool.\n");
        break;
    }
  }
}

// p : Current board state.
// c : Color of king shooting laser.
// pinned_pawn_list: all the pawns of the opposite side pinned by color c's king  
// return number of the pawns pinned, i.e. the length of pinned_pawn_list   
int generate_pinned_pawn_list(position_t * p, color_t c,
                              square_t * pinned_pawn_list) {
  int pinned_pawn_count = 0;
  square_t current_loc = p->kloc[c];
  tbassert(ptype_of(p->board[current_loc]) == KING,
           "ptype: %d\n", ptype_of(p->board[current_loc]));
  int laser_dir = ori_of(p->board[current_loc]);
  piece_t current_piece;
  color_t opposite_color = opp_color(c);

  while (true) {
    current_loc += beam[laser_dir];     // beam_of(laser_dir);
    tbassert(current_loc < ARR_SIZE
             && current_loc >= 0, "current_loc: %d\n", current_loc);
    current_piece = ptype_of(p->board[current_loc]);
    if (current_piece == PAWN) {
      laser_dir = reflect_of(laser_dir, ori_of(p->board[current_loc]));
      if (laser_dir < 0) {      // Hit back of Pawn
        return pinned_pawn_count;
      }
      if (color_of(p->board[current_loc]) == opposite_color) {
        pinned_pawn_list[pinned_pawn_count++] = current_loc;
      }
    } else if ((current_piece == INVALID) || (current_piece == KING)) { // Ran off edge of board or hit KING
      return pinned_pawn_count;
    }
  }
}

// PAWNPIN Heuristic: count number of pawns that are not pinned by the
//   opposing king's laser --- and are thus mobile.
int pawnpin(position_t * p, color_t color) {
  color_t c = opp_color(color);

  char laser_map[ARR_SIZE] = {0};

  mark_laser_path(p, c, laser_map, 1);  // find path of laser given that you aren't moving

  int unpinned_pawns = 0;
  for (int i = 0; i < p->ploc[color].pawns_count; ++i) {
     if (laser_map[p->ploc[color].squares[i]] == 0) {
       unpinned_pawns += 1;
     }
  }
  return unpinned_pawns;
}

int get_king_mobility(position_t * p, char *laser_map, color_t color) {
  square_t king_sq = p->kloc[color];
  int mobility = 0;
  if (laser_map[king_sq] == 0) {
    mobility++;
  }
  for (int d = 0; d < 8; ++d) {
    square_t sq = king_sq + dir_of(d);
    if (laser_map[sq] == 0) {
      mobility++;
    }
  }
  return mobility;
}

// MOBILITY heuristic: safe squares around king of given color.
int mobility(position_t * p, color_t color) {
  color_t c = opp_color(color);
  char laser_map[ARR_SIZE];

  for (int i = 0; i < ARR_SIZE; ++i) {
    laser_map[i] = 4;           // Invalid square
  }

  for (fil_t f = 0; f < BOARD_WIDTH; ++f) {
    for (rnk_t r = 0; r < BOARD_WIDTH; ++r) {
      laser_map[square_table[f][r]] = 0;
    }
  }

  mark_laser_path(p, c, laser_map, 1);  // find path of laser given that you aren't moving

  int mobility = 0;
  square_t king_sq = p->kloc[color];
  tbassert(ptype_of(p->board[king_sq]) == KING,
           "ptype: %d\n", ptype_of(p->board[king_sq]));
  tbassert(color_of(p->board[king_sq]) == color,
           "color: %d\n", color_of(p->board[king_sq]));

  if (laser_map[king_sq] == 0) {
    mobility++;
  }
  for (int d = 0; d < 8; ++d) {
    square_t sq = king_sq + dir_of(d);
    if (laser_map[sq] == 0) {
      mobility++;
    }
  }
  return mobility;
}

// H_SQUARES_ATTACKABLE heuristic: for shooting the enemy king
double h_squares_attackable(position_t * p, color_t c) {
  char laser_map[ARR_SIZE];

  for (int i = 0; i < ARR_SIZE; ++i) {
    laser_map[i] = 4;           // Invalid square
  }

  for (fil_t f = 0; f < BOARD_WIDTH; ++f) {
    for (rnk_t r = 0; r < BOARD_WIDTH; ++r) {
      laser_map[square_table[f][r]] = 0;
    }
  }

  mark_laser_path(p, c, laser_map, 1);  // 1 = path of laser with no moves

  square_t o_king_sq = p->kloc[opp_color(c)];
  tbassert(ptype_of(p->board[o_king_sq]) == KING,
           "ptype: %d\n", ptype_of(p->board[o_king_sq]));
  tbassert(color_of(p->board[o_king_sq]) != c,
           "color: %d\n", color_of(p->board[o_king_sq]));

  double h_attackable = EPSILON;
  for (fil_t f = 0; f < BOARD_WIDTH; f++) {
    for (rnk_t r = 0; r < BOARD_WIDTH; r++) {
      square_t sq = square_table[f][r];
      if (laser_map[sq] != 0) {
        h_attackable += h_dist(sq, o_king_sq);
      }
    }
  }
  return h_attackable;
}

// Static evaluation.  Returns score
score_t eval(position_t * p, bool verbose) {
  // seed rand_r with a value of 1, as per
  // http://linux.die.net/man/3/rand_r
  static __thread unsigned int seed = 1;
  // verbose = true: print out components of score
  ev_score_t score[2] = { 0, 0 };
  //  int corner[2][2] = { {INF, INF}, {INF, INF} };
  ev_score_t bonus;
  char buf[MAX_CHARS_IN_MOVE];

  if (!verbose) {
    for (int c = 0; c < 2; ++c) {

      // Pawn heuristics
      for (int i = 0; i < p->ploc[c].pawns_count; ++i) {
        fil_t f = fil_of(p->ploc[c].squares[i]);
        rnk_t r = rnk_of(p->ploc[c].squares[i]);

        // MATERIAL heuristic: Bonus for each Pawn
        score[c] += PAWN_EV_VALUE;

        // PBETWEEN heuristic
        score[c] += pbetween(p, f, r);

        // PCENTRAL heuristic
        score[c] += pcentral_table[f][r];
        tbassert(pcentral_table[f][r] == pcentral(f, r), "\n");
      }

      // King heuristics
      fil_t f = fil_of(p->kloc[c]);
      rnk_t r = rnk_of(p->kloc[c]);

      // KFACE heuristic
      score[c] += kface(p, f, r);

      // KAGGRESSIVE heuristic
      score[c] += kaggressive(p, f, r);
    }

  } else {
    // Verbose output

    for (fil_t f = 0; f < BOARD_WIDTH; f++) {
      for (rnk_t r = 0; r < BOARD_WIDTH; r++) {
        square_t sq = square_table[f][r];
        piece_t x = p->board[sq];
        color_t c = color_of(x);
        if (verbose) {
          square_to_str(sq, buf, MAX_CHARS_IN_MOVE);
        }

        switch (ptype_of(x)) {
          case EMPTY:
            break;
          case PAWN:
            // MATERIAL heuristic: Bonus for each Pawn
            bonus = PAWN_EV_VALUE;
            if (verbose) {
              printf("MATERIAL bonus %d for %s Pawn on %s\n", bonus,
                     color_to_str(c), buf);
            }
            score[c] += bonus;

            // PBETWEEN heuristic
            bonus = pbetween(p, f, r);
            if (verbose) {
              printf("PBETWEEN bonus %d for %s Pawn on %s\n", bonus,
                     color_to_str(c), buf);
            }
            score[c] += bonus;

            // PCENTRAL heuristic
            bonus += pcentral_table[f][r];
            tbassert(pcentral_table[f][r] == pcentral(f, r), "\n");
            if (verbose) {
              printf("PCENTRAL bonus %d for %s Pawn on %s\n", bonus,
                     color_to_str(c), buf);
            }
            score[c] += bonus;
            break;

          case KING:
            // KFACE heuristic
            bonus = kface(p, f, r);
            if (verbose) {
              printf("KFACE bonus %d for %s King on %s\n", bonus,
                     color_to_str(c), buf);
            }
            score[c] += bonus;

            // KAGGRESSIVE heuristic
            bonus = kaggressive(p, f, r);
            if (verbose) {
              printf("KAGGRESSIVE bonus %d for %s King on %s\n", bonus,
                     color_to_str(c), buf);
            }
            score[c] += bonus;
            break;
          case INVALID:
            break;
          default:
            tbassert(false, "Jose says: no way!\n");    // No way, Jose!
        }
      }
    }
  }

  // Initialize laser map.
  // char laser_map[2][ARR_SIZE];
  char white_laser_map[ARR_SIZE];
  char black_laser_map[ARR_SIZE];
  for (int i = 0; i < ARR_SIZE; ++i) {
    // Invalid squares
    white_laser_map[i] = 4;
    black_laser_map[i] = 4;
  }
  for (fil_t f = 0; f < BOARD_WIDTH; ++f) {
    for (rnk_t r = 0; r < BOARD_WIDTH; ++r) {
      white_laser_map[square_table[f][r]] = 0;
      black_laser_map[square_table[f][r]] = 0;
    }
  }

  // Compute some values for heuristics.
  double squares_attackable[2] = { 0, 0 };
  int num_pinned_pawns[2] = { 0, 0 };
  mark_laser_path_with_heuristics(p, WHITE, white_laser_map, 1,
                                  &(squares_attackable[WHITE]),
                                  &(num_pinned_pawns[BLACK]));
  mark_laser_path_with_heuristics(p, BLACK, black_laser_map, 1,
                                  &(squares_attackable[BLACK]),
                                  &(num_pinned_pawns[WHITE]));

  // H_SQUARES_ATTACKABLE heuristic
  score[WHITE] += HATTACK * (int) squares_attackable[WHITE];
  score[BLACK] += HATTACK * (int) squares_attackable[BLACK];
  if (verbose) {
    printf("HATTACK bonus %d for White\n",
           HATTACK * (int) squares_attackable[WHITE]);
    printf("HATTACK bonus %d for Black\n",
           HATTACK * (int) squares_attackable[BLACK]);
  }
  tbassert((int) h_squares_attackable(p, WHITE) ==
           (int) squares_attackable[WHITE], "\n");
  tbassert((int) h_squares_attackable(p, BLACK) ==
           (int) squares_attackable[BLACK], "\n");

  // MOBILITY heuristic
  score[WHITE] += MOBILITY * get_king_mobility(p, black_laser_map, WHITE);
  score[BLACK] += MOBILITY * get_king_mobility(p, white_laser_map, BLACK);
  if (verbose) {
    printf("MOBILITY bonus %d for White\n",
           MOBILITY * get_king_mobility(p, black_laser_map, WHITE));
    printf("MOBILITY bonus %d for Black\n",
           MOBILITY * get_king_mobility(p, white_laser_map, BLACK));
  }
  tbassert(mobility(p, WHITE) == get_king_mobility(p, black_laser_map, WHITE), "\n");
  tbassert(mobility(p, BLACK) == get_king_mobility(p, white_laser_map, BLACK), "\n");

  // PAWNPIN heuristic --- is a pawn immobilized by the enemy laser.
  score[WHITE] +=
    PAWNPIN * (p->ploc[WHITE].pawns_count - num_pinned_pawns[WHITE]);
  score[BLACK] +=
    PAWNPIN * (p->ploc[BLACK].pawns_count - num_pinned_pawns[BLACK]);
  tbassert(pawnpin(p, WHITE) ==
           (p->ploc[WHITE].pawns_count - num_pinned_pawns[WHITE]), "\n");
  tbassert(pawnpin(p, BLACK) ==
           (p->ploc[BLACK].pawns_count - num_pinned_pawns[BLACK]), "\n");

  // score from WHITE point of view
  ev_score_t tot = score[WHITE] - score[BLACK];

  if (RANDOMIZE) {
    ev_score_t z = rand_r(&seed) % (RANDOMIZE * 2 + 1);
    tot = tot + z - RANDOMIZE;
  }

  if (color_to_move_of(p) == BLACK) {
    tot = -tot;
  }

  return tot / EV_SCORE_RATIO;
}

// Copyright (c) 2015 MIT License by 6.172 Staff

#include "./eval.h"

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "./move_gen.h"
#include "./tbassert.h"

// The reason for this constant is to handle floating point errors.
// For example, casting 12.000000 to an int is sometimes 11 and sometimes 12.
// Used in the h_squares_attackable heuristic.
#define EPSILON 1e-7
#define NUM_SENTINELS (ARR_SIZE - (BOARD_WIDTH * BOARD_WIDTH))
#define between(c, a, b) ((((c) >= (a)) && ((c) <= (b))) || (((c) <= (a)) && ((c) >= (b))))
#define MIN_SQUARE_VALUE 68

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
  float df = BOARD_WIDTH / 2 - f - 1;
  if (df < 0) {
    df = f - BOARD_WIDTH / 2;
  }
  float dr = BOARD_WIDTH / 2 - r - 1;
  if (dr < 0) {
    dr = r - BOARD_WIDTH / 2;
  }
  float bonus = 1 - sqrt(df * df + dr * dr) / BOARD_WIDTH * sqrt(2);
  return PCENTRAL * bonus;
}

// returns true if c lies on or between a and b, which are not ordered
// bool between(int c, int a, int b) {
//   bool x = ((c >= a) && (c <= b)) || ((c <= a) && (c >= b));
//   return x;
// }

// PBETWEEN heuristic: Bonus for Pawn at (f, r) in rectangle defined by Kings at the corners
ev_score_t pbetween(position_t * p, fil_t f, rnk_t r) {
  bool is_between =
    between(f, fil_of(p->kloc[WHITE]), fil_of(p->kloc[BLACK])) &&
    between(r, rnk_of(p->kloc[WHITE]), rnk_of(p->kloc[BLACK]));
  //return is_between ? PBETWEEN : 0;
  return (-((int) is_between)) & PBETWEEN;
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

  return (KAGGRESSIVE * bonus) / BOARD_SIZE;
}

float h_dist_table[BOARD_WIDTH][BOARD_WIDTH] = {
  {
   2.000000000000000000000e+00,
   1.500000000000000000000e+00,
   1.333333333333333259318e+00,
   1.250000000000000000000e+00,
   1.199999999999999955591e+00,
   1.166666666666666740682e+00,
   1.142857142857142793702e+00,
   1.125000000000000000000e+00},
  {
   1.500000000000000000000e+00,
   1.000000000000000000000e+00,
   8.333333333333332593185e-01,
   7.500000000000000000000e-01,
   6.999999999999999555911e-01,
   6.666666666666666296592e-01,
   6.428571428571427937015e-01,
   6.250000000000000000000e-01},
  {
   1.333333333333333259318e+00,
   8.333333333333332593185e-01,
   6.666666666666666296592e-01,
   5.833333333333332593185e-01,
   5.333333333333333259318e-01,
   5.000000000000000000000e-01,
   4.761904761904761640423e-01,
   4.583333333333333148296e-01},
  {
   1.250000000000000000000e+00,
   7.500000000000000000000e-01,
   5.833333333333332593185e-01,
   5.000000000000000000000e-01,
   4.500000000000000111022e-01,
   4.166666666666666296592e-01,
   3.928571428571428492127e-01,
   3.750000000000000000000e-01},
  {
   1.199999999999999955591e+00,
   6.999999999999999555911e-01,
   5.333333333333333259318e-01,
   4.500000000000000111022e-01,
   4.000000000000000222045e-01,
   3.666666666666666962726e-01,
   3.428571428571428603149e-01,
   3.250000000000000111022e-01},
  {
   1.166666666666666740682e+00,
   6.666666666666666296592e-01,
   5.000000000000000000000e-01,
   4.166666666666666296592e-01,
   3.666666666666666962726e-01,
   3.333333333333333148296e-01,
   3.095238095238095343831e-01,
   2.916666666666666296592e-01},
  {
   1.142857142857142793702e+00,
   6.428571428571427937015e-01,
   4.761904761904761640423e-01,
   3.928571428571428492127e-01,
   3.428571428571428603149e-01,
   3.095238095238095343831e-01,
   2.857142857142856984254e-01,
   2.678571428571428492127e-01},
  {
   1.125000000000000000000e+00,
   6.250000000000000000000e-01,
   4.583333333333333148296e-01,
   3.750000000000000000000e-01,
   3.250000000000000111022e-01,
   2.916666666666666296592e-01,
   2.678571428571428492127e-01,
   2.500000000000000000000e-01}
};


// indices of board borders
int edges[NUM_SENTINELS] = {
  0, 1, 2, 3, 4, 5,
  6, 7, 8, 9, 10, 19,
  20, 29, 30, 39, 40, 49,
  50, 59, 60, 69, 70, 79,
  80, 89, 90, 91, 92, 93,
  94, 95, 96, 97, 98, 99
};


// Harmonic-ish distance: 1/(|dx|+1) + 1/(|dy|+1)
static inline float h_dist(square_t a, square_t b) {
  //  printf("a = %d, FIL(a) = %d, RNK(a) = %d\n", a, FIL(a), RNK(a));
  //  printf("b = %d, FIL(b) = %d, RNK(b) = %d\n", b, FIL(b), RNK(b));
  int delta_fil = abs(fil_of(a) - fil_of(b));
  int delta_rnk = abs(rnk_of(a) - rnk_of(b));
  //tbassert(h_dist_table[delta_fil][delta_rnk] ==
  //         (1.0 / (delta_fil + 1)) + (1.0 / (delta_rnk + 1)), "\n");
  return h_dist_table[delta_fil][delta_rnk];
}

// Same as mark_laser_path(), except also computes heuristics.
void mark_laser_path_with_heuristics(position_t * p, color_t c, char *laser_map,
                                     char mark_mask, float *squares_attackable,
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
        // int oldbdir = bdir;
        bdir = reflect[bdir][ori_of(p->board[sq])];
        // tbassert (bdir == reflect_of(oldbdir, ori_of(p->board[sq])), "\n");
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
        bdir = reflect[bdir][ori_of(p->board[sq])];
        // tbassert (newbdir == reflect_of(bdir, ori_of(p->board[sq])), "\n");
        // bdir = newbdir;
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

// generate piece_list along the path/line-of-sight of the laser until it hits a piece or goes off
// the board.
//
// p : Current board state.
// c : Color of king shooting laser.
// piece_list: the list of pieces along the laser path
// return the size of piece_list
int generate_pieces_along_laser_path(position_t * p, color_t c, square_t* piece_list) {
  int piece_count = 0;
  square_t current_loc = p->kloc[c];
  int laser_dir = ori_of(p->board[current_loc]);
  piece_list[piece_count++] = current_loc;

  while (true) {
    square_t original_loc = current_loc;
    current_loc = next_piece(p, current_loc, laser_dir);
    tbassert(current_loc < ARR_SIZE
             && current_loc >= 0, "current_loc: %d\n", current_loc);

    piece_t current_piece = ptype_of(p->board[current_loc]);

    if (current_piece == PAWN) {
      // laser_dir = reflect_of(laser_dir, ori_of(p->board[current_loc]));
      // int olddir = laser_dir;
      laser_dir = reflect[laser_dir][ori_of(p->board[current_loc])];
      piece_list[piece_count++] = current_loc;

      // tbassert(laser_dir == reflect_of(olddir, ori_of(p->board[current_loc])), "\n");
      if (laser_dir < 0) {      // Hit back of Pawn
        piece_list[piece_count] = -1;
        return piece_count;
      }
    } else if (current_piece == KING) { // hit KING
      piece_list[piece_count++] = current_loc;
      piece_list[piece_count] = -1;
      return piece_count;
    } else {
      square_t next_loc = original_loc + beam[laser_dir];
      if (ptype_of(p->board[next_loc]) == EMPTY) {
        int rank = rnk_of(original_loc);
        int file = fil_of(original_loc);
        switch (laser_dir) {
          case NN: {
            piece_list[piece_count] = original_loc + 7 - rank;
            //piece_list[piece_count + 1] = next_loc;
            //piece_list[piece_count + 2] = laser_dir;
            break;
          }
          case SS: {
            piece_list[piece_count] = original_loc - rank;
            //piece_list[piece_count + 1] = next_loc;
            //piece_list[piece_count + 2] = laser_dir;
            break;
          }
          case WW: {
            piece_list[piece_count] = original_loc - file * ARR_WIDTH;
            //piece_list[piece_count + 1] = next_loc;
            //piece_list[piece_count + 2] = laser_dir;
            //piece_list[piece_count + 3] = file;
            break;
          }
          case EE: {
            piece_list[piece_count] = original_loc + (7 - file) * ARR_WIDTH;
            //piece_list[piece_count + 1] = next_loc;
            //piece_list[piece_count + 2] = laser_dir;
            //piece_list[piece_count + 3] = file;
            break;
          }
        }
        return piece_count;
      } else {
        piece_list[piece_count] = -1;
        return piece_count;
      }
    }
  }
}

// p : Current board state.
// c : Color of king shooting laser.
// pinned_pawn_list: all the pawns of the opposite side pinned by color c's king
// return number of the pawns pinned, i.e. the length of pinned_pawn_list
int generate_pinned_pawn_list(position_t * p, color_t c,
                              square_t * pinned_pawn_list) {
  tbassert(pinned_pawn_list, "\n");

  int pinned_pawn_count = 0;
  square_t current_loc = p->kloc[c];

  tbassert(ptype_of(p->board[current_loc]) == KING,
           "ptype: %d\n", ptype_of(p->board[current_loc]));

  int laser_dir = ori_of(p->board[current_loc]);
  piece_t current_piece;
  color_t opposite_color = opp_color(c);

  while (true) {
    current_loc = next_piece(p, current_loc, laser_dir);
    //current_loc += beam[laser_dir];
    tbassert(current_loc < ARR_SIZE
             && current_loc >= 0, "current_loc: %d\n", current_loc);

    current_piece = ptype_of(p->board[current_loc]);

    if (current_piece == PAWN) {
      // laser_dir = reflect_of(laser_dir, ori_of(p->board[current_loc]));
      // int olddir = laser_dir;
      laser_dir = reflect[laser_dir][ori_of(p->board[current_loc])];
      // tbassert(laser_dir == reflect_of(olddir, ori_of(p->board[current_loc])), "\n");
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
int pawnpin(position_t * p, color_t c, square_t* piece_list, int piece_count) {
  int unpinned_pawns = p->ploc[c].pawns_count;
  for (int i = 1; i < piece_count; i++) {
    if (ptype_of(p->board[piece_list[i]]) == PAWN && color_of(p->board[piece_list[i]]) == c) {
      unpinned_pawns--;
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

int king_mobility(position_t * p, color_t c, square_t* piece_list, int piece_count) {
  int mobility = 0;
  square_t king_sq = p->kloc[c];
  mobility++;

  int num_iter = piece_count - 1;
  if (piece_list[piece_count] != -1 ) {
    num_iter++;
  }

  for (int i = 0; i < num_iter; i++) {
    square_t first_sq = piece_list[i];
    square_t next_sq = piece_list[i + 1];
    if (between(rnk_of(king_sq), rnk_of(first_sq), rnk_of(next_sq)) && between(fil_of(king_sq), fil_of(first_sq), fil_of(next_sq))) {
      mobility--;
      break;
    }
  }

  for (int d = 0; d < 8; ++d) {
    square_t sq = king_sq + dir_of(d);
    if (ptype_of(p->board[sq]) != INVALID) {  // outside the board
      mobility++;
      for (int i = 0; i < num_iter; i++) {
        square_t first_sq = piece_list[i];
        square_t next_sq = piece_list[i + 1];
        if (between(rnk_of(sq), rnk_of(first_sq), rnk_of(next_sq)) && between(fil_of(sq), fil_of(first_sq), fil_of(next_sq))) {
          mobility--;
          break;
        }
      }
    }
  }
  return mobility;
}


// MOBILITY heuristic: safe squares around king of given color.
int mobility(position_t * p, color_t color) {
  color_t c = opp_color(color);
  char laser_map[ARR_SIZE];

  // manually set the border
  // laser_map_edges(laser_map);
  for (int i = 0; i < NUM_SENTINELS; i++) {
    laser_map[edges[i]] = 4;
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
float h_squares_attackable(position_t * p, color_t c, square_t* piece_list, int piece_count) {
  square_t o_king_sq = p->kloc[opp_color(c)];
  tbassert(ptype_of(p->board[o_king_sq]) == KING,
           "ptype: %d\n", ptype_of(p->board[o_king_sq]));
  tbassert(color_of(p->board[o_king_sq]) != c,
           "color: %d\n", color_of(p->board[o_king_sq]));

  float h_attackable = EPSILON;

  for (int i = 0; i < piece_count; i++) {
    h_attackable += h_dist(piece_list[i], o_king_sq);
  }

  return h_attackable;
}

// Static evaluation.  Returns score
/*score_t eval(position_t * p, bool verbose) {
  // seed rand_r with a value of 1, as per
  // http://linux.die.net/man/3/rand_r
  static __thread unsigned int seed = 1;
  // verbose = true: print out components of score
  ev_score_t score[2] = { 0, 0 };
  //  int corner[2][2] = { {INF, INF}, {INF, INF} };

  for (int c = 0; c < 2; ++c) {

    // Pawn heuristics
    for (int i = 0; i < p->ploc[c].pawns_count; ++i) {
      fil_t f = fil_of(p->ploc[c].squares[i]);
      rnk_t r = rnk_of(p->ploc[c].squares[i]);

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
 

  // MATERIAL heuristic: Bonus for each Pawn
  score[0] += p->ploc[0].pawns_count * PAWN_EV_VALUE;
  score[1] += p->ploc[1].pawns_count * PAWN_EV_VALUE;


  // Initialize piece_list along the laser path.
  square_t white_piece_list[MAX_NUM_PIECES];
  square_t black_piece_list[MAX_NUM_PIECES];

  int white_piece_count = generate_pieces_along_laser_path(p, WHITE, white_piece_list);
  int black_piece_count = generate_pieces_along_laser_path(p, BLACK, black_piece_list);

  // H_SQUARES_ATTACKABLE heuristic
  score[WHITE] += HATTACK * (int) h_squares_attackable(p, WHITE, white_piece_list, white_piece_count);
  score[BLACK] += HATTACK * (int) h_squares_attackable(p, BLACK, black_piece_list, black_piece_count);

  // MOBILITY heuristic
  score[WHITE] += MOBILITY * king_mobility(p, WHITE, white_piece_list, white_piece_count);
  score[BLACK] += MOBILITY * king_mobility(p, BLACK, black_piece_list, black_piece_count);

  // PAWNPIN heuristic --- is a pawn immobilized by the enemy laser.
  score[WHITE] += PAWNPIN * pawnpin(p, WHITE, white_piece_list, white_piece_count);
  score[BLACK] += PAWNPIN * pawnpin(p, BLACK, black_piece_list, black_piece_count);

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
}*/

// Static evaluation.  Returns score
score_t eval(position_t * p, bool verbose) {

    // seed rand_r with a value of 1, as per
  // http://linux.die.net/man/3/rand_r
  static __thread unsigned int seed = 1;
  // verbose = true: print out components of score
  ev_score_t score[2] = { 0, 0 };
  //  int corner[2][2] = { {INF, INF}, {INF, INF} };
  //ev_score_t bonus;
  //char buf[MAX_CHARS_IN_MOVE];

  for (int c = 0; c < 2; ++c) {

    // Pawn heuristics
    for (int i = 0; i < p->ploc[c].pawns_count; ++i) {
      fil_t f = fil_of(p->ploc[c].squares[i]);
      rnk_t r = rnk_of(p->ploc[c].squares[i]);

      // PBETWEEN heuristic
      score[c] += pbetween(p, f, r);

      // PCENTRAL heuristic
      score[c] += pcentral_table[f][r];
      //tbassert(pcentral_table[f][r] == pcentral(f, r), "\n");
    }

    // King heuristics
    fil_t f = fil_of(p->kloc[c]);
    rnk_t r = rnk_of(p->kloc[c]);

    // KFACE heuristic
    score[c] += kface(p, f, r);

    // KAGGRESSIVE heuristic
    score[c] += kaggressive(p, f, r);
  }
 
  // MATERIAL heuristic: Bonus for each Pawn
  score[0] += p->ploc[0].pawns_count * PAWN_EV_VALUE;
  score[1] += p->ploc[1].pawns_count * PAWN_EV_VALUE;

  // Initialize laser map.
  // char laser_map[2][ARR_SIZE];
  char white_laser_map[ARR_SIZE];
  char black_laser_map[ARR_SIZE];

  // fill in invalid squares
  for (int i = 0; i < NUM_SENTINELS; i++) {
    white_laser_map[edges[i]] = 4;
    black_laser_map[edges[i]] = 4;
  }
  // for (int i = 0; i < ARR_SIZE; ++i) {
  //   // Invalid squares
  //   white_laser_map[i] = 4;
  //   black_laser_map[i] = 4;
  // }
  for (int i = RNK_ORIGIN; i < ARR_WIDTH - 1; ++i) {
    for (int j = RNK_ORIGIN; j < ARR_WIDTH - 1; ++j) {
      white_laser_map[ARR_WIDTH * i + j] = 0;
      black_laser_map[ARR_WIDTH * i + j] = 0;
    }
  }

  // Compute some values for heuristics.
  float squares_attackable[2] = { 0, 0 };
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

  // MOBILITY heuristic
  score[WHITE] += MOBILITY * get_king_mobility(p, black_laser_map, WHITE);
  score[BLACK] += MOBILITY * get_king_mobility(p, white_laser_map, BLACK);

  // PAWNPIN heuristic --- is a pawn immobilized by the enemy laser.
  score[WHITE] += PAWNPIN * (p->ploc[WHITE].pawns_count - num_pinned_pawns[WHITE]);
  score[BLACK] += PAWNPIN * (p->ploc[BLACK].pawns_count - num_pinned_pawns[BLACK]);
  
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

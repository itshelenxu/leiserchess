// Copyright (c) 2015 MIT License by 6.172 Staff

#include "./move_gen.h"

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include "./eval.h"
#include "./fen.h"
#include "./search.h"
#include "./tbassert.h"
#include "./util.h"

#define MAX(x, y)  ((x) > (y) ? (x) : (y))
#define MIN(x, y)  ((x) < (y) ? (x) : (y))

// square table definition


// 16x16 square table
const int large_square_table[8][8] = {
  {68, 69, 70, 71, 72, 73, 74, 75},
  {84, 85, 86, 87, 88, 89, 90, 91},
  {100, 101, 102, 103, 104, 105, 106, 107},
  {116, 117, 118, 119, 120, 121, 122, 123},
  {132, 133, 134, 135, 136, 137, 138, 139},
  {148, 149, 150, 151, 152, 153, 154, 155},
  {164, 165, 166, 167, 168, 169, 170, 171},
  {180, 181, 182, 183, 184, 185, 186, 187}
};

// 10X10
const int square_table[BOARD_WIDTH][BOARD_WIDTH] = {
  {11, 12, 13, 14, 15, 16, 17, 18},
  {21, 22, 23, 24, 25, 26, 27, 28},
  {31, 32, 33, 34, 35, 36, 37, 38},
  {41, 42, 43, 44, 45, 46, 47, 48},
  {51, 52, 53, 54, 55, 56, 57, 58},
  {61, 62, 63, 64, 65, 66, 67, 68},
  {71, 72, 73, 74, 75, 76, 77, 78},
  {81, 82, 83, 84, 85, 86, 87, 88}
};

int USE_KO;                     // Respect the Ko rule

static char *color_strs[2] = { "White", "Black" };

char *color_to_str(color_t c) {
  return color_strs[c];
}


// helper function
// first compare by rank, then by file
int compare_sq(square_t s, square_t t) {
  if (fil_of(s) < fil_of(t)) {
    return -1;
  }
  if (fil_of(s) > fil_of(t)) {
    return 1;
  }

  if (rnk_of(s) < rnk_of(t)) {
    return -1;
  }
  if (rnk_of(s) > rnk_of(t)) {
    return 1;
  }
  // ranks are equal
  return 0;
}

// -----------------------------------------------------------------------------
// Piece getters and setters (including color, ptype, orientation)
// -----------------------------------------------------------------------------

// which color is moving next
color_t color_to_move_of(position_t * p) {
  return (p->ply & 1); 
}

color_t opp_color(color_t c) {
  if (c == WHITE) {
    return BLACK;
  } else {
    return WHITE;
  }
}

void set_color(piece_t * x, color_t c) {
  tbassert((c >= 0) & (c <= COLOR_MASK), "color: %d\n", c);
  *x = ((c & COLOR_MASK) << COLOR_SHIFT) | (*x & ~(COLOR_MASK << COLOR_SHIFT));
}

void set_ptype(piece_t * x, ptype_t pt) {
  *x = ((pt & PTYPE_MASK) << PTYPE_SHIFT) | (*x & ~(PTYPE_MASK << PTYPE_SHIFT));
}

void set_ori(piece_t * x, int ori) {
  *x = ((ori & ORI_MASK) << ORI_SHIFT) | (*x & ~(ORI_MASK << ORI_SHIFT));
}

void set_rank_and_file(position_t * p, square_t sq) {
  int rank = rnk_of(sq);
  int file = fil_of(sq);

  p->files[file] |= (1 << rank);
  p->ranks[rank] |= (1 << file);
}

void remove_rank_and_file(position_t * p, square_t sq) {
  int rank = rnk_of(sq);
  int file = fil_of(sq);

  p->files[file] &= ~(1 << rank);
  p->ranks[rank] &= ~(1 << file);
}

// find the location of next piece along the laser path 
// return 0 if go out of the board
square_t next_piece(position_t * p, square_t current, king_ori_t dir) {
  int rank = rnk_of(current);
  int file = fil_of(current);

  switch (dir) {
    case NN:{
        uint8_t bit_array = p->files[file];
        uint8_t bits_left = bit_array >> (rank + 1);
        if (bits_left == 0) {   // laser goes out of the board
          return 0;  
        } else {
          int trailing_zeros = __builtin_ctz(bits_left);
          return current + trailing_zeros + 1;      // location of next piece 
        }
      }
    case SS:{
        uint8_t bit_array = p->files[file];
        uint8_t bits_left = bit_array << (8 - rank);
        if (bits_left == 0) {   // laser goes out of the board
          return 0; 
        } else {
          int leading_zeros = __builtin_clz(bits_left) - 24;    // __builtin_clz is for 32 bit unsigned int  
          tbassert(((file + FIL_ORIGIN) * ARR_WIDTH + RNK_ORIGIN + rank -
                    leading_zeros - 1) < ARR_SIZE
                   && ((file + FIL_ORIGIN) * ARR_WIDTH + RNK_ORIGIN + rank -
                       leading_zeros - 1) >= 0,
                   "file:%d, rank: %d, bits_left: %d, bit_array: %d, leading zeros: %d\n",
                   file, rank, bits_left, bit_array, leading_zeros);
          return current - leading_zeros - 1;       // location of next piece 
        }
      }
    case WW:{
        uint8_t bit_array = p->ranks[rank];
        uint8_t bits_left = bit_array << (8 - file);
        if (bits_left == 0) {   // laser goes out of the board
          return 0;
        } else {
          int leading_zeros = __builtin_clz(bits_left) - 24;    // __builtin_clz is for 32 bit unsigned int 
          tbassert(((file + FIL_ORIGIN - leading_zeros - 1) * ARR_WIDTH +
                    RNK_ORIGIN + rank) < ARR_SIZE
                   && ((file + FIL_ORIGIN - leading_zeros - 1) * ARR_WIDTH +
                       RNK_ORIGIN + rank) >= 0, "error\n");
          return current - (leading_zeros + 1) * ARR_WIDTH;      // location of next piece 
        }
      }
    case EE:{
        uint8_t bit_array = p->ranks[rank];
        uint8_t bits_left = bit_array >> (file + 1);
        if (bits_left == 0) {   // laser goes out of the board
          return 0;
        } else {
          int trailing_zeros = __builtin_ctz(bits_left);
          return current + (trailing_zeros + 1) * ARR_WIDTH;     // location of next piece 
        }
      }

    default:
      tbassert(false, "error");
      return -1;
  }
}

// -----------------------------------------------------------------------------
// Piece orientation strings
// -----------------------------------------------------------------------------


/*// King orientations
char *king_ori_to_rep[2][NUM_ORI] = { {"NN", "EE", "SS", "WW"},
{"nn", "ee", "ss", "ww"}
};

// Pawn orientations
char *pawn_ori_to_rep[2][NUM_ORI] = { {"NW", "NE", "SE", "SW"},
{"nw", "ne", "se", "sw"}
};
*/
char *nesw_to_str[NUM_ORI] = { "north", "east", "south", "west" };

// -----------------------------------------------------------------------------
// Board hashing
// -----------------------------------------------------------------------------

// Zobrist hashing
//[
// https://chessprogramming.wikispaces.com/Zobrist+Hashing
//
// NOTE: Zobrist hashing uses piece_t as an integer index into to the zob table.
// So if you change your piece representation, you'll need to recompute what the
// old piece representation is when indexing into the zob table to get the same
// node counts.
static uint64_t zob[ARR_SIZE][1 << PIECE_SIZE];
static uint64_t zob_color;
uint64_t myrand();

uint64_t compute_zob_key(position_t * p) {
  uint64_t key = 0;
  for (fil_t f = 0; f < BOARD_WIDTH; f++) {
    for (rnk_t r = 0; r < BOARD_WIDTH; r++) {
      square_t sq = square_table[f][r];
      // key ^= zob[square_table[f][r]][p->board[sq]];
      key ^= zob[sq][p->board[sq]];
    }
  }
  if (color_to_move_of(p) == BLACK)
    key ^= zob_color;

  return key;
}

void init_zob() {
  for (int i = 0; i < ARR_SIZE; i++) {
    for (int j = 0; j < (1 << PIECE_SIZE); j++) {
      zob[i][j] = myrand();
    }
  }
  zob_color = myrand();
}

// -----------------------------------------------------------------------------
// Squares
// -----------------------------------------------------------------------------

// // For no square, use 0, which is guaranteed to be off board
square_t square_of(fil_t f, rnk_t r) {
  square_t s = ARR_WIDTH * (FIL_ORIGIN + f) + RNK_ORIGIN + r;
  DEBUG_LOG(1, "Square of (file %d, rank %d) is %d\n", f, r, s);
  tbassert((s >= 0) && (s < ARR_SIZE), "s: %d\n", s);
  return s;
}

// converts a square to string notation, returns number of characters printed
int square_to_str(square_t sq, char *buf, size_t bufsize) {
  fil_t f = fil_of(sq);
  rnk_t r = rnk_of(sq);
  if (f >= 0) {
    return snprintf(buf, bufsize, "%c%d", 'a' + f, r);
  } else {
    return snprintf(buf, bufsize, "%c%d", 'z' + f + 1, r);
  }
}

// -----------------------------------------------------------------------------
// Board direction and laser direction
// -----------------------------------------------------------------------------

// direction map
static int dir[8] = { -ARR_WIDTH - 1, -ARR_WIDTH, -ARR_WIDTH + 1, -1, 1,
  ARR_WIDTH - 1, ARR_WIDTH, ARR_WIDTH + 1
};

int dir_of(int i) {
  tbassert(i >= 0 && i < 8, "i: %d\n", i);
  return dir[i];
}

// directions for laser: NN, EE, SS, WW

// extern inline int beam_of(int direction);
const int beam[NUM_ORI] = { 1, ARR_WIDTH, -1, -ARR_WIDTH };

/*
extern inline int beam_of(int direction) {
  tbassert(direction >= 0 && direction < NUM_ORI, "dir: %d\n", direction);
  return beam[direction];
}
*/
// reflect[beam_dir][pawn_orientation]
// -1 indicates back of Pawn
const int reflect[NUM_ORI][NUM_ORI] = {
  //  NW  NE  SE  SW
  {-1, -1, EE, WW},             // NN
  {NN, -1, -1, SS},             // EE
  {WW, EE, -1, -1},             // SS
  {-1, NN, SS, -1}              // WW
};

int reflect_of(int beam_dir, int pawn_ori) {
  tbassert(beam_dir >= 0 && beam_dir < NUM_ORI, "beam-dir: %d\n", beam_dir);
  tbassert(pawn_ori >= 0 && pawn_ori < NUM_ORI, "pawn-ori: %d\n", pawn_ori);
  return reflect[beam_dir][pawn_ori];
}

// -----------------------------------------------------------------------------
// Move getters and setters
// -----------------------------------------------------------------------------

move_t move_of(ptype_t typ, rot_t rot, square_t from_sq, square_t to_sq) {
  return ((typ & PTYPE_MV_MASK) << PTYPE_MV_SHIFT) |
    ((rot & ROT_MASK) << ROT_SHIFT) |
    ((from_sq & FROM_MASK) << FROM_SHIFT) | ((to_sq & TO_MASK) << TO_SHIFT);
}


// converts a move to string notation for FEN
void move_to_str(move_t mv, char *buf, size_t bufsize) {
  square_t f = from_square(mv); // from-square
  square_t t = to_square(mv);   // to-square
  rot_t r = rot_of(mv);         // rotation
  const char *orig_buf = buf;

  buf += square_to_str(f, buf, bufsize);
  if (f != t) {
    buf += square_to_str(t, buf, bufsize - (buf - orig_buf));
  } else {
    switch (r) {
      case NONE:
        buf += square_to_str(t, buf, bufsize - (buf - orig_buf));
        break;
      case RIGHT:
        buf += snprintf(buf, bufsize - (buf - orig_buf), "R");
        break;
      case UTURN:
        buf += snprintf(buf, bufsize - (buf - orig_buf), "U");
        break;
      case LEFT:
        buf += snprintf(buf, bufsize - (buf - orig_buf), "L");
        break;
      default:
        tbassert(false, "Whoa, now.  Whoa, I say.\n");  // Bad, bad, bad
        break;
    }
  }
}

// sort of the move list.
void sort_move_list(sortable_move_t * move_list, int num_of_moves, int mv_index) {
  for (int j = 0; j < num_of_moves; j++) {
    sortable_move_t insert = move_list[j];
    int hole = j;
    while (hole > 0 && insert > move_list[hole - 1]) {
      move_list[hole] = move_list[hole - 1];
      hole--;
    }
    move_list[hole] = insert;
  }
}

// // -----------------------------------------------------------------------------
// // Move generation
// // -----------------------------------------------------------------------------

// // Generate all moves from position p.  Returns number of moves.
// // strict currently ignored
// //
// // https://chessprogramming.wikispaces.com/Move+Generation
// int generate_all(position_t *p, sortable_move_t *sortable_move_list,
//                  bool strict) {
//   color_t color_to_move = color_to_move_of(p);
//   // Make sure that the enemy_laser map is marked
//   char laser_map[ARR_SIZE];

//   for (int i = 0; i < ARR_SIZE; ++i) {
//     laser_map[i] = 4;   // Invalid square
//   }

//   for (fil_t f = 0; f < BOARD_WIDTH; ++f) {
//     for (rnk_t r = 0; r < BOARD_WIDTH; ++r) {
//       laser_map[square_table[f][r]] = 0;
//     }
//   }

//   // 1 = path of laser with no moves
//   mark_laser_path(p, opp_color(color_to_move), laser_map, 1);

//   int move_count = 0;

//   for (fil_t f = 0; f < BOARD_WIDTH; f++) {
//     for (rnk_t r = 0; r < BOARD_WIDTH; r++) {
//       square_t  sq = square_table[f][r];
//       piece_t x = p->board[sq];

//       ptype_t typ = ptype_of(x);
//       color_t color = color_of(x);

//       switch (typ) {
//         case EMPTY:
//           break;
//         case PAWN:
//           if (laser_map[sq] == 1) continue;  // Piece is pinned down by laser.
//         case KING:
//           if (color != color_to_move) {  // Wrong color
//             break;
//           }
//           // directions
//           for (int d = 0; d < 8; d++) {
//             int dest = sq + dir_of(d);
//             // Skip moves into invalid squares
//             if (ptype_of(p->board[dest]) == INVALID) {
//               continue;    // illegal square
//             }

//             WHEN_DEBUG_VERBOSE(char buf[MAX_CHARS_IN_MOVE]);
//             WHEN_DEBUG_VERBOSE({
//                 move_to_str(move_of(typ, (rot_t) 0, sq, dest), buf, MAX_CHARS_IN_MOVE);
//                 DEBUG_LOG(1, "Before: %s ", buf);
//               });
//             tbassert(move_count < MAX_NUM_MOVES, "move_count: %d\n", move_count);
//             sortable_move_list[move_count++] = move_of(typ, (rot_t) 0, sq, dest);

//             WHEN_DEBUG_VERBOSE({
//                 move_to_str(get_move(sortable_move_list[move_count-1]), buf, MAX_CHARS_IN_MOVE);
//                 DEBUG_LOG(1, "After: %s\n", buf);
//               });
//           }

//           // rotations - three directions possible
//           for (int rot = 1; rot < 4; ++rot) {
//             tbassert(move_count < MAX_NUM_MOVES, "move_count: %d\n", move_count);
//             sortable_move_list[move_count++] = move_of(typ, (rot_t) rot, sq, sq);
//           }
//           if (typ == KING) {  // Also generate null move
//             tbassert(move_count < MAX_NUM_MOVES, "move_count: %d\n", move_count);
//             sortable_move_list[move_count++] = move_of(typ, (rot_t) 0, sq, sq);
//           }
//           break;
//         case INVALID:
//         default:
//           tbassert(false, "Bogus, man.\n");  // Couldn't BE more bogus!
//       }
//     }
//   }

//   WHEN_DEBUG_VERBOSE({
//       DEBUG_LOG(1, "\nGenerated moves: ");
//       for (int i = 0; i < move_count; ++i) {
//         char buf[MAX_CHARS_IN_MOVE];
//         move_to_str(get_move(sortable_move_list[i]), buf, MAX_CHARS_IN_MOVE);
//         DEBUG_LOG(1, "%s ", buf);
//       }
//       DEBUG_LOG(1, "\n");
//     });
//   sort_move_list(sortable_move_list, move_count, 0);
//   return move_count;
// }


// -----------------------------------------------------------------------------
// Move generation
// -----------------------------------------------------------------------------
/*
static inline int enumerate_moves(position_t * p,
                                  sortable_move_t * sortable_move_list,
                                  int *move_count, ptype_t typ, square_t sq) {

  int num_moves = 0;
  // directions
  for (int d = 0; d < 8; d++) {
    int dest = sq + dir_of(d);
    // Skip moves into invalid squares
    if (ptype_of(p->board[dest]) == INVALID) {
      continue;                 // illegal square
    }

    WHEN_DEBUG_VERBOSE(char buf[MAX_CHARS_IN_MOVE]);
    WHEN_DEBUG_VERBOSE( {
                       move_to_str(move_of(typ, (rot_t) 0, sq, dest), buf,
                                   MAX_CHARS_IN_MOVE);
                       DEBUG_LOG(1, "Before: %s ", buf);});
    tbassert(*move_count < MAX_NUM_MOVES, "move_count: %d\n", *move_count);
    sortable_move_list[(*move_count)++] = move_of(typ, (rot_t) 0, sq, dest);
    num_moves++;

    WHEN_DEBUG_VERBOSE( {
                       move_to_str(get_move
                                   (sortable_move_list[*move_count - 1]), buf,
                                   MAX_CHARS_IN_MOVE);
                       DEBUG_LOG(1, "After: %s\n", buf);});
  }

  // rotations - three directions possible
  for (int rot = 1; rot < 4; ++rot) {
    tbassert(*move_count < MAX_NUM_MOVES, "move_count: %d\n", *move_count);
    sortable_move_list[(*move_count)++] = move_of(typ, (rot_t) rot, sq, sq);
    num_moves++;
  }
  if (typ == KING) {            // Also generate null move
    tbassert(*move_count < MAX_NUM_MOVES, "move_count: %d\n", *move_count);
    sortable_move_list[(*move_count)++] = move_of(typ, (rot_t) 0, sq, sq);
    num_moves++;
  }

  return num_moves;
}
*/
// -----------------------------------------------------------------------------
// Move generation
// -----------------------------------------------------------------------------

// print output of square_of
void print_square_table() {
  for (fil_t f = 0; f < BOARD_WIDTH; ++f) {
    for (rnk_t r = 0; r < BOARD_WIDTH; ++r) {
      printf("f: %d, r: %d, square_of: %d\n", f, r, square_of(f, r));
    }
  }
  exit(0);
}

// Generate all moves from position p.  Returns number of moves.
// strict currently ignored
//
// https://chessprogramming.wikispaces.com/Move+Generation
int generate_all(position_t * p, sortable_move_t * sortable_move_list,
                 bool strict) {

  color_t color_to_move = color_to_move_of(p);
  color_t opposite_color = opp_color(color_to_move);

  square_t pinned_pawn_list[MAX_PAWNS];
  int pinned_pawn_count =
    generate_pinned_pawn_list(p, opposite_color, pinned_pawn_list);
  int move_count = 0;

  // collect all king's moves
  square_t king_loc = p->kloc[color_to_move];
  tbassert(ptype_of(p->board[king_loc]) == KING,
           "ptype: %d\n", ptype_of(p->board[king_loc]));
  for (int d = 0; d < 8; d++) {
    int dest = king_loc + dir_of(d);
    // Skip moves into invalid squares
    if (ptype_of(p->board[dest]) == INVALID) {
      continue;                 // illegal square
    }
    tbassert(move_count < MAX_NUM_MOVES, "move_count: %d\n", move_count);
    sortable_move_list[move_count++] = move_of(KING, (rot_t) 0, king_loc, dest);
  }
  // rotations - four directions possible.
  // King also generates null move.
  for (int rot = 0; rot < 4; ++rot) {
    tbassert(move_count < MAX_NUM_MOVES, "move_count: %d\n", move_count);
    sortable_move_list[move_count++] =
      move_of(KING, (rot_t) rot, king_loc, king_loc);
  }

  // collect all pawns' moves
  pawns_t pawns = p->ploc[color_to_move];
  int num_pawns = pawns.pawns_count;

  for (int i = 0; i < num_pawns; i++) {
    square_t pawn_loc = pawns.squares[i];
    tbassert(ptype_of(p->board[pawn_loc]) == PAWN,
             "ptype: %d\n", ptype_of(p->board[pawn_loc]));

    // if this pawn is pinned, do not generate moves
    bool pinned_flag = false;
    for (int j = 0; j < pinned_pawn_count; j++) {
      if (pinned_pawn_list[j] == pawn_loc) {
        pinned_flag = true;
        break;
      }
    }
    if (pinned_flag) {
      continue;
    }
    // otherwise, generate moves
    for (int d = 0; d < 8; d++) {
      int dest = pawn_loc + dir_of(d);
      // Skip moves into invalid squares
      if (ptype_of(p->board[dest]) == INVALID) {
        continue;               // illegal square
      }
      sortable_move_list[move_count++] =
        move_of(PAWN, (rot_t) 0, pawn_loc, dest);
    }

    // rotations - three directions possible
    for (int rot = 1; rot < 4; ++rot) {
      sortable_move_list[move_count++] =
        move_of(PAWN, (rot_t) rot, pawn_loc, pawn_loc);
    }
  }
  tbassert(move_count < MAX_NUM_MOVES, "move_count: %d\n", move_count);
  return move_count;
}

// // -----------------------------------------------------------------------------
// // Move execution
// // -----------------------------------------------------------------------------

// // Returns the square of piece that would be zapped by the laser if fired once,
// // or 0 if no such piece exists.
// //
// // p : Current board state.
// // c : Color of king shooting laser.
// static inline square_t fire_laser(position_t *p, color_t c) {
//   // color_t fake_color_to_move = (color_to_move_of(p) == WHITE) ? BLACK : WHITE;0
//   square_t sq = p->kloc[c];
//   int bdir = ori_of(p->board[sq]);

//   tbassert(ptype_of(p->board[sq]) == KING,
//            "ptype: %d\n", ptype_of(p->board[sq]));

//   while (true) {
//     sq += beam_of(bdir);
//     tbassert(sq < ARR_SIZE && sq >= 0, "sq: %d\n", sq);

//     switch (ptype_of(p->board[sq])) {
//       case EMPTY:  // empty square
//         break;
//       case PAWN:  // Pawn
//         bdir = reflect_of(bdir, ori_of(p->board[sq]));
//         if (bdir < 0) {  // Hit back of Pawn
//           return sq;
//         }
//         break;
//       case KING:  // King
//         return sq;  // sorry, game over my friend!
//         break;
//       case INVALID:  // Ran off edge of board
//         return 0;
//         break;
//       default:  // Shouldna happen, man!
//         tbassert(false, "Like porkchops and whipped cream.\n");
//         break;
//     }
//   }
// }

// -----------------------------------------------------------------------------
// Move execution
// -----------------------------------------------------------------------------

// Returns the square of piece that would be zapped by the laser if fired once,
// or 0 if no such piece exists.
//
// p : Current board state.
// c : Color of king shooting laser.
static inline square_t fire_laser(position_t * p, color_t c) {
  // color_t fake_color_to_move = (color_to_move_of(p) == WHITE) ? BLACK : WHITE;0
  square_t sq = p->kloc[c];
  int bdir = ori_of(p->board[sq]);

  tbassert(ptype_of(p->board[sq]) == KING,
           "ptype: %d\n", ptype_of(p->board[sq]));

  while (true) {
    sq = next_piece(p, sq, bdir);
    tbassert(sq < ARR_SIZE && sq >= 0, "sq: %d\n", sq);
    if (sq == 0) {
      return 0;
    }

    piece_t current_piece = ptype_of(p->board[sq]);
    tbassert(current_piece == PAWN || current_piece == KING, "invalid piece\n");

    if (current_piece == PAWN) {
      // int oldbdir = bdir;
      bdir = reflect[bdir][ori_of(p->board[sq])];
      // tbassert(bdir == reflect_of(oldbdir, ori_of(p->board[sq])), "\n");
      if (bdir < 0) {           // Hit back of Pawn
        return sq;
      }
    } else {
      return sq;
    } 
  }
}

// maintain pawn list in sorted order
// might want to remove later because default ordering not necessarily better
// ie no reason that sorted is better than unsorted
void sort_pawns(position_t * p, color_t color, int incorrect_index) {
  int num_squares = p->ploc[color].pawns_count;

  // go to the left if we need to swap
  int left = incorrect_index - 1;
  while (left >= 0 &&
         (compare_sq(p->ploc[color].squares[left],
                     p->ploc[color].squares[incorrect_index]) == 1)) {
    // swap left and current incorrect
    square_t left_square = p->ploc[color].squares[left];
    p->ploc[color].squares[left] = p->ploc[color].squares[incorrect_index];
    p->ploc[color].squares[incorrect_index] = left_square;
    left--;
    incorrect_index--;
  }

  // otherwise, check to the right if we need to swap
  int right = incorrect_index + 1;
  while (right < num_squares &&
         (compare_sq(p->ploc[color].squares[incorrect_index],
                     p->ploc[color].squares[right]) == 1)) {
    // swap right and current incorrect
    square_t right_square = p->ploc[color].squares[right];
    p->ploc[color].squares[right] = p->ploc[color].squares[incorrect_index];
    p->ploc[color].squares[incorrect_index] = right_square;
    right++;
    incorrect_index++;
  }
}


void low_level_make_move(position_t * old, position_t * p, move_t mv) {
  tbassert(mv != 0, "mv was zero.\n");

  WHEN_DEBUG_VERBOSE(char buf[MAX_CHARS_IN_MOVE]);
  WHEN_DEBUG_VERBOSE( {
                     move_to_str(mv, buf, MAX_CHARS_IN_MOVE);
                     DEBUG_LOG(1, "low_level_make_move: %s\n", buf);
                     });

  tbassert(old->key == compute_zob_key(old),
           "old->key: %" PRIu64 ", zob-key: %" PRIu64 "\n",
           old->key, compute_zob_key(old));

  WHEN_DEBUG_VERBOSE( {
                     fprintf(stderr, "Before:\n");
                     display(old);
                     });

  square_t from_sq = from_square(mv);
  square_t to_sq = to_square(mv);
  rot_t rot = rot_of(mv);

  WHEN_DEBUG_VERBOSE({
                     DEBUG_LOG(1, "low_level_make_move 2:\n");
                     square_to_str(from_sq, buf, MAX_CHARS_IN_MOVE);
                     DEBUG_LOG(1, "from_sq: %s\n", buf);
                     square_to_str(to_sq, buf, MAX_CHARS_IN_MOVE);
                     DEBUG_LOG(1, "to_sq: %s\n", buf);
                     switch (rot) {
                      case NONE:
                        DEBUG_LOG(1, "rot: none\n"); 
                        break; 
                      case RIGHT:
                        DEBUG_LOG(1, "rot: R\n"); 
                        break; 
                      case UTURN:
                        DEBUG_LOG(1, "rot: U\n"); 
                        break; 
                      case LEFT:
                        DEBUG_LOG(1, "rot: L\n"); 
                        break; 
                      default:
                        tbassert(false, "Not like a boss at all.\n");      // Bad, bad, bad
                        break;
                     }
                    });

  *p = *old;
  
  p->history = old;
  p->last_move = mv;

  tbassert(from_sq < ARR_SIZE && from_sq > 0, "from_sq: %d\n", from_sq);
  tbassert(p->board[from_sq] < (1 << PIECE_SIZE) && p->board[from_sq] >= 0,
           "p->board[from_sq]: %d\n", p->board[from_sq]);
  tbassert(to_sq < ARR_SIZE && to_sq > 0, "to_sq: %d\n", to_sq);
  tbassert(p->board[to_sq] < (1 << PIECE_SIZE) && p->board[to_sq] >= 0,
           "p->board[to_sq]: %d\n", p->board[to_sq]);

  p->key ^= zob_color;          // swap color to move

  piece_t from_piece = p->board[from_sq];
  piece_t to_piece = p->board[to_sq];

  if (to_sq != from_sq) {       // move, not rotation
    // Hash key updates
    p->key ^= zob[from_sq][from_piece]; // remove from_piece from from_sq
    p->key ^= zob[to_sq][to_piece];     // remove to_piece from to_sq

    p->board[to_sq] = from_piece;       // swap from_piece and to_piece on board
    p->board[from_sq] = to_piece;

    p->key ^= zob[to_sq][from_piece];   // place from_piece in to_sq
    p->key ^= zob[from_sq][to_piece];   // place to_piece in from_sq


    ptype_t from_ptype = ptype_of(from_piece);
    ptype_t to_ptype = ptype_of(to_piece);
    color_t from_color = color_of(from_piece);
    color_t to_color = color_of(to_piece);

    // update p->ranks and p->files
    tbassert(from_ptype != INVALID && to_ptype != INVALID,
             "Error: from_ptype or to_ptype is INVALID");
    tbassert(from_ptype == PAWN || from_ptype == KING,
             "Error: from_ptype should be either PAWN or KING");

    int from_rank = rnk_of(from_sq);
    int from_file = fil_of(from_sq);
    int to_rank = rnk_of(to_sq);
    int to_file = fil_of(to_sq);

    if (to_ptype == EMPTY) {
      // add the new piece
      p->ranks[to_rank] |= (1 << to_file);
      p->files[to_file] |= (1 << to_rank);

      // remove the old piece
      p->ranks[from_rank] &= ~(1 << from_file);
      p->files[from_file] &= ~(1 << from_rank);
    }

    // Update Pawn locations if necessary
    if (from_ptype == PAWN) {
      if (to_ptype == PAWN) {
        if (from_color != to_color) {
          // If both squares are pawns, we don't have to update the pawn arrays
          // if the pawns are the same color.
          // If the pawns are different colors, then we need to swap the squares
          // in the arrays.

          // First, get the index of the from-pawn.
          tbassert(p->ploc[from_color].pawns_map[from_file][from_rank] >=0 && 
                   p->ploc[from_color].pawns_map[from_file][from_rank] < p->ploc[from_color].pawns_count,
                   "Error: the index of pawn is INVALID");
          int i = p->ploc[from_color].pawns_map[from_file][from_rank];

          // Second, get the index of the to-pawn.
          tbassert(p->ploc[to_color].pawns_map[to_file][to_rank] >=0 && 
                   p->ploc[to_color].pawns_map[to_file][to_rank] < p->ploc[to_color].pawns_count,
                   "Error: the index of pawn is INVALID");
          int j = p->ploc[to_color].pawns_map[to_file][to_rank];
          p->ploc[from_color].squares[i] = to_sq;
          p->ploc[to_color].squares[j] = from_sq;

          // update pawns_map
          p->ploc[from_color].pawns_map[from_file][from_rank] = MAX_PAWNS;
          p->ploc[from_color].pawns_map[to_file][to_rank] = i;
          p->ploc[to_color].pawns_map[to_file][to_rank] = MAX_PAWNS;
          p->ploc[to_color].pawns_map[from_file][from_rank] = j;
        }
      } else {
        // The from-square is a pawn and the to-square is not a pawn.
        tbassert(p->ploc[from_color].pawns_map[from_file][from_rank] >=0 && 
                 p->ploc[from_color].pawns_map[from_file][from_rank] < p->ploc[from_color].pawns_count,
                 "Error: the index of pawn is INVALID");
        int i = p->ploc[from_color].pawns_map[from_file][from_rank];
        p->ploc[from_color].squares[i] = to_sq;
        p->ploc[from_color].pawns_map[from_file][from_rank] = MAX_PAWNS;
        p->ploc[from_color].pawns_map[to_file][to_rank] = i;
      }
    } else if (to_ptype == PAWN) {
      // The to-square is a pawn and the from-square is not a pawn.
      tbassert(p->ploc[to_color].pawns_map[to_file][to_rank] >=0 && 
               p->ploc[to_color].pawns_map[to_file][to_rank] < p->ploc[to_color].pawns_count,
               "Error: the index of pawn is INVALID"); 
      int j = p->ploc[to_color].pawns_map[to_file][to_rank];   
      p->ploc[to_color].squares[j] = from_sq; 
      p->ploc[to_color].pawns_map[to_file][to_rank] = MAX_PAWNS;
      p->ploc[to_color].pawns_map[from_file][from_rank] = j;
    }
    // Update King locations if necessary
    if (ptype_of(from_piece) == KING) {
      p->kloc[color_of(from_piece)] = to_sq;
    }
    if (ptype_of(to_piece) == KING) {
      p->kloc[color_of(to_piece)] = from_sq;
    }

  } else {                      // rotation
    // remove from_piece from from_sq in hash
    p->key ^= zob[from_sq][from_piece];
    set_ori(&from_piece, rot + ori_of(from_piece));     // rotate from_piece
    p->board[from_sq] = from_piece;     // place rotated piece on board
    p->key ^= zob[from_sq][from_piece]; // ... and in hash
  }

  // Increment ply
  p->ply++;

  tbassert(p->key == compute_zob_key(p),
           "p->key: %" PRIu64 ", zob-key: %" PRIu64 "\n",
           p->key, compute_zob_key(p));

  WHEN_DEBUG_VERBOSE( {
                     fprintf(stderr, "After:\n");
                     display(p);
                     }
  );
}

// return victims or KO
victims_t make_move(position_t * old, position_t * p, move_t mv) {
  tbassert(mv != 0, "mv was zero.\n");

  WHEN_DEBUG_VERBOSE(char buf[MAX_CHARS_IN_MOVE]);

  // move phase 1 - moving a piece
  low_level_make_move(old, p, mv);

  // move phase 2 - shooting the laser
  square_t victim_sq = 0;
  p->victims.zapped_count = 0;

  color_t color_to_move = color_to_move_of(old);
  while ((victim_sq = fire_laser(p, color_to_move))) {
    WHEN_DEBUG_VERBOSE( {
                       square_to_str(victim_sq, buf, MAX_CHARS_IN_MOVE);
                       DEBUG_LOG(1, "Zapping piece on %s\n", buf);
                       }
    );

    // we definitely hit something with laser, remove it from board
    piece_t victim_piece = p->board[victim_sq];
    p->victims.zapped[p->victims.zapped_count++] = victim_piece;
    p->key ^= zob[victim_sq][victim_piece];
    p->board[victim_sq] = 0;
    p->key ^= zob[victim_sq][0];

    // remove the zapped piece
    int victim_rank = rnk_of(victim_sq);
    int victim_file = fil_of(victim_sq);
    p->ranks[victim_rank] &= ~(1 << victim_file);
    p->files[victim_file] &= ~(1 << victim_rank);

    // If the victim piece is a pawn, remove it from the pawn array.
    color_t color = color_of(victim_piece);
    if (ptype_of(victim_piece) == PAWN) {
      int i = p->ploc[color].pawns_map[victim_file][victim_rank];
      p->ploc[color].squares[i] =
        p->ploc[color].squares[p->ploc[color].pawns_count - 1]; 
      // update pawns_map 
      p->ploc[color].pawns_map[fil_of(p->ploc[color].squares[i])][rnk_of(p->ploc[color].squares[i])] = i;

      p->ploc[color].pawns_count--;
      p->ploc[color].pawns_map[victim_file][victim_rank] = MAX_PAWNS;
    }

    tbassert(p->key == compute_zob_key(p),
             "p->key: %" PRIu64 ", zob-key: %" PRIu64 "\n",
             p->key, compute_zob_key(p));

    WHEN_DEBUG_VERBOSE( {
                       square_to_str(victim_sq, buf, MAX_CHARS_IN_MOVE);
                       DEBUG_LOG(1, "Zapped piece on %s\n", buf);
                       }
    );

    // laser halts on king
    if (ptype_of(victim_piece) == KING)
      break;
  }

  if (USE_KO && ((p->key == (old->key ^ zob_color))
                 || (p->key == old->history->key))) {
    return KO();
  }
  // if (USE_KO) {  // Ko rule
  //   if (p->key == (old->key ^ zob_color)) {
  //     bool match = true;

  //     for (fil_t f = 0; f < BOARD_WIDTH; f++) {
  //       for (rnk_t r = 0; r < BOARD_WIDTH; r++) {
  //         if (p->board[square_table[f][r]] !=
  //             old->board[square_table[f][r]]) {
  //           match = false;
  //         }   
  //       }   
  //     }   

  //     if (match) return KO();
  //   }   

  //   if (p->key == old->history->key) {
  //     bool match = true;

  //     for (fil_t f = 0; f < BOARD_WIDTH; f++) {
  //       for (rnk_t r = 0; r < BOARD_WIDTH; r++) {
  //         if (p->board[square_table[f][r]] !=
  //             old->history->board[square_table[f][r]]) {
  //           match = false;
  //         }   
  //       }   
  //     }   

  //     if (match) return KO();
  //   }   
  // }

  return p->victims;
}

// -----------------------------------------------------------------------------
// Move path enumeration (perft)
// -----------------------------------------------------------------------------

// Helper function for do_perft() (ply starting with 0).
//
// NOTE: This function reimplements some of the logic for make_move().
static uint64_t perft_search(position_t * p, int depth, int ply) {
  uint64_t node_count = 0;
  position_t np;
  sortable_move_t lst[MAX_NUM_MOVES];
  int num_moves;
  int i;

  if (depth == 0) {
    return 1;
  }

  num_moves = generate_all(p, lst, true);

  if (depth == 1) {
    return num_moves;
  }

  for (i = 0; i < num_moves; i++) {
    move_t mv = get_move(lst[i]);

    low_level_make_move(p, &np, mv);    // make the move baby!

    square_t victim_sq = 0;     // the guys to disappear
    np.victims.zapped_count = 0;

    while ((victim_sq = fire_laser(&np, color_to_move_of(p)))) {        // hit a piece
      piece_t victim_piece = np.board[victim_sq];
      tbassert((ptype_of(victim_piece) != EMPTY) &&
               (ptype_of(victim_piece) != INVALID),
               "type: %d\n", ptype_of(victim_piece));

      np.victims.zapped[np.victims.zapped_count++] = victim_piece;
      np.key ^= zob[victim_sq][victim_piece];   // remove from board
      np.board[victim_sq] = 0;
      np.key ^= zob[victim_sq][0];

      // remove the zapped piece
      int victim_rank = rnk_of(victim_sq);
      int victim_file = fil_of(victim_sq);
      p->ranks[victim_rank] &= ~(1 << victim_file);
      p->files[victim_file] &= ~(1 << victim_rank);

      // If the victim piece is a pawn, remove it from the pawn array.
      color_t color = color_of(victim_piece);
      if (ptype_of(victim_piece) == PAWN) {
        int i = p->ploc[color].pawns_map[victim_file][victim_rank];
        p->ploc[color].squares[i] =
          p->ploc[color].squares[p->ploc[color].pawns_count - 1];
        p->ploc[color].pawns_count--;
        p->ploc[color].pawns_map[victim_file][victim_rank] = MAX_PAWNS;
      }

      if (ptype_of(victim_piece) == KING)
        break;
    }

    if (np.victims.zapped_count > 0 &&
        ptype_of(np.victims.zapped[np.victims.zapped_count - 1]) == KING) {
      // do not expand further: hit a King
      node_count++;
      continue;
    }

    uint64_t partialcount = perft_search(&np, depth - 1, ply + 1);
    node_count += partialcount;
  }

  return node_count;
}

// Debugging function to help verify that the move generator is working
// correctly.
//
// https://chessprogramming.wikispaces.com/Perft
void do_perft(position_t * gme, int depth, int ply) {
  fen_to_pos(gme, "");

  for (int d = 1; d <= depth; d++) {
    printf("perft %2d ", d);
    uint64_t j = perft_search(gme, d, 0);
    printf("%" PRIu64 "\n", j);
  }
}

// -----------------------------------------------------------------------------
// Position display
// -----------------------------------------------------------------------------

void display(position_t * p) {
  char buf[MAX_CHARS_IN_MOVE];

  printf("\ninfo Ply: %d\n", p->ply);
  printf("info Color to move: %s\n", color_to_str(color_to_move_of(p)));

  // printf("kloc sq: %d, rank: %d, file: %d\n", sq, rnk_of(sq), fil_of(sq));
  square_to_str(p->kloc[WHITE], buf, MAX_CHARS_IN_MOVE);
  printf("info White King: %s, ", buf);
  square_to_str(p->kloc[BLACK], buf, MAX_CHARS_IN_MOVE);
  printf("info Black King: %s\n", buf);

  if (p->last_move != 0) {
    move_to_str(p->last_move, buf, MAX_CHARS_IN_MOVE);
    printf("info Last move: %s\n", buf);
  } else {
    printf("info Last move: NULL\n");
  }

  for (rnk_t r = BOARD_WIDTH - 1; r >= 0; --r) {
    printf("\ninfo %1d  ", r);
    for (fil_t f = 0; f < BOARD_WIDTH; ++f) {
      square_t sq = square_table[f][r];

      tbassert(ptype_of(p->board[sq]) != INVALID,
               "ptype_of(p->board[sq]): %d\n", ptype_of(p->board[sq]));

      if (ptype_of(p->board[sq]) == EMPTY) {    // empty square
        printf(" --");
        continue;
      }

      int ori = ori_of(p->board[sq]);   // orientation
      color_t c = color_of(p->board[sq]);

      if (ptype_of(p->board[sq]) == KING) {
        printf(" %2s", king_ori_to_rep[c][ori]);
        continue;
      }

      if (ptype_of(p->board[sq]) == PAWN) {
        printf(" %2s", pawn_ori_to_rep[c][ori]);
        continue;
      }
    }
  }

  printf("\n\ninfo    ");
  for (fil_t f = 0; f < BOARD_WIDTH; ++f) {
    printf(" %c ", 'a' + f);
  }
  printf("\n\n");
}

// -----------------------------------------------------------------------------
// Ko and illegal move signalling
// -----------------------------------------------------------------------------

extern inline victims_t KO();

extern inline victims_t ILLEGAL();

extern inline bool is_KO(victims_t victims);
extern inline bool is_ILLEGAL(victims_t victims);

extern inline bool zero_victims(victims_t victims);

extern inline bool victim_exists(victims_t victims);

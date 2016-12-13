/*
 * preloaded lookup tables
*/

#ifndef PRELOAD_H
#define PRELOAD_H

// depth that we searched for precomputed results
#define PRECOMP_DEPTH 8

// number of moves precomputed
#define NUM_PRECOMP 0

// macros
#define HASHES
#define MOVES
#define SCORES

// arrays
int64_t hash_arr[] = { HASHES };
move_t move_arr[] = { MOVES };
score_t score_arr[] = { SCORES };

#endif // PRELOAD_H

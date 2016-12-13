// Copyright (c) 2015 MIT License by 6.172 Staff

// This file contains the scout search routine. Although this routine contains
//   some duplicated logic from the searchPV routine in search.c, it is
//   convenient to maintain it separately. This allows one to easily
//   parallelize scout search separately from searchPV.

#include <cilk/cilk.h>
#include "./tbassert.h"
#include "./simple_mutex.h"

// #define YOUNG_SIBLINGS_WAIT 1
#define DEPTH_THRESHOLD 1
#define DEEP_DEPTH_THRESHOLD 5
#define MOVE_THRESHOLD 10

// Checks whether a node's parent has aborted.
//   If this occurs, we should just stop and return 0 immediately.
bool parallel_parent_aborted(searchNode * node) {
  searchNode *pred = node->parent;
  while (pred != NULL) {
    if (pred->abort) {
      return true;
    }
    pred = pred->parent;
  }
  return false;
}

// Checks whether this node has aborted due to a cut-off.
//   If this occurs, we should actually return the score.
bool parallel_node_aborted(searchNode * node) {
  if (node->abort) {
    return true;
  }
  return false;
}

// Initialize a scout search node for a "Null Window" search.
//   https://chessprogramming.wikispaces.com/Scout
//   https://chessprogramming.wikispaces.com/Null+Window
static void initialize_scout_node(searchNode * node, int depth) {
  node->type = SEARCH_SCOUT;
  node->beta = -(node->parent->alpha);
  node->alpha = node->beta - 1;
  node->depth = depth;
  node->ply = node->parent->ply + 1;
  node->subpv[0] = 0;
  node->legal_move_count = 0;
  node->fake_color_to_move = color_to_move_of(&(node->position));
  // point of view = 1 for white, -1 for black
  node->pov = 1 - node->fake_color_to_move * 2;
  node->best_move_index = 0;    // index of best move found
  node->abort = false;
}

static score_t scout_search(searchNode * node, int depth,
                            uint64_t * node_count_serial) {
  // Initialize the search node.
  initialize_scout_node(node, depth);

  // check whether we should abort
  if (should_abort_check() || parallel_parent_aborted(node)) {
    return 0;
  }
  // Pre-evaluate this position.
  leafEvalResult pre_evaluation_result = evaluate_as_leaf(node, SEARCH_SCOUT);

  // If we decide to stop searching, return the pre-evaluation score.
  if (pre_evaluation_result.type == MOVE_EVALUATED) {
    return pre_evaluation_result.score;
  }
  // Populate some of the fields of this search node, using some
  //  of the information provided by the pre-evaluation.
  int hash_table_move = pre_evaluation_result.hash_table_move;
  node->best_score = pre_evaluation_result.score;
  node->quiescence = pre_evaluation_result.should_enter_quiescence;

  // Grab the killer-moves for later use.
  move_t killer_a = killer[KMT(node->ply, 0)];
  move_t killer_b = killer[KMT(node->ply, 1)];
  move_t killer_c = killer[KMT(node->ply, 2)];
  move_t killer_d = killer[KMT(node->ply, 3)];

  // Store the sorted move list on the stack.
  // MAX_NUM_MOVES is all that we need.
  sortable_move_t move_list[MAX_NUM_MOVES];

  // Obtain the sorted move list.
  int num_of_moves = get_sortable_move_list(node, move_list, hash_table_move);
  int good_moves = move_list[num_of_moves + 1];
  int number_of_moves_evaluated = 0;

  // A simple mutex. See simple_mutex.h for implementation details.
  simple_mutex_t node_mutex;
  init_simple_mutex(&node_mutex);

  bool cutoff = false;
  // TODO: experiment with sorting at each iteration vs all at the beginning
  // Sort the move list.
  int critical_moves = move_list[num_of_moves];
  if (critical_moves == 0) {
    critical_moves++;
    sort_best_moves(move_list, num_of_moves, critical_moves);
  }
  // older siblings first in serial
  for (int mv_index = 0; mv_index < critical_moves; mv_index++) {

    // Get the next move from the move list.
    int local_index = number_of_moves_evaluated++;
    move_t mv = get_move(move_list[local_index]);
    if (TRACE_MOVES) {
      print_move_info(mv, node->ply);
    }
    // increase node count
    __sync_fetch_and_add(node_count_serial, 1);

    // serial evaluation
    moveEvaluationResult result;
    evaluateMove(node, mv, killer_a, killer_b, killer_c, killer_d,
                 SEARCH_SCOUT, node_count_serial, &result, NULL);
    if (result.type == MOVE_ILLEGAL || result.type == MOVE_IGNORE
        || abortf || parallel_parent_aborted(node)) {
      continue;
    }

    cutoff = search_process_score(node, mv, local_index, &result, SEARCH_SCOUT);

    if (cutoff) {
      // printf("cutoff\n");
      node->abort = true;
      break;
    }
  }

  // keep legal_move_count up to date
  simple_mutex_t LMR_mutex;
  init_simple_mutex(&LMR_mutex);

  if (!cutoff) {
    sort_incremental(move_list + critical_moves,
                     good_moves - critical_moves,
                     good_moves - critical_moves);
    //if (depth > DEEP_DEPTH_THRESHOLD || (num_of_moves > MOVE_THRESHOLD && depth > DEPTH_THRESHOLD)) { // parallel part with coarsening
    if (depth > DEPTH_THRESHOLD) {
      cilk_for(int mv_index = critical_moves;
               mv_index < num_of_moves; mv_index++) {
        do {
          if (node->abort)
            continue;

          simple_acquire(&LMR_mutex);
          // Get the next move from the move list.
           int local_index = number_of_moves_evaluated++;

          // Get the next move from the move list.
          move_t mv = get_move(move_list[local_index]);

          if (TRACE_MOVES) {
            print_move_info(mv, node->ply);
          }

          __sync_fetch_and_add(node_count_serial, 1);

          moveEvaluationResult result;
          evaluateMove(node, mv, killer_a, killer_b, killer_c, killer_d,
                          SEARCH_SCOUT, node_count_serial, &result, &LMR_mutex);

          // we unlock the mutex in evaluateMove
          tbassert(LMR_mutex == 0, "LMR mutex not unlocked\n");
          if (result.type == MOVE_ILLEGAL || result.type == MOVE_IGNORE
              || abortf || parallel_parent_aborted(node)) {
            continue;
          }

          bool local_cutoff = false;
          if (result.score > node->best_score) {
            simple_acquire(&node_mutex);
            // process the score. Note that this mutates fields in node.
            local_cutoff =
              search_process_score(node, mv, local_index, &result,
                                   SEARCH_SCOUT);
            // simple_release(&node_mutex);
            __sync_bool_compare_and_swap(&node_mutex, 1, 0); 
          }

          if (local_cutoff) {
            node->abort = true;
            continue;
          }
        } while (false);
      }
    } else {                    // serial part
      for (int mv_index = critical_moves; mv_index < num_of_moves; mv_index++) {
        if (node->abort)
          break;

        // Get the next move from the move list.
        int local_index = number_of_moves_evaluated++;
        move_t mv = get_move(move_list[local_index]);

        if (TRACE_MOVES) {
          print_move_info(mv, node->ply);
        }
        // increase node count
        __sync_fetch_and_add(node_count_serial, 1);

        moveEvaluationResult result;
        evaluateMove(node, mv, killer_a, killer_b, killer_c, killer_d,
                                                   SEARCH_SCOUT,
                                                   node_count_serial, &result, NULL);

        if (result.type == MOVE_ILLEGAL || result.type == MOVE_IGNORE
            || abortf || parallel_parent_aborted(node)) {
          continue;
        }

        bool local_cutoff = false;
        if (result.score > node->best_score) {
          // process the score. Note that this mutates fields in node.
          bool local_cutoff =
            search_process_score(node, mv, local_index, &result, SEARCH_SCOUT);

          if (local_cutoff) {
            node->abort = true;
            break;
          }
        }
      }
    }
  }

  if (parallel_parent_aborted(node)) {
    return 0;
  }

  if (node->quiescence == false) {
    update_best_move_history(&(node->position), node->best_move_index,
                             move_list, number_of_moves_evaluated);
  }

  tbassert(abs(node->best_score) != -INF, "best_score = %d\n",
           node->best_score);

  // Reads node->position.key, node->depth, node->best_score, and node->ply
  update_transposition_table(node);

  return node->best_score;
}

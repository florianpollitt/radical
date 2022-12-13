#include "internal.hpp"

namespace CaDiCaL {

/*------------------------------------------------------------------------*/

inline unsigned LratChecker::l2u (int lit) {
   assert (lit);
   assert (lit != INT_MIN);
   unsigned res = 2*(abs (lit) - 1);
   if (lit < 0) res++;
   return res;
}

signed char & LratChecker::mark (int lit) {
  const unsigned u = l2u (lit);
  assert (u < marks.size ());
  return marks[u];
}

signed char & LratChecker::checked_lit (int lit) {
  const unsigned u = l2u (lit);
  assert (u < checked_lits.size ());
  return checked_lits[u];
}

/*------------------------------------------------------------------------*/

LratCheckerClause * LratChecker::new_clause () {
  const size_t size = imported_clause.size ();
  assert (size <= UINT_MAX);
  const size_t bytes = sizeof (LratCheckerClause) + size * sizeof (int);
  LratCheckerClause * res = (LratCheckerClause *) new char [bytes];
  res->garbage = false;
  res->next = 0;
  res->hash = last_hash;
  res->id = last_id;
  res->size = size;
  int * literals = res->literals, * p = literals;
  for (const auto & lit : imported_clause)
    *p++ = lit;
  num_clauses++;

  return res;
}

void LratChecker::delete_clause (LratCheckerClause * c) {
  assert (c);
  if (!c->garbage) {
    assert (num_clauses);
    num_clauses--;
  } else {
    assert (num_garbage);
    num_garbage--;
  }
  delete [] (char*) c;
}

void LratChecker::enlarge_clauses () {
  assert (num_clauses == size_clauses);
  const uint64_t new_size_clauses = size_clauses ? 2*size_clauses : 1;
  LOG ("LRAT CHECKER enlarging clauses of checker from %" PRIu64 " to %" PRIu64,
    (uint64_t) size_clauses, (uint64_t) new_size_clauses);
  LratCheckerClause ** new_clauses;
  new_clauses = new LratCheckerClause * [ new_size_clauses ];
  clear_n (new_clauses, new_size_clauses);
  for (uint64_t i = 0; i < size_clauses; i++) {
    for (LratCheckerClause * c = clauses[i], * next; c; c = next) {
      next = c->next;
      const uint64_t h = reduce_hash (c->hash, new_size_clauses);
      c->next = new_clauses[h];
      new_clauses[h] = c;
    }
  }
  delete [] clauses;
  clauses = new_clauses;
  size_clauses = new_size_clauses;
}


// Probably not necessary since we have no watches.
//
void LratChecker::collect_garbage_clauses () {

  stats.collections++;

  LOG ("LRAT CHECKER collecting %" PRIu64 " garbage clauses %.0f%%",
    num_garbage, percent (num_garbage, num_clauses));

  for (LratCheckerClause * c = garbage, * next; c; c = next)
    next = c->next, delete_clause (c);

  assert (!num_garbage);
  garbage = 0;
}


/*------------------------------------------------------------------------*/

LratChecker::LratChecker (Internal * i)
:
  internal (i),
  size_vars (0), num_clauses (0), num_garbage (0),
  size_clauses (0), clauses (0), garbage (0), last_hash (0), last_id (0)
{
  LOG ("LRAT CHECKER new");

  // Initialize random number table for hash function.
  //
  Random random (42);
  for (unsigned n = 0; n < num_nonces; n++) {
    uint64_t nonce = random.next ();
    if (!(nonce & 1)) nonce++;
    assert (nonce), assert (nonce & 1);
    nonces[n] = nonce;
  }

  memset (&stats, 0, sizeof (stats));           // Initialize statistics.

}

LratChecker::~LratChecker () {
  LOG ("LRAT CHECKER delete");
  for (size_t i = 0; i < size_clauses; i++)
    for (LratCheckerClause * c = clauses[i], * next; c; c = next)
      next = c->next, delete_clause (c);
  for (LratCheckerClause * c = garbage, * next; c; c = next)
    next = c->next, delete_clause (c);
  delete [] clauses;
}

/*------------------------------------------------------------------------*/

void LratChecker::enlarge_vars (int64_t idx) {

  assert (0 < idx), assert (idx <= INT_MAX);

  int64_t new_size_vars = size_vars ? 2*size_vars : 2;
  while (idx >= new_size_vars) new_size_vars *= 2;
  LOG ("LRAT CHECKER enlarging variables of checker from %" PRId64 " to %" PRId64 "",
    size_vars, new_size_vars);
  
  marks.resize (2*new_size_vars);
  checked_lits.resize (2*new_size_vars);
  
  assert (idx < new_size_vars);
  size_vars = new_size_vars;
}

inline void LratChecker::import_literal (int lit) {
  assert (lit);
  assert (lit != INT_MIN);
  int idx = abs (lit);
  if (idx >= size_vars) enlarge_vars (idx);
  imported_clause.push_back (lit);
}

void LratChecker::import_clause (const vector<int> & c) {
  for (const auto & lit : c)
    import_literal (lit);
}

/*------------------------------------------------------------------------*/

uint64_t LratChecker::reduce_hash (uint64_t hash, uint64_t size) {
  assert (size > 0);
  unsigned shift = 32;
  uint64_t res = hash;
  while ((((uint64_t)1) << shift) > size) {
    res ^= res >> shift;
    shift >>= 1;
  }
  res &= size - 1;
  assert (res < size);
  return res;
}

uint64_t LratChecker::compute_hash (const uint64_t id) {
  assert (id > 0);
  unsigned j = id % num_nonces;                 // dont know if this is a good
  uint64_t tmp = nonces[j] * (uint64_t) id;     // hash funktion or if it is
  return last_hash = tmp;                       // even better than just using id
}

LratCheckerClause ** LratChecker::find (const uint64_t id) {
  stats.searches++;
  LratCheckerClause ** res, * c;
  const uint64_t hash = compute_hash (id);
  const uint64_t h = reduce_hash (hash, size_clauses);
  for (res = clauses + h; (c = *res); res = &c->next) {
    if (c->hash == hash && c->id == id) {
      break;
    }
    stats.collisions++;
  }
  return res;
}

void LratChecker::insert () {
  stats.insertions++;
  if (num_clauses == size_clauses) enlarge_clauses ();
  const uint64_t h = reduce_hash (compute_hash (last_id), size_clauses);
  LratCheckerClause * c = new_clause ();
  c->next = clauses[h];
  clauses[h] = c;
}

/*------------------------------------------------------------------------*/

bool LratChecker::check (vector<uint64_t> proof_chain) {
  LOG (imported_clause, "LRAT CHECKER checking clause");
  stats.checks++;
  // assert (proof_chain.size ());             // this might be attempting to
  for (auto & b : checked_lits) b = false;     // assert here but fails for
  for (const auto & lit : imported_clause) {   // tautological clauses
    checked_lit (-lit) = true;
    if (checked_lit (lit)) {
      LOG (imported_clause, "LRAT CHECKER clause tautological");
      return true;
    }
  }
  
  for (auto &id : proof_chain) {
    LratCheckerClause * c = * find (id);
    if (!c) {
      LOG ("LRAT CHECKER LRAT failed. Did not find clause with id %" PRIu64, id);
      return false;
    }
    int unit = 0;
    for (int * i = c->literals; i < c->literals + c->size; i++) {
      int lit = * i;
      if (checked_lit (-lit)) continue;
      // assert (!checked_lit (lit));      // attempting to assert here since
                                           // usually this should be a bug in
                                           // the proof chain but in some cases
                                           // this can occur (e.g. when we prove
                                           // the inconsistent clause to justify
                                           // whatever
      if (unit && unit != lit) {
        unit = INT_MIN;                    // multiple unfalsified literals
        break;
      }
      unit = lit;                          // potential unit
    }
    if (unit == INT_MIN) {
      LOG ("LRAT CHECKER check failed, found non unit clause %" PRIu64, id);
      return false;
    }
    if (!unit) {
      LOG ("LRAT CHECKER check succeded, clause falsified %" PRIu64, id);  // TODO:
      // assert (proof_chain.back () == id); // also attempting since this
                                             // basically means the proof chain
                                             // is unnecessarily long.
                                             // but unfortunatly this also
                                             // fails when we prove the
                                             // inconsistent clause to justify
                                             // whatever
      return true;
    }
    LOG ("LRAT CHECKER found unit clause %" PRIu64 ", assign %d", id, unit);
    checked_lit (unit) = true;
  }
  return false;         // check failed because no empty clause was found
}

/*------------------------------------------------------------------------*/

void LratChecker::add_original_clause (uint64_t id, const vector<int> & c) {
  START (checking);
  LOG (c, "LRAT CHECKER addition of original clause");
  LOG ("LRAT CHECKER clause id %" PRIu64, id);
  stats.added++;
  stats.original++;
  import_clause (c);
  last_id = id;
  assert (id);
  insert ();
  imported_clause.clear ();
  STOP (checking);
}

void LratChecker::add_derived_clause (uint64_t id, const vector<int>& c, const vector<uint64_t>& proof_chain) {
  START (checking);
  LOG (c, "LRAT CHECKER addition of derived clause");
  LOG ("LRAT CHECKER clause id %" PRIu64, id);
  stats.added++;
  stats.derived++;
  import_clause (c);
  last_id = id;
  assert (id);
  if (!check (proof_chain)) {
    fatal_message_start ();                        
    fputs ("failed to check derived clause:\n", stderr);
    for (const auto & lit : imported_clause)
      fprintf (stderr, "%d ", lit);
    fputc ('0', stderr);
    fatal_message_end ();
  }
  else insert ();
  imported_clause.clear ();
  STOP (checking);
}

void LratChecker::add_derived_clause (uint64_t id, const vector<int>& c) {
  START (checking);
  LOG (c, "LRAT CHECKER addition of derived unproven clause");
  LOG ("LRAT CHECKER clause id %" PRIu64, id);
  stats.added++;
  import_clause (c);
  last_id = id;
  assert (id);
  insert ();
  imported_clause.clear ();
  STOP (checking);
}

/*------------------------------------------------------------------------*/

void LratChecker::delete_clause (uint64_t id, const vector<int> & c) {
  START (checking);
  LOG (c, "LRAT CHECKER checking deletion of clause");
  LOG ("LRAT CHECKER clause id %" PRIu64, id);
  stats.deleted++;
  import_clause (c);
  last_id = id;
  LratCheckerClause ** p = find (id), * d = *p;
  if (d) {
    for (const auto & lit : imported_clause) mark (lit) = true;
    const int * dp = d->literals;
    for (unsigned i = 0; i < d->size; i++) {
      int lit = *(dp + i);
      if (!mark (lit)) {                   // should never happen since ids
        fatal_message_start ();            // are unique.
        fputs ("deleted clause not in proof:\n", stderr);
        for (const auto & lit : imported_clause)
          fprintf (stderr, "%d ", lit);
        fputc ('0', stderr);
        fatal_message_end ();
      }
    }
    for (const auto & lit : imported_clause) mark (lit) = false;
    
    // Remove from hash table, mark as garbage, connect to garbage list.
    num_garbage++;
    assert (num_clauses);
    num_clauses--;
    *p = d->next;
    d->next = garbage;
    garbage = d;
    d->garbage = true;
    
    // If there are enough garbage clauses collect them.
    // TODO: probably can just delete clause directly without
    // specific garbage collection phase.
    if (num_garbage > 0.5 * max ((size_t) size_clauses, (size_t) size_vars))
      collect_garbage_clauses ();
  } else {
    fatal_message_start ();
    fputs ("deleted clause not in proof:\n", stderr);
    for (const auto & lit : imported_clause)
      fprintf (stderr, "%d ", lit);
    fputc ('0', stderr);
    fatal_message_end ();
  }
  imported_clause.clear ();
  STOP (checking);
}

/*------------------------------------------------------------------------*/

void LratChecker::dump () {
  int max_var = 0;
  for (uint64_t i = 0; i < size_clauses; i++)
    for (LratCheckerClause * c = clauses[i]; c; c = c->next)
      for (unsigned i = 0; i < c->size; i++)
        if (abs (c->literals[i]) > max_var)
          max_var = abs (c->literals[i]);
  printf ("p cnf %d %" PRIu64 "\n", max_var, num_clauses);
  for (uint64_t i = 0; i < size_clauses; i++)
    for (LratCheckerClause * c = clauses[i]; c; c = c->next) {
      for (unsigned i = 0; i < c->size; i++)
        printf ("%d ", c->literals[i]);
      printf ("0\n");
    }
}

}

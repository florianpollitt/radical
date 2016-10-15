#include "internal.hpp"
#include "iterator.hpp"
#include "clause.hpp"
#include "message.hpp"
#include "macros.hpp"
#include "proof.hpp"

namespace CaDiCaL {

/*------------------------------------------------------------------------*/

// Returns 1 if the given clause is root level satisfied or -1 if it is not
// root level satisfied but contains a root level falsified literal and 0
// otherwise, if it does not contain a root level fixed literal.

int Internal::clause_contains_fixed_literal (Clause * c) {
  const int * lits = c->literals, size = c->size;
  int res = 0;
  for (int i = 0; res <= 0 && i < size; i++) {
    const int lit = lits[i];
    const int tmp = fixed (lit);
   if (tmp > 0) {
     LOG (c, "root level satisfied literal %d in", lit);
     res = 1;
   } else if (!res && tmp < 0) {
     LOG (c, "root level falsified literal %d in", lit);
     res = -1;
    }
  }
  return res;
}

// Assume that the clause is not root level satisfied but contains a literal
// set to false (root level falsified literal), so it can be shrunken.  The
// clause data is not actually reallocated at this point to avoid dealing
// with issues of special policies for watching binary clauses or whether a
// clause is extended or not. Only its size field is adjusted accordingly
// after flushing out root level falsified literals.

void Internal::flush_falsified_literals (Clause * c) {
  if (c->reason || c->size == 2) return;
  if (proof) proof->trace_flushing_clause (c);
  const int size = c->size;
  int * lits = c->literals, j = 0;
  for (int i = 0; i < size; i++) {
    const int lit = lits[j++] = lits[i];
    const int tmp = fixed (lit);
    assert (tmp <= 0);
    if (tmp >= 0) continue;
    LOG ("flushing %d", lit);
    j--;
  }
  int flushed = c->size - j;
  const size_t bytes = flushed * sizeof (int);
  stats.collected += bytes;
  dec_bytes (bytes);
  for (int i = j; i < size; i++) lits[i] = 0;
  c->size = j;
  LOG (c, "flushed %d literals and got", flushed);
}

void Internal::mark_satisfied_clauses_as_garbage () {

  // Only needed if there are new units (fixed variables) since last time.
  //
  if (fixed_limit >= stats.fixed) return;

  const_clause_iterator i;
  for (i = clauses.begin (); i != clauses.end (); i++) {
    Clause * c = *i;
    if (c->garbage) continue;
    const int tmp = clause_contains_fixed_literal (c);
         if (tmp > 0) c->garbage = true;
    else if (tmp < 0) flush_falsified_literals (c);
  }

  fixed_limit = stats.fixed;
}

/*------------------------------------------------------------------------*/

// This is a simple garbage collector which does not move clauses.

void Internal::delete_garbage_clauses () {
  LOG ("deleting garbage clauses");
  const_clause_iterator i = clauses.begin ();
  clause_iterator j = clauses.begin ();
  size_t collected_bytes = 0;
  while (i != clauses.end ()) {
    Clause * c = *j++ = *i++;
    if (!c->collect ()) continue;
    collected_bytes += c->bytes ();
    delete_clause (c);
    j--;
  }
  clauses.resize (j - clauses.begin ());
  VRB ("collected %ld bytes", (long) collected_bytes);
}

/*------------------------------------------------------------------------*/

// Copy a clause to the 'to' space of the arena.  Be careful if this clause
// is a reason of an assignment.  In that case update the reason reference.
//
void Internal::move_clause (Clause * c) {
  LOG (c, "moving");
  assert (!c->moved);
  char * p = c->start (), * q = arena.copy (p, c->bytes ());
  Clause * d = c->copy = (Clause *) (q - c->offset ());
  if (d->reason) var (d->literals[val (d->literals[1]) > 0]).reason = d;
  c->moved = true;
}

// This is the the moving garbage collector.

void Internal::move_non_garbage_clauses () {

  Clause * c;

  size_t collected_clauses = 0, collected_bytes = 0;
  size_t     moved_clauses = 0,     moved_bytes = 0;

  // First determine 'moved_bytes' and 'collected_bytes'.
  //
  const_clause_iterator i;
  for (i = clauses.begin (); i != clauses.end (); i++)
    if (!(c = *i)->collect ()) moved_bytes += c->bytes (), moved_clauses++;
    else collected_bytes += c->bytes (), collected_clauses++;

  VRB ("moving %ld bytes of %ld non garbage clauses",
    (long) moved_bytes, (long) moved_clauses);

  // Prepare 'to' space of size 'moved_bytes'.
  //
  arena.prepare (moved_bytes);

  // Copy clauses according to the order of calling 'move_clause'.
  //
  for (i = clauses.begin (); i != clauses.end (); i++)
    if (!(c = *i)->collect ()) move_clause (c);

  // Replace and flush clause references in 'clauses'.
  //
  clause_iterator j = clauses.begin ();
  for (i = clauses.begin (); i != clauses.end (); i++)
    if ((c = *i)->collect ()) delete_clause (c);
    else assert (c->moved), *j++ = c->copy, deallocate_clause (c);
  clauses.resize (j - clauses.begin ());

  // Release 'from' space completetly and then swap 'to' with 'from'.
  //
  arena.swap ();

  VRB ("collected %ld bytes of %ld garbage clauses",
    (long) collected_bytes, (long) collected_clauses);
}

/*------------------------------------------------------------------------*/

// Deallocate watcher stacks of inactive (fixed) variables and reset (clear)
// watcher stacks of still active variables.

void Internal::flush_watches () {
  size_t current_bytes = 0, max_bytes = 0;
  for (int idx = 1; idx <= max_var; idx++) {
    for (int sign = -1; sign <= 1; sign += 2) {
      const int lit = sign * idx;
      Watches & ws = watches (lit);
      const size_t bytes = ws.capacity () * sizeof ws[0];
      max_bytes += bytes;
      if (ws.empty () || fixed (lit)) ws = Watches ();
      else ws.clear (), current_bytes += bytes;
    }
  }
  stats.bytes.watcher.current = current_bytes;
  if (max_bytes > stats.bytes.watcher.max)
    stats.bytes.watcher.max = max_bytes;
}

void Internal::setup_watches () {
  for (const_clause_iterator i = clauses.begin (); i != clauses.end (); i++)
    watch_clause (*i);
}

/*------------------------------------------------------------------------*/

void Internal::garbage_collection () {
  START (collect);
  if (opts.arena) move_non_garbage_clauses ();
  else delete_garbage_clauses ();
  flush_watches ();
  setup_watches ();
  STOP (collect);
}

};
#include "internal.hpp"

namespace CaDiCaL {

/*------------------------------------------------------------------------*/

// We are using the address of 'decision_reason' as pseudo reason for
// decisions to distinguish assignment decisions from other assignments.
// Before we added chronological backtracking all learned units were
// assigned at decision level zero ('Solver.level == 0') and we just used a
// zero pointer as reason.  After allowing chronological backtracking units
// were also assigned at higher decision level (but with assignment level
// zero), and it was not possible anymore to just distinguish the case
// 'unit' versus 'decision' by just looking at the current level.  Both had
// a zero pointer as reason.  Now only units have a zero reason and
// decisions need to use the pseudo reason 'decision_reason'.

static Clause decision_reason_clause;
static Clause * decision_reason = &decision_reason_clause;

// If chronological backtracking is used the actual assignment level might
// be lower than the current decision level. In this case the assignment
// level is defined as the maximum level of the literals in the reason
// clause except the literal for which the clause is a reason.  This
// function determines this assignment level. For non-chronological
// backtracking as in classical CDCL this function always returns the
// current decision level, the concept of assignment level does not make
// sense, and accordingly this function can be skipped.

inline int Internal::assignment_level (int lit, Clause * reason) {

  assert (opts.chrono);
  if (!reason) return level;

  int res = 0;

  for (const auto & other : *reason) {
    if (other == lit) continue;
    assert (val (other));
    int tmp = var (other).level;
    if (tmp > res) res = tmp;
  }

  return res;
}

// calculate lrat_chain
//
inline void Internal::build_chain_for_units (int lit, Clause * reason) {
  if (!opts.lrat || opts.lratexternal) return;
  // LOG ("building chain for units");        bad line for debugging equivalence
  if (opts.chrono && assignment_level (lit, reason)) return;
  else if (!opts.chrono && level) return;   // not decision level 0
  assert (lrat_chain.empty ());
  for (auto & reason_lit : *reason) {
    if (lit == reason_lit) continue;
    assert (val (reason_lit));
    if (!val (reason_lit)) continue;
    const unsigned uidx = vlit (val (reason_lit) * reason_lit);
    uint64_t id = unit_clauses[uidx];
    lrat_chain.push_back (id);
  }
  lrat_chain.push_back (reason->id);
}

// same code as above but reason is assumed to be conflict and lit is not needed
//
void Internal::build_chain_for_empty () {
  if (!opts.lrat || opts.lratexternal || !lrat_chain.empty ()) return;
  assert (!level);
  assert (lrat_chain.empty ());
  assert (conflict);
  LOG (conflict, "lrat for global empty clause with conflict");
  for (auto & lit : *conflict) {
    assert (val (lit) < 0);
    const unsigned uidx = vlit (-lit);
    uint64_t id = unit_clauses[uidx];
    lrat_chain.push_back (id);
  }
  lrat_chain.push_back (conflict->id);
}

/*------------------------------------------------------------------------*/
inline int Internal::elevating_level (int lit, Clause * reason) {
  int l = 0;
  for (const auto & literal : *reason) {
    if (literal == lit) continue;
    assert (val (literal) < 0);
    const int ll = var (literal).level;
    l = l < ll ? ll : l;
  }
  return l;

}

/*------------------------------------------------------------------------*/

inline void Internal::elevate_lit (int lit, Clause * reason) {
  const int idx = vidx (lit);
  assert (vals[idx]);
  assert (reason);
  Var & v = var (idx);
  const int lit_level = elevating_level (lit, reason);
  if (lit_level >= v.level) return;
  assert (lit_level < v.level);
  LOG (reason, "elevated %d @ %d to %d", lit, v.level, lit_level);  
  if (!lit_level) {
    build_chain_for_units (lit, reason);
    learn_unit_clause (lit);  // increases 'stats.fixed'
    reason = 0;
    lrat_chain.clear ();
  }
  v.level = lit_level;
  v.reason = reason;
  v.trail = trail_size (lit_level);
  trail_push (lit, lit_level);
}

/*------------------------------------------------------------------------*/

inline void Internal::search_assign (int lit, Clause * reason) {

  if (level) require_mode (SEARCH);

  const int idx = vidx (lit);
  assert (!vals[idx]);
  assert (!flags (idx).eliminated () || reason == decision_reason);
  Var & v = var (idx);
  int lit_level;
  
  assert (!opts.lrat || opts.lratexternal || level || reason == decision_reason || !lrat_chain.empty ());

  // The following cases are explained in the two comments above before
  // 'decision_reason' and 'assignment_level'.
  //
  if (!reason) lit_level = 0;   // unit
  else if (reason == decision_reason) lit_level = level, reason = 0;
  else if (opts.chrono) lit_level = assignment_level (lit, reason);
  else lit_level = level;
  if (!lit_level) reason = 0;

  v.level = lit_level;
  v.trail = trail_size (lit_level);
  v.reason = reason;
  assert ((int) num_assigned < max_var);
  num_assigned++;
  if (!lit_level) learn_unit_clause (lit);  // increases 'stats.fixed'
  const signed char tmp = sign (lit);
  vals[idx] = tmp;
  vals[-idx] = -tmp;
  assert (val (lit) > 0);
  assert (val (-lit) < 0);
  if (!searching_lucky_phases)
    phases.saved[idx] = tmp;                // phase saving during search
  trail_push (lit, lit_level);
#ifdef LOGGING
  if (!lit_level) LOG ("root-level unit assign %d @ 0", lit);
  else LOG (reason, "search assign %d @ %d", lit, lit_level);
#endif

  if (watching ()) {
    const Watches & ws = watches (-lit);
    if (!ws.empty ()) {
      const Watch & w = ws[0];
      __builtin_prefetch (&w, 0, 1);
    }
  }
  lrat_chain.clear ();
}

/*------------------------------------------------------------------------*/

// External versions of 'search_assign' which are not inlined.  They either
// are used to assign unit clauses on the root-level, in 'decide' to assign
// a decision or in 'analyze' to assign the literal 'driven' by a learned
// clause.  This happens far less frequently than the 'search_assign' above,
// which is called directly in 'propagate' below and thus is inlined.

void Internal::assign_unit (int lit) {
  assert (!level);
  search_assign (lit, 0);
}

// Just assume the given literal as decision (increase decision level and
// assign it).  This is used below in 'decide'.

void Internal::search_assume_decision (int lit) {
  require_mode (SEARCH);
  assert (propagated == trail.size ());
  new_trail_level (lit);
  LOG ("search decide %d", lit);
  search_assign (lit, decision_reason);
}

void Internal::search_assign_driving (int lit, Clause * c) {
  require_mode (SEARCH);
  search_assign (lit, c);
}

/*------------------------------------------------------------------------*/

// if we found multiple conflicts in the previous propagation we have to
// process them in order to not miss any implications.
// this entails fixing watches and possibly assigning or elevating literals.
// Afterwards we propagate as usual.

bool Internal::propagate_conflicts () {
  if (conflicts.empty ()) return true;
  assert (opts.multitrail);
  
  LOG ("propagating conflicts");

  const auto eoc = conflicts.end ();
  auto j = conflicts.begin ();
  auto i = j;

  while (i != eoc) {
    Clause * c = *j++ = *i++;
  
    const literal_iterator lits = c->begin ();
    const literal_iterator end = c->end ();
    literal_iterator k = lits;

    int first = 0, second = 0;
    literal_iterator fpos, spos;
    
    // find first, second
    for (; k < end; k++) {
      const int lit = *k;
      const char tmp = val (lit);
      if (tmp < 0) continue;
      if (!first) {
        assert (tmp >= 0);
        first = lit;
        fpos = k;
        continue;
      }
      second = lit;
      spos = k;
      break;
    }
    LOG (c, "first %d, second %d in", first, second);
    if (!first) continue;        // still conflicting, might be impossible...
    j--;                         // drop conflict, but fix watches
    
    if (!second) {     // either elevate or assign first (or maybe there was a
                       // valid choice for second already, then elevate_lit
                       // will do nothing.
      if (val (first) > 0) elevate_lit (first, c);
      else search_assign (first, c);
      
      int other_level = var (first).level;
  
      // now find valid choice for second...
      for (spos = lits; spos < end; spos++)
        if (*spos != first && var (second = *spos).level == other_level)
          break;
    }
    assert (second);

    // watch first and second instead
    
    unwatch_clause (c);
    int f = c->literals[0];
    int s = c->literals[1];
    lits[0] = first;
    lits[1] = second;
    *fpos = f;
    *spos = s;
    watch_clause (c);
    
  }
  conflicts.resize (j - conflicts.begin ());
  
  // after backtracking we are guaranteed at least one
  // unassigned literals per conflict.
  // assigning literals (the uip from conflict analysis and
  // those during this routing) should not assign this literal to false
  // so the following assertion should hold actually:

  assert (conflicts.empty ());
  return conflicts.empty ();
}

/*------------------------------------------------------------------------*/

// The 'propagate' function is usually the hot-spot of a CDCL SAT solver.
// The 'trail' stack saves assigned variables and is used here as BFS queue
// for checking clauses with the negation of assigned variables for being in
// conflict or whether they produce additional assignments.

// This version of 'propagate' uses lazy watches and keeps two watched
// literals at the beginning of the clause.  We also use 'blocking literals'
// to reduce the number of times clauses have to be visited (2008 JSAT paper
// by Chu, Harwood and Stuckey).  The watches know if a watched clause is
// binary, in which case it never has to be visited.  If a binary clause is
// falsified we continue propagating.

// Finally, for long clauses we save the position of the last watch
// replacement in 'pos', which in turn reduces certain quadratic accumulated
// propagation costs (2013 JAIR article by Ian Gent) at the expense of four
// more bytes for each clause.

// this propagation routine has two different modes:
// normal, multitrailrepair.
// especially with opts.chrono these behave differently
// in normal mode everything we have only one trail which we propagate linearly
// in multitrail mode we have one trail for each level where we propagate
// literals bottom up for each level. We also differentiate conflicts
// and repair missed implications, which can lead to elevated literals

// the differences are mostly hidden in trail.cpp -> TODO inline these
// functions here instead.

bool Internal::propagate () {

  if (level) require_mode (SEARCH);
  assert (!unsat);
  assert (!conflict);
  START (propagate);

  // first we have to fix watches in the previous conflicts (and possibly
  // assign or elevate literals)
  
  propagate_conflicts ();
  
  int proplevel = -1;

  // Updating statistics counter in the propagation loops is costly so we
  // delay until propagation ran to completion.
  
  // LOG ("PROPAGATION");
  while (proplevel < level) {
    // LOG ("PROPAGATION outer loop");
    proplevel = next_propagation_level (proplevel);
    conflict = propagation_conflict (proplevel, 0);
    if (proplevel < 0) break;
    LOG ("PROPAGATION on level %d", proplevel);
    vector<int> * t = next_trail (proplevel);
    int64_t before = next_propagated (proplevel);
    size_t current = before;
    const bool ismultitrail = opts.multitrail; 
    while (!conflict && current != t->size ()) {
      assert (opts.multitrail || t == &trail);
      LOG ("propagating level %d from %zd to %zd", proplevel, before, t->size ());
  
      assert (current < t->size ());
      const int lit = -(*t)[current++];
      if (var (lit).level < proplevel) continue;
  
      LOG ("propagating %d", -lit);
      Watches & ws = watches (lit);
  
      const const_watch_iterator eow = ws.end ();
      watch_iterator j = ws.begin ();
      const_watch_iterator i = j;
  
      while (i != eow) {
  
        const Watch w = *j++ = *i++;
        const signed char b = val (w.blit);
        int l = var (w.blit).level;
        bool repair = ismultitrail && l > proplevel;
        int multisat = w.blit * (repair) * (b > 0);  // multitrailrepair mode

        if (b > 0 && !multisat) continue;   // blocking literal satisfied
        

        if (w.binary ()) {
  
          // In principle we can ignore garbage binary clauses too, but that
          // would require to dereference the clause pointer all the time with
          //
          // if (w.clause->garbage) { j--; continue; } // (*)
          //
          // This is too costly.  It is however necessary to produce correct
          // proof traces if binary clauses are traced to be deleted ('d ...'
          // line) immediately as soon they are marked as garbage.  Actually
          // finding instances where this happens is pretty difficult (six
          // parallel fuzzing jobs in parallel took an hour), but it does
          // occur.  Our strategy to avoid generating incorrect proofs now is
          // to delay tracing the deletion of binary clauses marked as garbage
          // until they are really deleted from memory.  For large clauses
          // this is not necessary since we have to access the clause anyhow.
          //
          // Thanks go to Mathias Fleury, who wanted me to explain why the
          // line '(*)' above was in the code. Removing it actually really
          // improved running times and thus I tried to find concrete
          // instances where this happens (which I found), and then
          // implemented the described fix.
  
          // Binary clauses are treated separately since they do not require
          // to access the clause at all (only during conflict analysis, and
          // there also only to simplify the code).
  
          if (multisat) {
            assert (b > 0);
            // fix missed implication by elevating w.blit
            elevate_lit (w.blit, w.clause);
          }
          else if (b < 0) conflict = propagation_conflict (proplevel, w.clause);   // but continue ...
          else {
            build_chain_for_units (w.blit, w.clause);
            search_assign (w.blit, w.clause);
            // lrat_chain.clear (); done in search_assign
          }
  
        } else {
  
          // TODO: why is this line here and not right below ????
          //         while (i != eow)
          if (conflict) break; // Stop if there was a binary conflict already.
  
          // The cache line with the clause data is forced to be loaded here
          // and thus this first memory access below is the real hot-spot of
          // the solver.  Note, that this check is positive very rarely and
          // thus branch prediction should be almost perfect here.
  
          if (w.clause->garbage) { j--; continue; }
  
          literal_iterator lits = w.clause->begin ();
  
          // Simplify code by forcing 'lit' to be the second literal in the
          // clause.  This goes back to MiniSAT.  We use a branch-less version
          // for conditionally swapping the first two literals, since it
          // turned out to be substantially faster than this one
          //
          //  if (lits[0] == lit) swap (lits[0], lits[1]);
          //
          // which achieves the same effect, but needs a branch.
          //
          const int other = lits[0] ^ lits[1] ^ lit;
          const signed char u = val (other); // value of the other watch
          l = var (other).level;
          repair = ismultitrail && l > proplevel;
          multisat = other * (repair) * (u > 0);  // multitrail mode
  
          if (u > 0 && !multisat) j[-1].blit = other; // satisfied, just replace blit
          else {
  
            // This follows Ian Gent's (JAIR'13) idea of saving the position
            // of the last watch replacement.  In essence it needs two copies
            // of the default search for a watch replacement (in essence the
            // code in the 'if (v < 0) { ... }' block below), one starting at
            // the saved position until the end of the clause and then if that
            // one failed to find a replacement another one starting at the
            // first non-watched literal until the saved position.
  
            const int size = w.clause->size;
            const literal_iterator middle = lits + w.clause->pos;
            const const_literal_iterator end = lits + size;
            literal_iterator k = middle;
  
            // Find replacement watch 'r' at position 'k' with value 'v'.
  
            int r = 0;
            signed char v = -1;
            
  
            while (k != end && (v = val (r = *k)) < 0)
              k++;
  
            if (v < 0) {  // need second search starting at the head?
  
              k = lits + 2;
              assert (w.clause->pos <= size);
              while (k != middle && (v = val (r = *k)) < 0)
                k++;
            }
  
            w.clause->pos = k - lits;  // always save position
  
            assert (lits + 2 <= k), assert (k <= w.clause->end ());
  
            if (v > 0) {
              // check if w.clause is unisat
              // if it is elevate literal
              // fix watches
              // the watch for lit has to be changed in case
              // var (lit).level < var (x).level for all positively assigned
              // literals x.
              // for other similarly, but only if var (other).level <= proplevel
              // and if var (other).level == proplevel then only if
              // var (other).trail < var (lit).trail
              // there is a high chance that this cannot happen...
              if (!multisat && ismultitrail) {
                literal_iterator j = lits;
                for (; j < end; j++) {
                  int literal = *j;
                  if (literal == r) continue;
                  const auto tmp = val (literal);
                  if (tmp < 0) continue;
                  multisat = literal;
                  break;
                }
              }
              if (!multisat && ismultitrail) {
                // potentially elevating r...
                elevate_lit (r, w.clause);
                multisat = other;        // instead we could search for a better
                                         // blit (one with level == r.level)
              }
              if (multisat) {
                // replace watch
                LOG (w.clause, "unwatch %d in", lit);
    
                lits[0] = other;
                lits[1] = r;
                *k = lit;
    
                watch_literal (r, multisat, w.clause);
    
                j--;  // Drop this watch from the watch list of 'lit'.
              } else
                // Replacement satisfied, so just replace 'blit'.  
                j[-1].blit = r;
                
                /*
              if (var (r).level > var (other).level) {
                assert (false);     // can this even happen???
                // if other level < r level we might have to change
                // other watch which is a lot of work... TODO
                // I think this is necessary though...
                
                // delete other watch
                // find literal in clause with level <= level of r
                // watch literal, r, w.clause
                remove_watch (watches (other), w.clause);
                if (var (r).level == proplevel) {
                  watch_literal (lit, r, w.clause);
                } else {                   // otherwise we search for a new watch
                  int pos, s = 0;          // which is guaranteed to exist because
                                           // of elevation.
                  for (pos = 2; pos < size; pos++) {
                    if (var (s = lits[pos]).level == var (other).level)
                      break;
                  }
                  assert (s);
                  assert (pos < size);
                    
                  LOG (w.clause, "unwatch %d in", lit);
                  lits[pos] = lit;
                  lits[0] = other;
                  lits[1] = s;
                  watch_literal (s, other, w.clause);
      
                  j--;  // Drop this watch from the watch list of 'lit'.
                }
              }
                */
            } else if (!v) {
  
              // Found new unassigned replacement literal to be watched.
  
              LOG (w.clause, "unwatch %d in", lit);
  
              lits[0] = other;
              lits[1] = r;
              *k = lit;
  
              watch_literal (r, lit, w.clause);
  
              j--;  // Drop this watch from the watch list of 'lit'.
  
            } else if (!u) {
  
              assert (v < 0);
  
              // The other watch is unassigned ('!u') and all other literals
              // assigned to false (still 'v < 0'), thus we found a unit.
              //
              build_chain_for_units (other, w.clause);
              search_assign (other, w.clause);
              // lrat_chain.clear (); done in search_assign
  
              
              // we need to change the blocking lit anyways
              // not really neccessary
              j[-1].blit = other;
              
              // Similar code is in the implementation of the SAT'18 paper on
              // chronological backtracking but in our experience, this code
              // first does not really seem to be necessary for correctness,
              // and further does not improve running time either.
              //
              // this is actually necessary to preserve the invariant for
              // opts.multitrail. otherwise the watches break if we backtrack.
              
              if (opts.multitrail || opts.chrono > 1) {  // ... always do some variant ...
  
                const int other_level = var (other).level;
    
                if (other_level > var (lit).level) {
    
                  // The assignment level of the new unit 'other' is larger
                  // than the assignment level of 'lit'.  Thus we should find
                  // another literal in the clause at that higher assignment
                  // level and watch that instead of 'lit'.
    
                  assert (size > 2);
    
                  int pos, s = 0;
    
                  for (pos = 2; pos < size; pos++)
                    if (var (s = lits[pos]).level == other_level)
                      break;
    
                  assert (s);
                  assert (pos < size);
    
                  LOG (w.clause, "unwatch %d in", lit);
                  lits[pos] = lit;
                  lits[0] = other;
                  lits[1] = s;
                  watch_literal (s, other, w.clause);
    
                  j--;  // Drop this watch from the watch list of 'lit'.
                }
              }
            } else if (u > 0) {
              assert (v < 0);
              assert (multisat);

              // we might have to elevate...
              elevate_lit (other, w.clause);

              // now other_level might have changed
              int other_level = var (other).level;
              
              // if we elevated to proplevel we can just change blit to other
              assert (other_level >= proplevel);
              if (other_level == proplevel) {
                j[-1].blit = other;
              } else {                   // otherwise we search for a new watch
                int pos, s = 0;          // which is guaranteed to exist because
                                         // of elevation.
                for (pos = 2; pos < size; pos++) {
                  if (var (s = lits[pos]).level >= other_level)
                    break;
                }
                assert (s);
                assert (pos < size);
                  
                LOG (w.clause, "unwatch %d in", lit);
                lits[pos] = lit;
                lits[0] = other;
                lits[1] = s;
                watch_literal (s, other, w.clause);
    
                j--;  // Drop this watch from the watch list of 'lit'.
              }
            } else {
              
              assert (u < 0);
              assert (v < 0);
  
              // The other watch is assigned false ('u < 0') and all other
              // literals as well (still 'v < 0'), thus we found a conflict.
  
              conflict = propagation_conflict (proplevel, w.clause);
              if (conflict)
                break;
            }
          }
        }
      }

      if (j != i) {
  
        while (i != eow)
          *j++ = *i++;
  
        ws.resize (j - ws.begin ());
      }
      // LOG ("PROPAGATION end loop with current %ld, trail %zd", current, t->size ());
    }
    // LOG ("PROPAGATION set propagated");
    set_propagated (proplevel, current);
    // LOG ("PROPAGATION set conflict");
    if (!conflict)
      conflict = propagation_conflict (-1, 0);
    
    if (!searching_lucky_phases) 
      stats.propagations.search += propagated - before;
    if (conflict) break;
  }
  if (!conflict)
    conflict = propagation_conflict (level, 0);
  // conflicts.clear ();   // TODO: do smth with the conflicts -> i.e. propagating
  if (searching_lucky_phases) {

    if (conflict)
      LOG (conflict, "ignoring lucky conflict");

  } else {

    // Avoid updating stats eagerly in the hot-spot of the solver.
    //

    if (!conflict) {
      no_conflict_level = level;
      no_conflict_until = next_propagated (level);
    } else {

      if (stable) stats.stabconflicts++;
      stats.conflicts++;

      LOG (conflict, "conflict");

      // The trail before the current decision level was conflict free.
      //
      // TODO: opts.multitrail
      no_conflict_until = control[proplevel].trail;
      no_conflict_level = proplevel - 1;
    }
  }

  // LOG ("PROPAGATION stop");

  STOP (propagate);

#ifndef NDEBUG
  // TODO: very unsure about watches (also inside of propagate)
  if (!conflict)
    test_watch_invariant ();
#endif
  return !conflict;
}

}

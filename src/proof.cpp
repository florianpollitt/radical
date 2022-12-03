#include "internal.hpp"

namespace CaDiCaL {

using namespace std;

/*------------------------------------------------------------------------*/

// Enable proof logging and checking by allocating a 'Proof' object.

void Internal::new_proof_on_demand () {
  if (!proof) {
    bool lrat = opts.checkprooflrat;
    proof = new Proof (this, lrat);
    LOG ("connecting proof to internal solver");
    if (lrat) {
      assert (!lratbuilder);
      lratbuilder = new LratBuilder (this);
      LOG ("PROOF connecting lrat chain builder");
      proof->connect (lratbuilder);
    }
  }
}

// Enable proof tracing.

void Internal::trace (File * file) {
  assert (!tracer);
  new_proof_on_demand ();
  // both checkprooflrat and lrat have to be anabled for lrat proofs (at least
  // at the moment
  tracer = new Tracer (this, file, opts.binary, (opts.checkprooflrat && opts.lrat));
  LOG ("PROOF connecting proof tracer");
  proof->connect (tracer);
}

// Enable proof checking.

void Internal::check () {
  assert (!checker);
  assert (!lratchecker);
  new_proof_on_demand ();
  if (opts.checkprooflrat) {
    lratchecker = new LratChecker (this);
    LOG ("PROOF connecting lrat proof checker");
    proof->connect (lratchecker);
  } else {
    checker = new Checker (this);
    LOG ("PROOF connecting proof checker");
    proof->connect (checker);
  }
}

// We want to close a proof trace and stop checking as soon we are done.

void Internal::close_trace () {
  assert (tracer);
  tracer->close ();
}

// We can flush a proof trace file before actually closing it.

void Internal::flush_trace () {
  assert (tracer);
  tracer->flush ();
}

/*------------------------------------------------------------------------*/

Proof::Proof (Internal * s, bool l) : internal (s), lrat (l), checker (0),
  tracer (0), lratbuilder (0), lratchecker (0) { LOG ("PROOF new"); }

Proof::~Proof () { LOG ("PROOF delete"); }

/*------------------------------------------------------------------------*/

inline void Proof::add_literal (int internal_lit) {
  const int external_lit = internal->externalize (internal_lit);
  clause.push_back (external_lit);
}

inline void Proof::add_literals (Clause * c) {
  for (auto const & lit : * c)
    add_literal (lit);
}

inline void Proof::add_literals (const vector<int> & c) {
  for (auto const & lit : c)
    add_literal (lit);
}

/*------------------------------------------------------------------------*/

void Proof::add_original_clause (uint64_t id, const vector<int> & c) {
  LOG (c, "PROOF adding original internal clause");
  add_literals (c);
  clause_id = id;
  add_original_clause ();
}

void Proof::add_derived_empty_clause (uint64_t id) {
  LOG ("PROOF adding empty clause");
  assert (clause.empty ());
  clause_id = id;
  add_derived_clause ();
}

void Proof::add_derived_unit_clause (uint64_t id, int internal_unit) {
  LOG ("PROOF adding unit clause %d", internal_unit);
  assert (clause.empty ());
  add_literal (internal_unit);
  clause_id = id;
  add_derived_clause ();
}

/*------------------------------------------------------------------------*/

void Proof::add_derived_clause (Clause * c) {
  LOG (c, "PROOF adding to proof derived");
  assert (clause.empty ());
  add_literals (c);
  clause_id = c->id;
  add_derived_clause ();
}

void Proof::delete_clause (Clause * c) {
  LOG (c, "PROOF deleting from proof");
  assert (clause.empty ());
  add_literals (c);
  clause_id = c->id;
  delete_clause ();
}

void Proof::delete_clause (uint64_t id, const vector<int> & c) {
  LOG (c, "PROOF deleting from proof");
  assert (clause.empty ());
  add_literals (c);
  clause_id = id;
  delete_clause ();
}

void Proof::add_derived_clause (uint64_t id, const vector<int> & c) {
  LOG (internal->clause, "PROOF adding derived clause");
  assert (clause.empty ());
  for (const auto & lit : c)
    add_literal (lit);
  clause_id = id;
  add_derived_clause ();
}

/*------------------------------------------------------------------------*/

// During garbage collection clauses are shrunken by removing falsified
// literals. To avoid copying the clause, we provide a specialized tracing
// function here, which traces the required 'add' and 'remove' operations.

void Proof::flush_clause (Clause * c) {
  LOG (c, "PROOF flushing falsified literals in");
  assert (clause.empty ());
  for (int i = 0; i < c->size; i++) {
    int internal_lit = c->literals[i];
    if (internal->fixed (internal_lit) < 0) continue;
    add_literal (internal_lit);
  }
  int64_t id = ++internal->clause_id;
  clause_id = id;
  add_derived_clause ();
  delete_clause (c);
  c->id = id;
}

// While strengthening clauses, e.g., through self-subsuming resolutions,
// during subsumption checking, we have a similar situation, except that we
// have to remove exactly one literal.  Again the following function allows
// to avoid copying the clause and instead provides tracing of the required
// 'add' and 'remove' operations.

void Proof::strengthen_clause (Clause * c, int remove) {
  LOG (c, "PROOF strengthen by removing %d in", remove);
  assert (clause.empty ());
  for (int i = 0; i < c->size; i++) {
    int internal_lit = c->literals[i];
    if (internal_lit == remove) continue;
    add_literal (internal_lit);
  }
  int64_t id = ++internal->clause_id;
  clause_id = id;
  add_derived_clause ();
  delete_clause (c);
  c->id = id;
}

/*------------------------------------------------------------------------*/

void Proof::add_original_clause () {
  LOG (clause, "PROOF adding original external clause");
  assert (clause_id);

  if (lratbuilder)
    lratbuilder->add_original_clause (clause_id, clause);
  if (lratchecker)
    lratchecker->add_original_clause (clause_id, clause);
  if (checker)
    checker->add_original_clause (clause_id, clause);
  if (tracer)
    tracer->add_original_clause (clause_id, clause);
  clause.clear ();
}


void Proof::add_derived_clause () {
  LOG (clause, "PROOF adding derived external clause");
  assert (clause_id);
  if (lrat) {
    assert (lratbuilder);
    proof_chain = lratbuilder->add_clause_get_proof (clause_id, clause);
    if (lratchecker)
      lratchecker->add_derived_clause (clause_id, clause, proof_chain);
    if (checker)
      checker->add_derived_clause (clause_id, clause);
    if (tracer)
      tracer->add_derived_clause (clause_id, clause, proof_chain);
  }
  else {
    if (lratbuilder)
      lratbuilder->add_derived_clause (clause_id, clause);
    if (lratchecker)
      lratchecker->add_derived_clause (clause_id, clause);
    if (checker)
      checker->add_derived_clause (clause_id, clause);
    if (tracer)
      tracer->add_derived_clause (clause_id, clause);
  }
  proof_chain.clear ();
  clause.clear ();
}

void Proof::delete_clause () {
  LOG (clause, "PROOF deleting external clause");
  if (lratbuilder)
    lratbuilder->delete_clause (clause_id, clause);
  if (lratchecker)
    lratchecker->delete_clause (clause_id, clause);
  if (checker)
    checker->delete_clause (clause_id, clause);
  if (tracer)
    tracer->delete_clause (clause_id, clause);
  clause.clear ();
}

void Proof::finalize () {
  if (!lrat) return;
  if (!tracer) return;
  assert (lratbuilder);
  lratbuilder->finalize ();
}

}

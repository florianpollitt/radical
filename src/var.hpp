#ifndef _var_hpp_INCLUDED
#define _var_hpp_INCLUDED

namespace CaDiCaL {

struct Clause;

// This structure captures data associated with an assigned variable.

struct Var {

  // Note that none of these members is valid unless the variable is
  // assigned.  During unassigning a variable we do not reset it.

  int level;         // decision level
  int trail;         // trail height at assignment
  int before;        // 0 if no other assignment before on this level
  int next;          // 0 if no other assignment after on this level
  Clause * reason;   // implication graph edge during search
};

}

#endif

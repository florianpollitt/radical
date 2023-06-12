#include "internal.hpp"

namespace CaDiCaL {

// adds a trail to trails and the control to multitrail
//
void Internal::new_trail_level (int lit) {
  level++;
  control.push_back (Level (lit, trail.size ()));
  if (!opts.multitrail) return;
  assert (opts.multitrail);
  multitrail.push_back (0);
  control.back ().trail = 0;
  trails.push_back (new vector<int> ());
  assert (level == (int) trails.size ());
}

// clears all trails above level
//
void Internal::clear_trails (int level) {
  assert (level >= 0);
  while (trails.size () > (size_t) level) {
    auto t = trails.back ();
    trails.pop_back ();
    delete t;
  }
}

// returns size of trail
// with opts.multitral returns size of trail of level l
//
int Internal::trail_size (int l) {
  if (!opts.multitrail || l == 0)
    return (int) trail.size ();
  assert (l > 0 && trails.size () >= (size_t) l);
  return (int) trails[l - 1]->size ();
}

// pushes lit on trail for level l
//
void Internal::trail_push (int lit, int l) {
  if (!opts.multitrail || l == 0) {
    trail.push_back (lit);
    return;
  }
  assert (l > 0 && trails.size () >= (size_t) l);
  trails[l-1]->push_back (lit);
}


// returns the next level that needs to be propagated
//
int Internal::next_propagation_level (int last) {
  if (!opts.multitrail) {
    return - (propagated == trail.size ());
  }
  if (last == -1 && propagated < trail.size ())
    return 0;
  for (int l = last; l < level; l++) {
    if (l < 0) continue;
    assert (l >= 0 && trails.size () >= (size_t) l);
    if (multitrail[l] < trails[l]->size ()) {
      return l + 1;
    }
  }
  return -1;
}

// returns the trail that needs to be propagated
//
vector<int> * Internal::next_trail (int l) {
  if (!opts.multitrail || l <= 0) {
    return &trail;
  }
  assert (l > 0 && trails.size () >= (size_t) l);
  return trails[l-1];
}

// returns the point from which the trail is propagated
//
int Internal::next_propagated (int l) {
  if (l < 0) return 0;
  if (!opts.multitrail || l == 0) {
    return propagated;
  }
  assert (l > 0 && trails.size () >= (size_t) l);
  return multitrail[l-1];
}

// without opts.multitrail returns c, else
// returns a conflict of conflicting_level at most l
//
Clause * Internal::propagation_conflict (int l, Clause * c) {
  if (!opts.multitrail)
    return c;
  if (c)
    conflicts.push_back (c);
  else if (conflicts.empty ()) return 0;
  else c = conflicts.back ();
  int conf = conflicting_level (c);
  for (auto cl : conflicts) {
    int ccl = conflicting_level (cl);
    if (ccl < conf) {
      c = cl;
      conf = ccl;
    }
  }
  if (conf <= l || l < 0) return c;
  return 0;
}

// returns the lowest level within some conflicting clause
//
int Internal::conflicting_level (Clause * c) {
  int l = 0;
  for (const auto & lit : *c) {
    const int ll = var (lit).level;
    l = l < ll ? ll : l;
  }
  return l;
}



// updates propagated for the current level
//
void Internal::set_propagated (int l, int prop) {
  if (!opts.multitrail || l == 0) {
    propagated = prop;
    return;
  }
  multitrail[l-1] = prop;
  
}


}

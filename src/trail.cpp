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
  trailsize++;
  assert (l > 0 && trails.size () >= (size_t) l);
  trails[l-1]->push_back (lit);
}

}

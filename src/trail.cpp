#include "internal.hpp"

namespace CaDiCaL {

// adds a trail to trails and the control to multitrail
//
void Internal::new_trail_level () {
  assert (control2.levels == (int) trails.size ());
  multitrail.push_back (Trail (trails.size ()));
  trails.push_back (new vector<int> ());
  control2.levels = trails.size ();
}

}

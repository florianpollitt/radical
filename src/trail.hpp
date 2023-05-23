#ifndef _trail_h_INCLUDED
#define _trail_h_INCLUDED


namespace CaDiCaL {


// trail is a doubly linked list with assignments
// this is realized a vector of Var (in vtab)
// additionally we need a control stack pointing to the decision and last
// propagated literal in each level

// we want to be able to iterate over a level -> ??
//                        iterator for each level l
//                        that starts with
//                        v = controls[l].first and iterates with
//                        v = Var (v).next until v = 0
// we want a priority queue over levels which need to be propagated -> ??
//                        Var (propagated).end != 0 -> needs to be propagated


// trail also needs:
//       size            -> int, size_t, int64_t or uint64_t ??


struct Control {
  int level;                  // same as index of controls
  int first;                  // decision var of current level
  // maybe call this last ???
  int propagated;             // last propagated var for current level
  int size;                   // int, size_t, int64_t or uint64_t ??
  // int64_t position () { return ((int64_t) level << 32) + size; }
};

class Trail {
  vector<Control> controls;   // maybe put in interal instead...

  int size ();                // sizes of all levels combined

};

}

#endif

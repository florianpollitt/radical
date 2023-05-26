#ifndef _trail_h_INCLUDED
#define _trail_h_INCLUDED


namespace CaDiCaL {


// realizes trail or multitrail
// assignments on level 0 are always pushed on trail
// with option multitrail levels > 0 are on trails
// then we need additional control structures.
  
/*
struct Trail {
  int level;                  // same as index of controls
  int first;                  // decision var of current level
  int last;                   // last var for level
  int propagated;             // last propagated var for current level
  int size;                   // int, size_t, int64_t or uint64_t ??
  Trail (int l) : level (l), first (0), last (0), propagated (0), size (0) {}
};

struct Control {

  int size;                   // sizes of all levels combined
  int levels;

  static unsigned inc (unsigned u) { return u + 1u; }
  class iterator {
    int idx;
  public:
    iterator (int i) : idx (i) { }
    void operator++ () { idx = inc (idx); }
    const int & operator* () const { return idx; }
    friend bool operator != (const iterator & a, const iterator & b) {
      return a.idx != b.idx;
    }
  };

  iterator begin () { return assert (levels >= 0), iterator (0); }
  iterator end ()   { return assert (levels >= 0), iterator (levels); }
  Control () : levels (1) { }
};
*/
}

#endif

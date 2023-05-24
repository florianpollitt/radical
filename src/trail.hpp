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
  int last;                   // last var for level
  int propagated;             // last propagated var for current level
  int size;                   // int, size_t, int64_t or uint64_t ??
  Control (int l) : level (l), first (0), last (0), propagated (0), size (0) {}
};

class Trail {
  vector<Control> controls;   // maybe put in internal instead...

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

public:
  iterator begin () { return assert (levels >= 0), iterator (0); }
  iterator end ()   { return assert (levels >= 0), iterator (levels); }
  Trail ();
  ~Trail ();
};

}

#endif

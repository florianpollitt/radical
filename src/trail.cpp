#include "internal.hpp"

namespace CaDiCaL {

Trail::Trail () : levels (1)
{
  LOG ("Trail new");
  // initialize Controls
  controls.push_back (Control (0));
}

Trail::~Trail () {
  LOG ("Trail delete");
}


}

#pragma once
// Which reverb tank the plugin uses. Both are compiled; the alias picks one, so
// the two can be measured against the same probes and against each other:
//
//   cmake -S . -B build -DMIXAGENT_REVERB_FDN=ON     # the feedback delay network
//   cmake -S . -B build -DMIXAGENT_REVERB_FDN=OFF    # the comb bank (the default)
//
// ./build/ReverbProbe_artefacts/Release/ReverbProbe measures BOTH in one run,
// whichever is selected here. AUDIT.md records the numbers and the decision.
#include "Reverb.h"
#include "ReverbFDN.h"

namespace agm {

#if defined(MIXAGENT_REVERB_FDN) && MIXAGENT_REVERB_FDN
using ReverbEngine = ReverbFDN;
#else
using ReverbEngine = Reverb;
#endif

} // namespace agm

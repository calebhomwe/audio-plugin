#pragma once

// ---------------------------------------------------------------------------
// Mutation switch for the test-the-tests audit.
//
// A test suite that has never been seen to fail is not evidence. This header
// lets exactly one deliberate defect be compiled into the DSP, so the suite can
// be shown to go RED for each defect that actually matters to a plugin.
//
// MIXAGENT_MUTATE is 0 in every normal build, and every AGM_MUTATION block below
// therefore compiles out completely. The defects are the ones that are known to
// ship in audio code - a stage that passes audio through untouched, a knob that
// is read 100x out, a latency report that lies, an interpolator that is skipped
// - not arbitrary character edits.
//
//   n   defect                                                   assertions that must fail
//   1   compressor gain computer neutralised (no gain reduction)  compressor ratio/knee/attack/detector, smoke comp, dead-knob
//   2   make-up gain folded back into the clamped reduction       compressor make-up
//   3   attack calibration removed (knob taken as internal time)  compressor printed-vs-measured attack
//   4   compressor threshold/ratio smoothing removed              audit click hunt
//   5   limiter true-peak interpolator skipped (sample peak only) limiter dBTP bounds, smoke true-peak
//   6   limiter getLatencySamples() reports 0                     latency == measured, everywhere
//   7   imager side gain frozen at unity                          width table, width 0 mono collapse
//   8   imager takes the percentage as a raw side gain            width table
//   9   pre-delay moved back behind the tank                      pre-delay onset / tail / automation
//  10   reverb denormal + non-finite flush removed                reverb reaches exact silence, NaN recovery
//  11   low tom rendered from the kick's voice spec               toms are not copies of the kick
//  12   PolyBLEP disabled (naive sawtooth)                        instrument fold-down aliasing
//  13   saturation oversampling bypassed                         saturation fold-down aliasing
//  14   preset apply no longer starts from the factory defaults   host-preset ordered pairs
//  15   in/out gain smoothing coefficient computed per block      audit click hunt
//  16   delay read head degraded to nearest neighbour             delay distortion / wobble
//  17   EQ coefficient smoothing removed                          EQ zipper step
//  18   comb bank halved back to 8 lines                          reverb flatness over six seeds
//  19   ScopedNoDenormals removed from processBlock               (performance only - see AUDIT.md)
//  20   one parameter left out of the state restore                state restoration, all parameters
//
// Run the whole table with:
//   tools/mutation_audit.sh
// ---------------------------------------------------------------------------

#ifndef MIXAGENT_MUTATE
 #define MIXAGENT_MUTATE 0
#endif

#define AGM_MUTATION(n) (MIXAGENT_MUTATE == (n))

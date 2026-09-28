#!/usr/bin/env bash
# Mutation audit: compile one deliberate defect at a time into the DSP and check
# that the test suite goes RED. A suite that has never been seen to fail is not
# evidence, and this is what turns "all green" into a claim with a proof.
#
#   tools/mutation_audit.sh                     # every mutation in the table
#   tools/mutation_audit.sh 5 9 14              # only these
#   MUT_BUILD=build-mut tools/mutation_audit.sh # use another build directory
#
# The table of defects lives in Source/DSP/Mutate.h. Exit status is 0 when every
# mutation was caught (the suite went red for each) and 1 if any SURVIVED, which
# is a finding about the suite, not about the mutation.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${MUT_BUILD:-$ROOT/build}"
JUCE_DIR="${MUT_JUCE:-${FETCHCONTENT_SOURCE_DIR_JUCE:-/home/user/JUCE}}"
JOBS="${MUT_JOBS:-3}"
ALL_MUTATIONS=(1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20)
TARGETS=(MixAgentSmokeTest MixAgentAuditTest MixAgentCharacterTest)

MUTATIONS=("$@")
[ ${#MUTATIONS[@]} -eq 0 ] && MUTATIONS=("${ALL_MUTATIONS[@]}")

configure() {   # $1 = mutation number
    cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
          -DMIXAGENT_COPY_PLUGIN=OFF -DFETCHCONTENT_SOURCE_DIR_JUCE="$JUCE_DIR" \
          -DMIXAGENT_MUTATE="$1" >/dev/null 2>&1
}

survivors=0
printf '%-4s %-10s %-10s %-10s %s\n' n smoke audit character verdict

for n in "${MUTATIONS[@]}"; do
    configure "$n"
    if ! ninja -C "$BUILD" -j"$JOBS" -l 4.5 "${TARGETS[@]}" >/dev/null 2>&1; then
        printf '%-4s %s\n' "$n" "BUILD FAILED (a mutation must still compile)"
        survivors=$((survivors + 1))
        continue
    fi
    line=""
    red=0
    for t in "${TARGETS[@]}"; do
        out="$("$BUILD/${t}_artefacts/Release/$t" 2>&1)"
        rc=$?
        fails=$(printf '%s' "$out" | grep -c '^\[FAIL\]')
        if [ "$rc" -ne 0 ] || [ "$fails" -gt 0 ]; then red=1; fi
        [ "$rc" -ne 0 ] && [ "$fails" -eq 0 ] && fails="crash"
        line="$line$(printf '%-10s' "$fails")"
        printf '%s\n' "$out" | grep '^\[FAIL\]' | sed "s|^|    mut$n $t: |" >> "$BUILD/mutation_fails.log"
    done
    if [ "$red" -eq 1 ]; then verdict=caught; else verdict="SURVIVED"; survivors=$((survivors + 1)); fi
    printf '%-4s %s%s\n' "$n" "$line" "$verdict"
done

configure 0
ninja -C "$BUILD" -j"$JOBS" -l 4.5 "${TARGETS[@]}" >/dev/null 2>&1
echo "restored to MIXAGENT_MUTATE=0"
echo "surviving mutations: $survivors of ${#MUTATIONS[@]}   (per-assertion detail: $BUILD/mutation_fails.log)"
[ "$survivors" -eq 0 ]

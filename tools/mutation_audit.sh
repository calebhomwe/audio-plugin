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
# Wave 6a: 21-34 break the editor's APPEARANCE, and the rule each one must turn red
# is measured by EditorProbe, which needs a display - run under xvfb-run when there
# is one. Building only that target also makes a visual mutation three times faster
# than a DSP one.
VISUAL_MUTATIONS=(21 22 23 24 25 26 27 28 29 30 31 32 33 34)
VISUAL_TARGET=EditorProbe
# 33 is EXPECTED to survive: R13 is measured on what a meter reports about its own
# scale, not on its pixels, so it cannot catch a meter that lies. Declared here so
# the exit status still means something; AUDIT.md says why it is kept.
EXPECTED_SURVIVORS=(33)

MUTATIONS=("$@")
[ ${#MUTATIONS[@]} -eq 0 ] && MUTATIONS=("${ALL_MUTATIONS[@]}" "${VISUAL_MUTATIONS[@]}")

is_visual() { for v in "${VISUAL_MUTATIONS[@]}"; do [ "$v" = "$1" ] && return 0; done; return 1; }
is_expected_survivor() { for v in "${EXPECTED_SURVIVORS[@]}"; do [ "$v" = "$1" ] && return 0; done; return 1; }
XVFB=$(command -v xvfb-run || true)

configure() {   # $1 = mutation number
    cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
          -DMIXAGENT_COPY_PLUGIN=OFF -DFETCHCONTENT_SOURCE_DIR_JUCE="$JUCE_DIR" \
          -DMIXAGENT_MUTATE="$1" >/dev/null 2>&1
}

survivors=0
printf '%-4s %-10s %-10s %-10s %s\n' n smoke audit 'char/probe' verdict

for n in "${MUTATIONS[@]}"; do
    configure "$n"
    if is_visual "$n"; then
        if ! ninja -C "$BUILD" -j"$JOBS" -l 4.5 "$VISUAL_TARGET" >/dev/null 2>&1; then
            printf '%-4s %s\n' "$n" "BUILD FAILED (a mutation must still compile)"
            survivors=$((survivors + 1))
            continue
        fi
        bin="$BUILD/${VISUAL_TARGET}_artefacts/Release/$VISUAL_TARGET"
        if [ -n "$XVFB" ]; then out="$("$XVFB" -a "$bin" 2>&1)"; else out="$("$bin" 2>&1)"; fi
        rc=$?
        fails=$(printf '%s' "$out" | grep -c '^\[FAIL\]')
        printf '%s\n' "$out" | grep '^\[FAIL\]' | sed "s|^|    mut$n $VISUAL_TARGET: |" >> "$BUILD/mutation_fails.log"
        if [ "$rc" -ne 0 ] || [ "$fails" -gt 0 ]; then
            verdict=caught
        elif is_expected_survivor "$n"; then
            verdict="SURVIVED (expected - see Source/DSP/Mutate.h)"
        else
            verdict="SURVIVED"; survivors=$((survivors + 1))
        fi
        printf '%-4s %-10s %-10s %-10s %s\n' "$n" "-" "-" "$fails" "$verdict"
        continue
    fi
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
ninja -C "$BUILD" -j"$JOBS" -l 4.5 "${TARGETS[@]}" "$VISUAL_TARGET" >/dev/null 2>&1
echo "restored to MIXAGENT_MUTATE=0"
echo "surviving mutations: $survivors of ${#MUTATIONS[@]}   (per-assertion detail: $BUILD/mutation_fails.log)"
[ "$survivors" -eq 0 ]

# Native builds and release gates

Use CMake 3.24+, C++17, Git, Python 3.11+ and JUCE 8.0.9 at commit
`f72bad64d29715216226685810c5196bd0d79d77`. CMake fetches that exact revision.
For an existing verified checkout, use
`-DFETCHCONTENT_SOURCE_DIR_JUCE=/absolute/path/to/JUCE`. CI fetches and verifies
the SHA before configuring, and disables system plugin copying.

## Windows x64

Install Visual Studio 2022 with Desktop development with C++ and a Windows SDK:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DMIXAGENT_COPY_PLUGIN_AFTER_BUILD=OFF
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure --no-tests=error --repeat until-fail:2
python -B -m unittest discover -s Tests -p 'test_*vst3*.py' -v
```

Keep the entire `build/MixAgent_artefacts/Release/VST3/MixAgent.vst3` directory,
including `Contents/Resources/moduleinfo.json`. Its Windows binary is
`Contents/x86_64-win/MixAgent.vst3`. The standalone is
`build/MixAgent_artefacts/Release/Standalone/MixAgent.exe`.

## Linux x86_64

Ubuntu 24.04 needs Ninja and JUCE's native audio/UI development packages:

```sh
sudo apt-get update
sudo apt-get install -y ninja-build libasound2-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libxcomposite-dev libfreetype-dev libfontconfig1-dev libcurl4-openssl-dev libgtk-3-dev mesa-common-dev libglu1-mesa-dev xvfb
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DMIXAGENT_COPY_PLUGIN_AFTER_BUILD=OFF
cmake --build build --parallel 2
xvfb-run -a ctest --test-dir build --output-on-failure --no-tests=error --repeat until-fail:2
python3 -B -m unittest discover -s Tests -p 'test_*vst3*.py' -v
```

The module is `MixAgent.vst3/Contents/x86_64-linux/MixAgent.so`; the standalone
is `build/MixAgent_artefacts/Release/Standalone/MixAgent`. Configured Ninja and
Visual Studio builds both use the `Release/` artifact level.

## Stress and benchmarks

`MixAgentStressTest` runs deterministic audio/state/MIDI/lifecycle oracles.
On Linux the target also wraps `malloc`, `calloc` and `realloc` for callback
allocation checks. The baseline and changed processor can run the identical
test source against separate source trees. Benchmark mode uses fixed signal,
voice and block-size profiles, reports callback p50/p95/max and quantized
output hashes, and applies no timing threshold:

```sh
xvfb-run -a ./build/MixAgentStressTest_artefacts/Release/MixAgentStressTest --benchmark
```

CI repeats the registered tests twice and records three benchmark repetitions.
Compare the same machine, compiler, Release flags and workload; preserve the
individual measurements when reporting a change. Windows uses the corresponding
`.exe` path and needs no `xvfb-run`.

## Verify and stage actual deliverables

`Tools/vst3_gate.py` validates the exact bundle set, structurally bounded
ELF64/PE32+ headers, x86_64 architecture, AgentMix vendor, unique VST3 audio and
controller identities, and every resource hash. `--snapshot FILE` and
`--check-snapshot FILE` prove test runs preserved binaries/resources. The
actual VST3 CTest uses this guard to run the real host twice, retaining its
exit status and rechecking files after each invocation. Header inspection is
not a substitute for successful host loading.

After building and passing tests (including the screenshot probe), stage a
new directory using the actual full source SHA:

```powershell
python Tools/package_release.py --build-dir build --platform windows-x64 --source-commit (git rev-parse HEAD) --output release/MixAgent
python Tools/package_release.py --verify release/MixAgent
```

On Linux use `python3` and `--platform linux-x86_64`. Staging preserves the
whole VST3 in `vst3/`, the standalone in `standalone/`, and `mixagent-ui.png`.
`build-manifest.json` records source/JUCE commits, native identities and each
file's size/hash. `checksums.sha256` lists all payload files. The utility
verifies completeness, identities and integrity before atomically exposing
the directory and refuses to overwrite a previous package.

Both CI jobs host the staged copy twice and reverify the full package before
upload. Separate evidence artifacts contain actual CTest results, snapshots,
host exits and benchmark output; the manifest does not invent test success.
Install the complete outer bundle under Windows
`C:\Program Files\Common Files\VST3` or Linux `~/.vst3/`, then rescan in a DAW.
The current structural release guards cover Windows/Linux x86_64. A new workflow
or passing synthetic fixture does not prove a platform build or DAW session.

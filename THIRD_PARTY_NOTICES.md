# Third-party notices

This project is licensed under the GNU Affero General Public License v3.0 or
later (see `LICENSE`). It is not distributed with any third-party source in the
repository, but it builds and links against the components below. Anyone
distributing a compiled build of this project is bound by their terms as well.

## JUCE

The plugin is built with the [JUCE framework](https://juce.com/), which its
authors dual-license under the **AGPLv3** and a separate commercial JUCE
licence.

This project takes the AGPLv3 option. That is why this repository is AGPLv3 and
why its complete corresponding source must stay available to anyone who receives
a binary. If you instead want to ship a closed-source build, you need a paid
JUCE licence from the JUCE authors, and this project's own licence would have to
change too — an AGPLv3 project cannot be relicensed by a downstream user.

JUCE is not vendored in this repository: `CMakeLists.txt` fetches it at configure
time with `FetchContent`, pinned to tag `8.0.9`. Its own licence text ships in the
fetched checkout as `LICENSE.md`.

## Steinberg VST 3 SDK

The VST3 build target links the [VST 3 SDK](https://github.com/steinbergmedia/vst3sdk),
which Steinberg dual-licenses under the **GPLv3** and a separate proprietary VST3
licence agreement.

This project takes the GPLv3 option. GPLv3 §13 and AGPLv3 §13 expressly permit
linking works under the two licences, so an AGPLv3 plugin built against the
GPLv3 VST3 SDK is a permitted combination; the AGPL's network-use clause applies
to this project's own code.

Shipping a VST3 binary under Steinberg's *proprietary* option instead would
require signing their VST3 licence agreement.

"VST" is a registered trademark of Steinberg Media Technologies GmbH. Using the
VST name or logo to market a product is governed by Steinberg's trademark terms,
separately from the SDK's source licence. This repository uses the name only to
describe the plugin format it builds.

## No bundled third-party content

No third-party presets, artwork, impulse responses, samples or source code are
included in this repository.

# Third-party notices

Our own code and documentation are under the MIT licence in [LICENSE](LICENSE). This repository also contains or is
derived from the third-party material listed below. Each vendored file keeps its original licence header.

## unswbc toolkit and game engine

- Copyright (c) 2026 UNSW CPMSoc
- Licence: MIT
- Source: https://github.com/unswcpmsoc/unswbc (the 1.2.9 package metadata lists https://github.com/unswcpmsoc/battlecode)

What this repository contains from it:

- [`tools/abyss_tools/schema/replay.capnp`](tools/abyss_tools/schema/replay.capnp): the engine's replay schema,
  vendored so that replays can be read without the engine. A few fields were reconstructed from the toolkit's replay
  viewer, as the file's header notes.
- [`tools/tests/fixtures/*.replay`](tools/tests/fixtures): two games recorded with the toolkit in its sandbox. Both
  contain the official Arena map, and one also contains the moves and log lines of the toolkit's C++ template bot
  (`unswbc init cpp`).
- Short excerpts in the format of the toolkit's console output, used as parser test data in [`infra/tests`](infra/tests)
  and [`tools/tests`](tools/tests).

Parts of the bot and of the tools re-implement engine behaviour so that they can predict or replay it. Examples are
the tile stepping across kelp and portals in [`bots/abyss/board.hpp`](bots/abyss/board.hpp), the round protocol
parser and the replay state rebuilder. The game rules summarised in [docs/rules](docs/rules) are the engine's rules.

Not included: the toolkit itself, its template bot and the official map files. Install the toolkit with
`uv tool install unswbc==1.2.9` (or `pip install unswbc==1.2.9`, which needs Python 3.11 or newer). `unswbc maps` and
`unswbc init` then create the maps and the template bot locally.

```
MIT License

Copyright (c) 2026 UNSW CPMSoc

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Cap'n Proto `c++.capnp`

- Copyright (c) 2013-2014 Sandstorm Development Group, Inc. and contributors
- Licence: MIT
- File: [`tools/abyss_tools/schema/capnp/c++.capnp`](tools/abyss_tools/schema/capnp/c++.capnp), the standard
  annotation schema that the replay schema imports. It is vendored so that `pycapnp` can load the replay schema
  without a Cap'n Proto installation.

```
Copyright (c) 2013-2014 Sandstorm Development Group, Inc. and contributors
Licensed under the MIT License:

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

## Packages installed separately

The Python tools and pipelines use packages that are installed from PyPI and are not part of this repository:
`unswbc`, `wasmtime` (the WASM runtime `unswbc` depends on; CI pins 49.0.0), `pycapnp`, `numpy`, `scipy`, `zstandard`,
`pytest`, `torch`, `cma` (the tuning backend) and `tomli` (optional, used only on Python versions before 3.11). Each
is distributed under its own licence.

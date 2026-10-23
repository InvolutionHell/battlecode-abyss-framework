# Contributing

Issues and pull requests are welcome. The working rules for changing the bot are in
[docs/dev-guide.md](docs/dev-guide.md), and how a change is judged is in [docs/methodology.md](docs/methodology.md).
Please read both before you open a pull request that touches a bot.

## The released bot is frozen

[`bots/abyss`](bots/abyss) is our final submission. Its WASM, built by `unswbc` 1.2.9 with `--sandbox`, must keep the
sha256 `0f39d15f10a503b5d5602144660b8c3d3e62cae53ed1fa94e792c88e395e3b0e`. Code that is switched off stays in the source
on purpose, because removing it can change the instruction-metered search.

- Fixes to comments and documentation are welcome. Check the hash after the change
  ([docs/dev-guide.md](docs/dev-guide.md#verifying-the-released-bot)); CI checks it too.
- To change the behaviour, copy the bot to a new folder (for example `bots/mybot`) and change the copy.

## Rules for bot changes

The short version of [docs/dev-guide.md](docs/dev-guide.md):

1. Always emit a valid action. The engine's default action is suicide.
2. Put each new feature behind a compile-time switch whose default reproduces the previous build.
3. Test against the previous best on several maps, with paired seeds and a same-seed baseline, and report the
   numbers in the pull request.
4. Run `unswbc run --sandbox` before you call a change done. Stay under the 95M-point line per turn; the judge's
   limit is 100M.
5. Keep the bot general: no map names, map databases, pearl timetables or opening books.
6. Purge the WASM cache after you edit a header (see the [README](README.md#quick-start)).
7. Never commit secrets. The upload key belongs in `unswbc auth` or the `UNSWBC_KEY` environment variable.

## Before you open a pull request

Run the tests that cover your change (see [README.md](README.md#tests)):

```sh
tests/native/run.sh
(cd tools && uv run --with pycapnp --with numpy --with scipy --with pytest --with zstandard python -m pytest -q)
uv run --with pytest python -m pytest -q infra/tests
rl/tests/run_tests.sh
```

- Write code, comments and documentation in English.
- Every directory README comes in two files, `README.md` (English) and `README.zh-CN.md` (Simplified Chinese), with
  the same content.
- Tools take paths, hosts and users from flags, environment variables or example config files. Never hard-code them.

By contributing you agree that your contribution is licensed under the MIT licence of this repository.

# Horcrux

Horcrux is my personal, cross-platform terminal code editor: familiar enough
to start using immediately, capable enough for serious coding, Git workflows,
terminals, debuggers, and reviewed agent-assisted changes.

This repository is my personal workbench, built first for my own Linux,
Windows, and macOS workflow. It is public for collaboration and learning, not a
polished general-purpose editor or a promise of support for other users.

## Why Horcrux?

I initially named the editor `vijai`, but a Harry Potter compilation happened
to be playing on YouTube while I was working on it. `Horcrux` felt like a better
name than simply calling the editor `vijai`.

The project is written in modern C++ and is released under the MIT license.
The initial target is `0.1.0-dev`.

## Build

Requirements: CMake 3.28+, a C++20 compiler, and Git. Initialize the pinned
Tree-sitter and vcpkg submodules. CMake bootstraps the pinned vcpkg copy and
installs the manifest dependencies automatically:

```bash
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
./build/horcrux --help
```

Run `./build/horcrux path/to/file` from a terminal for the current prototype
workspace. It supports ASCII typing, arrows, Backspace, Enter,
Home/End, paging, Delete, Tab, `Ctrl+Z`/`Ctrl+Y` (undo/redo), `Ctrl+S` (save),
`Ctrl+F` with `F3` find-next, and `Ctrl+Q` (quit). The terminal frontend is intentionally
small while the platform-neutral editor core is developed.

`Ctrl+T` opens a persistent Bash session in the workspace folder. The Shell tab
accepts normal shell input and common control keys such as `Ctrl+C`, `Ctrl+D`,
and `Ctrl+L`; the session stays alive while switching tools or files. On
Windows, Git Bash's `bash.exe` must be available on `PATH`.

C++ syntax highlighting is powered by the pinned Tree-sitter runtime and C++
grammar submodules. Additional languages use the existing lexical fallback
until their Tree-sitter grammars are added.

Unsaved edits are appended to a recovery journal. If a later launch finds one,
the workspace pauses editing until `F2` restores it or `F3` discards it.
Cursor and viewport state are restored per document unless `--no-restore` or
`--safe` is used.

Trusted `horcrux.json` files are schema-checked before use. `F5` runs the named
`build` task (or the first configured task) as an argv process, and `F4` shows
its captured output. A task cannot set its working directory outside the
project root; shell tasks must explicitly opt into one Bash script string.

`F6` opens a read-only Git status view using the installed Git CLI. It parses
NUL-delimited porcelain output so spaces and rename paths are handled safely.
In a trusted project, `F7` explicitly toggles the current file between staged
and unstaged without changing its working-tree contents.
`F8` prompts for a commit message and commits the already-staged changes through
an argv call to Git; pressing Escape cancels without changing repository state.

## Status

Horcrux is in active development. See [the product plan](docs/PLAN.md) for the
locked v0.1 design and delivery milestones.

The repository includes CI for Linux x64, Windows x64, and Apple Silicon macOS.
Before a release, it also needs a direct Windows 11 x64 smoke test because the
standard hosted Windows x64 runner is Windows Server.

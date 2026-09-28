# Repository Guidelines

## Project scope and structure

ToyLisp is a C++17 / Qt5 Widgets desktop application built with CMake. Linux is
its initial target; Windows compatibility is intended but unverified. Arithmetic
is implemented. Identity, Lisp and Macaulay2 remain planned capabilities.

- `src/app/`: application entry point.
- `src/ui/`: main window, text editors, menus and file dialogs.
- `src/editor/`: widget-independent block and bracket operations.
- `src/evaluators/`: evaluator interface, registry and arithmetic environment.
- `tests/`: Qt Test unit and integration coverage.
- `resources/`: bundled toolbar SVG icons.
- `README.md`: supported behavior, limitations, setup and commands.

Update README.md and this file with each relevant change in project progress,
layout, commands or capabilities. Clearly distinguish implemented and planned work.
Preserve user changes already present in the working tree.

## Build, run and test

Require CMake 3.16+, C++17, Qt 5.12+ Widgets and Qt Test for testing. The current
machine has CMake 3.28.3, GCC 13.3 and Qt 5.15.2 at the prefix below.

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/home/juergen/Qt/5.15.2/gcc_64 -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
./build/toylisp
git diff --check
```

CTest uses the offscreen Qt platform. The window test emits a screenshot in the
build directory. Check native dialogs and desktop integration manually where a
GUI is available. Do not claim Windows verification without a Windows build.

## Architecture and behavior

Keep parser/evaluator logic separate from editor UI. One application-wide registry
owns one persistent environment per evaluator; editors do not own environments.
Evaluator overrides and the global default must use that same registry.

Markers are ordinary text. Preserve the agreed `>>`, `==`, `##` replacement
boundaries and stop end-marker searches at the next input block. Evaluation is
triggered only by unmodified Return at the end of an evaluation line. Output
replacement must be one undo operation. Document undo does not undo session state.

Arithmetic blocks commit assignments only after the whole block succeeds. Test
valid and malformed input, result formatting, error lines and environment state.
Use Qt's portable APIs and atomic saving; do not silently fall back to truncating
existing files when atomic saving fails.

## Style and validation

Use C++17, four-space indentation and `.clang-format` (LLVM base, 100 columns).
Use PascalCase for types and camelCase for functions and variables. Prefer
specific names distinguishing editor behavior from evaluator behavior. Avoid
platform-specific APIs when Qt provides a portable equivalent.

Add meaningful regression tests for evaluator and block behavior changes. Include
boundary cases, undo/redo and file preservation where relevant. Run the documented
build and CTest commands, review documentation accuracy and check whitespace.

## Commits and pull requests

Use short imperative commit subjects and focused commits. PRs describe behavior,
purpose and validation, link relevant issues and include screenshots for UI changes.
Do not claim tests or manual checks that were not performed.

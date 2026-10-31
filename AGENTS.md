# Repository Guidelines

## Project scope and structure

ToyLisp is a C++17 / Qt5 Widgets desktop application built with CMake. Linux is
its initial target; Windows compatibility is intended but unverified. Arithmetic
is implemented. Identity, Lisp and Macaulay2 remain planned capabilities.
The standalone Lisp500 CLI has a Linux LP64 port; Lisp integration into the Qt
editor remains planned.

- `src/app/`: application entry point.
- `src/ui/`: main window, text editors, menus and file dialogs.
- `src/editor/`: widget-independent QtGui block operations and a bracket highlighter
  with metadata-based matching; both are built into `toylisp_ui`.
- `src/evaluators/`: evaluator interface, registry and arithmetic environment.
- `src/lisp500/`: unchanged `lisp500a.c` / `init500.lisp` originals and standalone
  `lisp500a64.c` / `init500_64.lisp` Linux LP64 ports.
- `tests/`: Qt Test coverage plus Lisp500 C regressions and a native FASL fixture.
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
./build/lisp500
./build/lisp500 src/lisp500/init500_64.lisp
git diff --check
```

CTest uses the offscreen Qt platform. The window test emits a screenshot in the
build directory. Check native dialogs and desktop integration manually where a
GUI is available. Do not claim Windows verification without a Windows build.

On 64-bit Linux the CMake build enables C11 for the standalone `lisp500` target
and its `lisp500_tests` CTest suite. Direct compilation is also supported:

```bash
gcc src/lisp500/lisp500a64.c -o build/lisp500 -lm -ldl
```

Add `-rdynamic` when loading native FASL modules that call interpreter functions;
CMake exports those functions already. The port requires LP64 (eight-byte `long`
and pointers), so Windows LLP64 is unsupported. Never overwrite the 32-bit original.

Optional bracket search benchmarks (100,000 text lines, no timing assertions) are
built with tests but run separately from CTest:

```bash
QT_QPA_PLATFORM=offscreen ./build/toylisp_bracket_benchmarks
```

## Architecture and behavior

Lisp500 remains independent of the Qt evaluator registry. Preserve the existing
interpreter function and variable names. Its two-word headers, allocations and
copy/clear sizes must agree with eight-byte `lval`. Strings encode their length
as `(length + 8) << 5`; raw bit buffers store the 64-bit payload word count above
bit 7. Keep GC mark bit 2 clear in newly allocated headers. Variadic `ma`/`ms`
payloads must have type `lval`, including negative constants and native pointers.
Nonlocal jumps carry Lisp values in the existing GC root `xvalues`, with `1` as
the `longjmp` signal. Keep cleanup, nested jumps and multiple values covered.
Native FASL relocations use bits 62..63 and eight-byte table entries; 32-bit
FASLs are incompatible and require regeneration. Arithmetic still uses `double`.
Raw bit-buffer lengths decode as `(header >> 8) * 64 + ((header >> 3) & 31) * 2 +
(header & 1) - 63`. `MAKEJ` type 20 creates a string with its own byte-length header
and terminator. `IVAL`/`JREF`/`DPB` retain large raw integers in two-payload-word
numeric boxes; `LDB` and `(SETF JREF)` use that exact copy. Ordinary doubles have
one payload word. A `JREF` index `(word . shift)`, shift 0 or 32, transfers an exact
unsigned half-word. Keep the library's packed data and emitted C/FASL expressions
lossless. Generated C includes its declarations and needs no external header.
`fasr` roots all relocated module objects through a vector in `f[1]`; generated
initializers retain that slot and attach it to native function environments.
Cover literals referenced only by compiled functions across GC.
Library/compiler regressions require `cc` and run in a temporary directory.
Preserve the original library and its function/variable names. Its remaining
incomplete Common Lisp features are outside this port's scope.

The application uses Qt's Fusion widget style and requests Segoe UI at 9 points
for the application and Courier New at 10 points for editors. Qt handles font
substitution when needed; user-configurable fonts remain planned.

Keep parser/evaluator logic separate from editor UI. One application-wide registry
owns one persistent environment per evaluator; editors do not own environments.
Evaluator overrides and the global default must use that same registry.

Markers are ordinary text. Preserve the agreed `>>`, `==`, `##` replacement
boundaries and stop end-marker searches at the next input block. Evaluation is
triggered only by unmodified Return at the end of an evaluation line. Output
replacement must be one undo operation. Document undo does not undo session state.
Locate evaluation blocks by traversing `QTextBlock`s from the evaluation marker;
do not flatten or split the entire document. Preserve plain-text normalization
of non-breaking spaces and soft line breaks, including marker boundaries within
a text block. Keep evaluator code in the QtCore-only `toylisp_core`.

Bracket matching uses `QSyntaxHighlighter` and block-relative `QTextBlockUserData`.
Keep cursor-triggered searches on cached metadata; do not materialize the document
text or rehighlight it on cursor movement. Preserve textual matching of `()[]{}`,
including in strings/comments, mixed-nesting validation, and right-before-left
cursor priority. Coalesce editor selection updates until block metadata is current.
Block data is owned by the document and rebuilt after edits, including undo/redo;
use one shared block-data type when adding language-aware highlighting later.

Arithmetic blocks commit assignments only after the whole block succeeds. Test
valid and malformed input, result formatting, error lines and environment state.
Use Qt's portable APIs and atomic saving; do not silently fall back to truncating
existing files when atomic saving fails.

## Style and validation

Use C++17 and the repository's `.clang-format`, exported from CLion's Qt preset
(four-space indentation, 100-column target). Its LLVM base supplies defaults;
preserve the explicit options implementing the selected Qt-based style. Keep IDE
formatting consistent with this file.
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

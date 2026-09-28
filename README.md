# ToyLisp

ToyLisp is a Qt5 Widgets desktop editor with evaluatable text blocks. The first
version targets Linux using C++17 and CMake. Windows portability is intended,
but a Windows build has not yet been verified.

## Current capabilities

- File and Edit menus; bundled New, Open and Save toolbar icons.
- Movable, closable editor tabs, a narrow right-hand tools placeholder, and a
  full-width bottom Output tab. Both boundaries can be resized with splitters.
- UTF-8 text files, Save As, modified indicators and save/discard/cancel prompts.
  Saves use `QSaveFile` for atomic replacement. Invalid UTF-8 is rejected.
  CRLF files retain CRLF when saved; new files use LF. Mixed endings are normalized.
- Qt's Fusion widget style, with Segoe UI requested at 9 points for the application
  and Courier New at 10 points for editors. Qt substitutes an available font if needed.
- Text editors with undo/redo, clipboard operations, select all and
  case-sensitive find/replace (including Replace All).
- Matching `()`, `[]` and `{}` highlighting at or immediately before the cursor.
  Unmatched brackets are marked. Matching is textual, including inside strings
  and comments; language-aware highlighting is future work. A `QSyntaxHighlighter`
  caches bracket positions in each text block. Cursor movement searches this
  metadata without copying or scanning the document text. Highlight updates are
  coalesced until the next event-loop turn, after edited blocks have been updated.
- Arithmetic evaluation with a shared session across all editor tabs.

The right-hand Tools tab is deliberately a placeholder. Identity, Lisp and
Macaulay2 evaluators, configurable fonts, and additional helper views are planned,
not implemented. Lisp is intended to become the primary default evaluator later.

## Evaluation blocks

Enter the following, then press Return at the end of `==`:

```text
>>
x = 3
x * (x + 1)
==
```

The editor inserts the result directly into the document:

```text
>>
x = 3
x * (x + 1)
==
12
##
```

`>>` and `##` must occupy their own lines in column one. An evaluation line
starts with `==` in column one. Bare `==` uses the global default selected in
the toolbar. `==arith` explicitly selects arithmetic; unknown evaluator names
produce an error, so `==lisp` is reserved but is not executable yet.

Return without modifiers at the end of an evaluation line evaluates all text
between the nearest preceding `>>` and that line, excluding the markers.
Opening files, pasting text and typing Return elsewhere do not evaluate anything.
A missing start marker reports a diagnostic without modifying the document.

Block lookup traverses the document's text blocks starting at the evaluation
marker, without copying or splitting the entire document. Only the surrounding
input and output region is visited; missing markers can still require a search
to the beginning or end of the document. Non-breaking spaces and soft line breaks
retain their plain-text interpretation during evaluation.

An existing result through the next standalone `##` is replaced. The search
stops at the next standalone `>>`. If no closing marker exists before that
boundary, the new result and `##` are inserted without deleting following text.
Markers remain freely editable. Generated output and errors are saved as ordinary
text. A complete result insertion/replacement is one undo operation. Undo changes
the document only; it does not roll back evaluator variables.

Errors appear in the result block and in the Output tab. The Output tab keeps
up to 2,000 lines of diagnostic text. Successful results appear in the document.

## Arithmetic session

Arithmetic supports decimal numbers (decimal point, optional scientific notation),
`+`, `-`, `*`, `/`, unary signs, round parentheses and standard operator precedence.
Calculations use `double`; output uses up to 15 significant digits. Variable names
match `[A-Za-z_][A-Za-z_0-9]*`. Use `name = expression` for assignment.

There is one statement per line; blank lines are ignored. The last statement's
value is returned; an empty block returns empty output. Comments, functions,
strings, exponentiation and semicolon-separated statements are not supported.
Division by zero, unknown variables, malformed syntax, non-finite results and
excessive nesting are errors, reported relative to the input block's line numbers.
A failed block commits none of its assignments.

Each evaluator owns one environment for the entire application, not one per
editor. Assigning `x` in one tab makes it available in every other tab using
arithmetic. Closing a tab does not reset the session. Sessions exist only for the
application lifetime and are not restored by opening saved files. Future evaluators
will have separate environments, with overrides using the same shared session.

## Build and run

Requirements: CMake 3.16+, a C++17 compiler, Qt 5.12+ with Widgets, and Qt Test
when tests are enabled. Verified development toolchain: CMake 3.28.3, GCC 13.3,
Qt 5.15.2 on Linux. Qt's SVG image plugin is needed for the bundled toolbar icons
and is included in the local Qt installation.

On the current development machine:

```bash
cmake -S . -B build \
  -DCMAKE_PREFIX_PATH=/home/juergen/Qt/5.15.2/gcc_64 \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON
cmake --build build
./build/toylisp
```

On another machine, point `CMAKE_PREFIX_PATH` to its Qt5 installation, or omit
it if Qt is already discoverable by CMake. No qmake invocation is required.
For a multi-configuration generator, use `--config Debug` when building and
`-C Debug` when running CTest. Windows requires a matching compiler/Qt kit and
Qt runtime deployment (for example with that kit's `windeployqt`).

## Tests

```bash
ctest --test-dir build --output-on-failure
```

CTest sets `QT_QPA_PLATFORM=offscreen`, so tests need no display server. Tests
cover arithmetic, malformed input, transactional variables, shared sessions,
block boundaries, replacement and undo/redo, bracket matching, file round trips,
write failures, save/discard/cancel prompts and window construction. The window test writes
`build/toylisp-screenshot.png` when run through CTest.

The separate evaluation block tests cover exact marker boundaries, missing markers,
empty lines, Unicode positions and normalization, large surrounding documents,
and block edits with undo/redo. Editor integration tests check that replacement
preserves surrounding text and remains a single undo operation.

The separate bracket tests cover cursor boundaries, mixed nesting, Unicode offsets,
edits to distant partners, splitting/merging blocks, undo/redo, and incremental
metadata updates. Matching preserves the preference for the bracket at the cursor
over the one immediately before it. Visual selections do not modify the document
or its undo history.

An optional benchmark measures metadata searches across 100,000 text lines, with
distant and missing partners and with the cursor away from brackets:

```bash
QT_QPA_PLATFORM=offscreen ./build/toylisp_bracket_benchmarks
```

The benchmark is built with `BUILD_TESTING=ON` but is not part of CTest and has no
timing pass/fail threshold. Partner searches still visit intervening blocks and
bracket entries; missing or distant partners can require a document-wide search.

For manual acceptance, launch the application, resize both splitters, create
and close tabs, try save/discard/cancel, use the File dialogs, and verify
find/replace and keyboard shortcuts. Evaluate a variable assignment in one tab
and use it in another, edit the input, reevaluate, undo, save and reopen.
Native file dialogs and Windows behavior require platform-specific manual checks.
In the Codex filesystem sandbox, Qt 5.15.2 atomic saves can fail despite writable
paths; the file tests were verified outside that sandbox. Do not disable atomic
saving to work around this environment restriction.

## Layout and development

- `src/app/`: startup; the main window owns the application's evaluator registry.
- `src/ui/`: main window, file workflow and plain-text evaluation editor.
- `src/editor/`: widget-independent block recognition, bracket metadata highlighter
  and matching. This QtGui-based code is compiled into `toylisp_ui`;
  `toylisp_core` contains only the QtCore-based evaluators.
- `src/evaluators/`: evaluator interface, registry and arithmetic parser/session.
- `tests/`: Qt Test regression and integration tests, plus optional bracket benchmarks.
- `resources/`: bundled SVG toolbar icons and Qt resource manifest.

Use the repository's `.clang-format` for C++ changes. It was exported from CLion's
Qt preset and uses four-space indentation and a 100-column target.
`BasedOnStyle: LLVM` supplies defaults; the explicit options implement the selected
Qt-based style. Configure CLion to use this file when formatting C++ code.

Keep evaluator and block logic independent of widgets. See `AGENTS.md` for
contributor instructions. README.md and AGENTS.md are updated as capabilities,
commands and layout change.

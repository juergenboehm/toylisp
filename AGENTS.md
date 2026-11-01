# Repository Guidelines

## Project scope and structure

ToyLisp is a C++17 / Qt5 Widgets desktop application built with CMake. Linux is
its initial target; Windows compatibility is intended but unverified. Arithmetic
and asynchronous Sanskrit lookup are implemented. Identity, Lisp and Macaulay2
remain planned capabilities.
The standalone Lisp500 CLI has a Linux LP64 port; Lisp integration into the Qt
editor remains planned.

- `src/app/`: application entry point.
- `src/ui/`: main window, text editors, menus and file dialogs.
- `src/editor/`: widget-independent QtGui block operations and a bracket highlighter
  with metadata-based matching; both are built into `toylisp_ui`.
- `src/evaluators/`: shared evaluator interface, result, job and registry.
- `src/evaluators/arith/`: arithmetic parser, evaluator and persistent environment.
- `src/evaluators/lisp500/`: unchanged `lisp500a.c` / `init500.lisp` originals and
  standalone `lisp500a64.c` / `init500_64.lisp` Linux LP64 ports; Qt integration
  remains planned.
- `src/evaluators/sanskrit/`: Devanagari/SLP1 conversion, Cologne dictionary/inflection
  adapters and asynchronous Sanskrit evaluator.
- `src/lookups/`: reusable HTTP client (`httpclient.h` / `httpclient.cpp`).
- `tests/`: Qt Test coverage plus Lisp500 C regressions and a native FASL fixture.
- `resources/`: bundled toolbar SVG icons.
- `README.md`: supported behavior, limitations, setup and commands.

Update README.md and this file with each relevant change in project progress,
layout, commands or capabilities. Clearly distinguish implemented and planned work.
Preserve user changes already present in the working tree.

## Build, run and test

Require CMake 3.16+, C++17, Qt 5.12+ Widgets/Network and Qt Test for testing.
The current machine has CMake 3.28.3 and GCC 13.3. Both build directories use
Qt 5.15.13 built from the official source archives and installed independently
under `/home/juergen/Qt/Qt5.15.13`, linked to system OpenSSL 3. Sources, build
recipes, source checksums, compatibility patches and non-Qt build dependencies
live separately under `/home/juergen/Qt/{Sources,Builds,BuildDependencies}/Qt5.15.13`.
The kit includes the desktop modules plus SVG, QML/Quick, Designer/Linguist,
Wayland and Multimedia; WebEngine is excluded. It has shared release libraries
with separate debug information. The older Qt 5.15.2 kit remains available,
but expects OpenSSL 1.1 and cannot use this machine's OpenSSL 3 for HTTPS.

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/home/juergen/Qt/Qt5.15.13 -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
./build/toylisp
./build/lisp500
./build/lisp500 src/evaluators/lisp500/init500_64.lisp
git diff --check
QT_QPA_PLATFORM=offscreen TOYLISP_LIVE_LOOKUP=1 ./build/toylisp_lookup_tests liveCologne
```

CTest uses the offscreen Qt platform. The window test emits a screenshot in the
build directory. Check native dialogs and desktop integration manually where a
GUI is available. Do not claim Windows verification without a Windows build.
CTest is offline; the explicit live probe is optional. Local TCP sockets and
atomic settings/saves can require running the suite outside the Codex sandbox.
The Sanskrit screenshot uses recorded dictionary excerpts. On fresh systems use
a compatible Qt development kit; the private installation is workstation setup,
not a bundled project dependency.

CLion's `cmake-build-debug` has a separate Qt cache and now also uses Qt 5.15.13.
Keep `-DCMAKE_PREFIX_PATH=/home/juergen/Qt/Qt5.15.13` in its
Debug profile's CMake options. After changing kits use Reset Cache and Reload
Project, or clear only Qt discovery entries with `cmake -S . -B cmake-build-debug
-U 'Qt5*' -DCMAKE_PREFIX_PATH=/home/juergen/Qt/Qt5.15.13`.
Rebuild the complete executable; changing runtime library paths alone does not
replace the headers/libraries used to compile and link it. The private Qt kit
stays available when ToyLisp build directories are cleaned. For command-line
development source `/home/juergen/Qt/Qt5.15.13/activate.sh`; the workstation's
`.bashrc` now does this for new interactive terminals. The local `.idea` Debug
profile and Qt tool/QML paths also select this kit. Do not set a global Qt
`LD_LIBRARY_PATH` or plugin-path override.

On 64-bit Linux the CMake build enables C11 for the standalone `lisp500` target
and its `lisp500_tests` CTest suite. Direct compilation is also supported:

```bash
gcc src/evaluators/lisp500/lisp500a64.c -o build/lisp500 -lm -ldl
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
substitution when needed. Editor text zoom uses Ctrl+mouse wheel, with 5% per notch
and fractional sizes for fine wheel/touchpad input, limited to 1–96 points.
View → Normal Size (Ctrl+0) restores 10 points in the current editor. Zoom belongs
to each open tab; new/reopened tabs start at normal size and zoom is not persisted.
Keep tab stops four spaces wide at every size. Zoom must preserve document text,
selection, modified state and undo/redo history. Configurable font families and
default sizes remain planned.

Keep parser/evaluator logic separate from editor UI. One application-wide registry
owns one persistent environment per evaluator; editors do not own environments.
Evaluator overrides and the global default must use that same registry.
Keep shared infrastructure directly under `src/evaluators/` and each concrete
evaluator with its helpers under `src/evaluators/<name>/`. The registry still
registers arithmetic automatically; moving files does not change session ownership.

Markers are ordinary text. Preserve the agreed `>>`, `==`, `##` replacement
boundaries and stop end-marker searches at the next input block. Evaluation is
triggered only by unmodified Return at the end of an evaluation line. Output
replacement must be one undo operation. Document undo does not undo session state.
Locate evaluation blocks by traversing `QTextBlock`s from the evaluation marker;
do not flatten or split the entire document. Preserve plain-text normalization
of non-breaking spaces and soft line breaks, including marker boundaries within
a text block. Keep shared evaluator infrastructure and arithmetic in the
QtCore-only `toylisp_core`.

`EvaluationJob` adds completion/cancellation while arithmetic remains immediate
and keeps its shared environment. `toylisp_lookups` builds the reusable HTTP client
from `src/lookups/` and Sanskrit code from `src/evaluators/sanskrit/`
(QtCore + QtNetwork). Sanskrit is registered once at application setup. Separate
HTTP transport, provider parsing and editor behavior. Sanskrit uses `==sanskrit` and
one global, saved German PWG / English MW setting. Running jobs capture that
language. Convert a single Devanagari word locally to SLP1; search both its exact
form and distinct Cologne MW Inflected Forms headwords/roots. Prefix every
meaning with its matched Devanagari headword. Sentence sandhi, compound splitting
and Vedic accent input remain unsupported.

Read TEI rather than trusting MW's incomplete flat `sense` array; PWG's schema
differs. Extraction is conservative and abbreviated: omit unrelated run-on entries,
examples and later derived/prefixed material. The separate morphology endpoint
returns HTML summary links; never execute provider HTML/JS. Keep jobs scoped to
their editor and marker. Cancel changed/superseded blocks and closed tabs;
allow unrelated edits. Revalidate input, marker and output before insertion,
preserve one undo step and do not move the cursor on delayed completion.
Show loading status outside document text. Retain usable meanings on partial
failure with a diagnostic. Distinguish no matches from transport and parsing
errors. Keep request timeout, size and candidate limits; never silently truncate
or disable TLS certificate checks. Test with recorded responses and a local HTTP
server, reserving live queries for an explicit opt-in probe.
PWG can encode an initial sub-sense as an empty label followed by sibling
definition text; retain that definition and its following lettered sub-senses.
Keep valid meanings from other homonyms when one entry is unsupported or
malformed and report the partial result explicitly. Regression fixtures cover
`दूरतस्`, `वसुधा` and `एव`. Preserve final virama and vowel length; do not silently
correct `दूरतस` or `भु`. Sandhi such as `वसुधैव` still requires separate component
lookups (`वसुधा`, `एव`).

After an empty MW main-headword lookup, search `re_headwords_slp1` and select only
TEI `re` elements whose own `form/orth[@ana='key1']` exactly matches the requested
headword. Do not mix the parent's meanings or neighboring subentries into it.
Resolve a leading, unnumbered PWG `dass.` through the preceding `tail/L` record
number, never by stripping letters from the headword. Search `field=xml` with
the number and `query_type=match`, then verify the exact returned record number;
an incidental citation match is insufficient. Keep the original matched headword
as the line prefix and name the inherited definition's source headword. Limit
reference chains to four hops and 16 reference requests per job, retain partial
meanings on failures and cancel these requests with the job. If morphology finds
a base form without dictionary meanings, show the recognized form explicitly.
Fixtures and live probes cover `कुटुम्बकम्` → `कुटुम्बक` in both languages.

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

# ToyLisp

ToyLisp is a Qt5 Widgets desktop editor with evaluatable text blocks. The first
version targets Linux using C++17 and CMake. Windows portability is intended,
but a Windows build has not yet been verified.

## Current capabilities

- File, Edit and View menus; bundled New, Open and Save toolbar icons.
- Movable, closable editor tabs, a narrow right-hand tools placeholder, and a
  full-width bottom Output tab. Both boundaries can be resized with splitters.
- UTF-8 text files, Save As, modified indicators and save/discard/cancel prompts.
  Saves use `QSaveFile` for atomic replacement. Invalid UTF-8 is rejected.
  CRLF files retain CRLF when saved; new files use LF. Mixed endings are normalized.
- Qt's Fusion widget style, with Segoe UI requested at 9 points for the application
  and DejaVu Sans Mono at 12 points for editors. Qt substitutes an available font
  if needed.
- Per-tab text zoom with Ctrl+mouse wheel: 5% per notch, with fractional sizes for
  fine wheel and touchpad input, limited to 1–96 points. View → Normal Size (Ctrl+0)
  restores 12 points in the current editor. Tabs start at normal size; zoom is not
  saved with files or retained after closing a tab. Tab stops remain four spaces
  wide. Zoom preserves text, selections, modified state and undo/redo history.
- Text editors with undo/redo, clipboard operations, select all and
  case-sensitive find/replace (including Replace All).
- Matching `()`, `[]` and `{}` highlighting at or immediately before the cursor.
  Unmatched brackets are marked. Matching is textual, including inside strings
  and comments; language-aware highlighting is future work. A `QSyntaxHighlighter`
  caches bracket positions in each text block. Cursor movement searches this
  metadata without copying or scanning the document text. Highlight updates are
  coalesced until the next event-loop turn, after edited blocks have been updated.
- Arithmetic evaluation with a shared session across all editor tabs.
- Asynchronous Sanskrit lookup through Cologne C-SALT, with Devanagari input,
  German/English results and lookup of inflected forms through a separate
  Cologne service. Each meaning starts with its matched headword/base form.
- Sanskrit verse analysis through Dharmamitra with Padapāṭha, grammatical
  tags and short Cologne Monier-Williams glosses.
- A development evaluator for testing Devanagari/SLP1 transliteration locally.

The right-hand Tools tab is deliberately a placeholder. Identity, Lisp and
Macaulay2 evaluators, configurable font families/default sizes, and additional
helper views are planned, not implemented. Lisp is intended to become the primary
default evaluator later.
The separate Lisp500 command-line interpreter described below is implemented;
it is not yet connected to the editor's evaluator registry.

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
`==sanskrit` explicitly selects the Sanskrit lookup. `==test` selects the
development evaluator.

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

## Sanskrit lookup

Enter one Devanagari word and press Return at the end of `==sanskrit`:

```text
>>
गच्छति
==sanskrit
```

Choose **Sanskrit → Result Language → German (Petersburg)** or **English
(Monier-Williams)**. This global setting applies to every tab and is saved in
Qt's application settings; German is the initial choice. It selects C-SALT's
PWG or MW dictionary, whose contents and sense divisions differ. The toolbar's
**Sanskrit Lookup** default also allows bare `==`; the startup default remains
Arithmetic.

Devanagari is converted to SLP1 internally. The evaluator searches the entered
word and asks Cologne's MW Inflected Forms service for possible base forms and
verbal roots. Each distinct candidate is searched in the selected dictionary.
Thus `रामेण` can lead to `राम`, `गच्छति` to `गम्`, and `भवति` to `भू` as well
as nominal analyses. Direct matches do not suppress morphological alternatives.
For `कुटुम्बकम्`, Cologne returns `कुटुम्बक`, not `कुटुम्ब`. The MW adapter
also searches `re_headwords_slp1` when a main-headword search is empty, then reads
only the exact matching TEI subentry. PWG's leading, unnumbered `dass.` definitions
inherit the preceding source record: its number is searched in `xml`, and the
response must contain that exact `tail/L` number. Inherited meanings explicitly
name the source headword, e.g. `कुटुम्बक — wie कुटुम्ब: Hausstand, …`.
Each extracted meaning occupies one line, prefixed by the matched headword,
for example `गम् — gehen, sich bewegen; …`. Input stays unchanged; results use
the dictionary's wording without automatic translation or a language model.

Input accepts one word of at most 128 Unicode code units, with standard Sanskrit
letters, vowel signs, virama, anusvara, visarga, chandrabindu and avagraha.
Whitespace around the word is ignored. Vedic accent input, nukta letters, OM,
multiword input, compound splitting and sentence-level sandhi are unsupported.
For example, `वसुधैव` combines `वसुधा` and `एव`; look up those components
separately. Preserve vowel length and a final virama: `दूरतस्` has the PWG
meaning “aus der Ferne her”, while `दूरतस` includes a final vowel and is a
different lookup. Likewise, `भु` and the root `भू` are distinct inputs.
Use a system Devanagari keyboard/IME or paste text; font fallback needs a suitable
font such as Noto Sans Devanagari. Morphology coverage depends on Cologne's tables;
an unmatched form does not establish that a Sanskrit word is invalid.

The editor stays usable while requests run; the status bar shows their count.
Existing output remains until the finished result replaces it in one undo step.
Changes to the input, marker or existing output cancel the affected request;
unrelated edits may proceed. Re-evaluation supersedes the old request for that
block, and closing a tab cancels its requests. **Sanskrit → Cancel Pending
Evaluations in This Tab** (Escape) also cancels them. Delayed completion does not
move the cursor to the result. Running requests retain their original language.

No matches, network errors and malformed responses are distinguished. On partial
failure usable meanings remain, accompanied by an explicit incomplete-lookup
diagnostic. If morphology identifies a base form but the dictionary has no usable
meaning, the result names the recognized base form instead of reporting that no
base form was found. Requests have a 20-second timeout and 4 MiB response limit.
More than 16 derived candidates or a full 20-entry response reports a limit rather
than silently truncating results. PWG predecessor references are limited to four hops
per chain and 16 reference requests per evaluation; failures retain other meanings.
Opening and saving files never initiate requests.

Providers: [C-SALT](https://cceh.github.io/c-salt_sanskrit_data/) serves JSON at
`https://api.c-salt.uni-koeln.de/dicts/{pwg,mw}/restful/entries`;
[MW Inflected Forms](https://www.sanskrit-lexicon.uni-koeln.de/scans/csl-inflect/web/index.php)
uses a separate `getWord.php` endpoint returning HTML summary links.
The adapter reads those URLs without executing HTML or JavaScript.
The C-SALT REST/GraphQL search itself is not a general lemmatizer; morphology
comes from the separate, table-based
[csl-inflect project](https://github.com/sanskrit-lexicon/csl-inflect).
Finding a stem there does not guarantee a standalone C-SALT dictionary entry.

Dictionary adapters read the TEI-XML body instead of trusting MW's incomplete
flat `sense` array. PWG extracts leading italic glosses in the first numbered
meaning group; MW extracts leading sense text and skips paradigms where the TEI
marks an infinitive gloss. Examples, citations, embedded run-on entries and later
PWG derivations are omitted conservatively; a targeted MW subentry lookup reads
only that subentry's senses. This is an abbreviated lookup;
unusual markup may need adapter updates. No cross-request cache, authentication
or additional API providers are implemented yet.
PWG sub-senses whose initial label is encoded separately are supported, including
`वसुधा` → “die Erde; Land, Reich”. If a homonymous entry is unsupported or
malformed, usable meanings from the other entries remain visible with an
incomplete-lookup diagnostic; the unsupported entry does not discard them.

## Sanskrit verse analysis

Install `requests` and `indic-transliteration` in a Python 3 environment and
enter its executable under **Sanskrit → Python Interpreter…**. Enter an absolute
path or a path starting with `~/`. ToyLisp saves the path and applies it
immediately. A blank setting disables verse analysis.

Use `==sanskrit-verse` or select **Sanskrit Verse (Dharmamitra + MW)** in the
default evaluator list.

```text
>>
कर्मण्येवाधिकारस्ते मा फलेषु कदाचन
==sanskrit-verse
```

The bundled Python dispatcher transliterates the text to IAST and sends it to
Dharmamitra's online tagger. ToyLisp formats the returned word forms and tags
as Padapāṭha and grammar, then looks up distinct recognized lemmata in Cologne's
MW dictionary. The verse evaluator uses English MW independently of the
word lookup's language setting. It shows up to two extracted dictionary glosses
per lemma with an ellipsis for further glosses. The grammar display formats
the supplied features and shows empty tags as `Unspecified`. MW queries use
the supplied lemmata transliterated to SLP1.

One asynchronous Python process starts at launch when configured and is reused
across tabs. It processes one analysis at a time. Cancelling an active analysis
discards its reply. A worker failure or timeout fails pending requests, and a
later evaluation starts the worker again. A request supports up to 4,000 input
characters and 64 analyzed words, with a 40-second worker deadline once sent.
Dictionary failures preserve the analysis and available glosses.

## Sanskrit C++ library

`src/sanskrit` contains a C++17 library for use in ToyLisp and other programs.
The `sanskrit` target exposes `sanskrit::Sanskrit` and the abstract
`sanskrit::SanskritService` through `sanskrit/sanskrit.h`. Public inputs and
results use UTF-8 and standard C++ types. The target uses utf8proc internally.

- `transliterate` works locally on words and whole texts in Devanagari, IAST
  and SLP1. It preserves whitespace and punctuation outside recognized Sanskrit
  letters and marks. It supports the classical repertoire, including vowel
  length, virama, anusvāra, visarga, candrabindu and avagraha.
  `transliterateWord` checks the stricter single-word repertoire used for
  dictionary queries.
- `lookupDictionary` searches a word in PWG or MW, with optional base forms.
  `lookupHeadwords` searches a batch of exact SLP1 headwords. Results separate
  meanings and their sources, recognized base forms and diagnostics.
- `analyze` returns unsandhied forms, lemmata and grammatical features.
  `analyzeWithGlosses` looks up distinct recognized lemmata in the selected
  dictionary and preserves analysis and available glosses on partial failure.

The optional `sanskrit_qt` target provides `SanskritServiceQt`, using Qt Core,
Qt Network and the bundled Python dispatcher for the online operations. Provide
that backend or an implementation of `SanskritService` when constructing
`Sanskrit`. Keep the backend alive until the requests are done. Retain each
returned `Request` until its callback runs. Cancelling or destroying the
handle suppresses completion and stops follow-up work. Start and cancel
operations on the backend's execution context. The Qt implementation uses the
event loop of the thread that created it, including for local validation errors.

To embed the library from a CMake project, use `add_subdirectory` on
`src/sanskrit` and link to `sanskrit`. Set `SANSKRIT_BUILD_QT_SERVICE=ON`
and link to `sanskrit_qt` if using the Qt backend. To build and test the
Qt-free target separately:

```bash
cmake -S src/sanskrit -B build/sanskrit-core -DBUILD_TESTING=ON
cmake --build build/sanskrit-core
ctest --test-dir build/sanskrit-core --output-on-failure
```

Connected translation of sentences and verses is planned.

## Development evaluator

The **Test** evaluator provides a place to try features during development. It
currently accepts one Devanagari word using the same input rules as the Sanskrit
lookup. Enter a word and press Return at the end of `==test`:

```text
>>
गच्छति
==test
```

The first result line is the SLP1 spelling (`gacCati`). The second line is the
Devanagari spelling obtained by converting that SLP1 back (`गच्छति`). Both
conversions run locally. Invalid input produces an error in the result block.
Selecting **Test** as the default evaluator in the toolbar also allows bare `==`.

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

The application requires CMake 3.16+, a C++17 compiler, Qt 5.12+ with
Widgets/Network, and Qt Test when tests are enabled. Install utf8proc
development files, for example `libutf8proc-dev` and `pkg-config` on Ubuntu.
CMake finds utf8proc through its CMake package or the `libutf8proc`
pkg-config module. Add a custom installation prefix to `CMAKE_PREFIX_PATH`
when needed. The verified development toolchain includes CMake 3.28.3, GCC 13.3,
Qt 5.15.2 and Qt 5.15.13 on Linux. HTTPS also requires a TLS runtime compatible
with the Qt build and a working certificate store. Qt's SVG image plugin is
needed for toolbar icons.

On the current development machine:

```bash
cmake -S . -B build \
  -DCMAKE_PREFIX_PATH=/home/juergen/Qt/Qt5.15.13 \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON
cmake --build build
./build/toylisp
```

The current machine's kit is built from the official Qt 5.15.13 sources and
installed under `/home/juergen/Qt/Qt5.15.13`. Its Qt libraries and plugins are
independent of Ubuntu's Qt installation. It includes the desktop modules, SVG,
QML/Quick, Designer/Linguist, Wayland and Multimedia, with shared release
libraries and separate debug information. Non-Qt runtime dependencies such as
OpenSSL 3 remain system libraries. No system packages were installed.

Sources, build directories and locally unpacked non-Qt development dependencies
live under `/home/juergen/Qt/Sources/Qt5.15.13`, `Builds/Qt5.15.13` and
`BuildDependencies/Qt5.15.13`, respectively. Build recipes, source checksums and
Linux compatibility patches are retained there; the kit's `README.md` documents
its configuration and rebuild commands. For command-line development:

```bash
source /home/juergen/Qt/Qt5.15.13/activate.sh
```

The workstation's `.bashrc` now sources this script in new interactive terminals;
its previous version was backed up before replacing the old Qt PATH entry.
WebEngine is excluded from this desktop kit.

The older `/home/juergen/Qt/5.15.2/gcc_64` kit is retained, but expects OpenSSL 1.1
and cannot perform HTTPS with this machine's OpenSSL 3. Never disable certificate
checks to work around a TLS mismatch.
On another machine, point `CMAKE_PREFIX_PATH` to its Qt5 installation, or omit
it if Qt is already discoverable by CMake. No qmake invocation is required.
For a multi-configuration generator, use `--config Debug` when building and
`-C Debug` when running CTest. Windows requires a matching compiler/Qt kit and
Qt runtime deployment (for example with that kit's `windeployqt`).

## CLion and Qt selection

Each CMake build directory has its own cached Qt selection. The local
`cmake-build-debug` build has also been configured and rebuilt against Qt 5.15.13.
The local Debug profile and Qt tool/QML paths have been updated in `.idea`.
For other profiles or projects, open **Settings → Build,
Execution, Deployment → CMake**, select **Debug**, and add this to **CMake options**:

```text
-DCMAKE_PREFIX_PATH=/home/juergen/Qt/Qt5.15.13
```

Use **Tools → CMake → Reset Cache and Reload Project** after changing Qt kits,
then build and run the `toylisp` target with the Debug profile. Updating the
prefix alone can leave old `Qt5*_DIR` cache entries pointing at Qt 5.15.2. From
the command line, retain other settings while clearing just those entries with:

```bash
cmake -S . -B cmake-build-debug -U 'Qt5*' \
  -DCMAKE_PREFIX_PATH=/home/juergen/Qt/Qt5.15.13
cmake --build cmake-build-debug
```

Keep compilation, linking and runtime Qt libraries from the same kit. The private
Qt kit lives outside the project, so cleaning either ToyLisp build directory
leaves it available for this and other projects. See
[CLion CMake profiles](https://www.jetbrains.com/help/clion/cmake-profile.html).

## Tests

```bash
ctest --test-dir build --output-on-failure
```

CTest sets `QT_QPA_PLATFORM=offscreen`, so tests need no display server. Tests
cover arithmetic, malformed input, transactional variables, shared sessions,
block boundaries, replacement and undo/redo, bracket matching, per-tab text zoom,
fine wheel/touchpad deltas, zoom limits, normal scrolling and the Ctrl+0 reset,
file round trips, write failures, save/discard/cancel prompts and window
construction. The window test writes
`build/toylisp-screenshot.png` when run through CTest.

`sanskrit_tests` links only the Qt-free Sanskrit library and covers text
transliteration, Unicode normalization, typed results, composition and cancellation.
`toylisp_lookup_tests` covers word transliteration, recorded MW/PWG and morphology
responses, malformed data, local HTTP requests, timeouts/limits, partial failure,
PWG inline sub-sense labels, mixed supported/unsupported homonyms, exact final
virama spelling and the reported `दूरतस्`, `वसुधा` and `एव` examples,
`कुटुम्बकम्` morphology, exact MW subentry selection, verified PWG predecessor
references, bounded reference chains and cancellation during reference lookup,
cancellation, language snapshots, saved preferences, delayed replacement,
unrelated edits, superseded requests, editor lifetime and undo/redo. It writes
`build/toylisp-sanskrit-screenshot.png` from an offline example. Normal CTest runs
never contact Cologne. An explicit live probe checks both languages, including
`कुटुम्बकम्` through both dictionary adapters:

```bash
QT_QPA_PLATFORM=offscreen TOYLISP_LIVE_LOOKUP=1 ./build/toylisp_lookup_tests liveCologne
```

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
find/replace and keyboard shortcuts. Try Ctrl+mouse wheel in different editor tabs,
check the four-space tab stops, and reset via View → Normal Size and Ctrl+0.
Evaluate a variable assignment in one tab and use it in another, edit the input,
reevaluate, undo, save and reopen.
Native file dialogs and Windows behavior require platform-specific manual checks.
The Codex sandbox can block atomic saves/settings and local TCP test servers
despite writable paths; the complete suite was verified outside the sandbox.
Do not disable atomic saving to work around this restriction.

## Standalone Lisp500 interpreter

`src/evaluators/lisp500/lisp500a.c` is the unchanged 32-bit C original. Its Linux
LP64 port, `src/evaluators/lisp500/lisp500a64.c`, uses `typedef long int lval` and
keeps the original function and variable names without adding interpreter functions
or variables.
`src/evaluators/lisp500/init500_64.lisp` ports the accompanying `init500.lisp` library;
the original Lisp file is also preserved, with the same definitions in the port.
The 64-bit version is separate from the Qt editor and does not enable `==lisp`.

Build directly, without `-m32`:

```bash
gcc src/evaluators/lisp500/lisp500a64.c -o build/lisp500 -lm -ldl
./build/lisp500
./build/lisp500 src/evaluators/lisp500/init500_64.lisp
```

Create `build/` first if it does not already exist. The normal CMake build also
builds `build/lisp500` on 64-bit Linux, using a C11 compiler. The interpreter reads
forms from standard input, evaluates file arguments before entering its REPL,
and exits at EOF. For example, `(+ 1 2)` returns `3`. The original startup symbol
listing is retained. This port requires eight-byte `long int` and pointers;
Windows LLP64 is unsupported by this source and is rejected at compile time.

Heap and stack capacities remain 2,097,152 and 65,536 Lisp words, respectively;
their byte sizes are now 16 MiB and 512 KiB. Allocation, clearing, copying,
strings, doubles and raw buffers use the 64-bit layout. Strings start after two
`lval` header words; their header is `(length + 8) << 5`. Raw bit buffers allocate
whole 64-bit words and store that word count in the header's high bits.
Bit-buffer headers also preserve the exact length: decode it as
`(header >> 8) * 64 + ((header >> 3) & 31) * 2 + (header & 1) - 63`.
`MAKEJ` with string type `20` uses the string header and reserves its terminator.
Variadic allocator payloads must have type `lval`: use `L` suffixes for integer literals
and explicit casts for native pointers. `GO`, `RETURN-FROM` and `THROW` transfer
their payload through the existing GC root `xvalues`; `longjmp` carries only `1`.
Nested `CATCH` unwinding invalidates the contained jump object correctly.

Fixnums occupy the signed range from `-2^58` through `2^58 - 1`; other numbers use
boxed doubles. Arithmetic still uses `double`, including its integer precision
limits. `IVAL`, `JREF` and `DPB` preserve exact raw integers for bit operations:
values outside the fixnum range retain their raw bits alongside the approximate
double in a two-payload-word numeric box. Ordinary arithmetic doubles have one
payload word. `LDB` and `(SETF JREF)` recognize this representation. `JREF` also
accepts a `(word-index . shift)` index, with shift `0` or `32`, to read or write
an unsigned 32-bit half without losing precision. The library uses those halves
for packed characters, bit vectors and generated C constants. The port retains the
legacy reader and evaluator behavior; it is not a new Common Lisp implementation.

The ported library loads completely, initializes its reader, streams, structures,
classes and REPL, and exits cleanly at EOF. String-stream reads and input-position
tracking also handle EOF correctly. Original incomplete features remain, including
string `REVERSE`, parts of the numeric reader and compiler operators.

Native FASL modules must be rebuilt for the new ABI and object layout. Relocations
use the top two bits of a 64-bit word and a 62-bit byte offset; table entries are
eight bytes. Existing 32-bit modules/data are incompatible. Add `-rdynamic` to
the direct GCC command when modules reference interpreter functions; the CMake
target already exports them. The library's Lisp-to-C compiler now emits eight-byte
offsets, exact 64-bit relocation expressions and correctly typed variadic arguments.
Generated modules contain their runtime declarations, so the absent original
`lisp500.h` is not needed. `fasr` supplies a vector of module objects in `f[1]`;
compiled functions retain it in their environment, keeping literal constants
alive across GC. `test-compilation` requires `cc` and writes `test.c` and
`test.so` in the current directory; for example:

```lisp
(test-compilation '(setq compiled-function #'(lambda (x) (+ x 2))))
(fasl "./test.so")
(funcall compiled-function 40)
```

On 64-bit Linux, CTest also runs `lisp500_tests`. These tests cover evaluation,
strings at word boundaries, complete array/buffer initialization, large numbers,
64-bit bit operations, forward/backward/nested jumps, zero and multiple return
values, cleanup and GC during unwinding, file streams, native FASL loading,
relocations, and 2.2 million allocations with live objects across collections.
They also load `init500_64.lisp`, check string and bit-vector lengths 0 through 130,
packed reads/writes across half-word and word boundaries, arrays, string streams,
structures, classes, hash-table growth and restarts. The real Lisp-to-C compiler,
`cc` and native loader round-trip constants and a callable function, including
package, symbol and class relocations, floating-point payloads and constants
referenced only by compiled functions across GC.
The suite has also been checked with GCC `-O2` and AddressSanitizer/UBSan. Leak
detection was disabled for that sanitizer run because the execution environment
uses tracing; memory access and undefined-behavior checks remained enabled.

## Layout and development

- `src/app/`: startup; the main window owns the application's evaluator registry.
- `src/ui/`: main window, file workflow and plain-text evaluation editor.
- `src/lookups/`: reusable asynchronous HTTP client (`httpclient.h` / `httpclient.cpp`).
- `src/editor/`: widget-independent block recognition, bracket metadata highlighter
  and matching. This QtGui-based code is compiled into `toylisp_ui`;
  `toylisp_core` contains the shared evaluator infrastructure and arithmetic (QtCore).
- `src/evaluators/`: shared evaluator interface, result, job and registry.
- `src/evaluators/arith/`: arithmetic parser, evaluator and persistent session.
- `src/evaluators/lisp500/`: standalone Lisp500 C/library originals and Linux LP64
  ports; Qt evaluator integration remains planned.
- `src/sanskrit/` contains the Qt-free Sanskrit API and transliteration, the
  Qt backend, the Cologne and Dharmamitra adapters, and the Python dispatcher.
- `src/evaluators/sanskrit/` contains ToyLisp's word and verse adapters.
  `src/evaluators/test/` contains the development evaluator. They build into
  `toylisp_sanskrit_evaluators`.
- `src/processes/` contains the reusable QtCore JSON-line process channel.
  The `sanskrit_qt` target builds it and the HTTP client under `src/lookups/`.
- `tests/` contains Qt Test coverage, standalone Sanskrit tests, optional bracket
  benchmarks and Lisp500 C tests with a native FASL module.
- `resources/`: bundled SVG toolbar icons and Qt resource manifest.

Use the repository's `.clang-format` for C++ changes. It was exported from CLion's
Qt preset and uses four-space indentation and a 100-column target.
`BasedOnStyle: LLVM` supplies defaults; the explicit options implement the selected
Qt-based style. Configure CLion to use this file when formatting C++ code.

Each concrete evaluator and its helpers belong under `src/evaluators/<name>/`;
shared infrastructure stays directly under `src/evaluators/`. The registry still
registers arithmetic automatically and owns the shared sessions.

Keep evaluator and block logic independent of widgets. See `AGENTS.md` for
contributor instructions. README.md and AGENTS.md are updated as capabilities,
commands and layout change.

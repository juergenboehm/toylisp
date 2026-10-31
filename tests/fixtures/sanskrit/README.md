# Cologne lookup fixtures

Retrieved from the public Cologne services on 2026-10-31:

- `mw-agni.json`: C-SALT MW `entries`, `field=headword_slp1`, `query=agni`,
  `query_type=term`. The TEI excerpt retains the main senses and one run-on entry;
  the incomplete original `sense` array is deliberately preserved.
- `mw-bhu.json`: same endpoint, `query=BU`. The excerpt retains the conjugation
  preamble and the first infinitive gloss through its first literary citation.
- `pwg-gam.json`: C-SALT PWG, `query=gam`. The TEI excerpt retains the leading
  material of the first numbered meanings, a restarted later enumeration and
  the other three homonymous entries returned for this headword.
- `pwg-duratas.json`, `pwg-vasudha.json`, `pwg-eva.json`: complete C-SALT PWG
  responses for `query=dUratas`, `vasuDA`, and `eva`. They cover the final virama
  spelling, an empty initial sub-sense label followed by its definition, and
  useful meanings alongside unsupported homonymous entries.
- `ramena.html`, `gacchati.html`, `bhavati.html`: unmodified responses from
  `https://www.sanskrit-lexicon.uni-koeln.de/scans/csl-inflect/web/getWord.php`,
  with `word=rAmeRa`, `gacCati`, or `Bavati`, and both `transLit` and `filter`
  set to `SLP2SLP`.
- `kutumbakam.html`: the same inflected-forms endpoint with `word=kuwumbakam`.
  Its summary identifies `kuwumbaka` as masculine accusative singular and
  neuter nominative/accusative singular.
- `pwg-kutumbaka.json`: PWG `headword_slp1=kuwumbaka`; the first homonym
  inherits its definition via `dass.` and carries source record number 17549.
- `pwg-before-kutumbaka.json`: PWG `field=xml`, `query=17548`, `query_type=match`.
  The returned `kuwumba` entry is verified through its exact `tail/L` number.
- `mw-kutumba.json`: MW `field=re_headwords_slp1`, `query=kuwumbaka`,
  `query_type=term`. The parent `kuwumba` entry contains the matching `re`
  subentry and unrelated siblings, which must not leak into its meanings.

C-SALT endpoints: `https://api.c-salt.uni-koeln.de/dicts/{mw,pwg}/restful/entries`.
Dictionary sources: Monier-Williams (1899), Böhtlingk and Roth (1855–1875).
These small snapshots and excerpts support offline regression tests, not a
dictionary distribution.

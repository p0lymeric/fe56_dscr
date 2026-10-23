## Programmer fanart

Some files related to *The Message from Deep Space*. Initially this repository started as an attempt to write a client for the [DSCR](https://github.com/dixonary/mfds-server), but devolved into work on one specific feature.

[`rust/crates/fe56_translation/compiler/mwis.rs`](rust/crates/fe56_translation/src/compiler/mwis.rs) is an implementation of a "signal compiler" designed to parse strings like those used to translate messages in the game, based on a [solver for the weighted interval scheduling problem](https://en.wikipedia.org/wiki/Interval_scheduling#Weighted).

Unlike the game's signal compiler, this repository's implementation considers user-provided whitespace and infers additional whitespace only when unambiguous, which makes its behaviour predictable and suitable for editing longer transmissions with a larger dictionary.

This implementation is decently fast. It returns results at text rendering speed when handling strings up to several thousands of characters long. Performance is theoretically bounded by input string length at `O(N log N)`, demonstrated by measurements below.

corpus | character count | parse time (µs)
| --- | --- | --- |
[(definition of a human second)](rust/crates/fe56_translation/tests/artifacts/human_second.txt) | 795 | 257
[*A Pickle for the Knowing Ones*](rust/crates/fe56_translation/tests/artifacts/third_party/a_pickle_for_the_knowing_ones.txt) | 90991 | 52427
[*Pride and Prejudice*](rust/crates/fe56_translation/tests/artifacts/third_party/pride_and_prejudice.txt) | 763082 | 507900

(with a 307660 word dictionary consisting of the author's [`DICTIONARY-1.save`](rust/crates/fe56_translation/tests/artifacts/DICTIONARY-1.save) and the [*English Speller Database*](rust/crates/fe56_translation/tests/artifacts/third_party/esdb.txt). Words in the dictionary that are not substrings of the input do not significantly affect parse time.)

Additionally, the implementation tracks token and error spans and generates traces of ambiguous parses, which can be used to provide visual feedback to a user.

A demonstration of the parser's functionality is available under [`rust/crates/fe56_translation_test`](rust/crates/fe56_translation_test).

![sus parsing](sus.gif)

### Acknowledgments:
* This repository directly includes various third-party materials, listed in [ATTRIBUTION.md](ATTRIBUTION.md).

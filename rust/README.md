# Fe56

Fe56 is a collection of Rust crates that implement client functionality for the [Deep Space Communication Relay (DSCR)](https://github.com/dixonary/mfds-server). Currently, its "signal compiler" implementation is exported as a static library for a C++-based client, C13.

[`crates/fe56_translation/compiler/mwis.rs`](crates/fe56_translation/src/compiler/mwis.rs) is an implementation of a "signal compiler" designed to parse strings like those used to translate messages in *The Message from Deep Space*, based on a [solver for the weighted interval scheduling problem](https://en.wikipedia.org/wiki/Interval_scheduling#Weighted).

Unlike the game's signal compiler, Fe56's implementation considers user-provided whitespace and infers additional whitespace only when unambiguous, which makes its behaviour predictable and suitable for editing longer transmissions with a larger dictionary.

This implementation is decently fast. It returns results at text rendering speed when handling strings up to several thousands of characters long. Performance is theoretically bounded by input string length at `O(N log N)`, demonstrated by measurements below.

corpus | character count | parse time (µs)
| --- | --- | --- |
[(definition of a human second)](crates/fe56_translation/tests/artifacts/human_second.txt) | 795 | 84
[*A Pickle for the Knowing Ones*](crates/fe56_translation/tests/artifacts/third_party/a_pickle_for_the_knowing_ones.txt) | 90991 | 19027
[*Pride and Prejudice*](crates/fe56_translation/tests/artifacts/third_party/pride_and_prejudice.txt) | 763082 | 176860

(with a 307660 word dictionary consisting of the author's [`DICTIONARY-1.save`](crates/fe56_translation/tests/artifacts/DICTIONARY-1.save) and the [*English Speller Database*](crates/fe56_translation/tests/artifacts/third_party/esdb.txt). Words in the dictionary that are not substrings of the input do not significantly affect parse time.)

Additionally, the implementation tracks token and error spans and generates traces of ambiguous parses, which is used to drive visual feedback and hyperlinks in C13.

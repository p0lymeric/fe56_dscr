use super::*;

use std::ops::Add;

// Compiler implementation based off a maximum-weight independent set (MWIS) solver for weighted interval graphs
// similar to https://en.wikipedia.org/wiki/Interval_scheduling#Weighted
// polymeric 2026

// [0-9]
// |-?[0-9]+
#[derive(Debug, Clone, Copy)]
enum SpecialTokenDfa {
    Inactive,
    Bar { start: usize },
    BarMinus { start: usize },
    BarDigit { start: usize },
}

impl SpecialTokenDfa {
    fn advance(&mut self, pos: usize, c: char, input: &str, token_candidates: &mut Vec<dictionary::TokenizeResult>) {
        match self {
            SpecialTokenDfa::Inactive => {
                match c {
                    '|' => *self = SpecialTokenDfa::Bar { start: pos },
                    '0'..='9' => {
                        if let Some(id_u32) = c.to_digit(10) && let Ok(id) = id_u32.try_into() {
                            token_candidates.push(dictionary::TokenizeResult {
                                kind: dictionary::TokenizeResultKind::Numeral,
                                id,
                                span: (pos..pos + 1).into()
                            });
                        }
                    },
                    _ => {}
                }
            },
            SpecialTokenDfa::Bar { start } => {
                match c {
                    '|' => *self = SpecialTokenDfa::Bar { start: pos },
                    '-' => *self = SpecialTokenDfa::BarMinus { start: *start },
                    '0'..='9' => *self = SpecialTokenDfa::BarDigit { start: *start },
                    _ => *self = SpecialTokenDfa::Inactive
                }
            },
            SpecialTokenDfa::BarMinus { start } => {
                match c {
                    '|' => *self = SpecialTokenDfa::Bar { start: pos },
                    '0'..='9' => *self = SpecialTokenDfa::BarDigit { start: *start },
                    _ => *self = SpecialTokenDfa::Inactive
                }
            },
            SpecialTokenDfa::BarDigit { start } => {
                match c {
                    '0'..='9' => {},
                    '|' => {
                        if let Ok(id) = input[*start + 1..pos].parse() {
                            token_candidates.push(dictionary::TokenizeResult {
                                kind: dictionary::TokenizeResultKind::BareId,
                                id,
                                span: (*start..pos).into()
                            });
                        } else {
                            token_candidates.push(dictionary::TokenizeResult {
                                kind: dictionary::TokenizeResultKind::BareIdBig,
                                id: 0,
                                span: (*start..pos).into()
                            });
                        }
                        *self = SpecialTokenDfa::Bar { start: pos };
                    },
                    _ => {
                        if let Ok(id) = input[*start + 1..pos].parse() {
                            token_candidates.push(dictionary::TokenizeResult {
                                kind: dictionary::TokenizeResultKind::BareId,
                                id,
                                span: (*start..pos).into()
                            });
                        } else {
                            token_candidates.push(dictionary::TokenizeResult {
                                kind: dictionary::TokenizeResultKind::BareIdBig,
                                id: 0,
                                span: (*start..pos).into()
                            });
                        }
                        *self = SpecialTokenDfa::Inactive;
                    }
                }
            },
        }
    }

    fn terminate(&mut self, input: &str, token_candidates: &mut Vec<dictionary::TokenizeResult>) {
        match self {
            SpecialTokenDfa::BarDigit { start } => {
                if let Ok(id) = input[*start + 1..input.len()].parse() {
                    token_candidates.push(dictionary::TokenizeResult {
                        kind: dictionary::TokenizeResultKind::BareId,
                        id,
                        span: (*start..input.len()).into()
                    });
                } else {
                    token_candidates.push(dictionary::TokenizeResult {
                        kind: dictionary::TokenizeResultKind::BareIdBig,
                        id: 0,
                        span: (*start..input.len()).into()
                    });
                }
            },
            _ => {},
        }
        *self = SpecialTokenDfa::Inactive;
    }
}

fn get_token_candidates(dictionary: &dyn dictionary::Dictionary, input: &str) -> Vec<dictionary::TokenizeResult>  {
    let mut token_candidates: Vec<dictionary::TokenizeResult> = Vec::new();
    let input_casefold = dictionary.casefold(input);
    let mut special_matcher = SpecialTokenDfa::Inactive;
    for (pos, c) in input.char_indices() {
        token_candidates.append(&mut dictionary.tokenize_at(input, &input_casefold, pos));
        special_matcher.advance(pos, c, input, &mut token_candidates);
    }
    special_matcher.terminate(input, &mut token_candidates);
    token_candidates
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
struct MwisWeight {
    non_whitespace_bytes: u32,
    tokens_penalty: u32,
    bare_ids: u32,
    words: u32,
    exact_words: u32,
    digits: u32,
}

impl PartialOrd for MwisWeight {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        Some(self.cmp(other))
    }
}

impl Ord for MwisWeight {
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        // Top-down lexicographic ordering, but one of the field comparisons is reversed
        self.non_whitespace_bytes.cmp(&other.non_whitespace_bytes)
            .then(other.tokens_penalty.cmp(&self.tokens_penalty)) // Inverted, lower is more optimal
            .then(self.bare_ids.cmp(&other.bare_ids))
            .then(self.words.cmp(&other.words))
            .then(self.exact_words.cmp(&other.exact_words))
            .then(self.digits.cmp(&other.digits))
    }
}

impl Add for MwisWeight {
    type Output = MwisWeight;

    fn add(self, rhs: Self) -> Self::Output {
        // Component-wise addition
        // Surely we won't see a 4 GB string or more than 4 billion tokens pass through this optimizer...
        // Surely we don't need to perform size checks and use saturating addition...
        // Right?
        MwisWeight {
            non_whitespace_bytes: self.non_whitespace_bytes + rhs.non_whitespace_bytes,
            tokens_penalty: self.tokens_penalty + rhs.tokens_penalty,
            bare_ids: self.bare_ids + rhs.bare_ids,
            words: self.words + rhs.words,
            exact_words: self.exact_words + rhs.exact_words,
            digits: self.digits + rhs.digits,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct MwisSymbolizer;

impl Symbolizer for MwisSymbolizer {
    fn symbolize(&self, dictionary: &dyn dictionary::Dictionary, input: &str) -> Symbolized {
        // List all possible tokenizations of the input string, then sort by earliest end for MWIS
        let mut token_candidates = get_token_candidates(dictionary, input);
        token_candidates.sort_by_key(|x| (x.span.end, x.span.start));

        // TODO pre-filter to pick 1 choice per group of perfectly overlapping spans (prioritize highest IDs)
        // FIXME delete tokens that overlap with a bare ID token

        // OPTIMIZATION GOAL
        // Under the framework of the weighted interval scheduling problem, find a schedule (non-overlapping token selection)
        // that optimizes the sum of weights described by the tuple:
        //     (
        //         W1. Number of covered non-whitespace bytes,
        //         W2. Constant 1,
        //         W3. 1 if bare ID,
        //         W4. 1 if word,
        //         W5: 1 if word AND exact case match,
        //         W6. 1 if numerical digits
        //     )
        // Individual weight components in the tuple add component-wise along the schedule.

        // OPTIMAL SOLUTIONS
        // Definition: A schedule with "optimal" total score has a sum of weights that, in priority order:
        //     W1. covers as many non-whitespace bytes in the text string as possible (higher is better)
        //     W2. with the fewest covering tokens (lower is better)
        //     W3. prioritizing bare IDs (higher is better)
        //     W4. over case-exact or case-inexact words (higher is better)
        //     W5. over case-inexact* words (higher is better, *we measure case-exact words to deprioritize case-INexact words)
        //     W6. over numbers (higher is better)

        // VALID SOLUTIONS
        // Definition: Iff a resultant schedule's W1 is equal to the total number of non-whitespace bytes in the text string, it is a "valid solution".
        // Fact: If a "valid solution" exists, any "optimal" schedule will be a "valid solution".
        // Handling: If an "optimal" schedule is not a "valid solution", the characters that were uncovered are reported as "unrecognized spans".

        // AMBIGUOUS SOLUTIONS
        // Definition: Any schedules with identical score are "ambiguous" with one another.
        // Handling: If there exists more than one "optimal" "valid solution", the set difference of two such solutions are reported as "ambiguous tokenizations".

        // NOTES ON PRECEDENCE
        // - These notes provide an interpretation of tokenizer/optimizer behaviour.
        // - Bare IDs (a bar followed by an integer string) are ultimately prioritized and may never be shadowed by a word.
        //     - However, the bar itself followed by a non-integer string or nothing is not interpreted as a bare ID, and thus may be shadowed.
        // - Words (dictionary entry matches) are highly prioritized.
        //     - Words match sequences of text via exact or casefolded comparison.
        //     - Tokens that match a word exactly are prioritized higher than casefolded matches.
        //     - Words can contain digits and whitespace.
        // - Whitespace is either literally matched or used to explicitly set token boundaries. In priority order:
        //     1. Any words that include whitespace will consume whitespace in order to produce a match.
        //     2. Any leftover whitespace can serve to force a choice or break ambiguity, when the parser cannot find a good position to infer token boundaries.
        //         - Consider an example dictionary with words ["PARTICLE", "SIT", "IT", "FIRETRUCK", "FIRE", "TRUCK", "IS", "ITIS"]
        //         - The parser's definition of "good position to infer token boundaries" are "ambiguity" are defined by the optimization goal, largely influenced by W2.
        //          e.g. "PARTICLESIT" is ambiguous with "PARTICLES IT" or "PARTICLE SIT". Only the two whitespace-qualified forms are accepted.
        //         - It is expected that a user will insert whitespace to solve parse ambiguity, naturally or by using feedback from ambiguity traces.
        //         - When a compound word and its constituents are both present in a dictionary, the compound is preferred when no whitespace is present.
        //           e.g. "FIRETRUCK" unambiguously parses as "FIRETRUCK", whereas "FIRE TRUCK" unambiguously parses as "FIRE TRUCK".
        //           e.g. For the same reason, "ITIS" unambiguously parses as "ITIS", instead of "IT IS".
        //         - It is expected that a user will type with redundant whitespace and curate their dictionary to defend against unwanted compound merging.

        // POSSIBLE IMPLEMENTATION DEFECTS
        // - ID priority between two case-folded word matches is undefined.
        //   Since we maintain sorted ID lists in our trie, actual behaviour is likely deterministic.
        // - Additional consideration is needed to ensure that bare IDs are not shadowable by a word.
        //   In cases where a word contains a bar-integer sequence, that word may win if it can replace what would've been multiple tokens.

        let mut weights = vec![MwisWeight::default(); token_candidates.len() + 1];
        let mut precedes = vec![0usize; token_candidates.len() + 1];
        let mut optimal = vec![MwisWeight::default(); token_candidates.len() + 1];

        // Note: weights[0]/precedes[0]/optimal[0] are fixed at 0 and can be accessed during search.

        for (i, tk) in token_candidates.iter().enumerate() {
            let non_whitespace_bytes = input[tk.span].chars().fold(0usize, |c, x| if !x.is_whitespace() { c + x.len_utf8() } else { c } );
            if let Ok(non_whitespace_bytes) = u32::try_from(non_whitespace_bytes) {
                match tk.kind {
                    dictionary::TokenizeResultKind::WordInexact | dictionary::TokenizeResultKind::WordExact => {
                        weights[i + 1] = MwisWeight {
                            non_whitespace_bytes: non_whitespace_bytes,
                            tokens_penalty: 1, // positive in the weight structure but penalized during Ord comparison
                            bare_ids: 0,
                            words: 1,
                            exact_words: if tk.kind == dictionary::TokenizeResultKind::WordExact { 1 } else { 0 },
                            digits: 0
                        };
                    },
                    dictionary::TokenizeResultKind::Numeral => {
                        weights[i + 1] = MwisWeight {
                            non_whitespace_bytes: non_whitespace_bytes,
                            tokens_penalty: 1, // positive in the weight structure but penalized during Ord comparison
                            bare_ids: 0,
                            words: 0,
                            exact_words: 0,
                            digits: 1
                        };
                    },
                    dictionary::TokenizeResultKind::BareId | dictionary::TokenizeResultKind::BareIdBig => {
                        weights[i + 1] = MwisWeight {
                            non_whitespace_bytes: non_whitespace_bytes,
                            tokens_penalty: 1, // positive in the weight structure but penalized during Ord comparison
                            bare_ids: 1,
                            words: 0,
                            exact_words: 0,
                            digits: 0
                        };
                    },
                }
            } else {
                // TODO push error (pathologically sized token)
            }

            precedes[i + 1] = token_candidates.partition_point(|x| x.span.end <= tk.span.start);
        }

        for i in 1..token_candidates.len() + 1 {
            optimal[i] = (weights[i] + optimal[precedes[i]]).max(optimal[i - 1])
        }

        let mut opt_schedule: Vec<usize> = Vec::new();
        let mut ambiguous_points: Vec<usize> = Vec::new();
        let mut j = token_candidates.len();
        while j > 0 {
            let compare = (weights[j] + optimal[precedes[j]]).cmp(&optimal[j - 1]);
            if compare.is_eq() {
                // Iff this branch is never reached, the optimal schedule is unique.
                ambiguous_points.push(j);
            }
            if compare.is_ge() {
                opt_schedule.push(j);
                j = precedes[j];
            } else {
                j = j - 1;
            }
        }

        let s0 = opt_schedule.iter().rev().map(|j| {
            let tk = token_candidates[*j - 1];
            SymbolizerToken {
                id: tk.id,
                span: tk.span,
            }
        }).collect::<Vec<SymbolizerToken>>();

        let mut errors: Vec<SymbolizerError> = Vec::new();

        // Collect spans of untokenized non-whitespace
        fn report_uncovered_non_whitespace(errors: &mut Vec<SymbolizerError>, input: &str, end_prev: usize, start: usize) {
            let mut maybe_error_start: Option<usize> = None;
            for (off, c) in input[end_prev..start].char_indices() {
                if !c.is_whitespace() {
                    if maybe_error_start == None {
                        maybe_error_start = Some(end_prev + off);
                    }
                } else {
                    if let Some(error_start) = maybe_error_start {
                        errors.push(SymbolizerError { kind: SymbolizerErrorKind::EntryNotFound, span: (error_start..end_prev + off).into() });
                        maybe_error_start = None;
                    }
                }
            }
            if let Some(error_start) = maybe_error_start {
                errors.push(SymbolizerError { kind: SymbolizerErrorKind::EntryNotFound, span: (error_start..start).into() });
            }
        }
        let mut end_prev = 0;
        for tk in s0.iter() {
            report_uncovered_non_whitespace(&mut errors, input, end_prev, tk.span.start);
            end_prev = tk.span.end;
        }
        if end_prev < input.len() {
            report_uncovered_non_whitespace(&mut errors, input, end_prev, input.len());
        }

        // Collect ambiguous spans
        for j in ambiguous_points {
            let mut left_divergence: Vec<SymbolizerToken> = Vec::new();
            let mut right_divergence: Vec<SymbolizerToken> = Vec::new();

            let mut j_left = precedes[j];
            let tk_left = token_candidates[j - 1];
            left_divergence.push(SymbolizerToken { id: tk_left.id, span: tk_left.span });

            let mut j_right = j - 1;

            while j_left != j_right {
                if j_left > j_right {
                    let compare = (weights[j_left] + optimal[precedes[j_left]]).cmp(&optimal[j_left - 1]);
                    if compare.is_ge() {
                        let tk = token_candidates[j_left - 1];
                        left_divergence.push(SymbolizerToken { id: tk.id, span: tk.span });
                        j_left = precedes[j_left];
                    } else {
                        j_left = j_left - 1;
                    }
                } else {
                    let compare = (weights[j_right] + optimal[precedes[j_right]]).cmp(&optimal[j_right - 1]);
                    if compare.is_ge() {
                        let tk = token_candidates[j_right - 1];
                        right_divergence.push(SymbolizerToken { id: tk.id, span: tk.span });
                        j_right = precedes[j_right];
                    } else {
                        j_right = j_right - 1;
                    }
                }
            }

            left_divergence.reverse();
            right_divergence.reverse();

            let start = if let Some(right_first) = right_divergence.first() {
                // Safe to unwrap first() as we assure at least one push
                left_divergence.first().unwrap().span.start.min(right_first.span.start)
            } else {
                // Safe to unwrap first() as we assure at least one push
                left_divergence.first().unwrap().span.start
            };

            errors.push(SymbolizerError {
                kind: SymbolizerErrorKind::AmbiguousSpan(left_divergence, right_divergence),
                span: (start..tk_left.span.end).into()
            })
        }

        Symbolized { tokens: s0, errors }
    }
}

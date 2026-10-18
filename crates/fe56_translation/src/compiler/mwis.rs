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
    fn advance(&mut self, input: &str, token_candidates: &mut Vec<dictionary::TokenizeResult>, pos: usize, c: char) {
        match self {
            SpecialTokenDfa::Inactive => {
                match c {
                    '|' => *self = SpecialTokenDfa::Bar { start: pos },
                    '0'..='9' => {
                        Self::push_numeral(token_candidates, pos, c);
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
                        Self::push_bare_id(input, token_candidates, *start, pos);
                        *self = SpecialTokenDfa::Bar { start: pos };
                    },
                    _ => {
                        Self::push_bare_id(input, token_candidates, *start, pos);
                        *self = SpecialTokenDfa::Inactive;
                    }
                }
            },
        }
    }

    fn terminate(&mut self, input: &str, token_candidates: &mut Vec<dictionary::TokenizeResult>) {
        match self {
            SpecialTokenDfa::BarDigit { start } => {
                Self::push_bare_id(input, token_candidates, *start, input.len());
            },
            _ => {},
        }
        *self = SpecialTokenDfa::Inactive;
    }

    fn push_bare_id(input: &str, token_candidates: &mut Vec<dictionary::TokenizeResult>, start: usize, pos: usize) {
        if let Ok(id) = input[start + 1..pos].parse() {
            token_candidates.push(dictionary::TokenizeResult {
                kind: dictionary::TokenizeResultKind::BareId,
                id,
                span: (start..pos).into()
            });
        } else {
            token_candidates.push(dictionary::TokenizeResult {
                kind: dictionary::TokenizeResultKind::BareIdBig,
                id: 0,
                span: (start..pos).into()
            });
        }
    }

    fn push_numeral(token_candidates: &mut Vec<dictionary::TokenizeResult>, pos: usize, c: char) {
        if let Some(id_u32) = c.to_digit(10) && let Ok(id) = id_u32.try_into() {
            token_candidates.push(dictionary::TokenizeResult {
                kind: dictionary::TokenizeResultKind::Numeral,
                id,
                span: (pos..pos + 1).into()
            });
        }
    }
}

fn get_token_candidates(dictionary: &dyn dictionary::Dictionary, input: &str) -> Vec<dictionary::TokenizeResult>  {
    let mut token_candidates: Vec<dictionary::TokenizeResult> = Vec::new();
    let input_casefold = dictionary.casefold(input);
    let mut special_matcher = SpecialTokenDfa::Inactive;
    for (pos, c) in input.char_indices() {
        // Prepend special matches. They are weighted with different priority than words, so we don't need to insort.
        // ...but we do have a nasty ordering dependency with the end index.
        // Bare IDs end at pos (end of the previous character).
        // Digits end at pos + c.len_utf8().
        special_matcher.advance(input, &mut token_candidates, pos, c);

        let out_start = token_candidates.len();
        dictionary.tokenize_at(input, &input_casefold, pos + c.len_utf8(), &mut token_candidates);
        let out_end: usize = token_candidates.len();
        if out_start != out_end {
            // Assure that RTrie returned words in length order
            debug_assert!(token_candidates[out_start..out_end].windows(2).all(|x| x[0].span.start >= x[1].span.start));

            // Reverse in place, which encourages the optimizer to place letters on earlier tokens in ambiguous cases
            token_candidates[out_start..out_end].reverse();

            // Assure that RTrie returns id in an order that prefers higher ids in ambiguous cases
            #[cfg(debug_assertions)]
            for group in token_candidates[out_start..out_end].chunk_by_mut(|a, b| a.span.start == b.span.start) {
                debug_assert!(group.windows(2).all(|x| x[0].id < x[1].id));
            }
        }
    }
    special_matcher.terminate(input, &mut token_candidates);
    debug_assert!(token_candidates.windows(2).all(|x| x[0].span.end <= x[1].span.end));
    token_candidates
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
struct MwisWeight {
    non_whitespace_bytes: u32,
    bare_ids: u32,
    tokens_penalty: u32,
    words: u32,
    exact_words: u32,
    // the lowest priority token type does not need to be counted
    // digits: u32,
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
            .then(self.bare_ids.cmp(&other.bare_ids))
            .then(other.tokens_penalty.cmp(&self.tokens_penalty)) // Inverted, lower is more optimal
            .then(self.words.cmp(&other.words))
            .then(self.exact_words.cmp(&other.exact_words))
            // .then(self.digits.cmp(&other.digits))
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
            bare_ids: self.bare_ids + rhs.bare_ids,
            tokens_penalty: self.tokens_penalty + rhs.tokens_penalty,
            words: self.words + rhs.words,
            exact_words: self.exact_words + rhs.exact_words,
            // digits: self.digits + rhs.digits,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct MwisSymbolizer;

impl Symbolizer for MwisSymbolizer {
    fn symbolize(&self, dictionary: &dyn dictionary::Dictionary, input: &str) -> Symbolized {
        // List all possible tokenizations of the input string
        // This must be sorted by end for the optimization algorithm
        let token_candidates = get_token_candidates(dictionary, input);

        // TODO pre-filter to pick 1 choice per group of perfectly overlapping spans (prioritize highest IDs)
        // FIXME delete tokens that overlap with a bare ID token

        // OPTIMIZATION GOAL
        // Under the framework of the weighted interval scheduling problem, find a schedule (non-overlapping token selection)
        // that optimizes the sum of weights described by the tuple:
        //     (
        //         W1. Number of covered non-whitespace bytes,
        //         W2. 1 if bare ID,
        //         W3. Constant 1,
        //         W4. 1 if word,
        //         W5: 1 if word AND exact case match,
        //     )
        // Individual weight components in the tuple add component-wise along the schedule.

        // OPTIMAL SOLUTIONS
        // Definition: A schedule with "optimal" total score has a sum of weights that, in priority order:
        //     W1. covers as many non-whitespace bytes in the text string as possible (higher is better)
        //     W2. maximizing number of bare ID tokens (higher is better)
        //     W3. then minimizing numer of covering tokens (lower is better)
        //     W4. then maximizing case-exact or case-inexact words (higher is better)
        //     W5. over case-inexact* words (higher is better, *we measure case-exact words to deprioritize case-INexact words)
        //     W*. over numbers (higher is better)
        // W* is implied by case fallthrough.

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
        //     - If a dictionary contains two different words that casefold identically, the parser will only unambiguously accept case-exact variants.
        //     - If a dictionary contains two identical words, the parser will treat any case variant of the word as ambiguous.
        // - Whitespace is either literally matched, used to explicitly set token boundaries, or ignored. In priority order:
        //     1. Any words that include whitespace will consume whitespace in order to produce a match.
        //     2. Any leftover whitespace can serve to force a choice or break ambiguity, when the parser cannot find a good position to infer token boundaries.
        //         - Consider an example dictionary with words ["PARTICLE", "SIT", "IT", "FIRETRUCK", "FIRE", "TRUCK", "IS", "ITIS"]
        //         - The parser's definition of "good position to infer token boundaries" are "ambiguity" are defined by the optimization goal, largely influenced by W3.
        //          e.g. "PARTICLESIT" is ambiguous with "PARTICLES IT" or "PARTICLE SIT". Only the two whitespace-qualified forms are accepted.
        //         - It is expected that a user will insert whitespace to solve parse ambiguity, naturally or by using feedback from ambiguity traces.
        //         - When a compound word and its constituents are both present in a dictionary, the compound is preferred when no whitespace is present.
        //           e.g. "FIRETRUCK" unambiguously parses as "FIRETRUCK", whereas "FIRE TRUCK" unambiguously parses as "FIRE TRUCK".
        //           e.g. For the same reason, "ITIS" unambiguously parses as "ITIS", instead of "IT IS".
        //         - It is expected that a user will type with redundant whitespace and curate their dictionary to defend against unwanted compound merging.

        let mut weights: Vec<MwisWeight> = Vec::with_capacity(token_candidates.len() + 1);
        let mut precedes: Vec<usize> = Vec::with_capacity(token_candidates.len() + 1);
        let mut optimal: Vec<MwisWeight> = Vec::with_capacity(token_candidates.len() + 1);

        // weights[0]/precedes[0]/optimal[0] are fixed at 0 and can be accessed during search.
        weights.push(MwisWeight::default());
        precedes.push(0);
        optimal.push(MwisWeight::default());

        for (i, tk) in token_candidates.iter().enumerate() {
            let non_whitespace_bytes = input[tk.span].chars().fold(0usize, |acc, c| if !c.is_whitespace() { acc + c.len_utf8() } else { acc } );
            weights.push(if let Ok(non_whitespace_bytes) = u32::try_from(non_whitespace_bytes) {
                match tk.kind {
                    dictionary::TokenizeResultKind::WordInexact | dictionary::TokenizeResultKind::WordExact => {
                        MwisWeight {
                            non_whitespace_bytes,
                            bare_ids: 0,
                            tokens_penalty: 1,
                            words: 1,
                            exact_words: if tk.kind == dictionary::TokenizeResultKind::WordExact { 1 } else { 0 },
                            // digits: 0
                        }
                    },
                    dictionary::TokenizeResultKind::Numeral => {
                        MwisWeight {
                            non_whitespace_bytes,
                            bare_ids: 0,
                            tokens_penalty: 1,
                            words: 0,
                            exact_words: 0,
                            // digits: 1
                        }
                    },
                    dictionary::TokenizeResultKind::BareId | dictionary::TokenizeResultKind::BareIdBig => {
                        MwisWeight {
                            non_whitespace_bytes,
                            bare_ids: 1,
                            tokens_penalty: 1,
                            words: 0,
                            exact_words: 0,
                            // digits: 0
                        }
                    },
                }
            } else {
                // FIXME should flag an error here instead of deprioritizing this token
                MwisWeight {
                    non_whitespace_bytes: 0,
                    bare_ids: 0,
                    tokens_penalty: 0,
                    words: 0,
                    exact_words: 0,
                    // digits: 0
                }
            });

            precedes.push(token_candidates[..i].partition_point(|x| x.span.end <= tk.span.start));

            optimal.push((weights[i + 1] + optimal[precedes[i + 1]]).max(optimal[i]));
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

        let mut tokens: Vec<SymbolizerToken> = Vec::with_capacity(opt_schedule.len());
        let mut errors: Vec<SymbolizerError> = Vec::new();
        let mut traces: Vec<SymbolizerTrace> = Vec::new();

        // Collect spans within untokenized regions
        fn trace_uncovered_region(errors: &mut Vec<SymbolizerError>, traces: &mut Vec<SymbolizerTrace>, input: &str, left: usize, right: usize) {
            let mut last_start = left;
            let mut maybe_error_start: Option<usize> = None;
            for (off, c) in input[left..right].char_indices() {
                if !c.is_whitespace() {
                    if maybe_error_start == None {
                        if last_start != left + off {
                            let span = SymbolizerSpan::from(last_start..left + off);
                            traces.push(SymbolizerTrace { span, kind: SymbolizerTraceKind::Whitespace });
                            last_start = left + off;
                        }
                        maybe_error_start = Some(left + off);
                    }
                } else {
                    if let Some(error_start) = maybe_error_start {
                        let span = SymbolizerSpan::from(error_start..left + off);
                        errors.push(SymbolizerError { kind: SymbolizerErrorKind::EntryNotFound, span });
                        traces.push(SymbolizerTrace { span, kind: SymbolizerTraceKind::Unconsumed });
                        maybe_error_start = None;
                        last_start = left + off;
                    }
                }
            }
            if let Some(error_start) = maybe_error_start {
                let span = SymbolizerSpan::from(error_start..right);
                errors.push(SymbolizerError { kind: SymbolizerErrorKind::EntryNotFound, span });
                traces.push(SymbolizerTrace { span, kind: SymbolizerTraceKind::Unconsumed });
            } else if last_start != right {
                let span = SymbolizerSpan::from(last_start..right);
                traces.push(SymbolizerTrace { span, kind: SymbolizerTraceKind::Whitespace });
            }
        }

        // Trace ambiguous spans
        fn trace_ambiguous_endpoint(errors: &mut Vec<SymbolizerError>, traces: &mut Vec<SymbolizerTrace>, weights: &Vec<MwisWeight>, precedes: &Vec<usize>, optimal: &Vec<MwisWeight>, token_candidates: &Vec<dictionary::TokenizeResult>, j: usize) {
            let mut left_divergence: Vec<SymbolizerToken> = Vec::new();
            let mut right_divergence: Vec<SymbolizerToken> = Vec::new();

            let mut j_left = precedes[j];
            let tk_left = token_candidates[j - 1];
            left_divergence.push(SymbolizerToken { id: tk_left.id, span: tk_left.span.into() });

            let mut j_right = j - 1;

            while j_left != j_right {
                if j_left > j_right {
                    let compare = (weights[j_left] + optimal[precedes[j_left]]).cmp(&optimal[j_left - 1]);
                    if compare.is_ge() {
                        let tk = token_candidates[j_left - 1];
                        left_divergence.push(SymbolizerToken { id: tk.id, span: tk.span.into() });
                        j_left = precedes[j_left];
                    } else {
                        j_left = j_left - 1;
                    }
                } else {
                    let compare = (weights[j_right] + optimal[precedes[j_right]]).cmp(&optimal[j_right - 1]);
                    if compare.is_ge() {
                        let tk = token_candidates[j_right - 1];
                        right_divergence.push(SymbolizerToken { id: tk.id, span: tk.span.into() });
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

            // Delete what was pushed before into traces
            // FIXME hacky, could we end up deleting whitespace and cause a frameshift?
            // May need to iterate through opt_schedule in actual order so we can skip the parent iterator
            // traces.truncate(traces.len() - left_divergence.len());
            // FIXME NOT IMPLEMENTED

            if left_divergence.len() == right_divergence.len() {
                for (left, right) in left_divergence.iter().zip(&right_divergence) {
                    traces.push(SymbolizerTrace { span: left.span, kind: SymbolizerTraceKind::TokenAmbiguousAlt { id: left.id, id_alt: right.id, span_alt: right.span } });
                }
            } else {
                for left in left_divergence.iter() {
                    traces.push(SymbolizerTrace { span: left.span, kind: SymbolizerTraceKind::TokenAmbiguous { id: left.id } });
                }
            }

            errors.push(SymbolizerError {
                kind: SymbolizerErrorKind::AmbiguousSpan(left_divergence, right_divergence),
                span: (start..tk_left.span.end).into()
            });
        }

        let mut end_prev = 0;
        let mut ambiguous_points_iter = ambiguous_points.iter().rev();
        let mut j_next = ambiguous_points_iter.next();
        for &i in opt_schedule.iter().rev() {
            let tk = token_candidates[i - 1];
            let span = SymbolizerSpan::from(tk.span);
            match tk.kind {
                dictionary::TokenizeResultKind::BareIdBig => {
                    errors.push(SymbolizerError {
                        kind: SymbolizerErrorKind::OutOfNativeIntBounds,
                        span,
                    });
                    traces.push(SymbolizerTrace {
                        span,
                        kind: SymbolizerTraceKind::TokenBig,
                    });
                },
                _ => {
                        tokens.push(SymbolizerToken {
                            id: tk.id,
                            span: span,
                        });
                    if let Some(j) = j_next && i == *j {
                        trace_ambiguous_endpoint(&mut errors, &mut traces, &weights, &precedes, &optimal, &token_candidates, *j);
                        j_next = ambiguous_points_iter.next();
                    } else {
                        traces.push(SymbolizerTrace {
                            span,
                            kind: SymbolizerTraceKind::Token { id: tk.id },
                        });
                    }
                }
            }

            trace_uncovered_region(&mut errors, &mut traces, input, end_prev, tk.span.start);
            end_prev = tk.span.end;
        }
        if end_prev < input.len() {
            trace_uncovered_region(&mut errors, &mut traces, input, end_prev, input.len());
        }

        Symbolized { tokens, errors, traces }
    }
}

use crate::dictionary::{self, SpacingKind};

use std::collections::HashSet;
use std::range::Range;
use std::fmt::Write;

// Signal compiler interface
// polymeric 2026

// Symbolizers transform a text sequence to a sequence of symbol tokens.
// Desymbolizers format a sequence of symbol tokens to text.

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum SymbolizerErrorKind {
    // Game errors:
    NullInput,
    // not a number or out of i32 bounds
    NotANumber,
    EntryNotFound,

    // Our errors:
    // out of native integer bounds
    OutOfNativeIntBounds,
    // muliple readings
    AmbiguousSpan(Vec<Vec<SymbolizerToken>>),
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SymbolizerError {
    pub kind: SymbolizerErrorKind,
    pub span: Range<usize>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct SymbolizerToken {
    pub id: dictionary::SymbolT,
    pub span: Range<usize>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Symbolized {
    pub tokens: Vec<SymbolizerToken>,
    pub errors: Vec<SymbolizerError>,
}

pub trait Symbolizer {
    fn symbolize(&self, dictionary: &dyn dictionary::Dictionary, input: &str) -> Symbolized;
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Desymbolized {
    pub text: String,
    pub spans: Vec<Range<usize>>,
}

pub trait Desymbolizer {
    fn desymbolize(&self, dictionary: &dyn dictionary::Dictionary, input: &[dictionary::SymbolT]) -> Desymbolized;
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct CReformatterSymbolizer;

impl Symbolizer for CReformatterSymbolizer {
    fn symbolize(&self, dictionary: &dyn dictionary::Dictionary, input: &str) -> Symbolized {
        // Reference CReformatter:
        // Force to uppercase
        // Strip tabs and spaces (and nulls)
        // Strip leading and trailing whitespace
        // Work with two copies of the strings, iterating over the dictionary (ignoring @_UNDEFs), longer words first:
        //     Search through Copy B left-right until the string is exhausted or there are 100 matches
        //     Copy A: Break matches with newlines
        //     Copy B: Break matches with newlines as in A and replace matched characters with @s
        // Collect tokens by accumulating digits and performing exact dictionary lookups using newlines as delimiters on Copy A
        todo!()
    }
}

fn get_token_candidates(dictionary: &dyn dictionary::Dictionary, input: &str) -> Vec<dictionary::TokenizeResult>  {
    let mut token_candidates: Vec<dictionary::TokenizeResult> = Vec::new();
    let input_casefold = dictionary.casefold(input);
    for (pos, _c) in input.char_indices() {
        token_candidates.append(&mut dictionary.tokenize_at(input, &input_casefold, pos));
    }
    token_candidates
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct EdfsSymbolizer;

impl Symbolizer for EdfsSymbolizer {
    fn symbolize(&self, dictionary: &dyn dictionary::Dictionary, input: &str) -> Symbolized {
        // https://en.wikipedia.org/wiki/Interval_scheduling#Unweighted
        let mut token_candidates = get_token_candidates(dictionary, input);
        token_candidates.sort_by_key(|x| (x.end, x.start));

        let mut best_tokenization: Vec<SymbolizerToken> = Vec::new();
        if token_candidates.len() >= 1 {
            let mut last_select = &token_candidates[0];
            best_tokenization.push(SymbolizerToken {
                id: last_select.id,
                span: (last_select.start..last_select.end).into(),
            });
            for tk in token_candidates.iter().skip(1) {
                if tk.start >= last_select.end {
                    last_select = tk;
                    best_tokenization.push(SymbolizerToken {
                        id: tk.id,
                        span: (tk.start..tk.end).into(),
                    });
                }
            }
        }

        Symbolized { tokens: best_tokenization, errors: Vec::new() }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct MwisSymbolizer;

impl Symbolizer for MwisSymbolizer {
    fn symbolize(&self, dictionary: &dyn dictionary::Dictionary, input: &str) -> Symbolized {
        // List all possible tokenizations of the input string, then sort by earliest end for MWIS
        let mut token_candidates = get_token_candidates(dictionary, input);
        token_candidates.sort_by_key(|x| (x.end, x.start));

        // Under the framework of the weighted interval scheduling problem, find a schedule (non-overlapping token selection) that
        // maximizes the sum of weights described by the tuple:
        // (
        //     W1. Number of covered non-whitespace bytes,
        //     W2. Constant -1,
        //     W3. 1 if bare ID,
        ///    W4. 1 if word,
        ///    W5: 1 if word AND exact case match,
        ///    W6. 1 if numerical digits
        /// )
        // Individual weight components add component-wise along the schedule, and are ordered lexicographically (W1 most significant).

        // A resultant schedule with maximum ("optimal") score has a path weight that:
        // W1. covers as many non-whitespace bytes in the text string as possible
        // W2. with the fewest covering tokens
        // W3. prioritizing bare IDs
        // W4. over potentially case-inexact words
        // W5. over exact words
        // W6. over numbers

        // Iff a resultant schedule's W1 is equal to the total number of non-whitespace bytes in the text string, it is a "valid solution".
        // If the "optimal" schedule is not a "valid solution", the characters that were uncovered should be reported as "unrecognized spans".

        // Any schedules with identical score are "ambiguous" with one another.
        // If there exists more than one "optimal" "valid solution", the set difference of two such solutions should be reported as "ambiguous tokenizations".

        // TODO implement custom Ord for WeightTuple
        //                  +    -    +    +    +    +
        type WeightTuple = (u32, i32, u32, u32, u32, u32);

        fn add(a: &WeightTuple, b: &WeightTuple) -> WeightTuple {
            (a.0 + b.0, a.1 + b.1, a.2 + b.2, a.3 + b.3, a.4 + b.4, a.5 + b.5)
        }

        let mut weights: Vec<WeightTuple> = vec![WeightTuple::default(); token_candidates.len() + 1];
        let mut precedes: Vec<usize> = vec![0; token_candidates.len() + 1];
        let mut optimal: Vec<WeightTuple> = vec![WeightTuple::default(); token_candidates.len() + 1];

        // Note: weights[0]/precedes[0]/optimal[0] are fixed at 0 and can be accessed during search.

        for (i, tk) in token_candidates.iter().enumerate() {
            // FIXME special error for dictionary lookup or overly long word
            // FIXME DO NOT UNWRAP HERE
            weights[i + 1] = (
                dictionary.get(tk.id).unwrap().word.chars().fold(0usize, |c, x| if !x.is_whitespace() { c + x.len_utf8() } else { c } ) as u32,
                -1, // TODO implement custom Ord for WeightTuple
                0,
                1,
                if tk.exact_case { 1 } else { 0 },
                0
            );
            precedes[i + 1] = token_candidates.partition_point(|x| x.end <= tk.start);
        }

        for i in 1..token_candidates.len() + 1 {
            optimal[i] = add(&weights[i], &optimal[precedes[i]]).max(optimal[i - 1])
        }

        // FIXME convert to iterative recursion
        let mut inter_schedule: Vec<usize> = Vec::new();
        let mut final_schedules: Vec<Vec<usize>> = Vec::new();
        fn schedule(j: usize, weights: &Vec<WeightTuple>, precedes: &Vec<usize>, optimal: &Vec<WeightTuple>, final_schedules: &mut Vec<Vec<usize>>, inter_schedule: &mut Vec<usize>) {
            if j == 0 {
                final_schedules.push(inter_schedule.clone());
                return;
            }
            let compare = add(&weights[j], &optimal[precedes[j]]).cmp(&optimal[j - 1]);
            if compare.is_ge() {
                inter_schedule.push(j);
                schedule(precedes[j], weights, precedes, optimal, final_schedules, inter_schedule);
                inter_schedule.pop();
            }
            if compare.is_le() {
                schedule(j - 1, weights, precedes, optimal, final_schedules, inter_schedule);
            }
        }

        schedule(token_candidates.len(), &weights, &precedes, &optimal, &mut final_schedules, &mut inter_schedule);

        // It is guaranteed there will be at least one final schedule
        let s0 = final_schedules[0].iter().rev().map(|j| {
            let tk = token_candidates[*j - 1];
            SymbolizerToken {
                id: tk.id,
                span: (tk.start..tk.end).into(),
            }
        }).collect::<Vec<SymbolizerToken>>();

        let mut errors: Vec<SymbolizerError> = Vec::new();

        let mut end_prev = 0;
        let mut maybe_error_start: Option<usize> = None;
        for tk in s0.iter() {
            for (off, c) in input[end_prev..tk.span.start].char_indices() {
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
                errors.push(SymbolizerError { kind: SymbolizerErrorKind::EntryNotFound, span: (error_start..tk.span.start).into() });
                maybe_error_start = None;
            }
            end_prev = tk.span.end;
        }
        if end_prev < input.len() {
            for (off, c) in input[end_prev..input.len()].char_indices() {
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
                errors.push(SymbolizerError { kind: SymbolizerErrorKind::EntryNotFound, span: (error_start..input.len()).into() });
            }
        }

        if final_schedules.len() > 1 {
            // FIXME think of better way to visit ambiguities.
            let h1 = final_schedules[1].iter().rev().map(|j| {
                let tk = token_candidates[*j - 1];
                SymbolizerToken {
                    id: tk.id,
                    span: (tk.start..tk.end).into(),
                }
            }).collect::<HashSet<SymbolizerToken>>();
            let h0: HashSet<SymbolizerToken> = s0.iter().copied().collect();
            let mut left_difference = h0.difference(&h1).copied().collect::<Vec<SymbolizerToken>>();
            left_difference.sort_by_key(|x| x.span.start);
            let mut right_difference = h1.difference(&h0).copied().collect::<Vec<SymbolizerToken>>();
            right_difference.sort_by_key(|x| x.span.start);
            // FIXME abutted spans should be grouped and reported individually
            errors.push(SymbolizerError { kind: SymbolizerErrorKind::AmbiguousSpan(vec![left_difference, right_difference]), span: (0..0).into() })
        }

        Symbolized { tokens: s0, errors }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct LessAmbiguousSymbolizer;

impl Symbolizer for LessAmbiguousSymbolizer {
    fn symbolize(&self, dictionary: &dyn dictionary::Dictionary, input: &str) -> Symbolized {
        // Less ambiguous parser:
        // At each position in the string, list all dictionary words and digit strings that could potentially start at that point.
        // Find all combinations of word selections that maximally cover the string
        //     If there exists more than one solution, record ambiguous spans (considering special rules below)
        //     (e.g. with a dictionary consisting of x, xx: xxx is ambiguously xx x or x xx)
        // Collect tokens from the maximal word selection and identify any uncovered regions.
        //     Only whitespace may be uncovered. Whitespace cannot be elided in order to form one token.

        // Syntax extensions
        // 1: |<id>/@<id>_UNDEF specifies a bare signal ID.
        // 2: Words may contain '|'/'@' signs.
        // 3. Words may contain whitespace.
        // 4: A dictionary may contain words that are normally different but compare identically after uppercasing.

        // Ambiguity rules
        // 1. On a contest between two covers with different token counts, the one with fewer tokens wins.
        // 2. On a contest between a word and a bare signal ID, bare signal ID wins.
        // 3. On a contest between an exact case match and a partial case match, exact case match wins.
        //    Partial matches resolve into the dictionary entry with the highest (less negative) ID.

        todo!()
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct CReformatterDesymbolizer;

impl Desymbolizer for CReformatterDesymbolizer {
    fn desymbolize(&self, dictionary: &dyn dictionary::Dictionary, input: &[dictionary::SymbolT]) -> Desymbolized {
        let mut maybe_prev_symbol: Option<dictionary::SymbolT> = None;
        let mut maybe_prev_after: Option<SpacingKind> = None;

        let mut text = String::new();
        let mut spans = Vec::with_capacity(input.len());

        for &symbol in input {
            let is_digit = symbol >= 0;
            let def = if is_digit { None } else { dictionary.get(symbol) };

            let format = match (is_digit, def) {
                (true, _) => dictionary::Formatting::DIGIT,
                (false, Some(d)) => d.format,
                (false, None) => dictionary.global_format(),
            };

            if let Some(prev_spacing) = maybe_prev_after {
                let spacing = prev_spacing.max(format.before);
                let _ = write!(text, "{}", spacing.as_str());
            }

            let start = text.len();
            if is_digit {
                let _ = write!(text, "{}", symbol);
            } else if let Some(entry) = def {
                let _ = write!(text, "{}", entry.word.as_str());
            } else {
                let _ = write!(text, "@{}_UNDEF", symbol);
            }
            spans.push((start..text.len()).into());

            let doubled = format.break_on_double && maybe_prev_symbol.is_some_and(|s| s == symbol);
            maybe_prev_symbol = Some(symbol);
            maybe_prev_after = Some(if doubled { SpacingKind::Newline } else { format.after });
        }

        Desymbolized { text, spans }
    }
}

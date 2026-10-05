use crate::dictionary::{self, SpacingKind};

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

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
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
            let def = if is_digit { None } else { dictionary.lookup_id(symbol) };

            let format = match (is_digit, def) {
                (true, _) => dictionary::Formatting::DIGIT,
                (false, Some(d)) => d.format,
                (false, None) => dictionary.global_formatting(),
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

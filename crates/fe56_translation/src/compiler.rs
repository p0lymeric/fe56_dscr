pub mod mwis;
pub mod reformatter;

use crate::dictionary;

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
    // two readings in an ambiguous span
    AmbiguousSpan(Vec<SymbolizerToken>, Vec<SymbolizerToken>),
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

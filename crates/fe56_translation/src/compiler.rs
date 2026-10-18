pub mod mwis;
pub mod reformatter;

use crate::dictionary;

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

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(C)]
pub struct SymbolizerSpan<T> {
    pub start: T,
    pub end: T,
}

impl<T> From<std::ops::Range<T>> for SymbolizerSpan<T> {
    fn from(value: std::ops::Range<T>) -> Self {
        Self {
            start: value.start,
            end: value.end,
        }
    }
}

impl<T> From<std::range::Range<T>> for SymbolizerSpan<T> {
    fn from(value: std::range::Range<T>) -> Self {
        Self {
            start: value.start,
            end: value.end,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(C)]
pub enum SymbolizerTraceKind {
    Token { id: dictionary::SymbolT, },
    TokenBig,
    TokenAmbiguousAlt { id: dictionary::SymbolT, id_alt: dictionary::SymbolT, span_alt: SymbolizerSpan<usize> },
    TokenAmbiguous { id: dictionary::SymbolT },
    Unconsumed,
    Whitespace,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(C)]
pub struct SymbolizerTrace {
    span: SymbolizerSpan<usize>,
    kind: SymbolizerTraceKind,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SymbolizerError {
    pub kind: SymbolizerErrorKind,
    pub span: SymbolizerSpan<usize>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct SymbolizerToken {
    pub id: dictionary::SymbolT,
    pub span: SymbolizerSpan<usize>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Symbolized {
    pub tokens: Vec<SymbolizerToken>,
    pub errors: Vec<SymbolizerError>,
    pub traces: Vec<SymbolizerTrace>,
}

pub trait Symbolizer {
    fn symbolize(&self, dictionary: &dyn dictionary::Dictionary, input: &str) -> Symbolized;
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Desymbolized {
    pub text: String,
    pub spans: Vec<SymbolizerSpan<usize>>,
}

pub trait Desymbolizer {
    fn desymbolize(&self, dictionary: &dyn dictionary::Dictionary, input: &[dictionary::SymbolT]) -> Desymbolized;
}

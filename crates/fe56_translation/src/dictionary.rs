pub mod shadow;
pub mod json;

use crate::case;

use std::range::Range;

// Dictionary interface
// polymeric 2026

// A Dictionary provides storage of definitions (e.g. JSON file and memory),
// interfaces to manage entries,
// and search helpers for implementing signal compilers

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum SpacingKind {
    None = 0,
    Space = 1,
    Newline = 2,
    TwoNewline = 3,
}

impl SpacingKind {
    pub fn as_str(&self) -> &str {
        match self {
            SpacingKind::None => "",
            SpacingKind::Space => " ",
            SpacingKind::Newline => "\n",
            SpacingKind::TwoNewline => "\n\n",
        }
    }
}

impl TryFrom<i64> for SpacingKind {
    type Error = ();

    fn try_from(value: i64) -> Result<Self, Self::Error> {
        match value {
            0 => Ok(SpacingKind::None),
            1 => Ok(SpacingKind::Space),
            2 => Ok(SpacingKind::Newline),
            3 => Ok(SpacingKind::TwoNewline),
            _ => Err(())
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Formatting {
    pub before: SpacingKind,
    pub after: SpacingKind,
    pub break_on_double: bool,
}

impl Formatting {
    pub const DIGIT: Self = Self {
        before: SpacingKind::None,
        after: SpacingKind::None,
        break_on_double: false,
    };

    pub const DEFAULT: Self = Self {
        before: SpacingKind::Space,
        after: SpacingKind::Newline,
        break_on_double: false,
    };
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Definition {
    pub word: String,
    pub desc: String,
    pub format: Formatting,
}

pub type SymbolT = i64;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct TokenizeResult {
    pub id: SymbolT,
    // these are byte offsets, not character offsets!
    pub span: Range<usize>,
    pub exact_case: bool,
}

pub trait Dictionary {
    fn get(&self, id: SymbolT) -> Option<&Definition>;
    fn lookup(&self, word: &str) -> Option<SymbolT>;
    fn lookup_casefold(&self, word: &str) -> &[SymbolT];

    fn global_format(&self) -> Formatting;

    fn iter(&self) -> Box<dyn Iterator<Item = (&SymbolT, &Definition)> + '_>;

    fn casefold(&self, text: &str) -> case::Casefolded;
    fn tokenize_at(&self, text: &str, text_casefold: &case::Casefolded, start: usize) -> Vec<TokenizeResult>;
}

pub trait DictionaryMut {
    fn add(&mut self, id: SymbolT, def: Definition) -> bool;
    fn remove(&mut self, id: SymbolT) -> bool;
    fn relink(&mut self, from: SymbolT, to: SymbolT) -> bool;
    fn edit(&mut self, id: SymbolT, f: &mut dyn FnMut(&mut Definition)) -> bool;

    fn set_global_format(&mut self, formatting: Formatting) -> bool;
}

pub mod json;

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
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Definition {
    pub word: String,
    pub desc: String,
    pub format: Formatting,
}

pub type SymbolT = i64;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct TokenCoords {
    pub id: SymbolT,
    // these are byte offsets, not character offsets!
    pub start: usize,
    pub end: usize,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct TokenizeResult {
    pub coords: TokenCoords,
    pub exact_case: bool,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Casefolded {
    pub text_norm: String,
    pub offsets: Vec<(usize, usize)>,
}

impl Casefolded {
    pub fn to_orig(&self, offset: usize) -> Option<usize> {
       self.offsets.binary_search_by_key(&offset, |(_orig, casefold)| *casefold).ok().map(|i| self.offsets[i].0)
    }
    pub fn to_casefold(&self, offset: usize) -> Option<usize> {
        self.offsets.binary_search_by_key(&offset, |(orig, _casefold)| *orig).ok().map(|i| self.offsets[i].1)
    }
}

pub trait Dictionary {
    fn lookup_id(&self, id: SymbolT) -> Option<&Definition>;
    fn lookup_word(&self, word: &str, case_sensitive: bool) -> &[SymbolT];
    fn global_formatting(&self) -> Formatting;

    fn iter(&self) -> Box<dyn Iterator<Item = (&SymbolT, &Definition)> + '_>;

    fn casefold(&self, text: &str) -> Casefolded;
    fn tokenize_at(&self, text: &str, text_casefold: &Casefolded, start: usize) -> Vec<TokenizeResult>;
}

pub trait DictionaryMut {
    fn add(&mut self, id: SymbolT, def: Definition) -> bool;
    fn remove(&mut self, id: SymbolT) -> bool;
    fn rename(&mut self, from: SymbolT, to: SymbolT) -> bool;
    fn lookup_id_mut(&mut self, id: SymbolT) -> Option<&mut Definition>;

    fn set_global_formatting(&mut self, formatting: Formatting) -> bool;
}

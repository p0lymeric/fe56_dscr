use super::*;

// Compiler implementation that emits translations with words separated by single spaces
// polymeric 2026

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SimpleDesymbolizer;

impl Desymbolizer for SimpleDesymbolizer {
    fn desymbolize(&self, dictionary: &dyn dictionary::Dictionary, input: &[dictionary::SymbolT]) -> Desymbolized {
        let mut first_token = true;

        let mut text = String::new();
        let mut spans = Vec::with_capacity(input.len());

        for &symbol in input {
            let is_digit = symbol >= 0;
            let def = if is_digit { None } else { dictionary.get(symbol) };

            if !first_token {
                let _ = write!(text, " ");
            }
            first_token = false;

            let start = text.len();
            if is_digit {
                let _ = write!(text, "{}", symbol);
            } else if let Some(entry) = def {
                let _ = write!(text, "{}", entry.word.as_str());
            } else {
                let _ = write!(text, "@{}_UNDEF", symbol);
            }
            spans.push((start..text.len()).into());
        }

        Desymbolized { text, spans }
    }
}

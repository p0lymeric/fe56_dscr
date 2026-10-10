use super::*;

// Compiler implementation based off C_Reformatter, which is the compiler used for the majority of the game
// polymeric 2026

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
pub struct CReformatterDesymbolizer;

impl Desymbolizer for CReformatterDesymbolizer {
    fn desymbolize(&self, dictionary: &dyn dictionary::Dictionary, input: &[dictionary::SymbolT]) -> Desymbolized {
        let mut maybe_prev_symbol: Option<dictionary::SymbolT> = None;
        let mut maybe_prev_after: Option<dictionary::SpacingKind> = None;

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
            maybe_prev_after = Some(if doubled { dictionary::SpacingKind::Newline } else { format.after });
        }

        Desymbolized { text, spans }
    }
}

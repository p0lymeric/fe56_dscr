use crate::dictionary::*;
use crate::trie;
use crate::case;

use std::collections::BTreeMap;

// In-memory dictionary implementation
// polymeric 2026

// This implementation is meant to shadow the state of another Dictionary
// in memory, and implements most generic search functionality.

#[derive(Debug, Clone)]
pub struct ShadowDictionary {
    defs: BTreeMap<SymbolT, CasefoldableDefinition>,
    global_format: Formatting,
    trie_exact: trie::RTrie<SymbolT>,
    trie_casefold: trie::RTrie<SymbolT>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
struct CasefoldableDefinition {
    def: Definition,
    word_casefold: String,
}

impl ShadowDictionary {
    pub fn new() -> Self {
        Self {
            defs: BTreeMap::new(),
            global_format: Formatting::DEFAULT,
            trie_exact: trie::RTrie::new(),
            trie_casefold: trie::RTrie::new(),
        }
    }
}

impl Dictionary for ShadowDictionary {
    fn get(&self, id: SymbolT) -> Option<&Definition> {
        self.defs.get(&id).map(|x| &x.def)
    }

    fn lookup(&self, word: &str) -> Option<SymbolT> {
        // TODO need to assert that the dictionary does not contain identical entries
        self.trie_exact.search(word).first().copied()
    }

    fn lookup_casefold(&self, word: &str) -> &[SymbolT] {
        self.trie_casefold.search(&case::to_casefolded_full(word))
    }

    fn global_format(&self) -> Formatting {
        self.global_format
    }

    fn iter(&self) -> Box<dyn Iterator<Item = (&SymbolT, &Definition)> + '_> {
        Box::new(self.defs.iter().map(|(k, v)| (k, &v.def)))
    }

    fn casefold(&self, text: &str) -> case::Casefolded {
        case::Casefolded::new(text)
    }

    fn tokenize_at(&self, text: &str, text_casefold: &case::Casefolded, end: usize, out: &mut Vec<TokenizeResult>) {
        // text is used for verifying exact matches
        let end_casefold = text_casefold.to_casefold(end);
        out.extend(self.trie_casefold.suffixes(&text_casefold.text_norm[..end_casefold]).filter_map(move |result| {
            // edge case: user passes "ßa", it casefolds to "ssa", trie returns a match for "sa",
            // which cannot map into to the 2-character casefolded sequence
            if let Some(start_orig) = text_casefold.to_orig(end_casefold - result.length) {
                let exact_case = self.lookup(&text[start_orig..end]) == Some(result.id);
                Some(TokenizeResult {
                    kind: if exact_case { TokenizeResultKind::WordExact } else { TokenizeResultKind::WordInexact },
                    id: result.id,
                    span: (start_orig..end).into()
                })
            } else {
                None
            }
        }))
    }
}

impl DictionaryMut for ShadowDictionary {
    fn add(&mut self, id: SymbolT, def: Definition) -> bool {
        let word_casefold = case::to_casefolded_full(&def.word);

        use std::collections::btree_map::Entry;
        if let Entry::Vacant(entry) = self.defs.entry(id) {
            let def = entry.insert(CasefoldableDefinition { def, word_casefold });
            self.trie_exact.insert(id, &def.def.word);
            self.trie_casefold.insert(id, &def.word_casefold);

            return true;
        }

        false
    }

    fn remove(&mut self, id: SymbolT) -> bool {
        use std::collections::btree_map::Entry;
        if let Entry::Occupied(entry) = self.defs.entry(id) {
            let def = entry.remove();
            self.trie_exact.delete(id, &def.def.word);
            self.trie_casefold.delete(id, &def.word_casefold);

            return true;
        }

        false
    }

    fn relink(&mut self, from: SymbolT, to: SymbolT) -> bool {
        todo!()
    }

    fn edit(&mut self, id: SymbolT, f: &mut dyn FnMut(&mut Definition)) -> bool {
        todo!()
    }

    fn set_global_format(&mut self, formatting: Formatting) -> bool {
        self.global_format = formatting;
        true
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn make_dict_untoward() -> ShadowDictionary {
        let mut dict = ShadowDictionary::new();
        dict.add(-1, Definition { word: "UNTO".to_string(), desc: "desc".to_string(), format: dict.global_format() });
        dict.add(-2, Definition { word: "WARD".to_string(), desc: "desc".to_string(), format: dict.global_format() });
        dict.add(-3, Definition { word: "UNTOWARD".to_string(), desc: "desc".to_string(), format: dict.global_format() });
        dict
    }

    #[test]
    fn test_add() {
        let _dict = make_dict_untoward();
    }

    #[test]
    fn test_lookups() {
        let dict = make_dict_untoward();

        assert_eq!(dict.get(-1).expect("Can't find -1").word, "UNTO");
        assert_eq!(dict.get(-2).expect("Can't find -2").word, "WARD");
        assert_eq!(dict.get(-3).expect("Can't find -3").word, "UNTOWARD");

        assert_eq!(dict.lookup_casefold("uNTO"), &[-1]);
        assert_eq!(dict.lookup("uNTO"), None);
        assert_eq!(dict.lookup_casefold("WARD"), &[-2]);
        assert_eq!(dict.lookup("UNTOWARD"), Some(-3));
    }

    fn assert_eq_tokenize_at(dictionary: &dyn Dictionary, text: &str, text_casefold: &case::Casefolded, end: usize, lhs: &[TokenizeResult]) {
        let mut out = Vec::new();
        dictionary.tokenize_at(text, text_casefold, end, &mut out);
        assert_eq!(out, lhs);
    }

    #[test]
    fn test_tokenize_at_simple() {
        let dict = make_dict_untoward();

        let untoward_case_folded = dict.casefold("untoward");

        assert_eq_tokenize_at(&dict, "UNTOWARD", &untoward_case_folded, 0, &[] as &[TokenizeResult]);

        assert_eq_tokenize_at(&dict, "UNTOWARD", &untoward_case_folded, 1, &[] as &[TokenizeResult]);

        assert_eq_tokenize_at(&dict, "UNTOWARD", &untoward_case_folded, 4, &[
            TokenizeResult { kind: TokenizeResultKind::WordExact, id: -1, span: (0..4).into() },
        ]);
        assert_eq_tokenize_at(&dict, "UNTOWARD", &untoward_case_folded, 8, &[
            TokenizeResult { kind: TokenizeResultKind::WordExact, id: -2, span: (4..8).into() },
            TokenizeResult { kind: TokenizeResultKind::WordExact, id: -3, span: (0..8).into() },
        ]);

        assert_eq_tokenize_at(&dict, "UNTOWARD", &untoward_case_folded, 5, &[] as &[TokenizeResult]);

        assert_eq_tokenize_at(&dict, "untoWARD", &untoward_case_folded, 0, &[] as &[TokenizeResult]);

        assert_eq_tokenize_at(&dict, "untoWARD", &untoward_case_folded, 4, &[
            TokenizeResult { kind: TokenizeResultKind::WordInexact, id: -1, span: (0..4).into() },
        ]);

        assert_eq_tokenize_at(&dict, "untoWARD", &untoward_case_folded, 8, &[
            TokenizeResult { kind: TokenizeResultKind::WordExact, id: -2, span: (4..8).into() },
            TokenizeResult { kind: TokenizeResultKind::WordInexact, id: -3, span: (0..8).into() },
        ]);
    }

    #[test]
    fn test_tokenize_at_full() {
        let mut dict = ShadowDictionary::new();
        dict.add(-1, Definition { word: "fußy".to_string(), desc: "desc".to_string(), format: dict.global_format() });
        dict.add(-2, Definition { word: "FUSSY".to_string(), desc: "desc".to_string(), format: dict.global_format() });
        dict.add(-3, Definition { word: "fus".to_string(), desc: "desc".to_string(), format: dict.global_format() });

        let sus = "FUSSY fußy fus sus 🫪 susSy ß";
        let sus_case_folded = dict.casefold(sus);

        assert_eq_tokenize_at(&dict, sus, &sus_case_folded, 3, &[
            TokenizeResult { kind: TokenizeResultKind::WordInexact, id: -3, span: (0..3).into() },
        ]);

        assert_eq_tokenize_at(&dict, sus, &sus_case_folded, 5, &[
            TokenizeResult { kind: TokenizeResultKind::WordInexact, id: -1, span: (0..5).into() },
            TokenizeResult { kind: TokenizeResultKind::WordExact, id: -2, span: (0..5).into() },
        ]);

        // TODO more predictions
    }
}

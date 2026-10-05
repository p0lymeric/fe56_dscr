use crate::dictionary::*;
use crate::trie;
use crate::case;

use std::collections::BTreeMap;
use std::collections::HashMap;

// In-memory dictionary implementation
// polymeric 2026

// This implementation is meant to shadow the state of another Dictionary
// in memory, and implements most generic search functionality.

#[derive(Debug, Clone)]
pub struct ShadowDictionary {
    defs: BTreeMap<SymbolT, CasefoldableDefinition>,
    global_formatting: Formatting,
    revmap_exact: HashMap<String, Vec<SymbolT>>,
    revmap_casefold: HashMap<String, Vec<SymbolT>>,
    trie_casefold: trie::Trie<SymbolT>,
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
            global_formatting: Formatting::DEFAULT,
            revmap_exact: HashMap::new(),
            revmap_casefold: HashMap::new(),
            trie_casefold: trie::Trie::new(),
        }
    }
}

impl Dictionary for ShadowDictionary {
    fn lookup_id(&self, id: SymbolT) -> Option<&Definition> {
        self.defs.get(&id).map(|x| &x.def)
    }

    fn lookup_word(&self, word: &str, case_sensitive: bool) -> &[SymbolT] {
        if case_sensitive {
            self.revmap_exact.get(word).map(Vec::as_slice).unwrap_or(&[])
        } else {
            self.revmap_casefold.get(&case::to_casefolded_full(word)).map(Vec::as_slice).unwrap_or(&[])
        }
    }

    fn global_formatting(&self) -> Formatting {
        self.global_formatting
    }

    fn iter(&self) -> Box<dyn Iterator<Item = (&SymbolT, &Definition)> + '_> {
        Box::new(self.defs.iter().map(|(k, v)| (k, &v.def)))
    }

    fn casefold(&self, text: &str) -> case::Casefolded {
        case::Casefolded::new(text)
    }

    fn tokenize_at(&self, text: &str, text_casefold: &case::Casefolded, start: usize) -> Vec<TokenizeResult> {
        // text is used for verifying exact matches
        if let Some(start_casefold) = text_casefold.to_casefold(start) {
            self.trie_casefold.tokenize_at(&text_casefold.text_norm, start_casefold).iter().filter_map(|&coords| {
                if let Some(end_orig) = text_casefold.to_orig(coords.end) {
                    let exact_case = if let Some(def) = self.defs.get(&coords.id) {
                        text[start..end_orig] == def.def.word
                    } else {
                        false
                    };
                    Some(TokenizeResult {
                        coords: TokenCoords {
                            id: coords.id,
                            start,
                            end: end_orig,
                        },
                        exact_case
                    })
                } else { None }

            }).collect()
        } else {
            Vec::new()
        }
    }

}

impl DictionaryMut for ShadowDictionary {
    fn add(&mut self, id: SymbolT, def: Definition) -> bool {
        let word_casefold = case::to_casefolded_full(&def.word);

        if let std::collections::btree_map::Entry::Vacant(entry) = self.defs.entry(id) {
            let def = entry.insert(CasefoldableDefinition { def, word_casefold });
            self.revmap_exact.entry(def.def.word.clone()).or_default().push(id);
            self.revmap_casefold.entry(def.word_casefold.clone()).or_default().push(id);
            self.trie_casefold.insert(id, &def.word_casefold);

            return true;
        }

        false
    }

    fn remove(&mut self, id: SymbolT) -> bool {
        todo!()
    }

    fn rename(&mut self, from: SymbolT, to: SymbolT) -> bool {
        todo!()
    }

    fn lookup_id_mut(&mut self, id: SymbolT) -> Option<&mut Definition> {
        todo!()
    }

    fn set_global_formatting(&mut self, formatting: Formatting) -> bool {
        self.global_formatting = formatting;
        true
    }
}

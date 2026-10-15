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
    trie_exact: trie::Trie<SymbolT>,
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
            global_format: Formatting::DEFAULT,
            trie_exact: trie::Trie::new(),
            trie_casefold: trie::Trie::new(),
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

    fn tokenize_at(&self, text: &str, text_casefold: &case::Casefolded, start: usize, out: &mut Vec<TokenizeResult>) {
        // text is used for verifying exact matches
        let start_casefold = text_casefold.to_casefold(start);
        out.extend(self.trie_casefold.prefixes(&text_casefold.text_norm[start_casefold..]).map(move |prefix| {
            let end_orig = text_casefold.to_orig(start_casefold + prefix.length);
            let exact_case = self.lookup(&text[start..end_orig]) == Some(prefix.id);
            TokenizeResult {
                kind: if exact_case { TokenizeResultKind::WordExact } else { TokenizeResultKind::WordInexact },
                id: prefix.id,
                span: (start..end_orig).into()
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
        todo!()
    }

    fn relink(&mut self, from: SymbolT, to: SymbolT) -> bool {
        todo!()
    }

    fn edit(&mut self, id: SymbolT, f: &mut dyn FnMut(&mut Definition)) -> bool {
        // if let Some(def) = self.defs.get_mut(&id) {
        //     f(&mut def.def);
        //     // TODO modify in trie
        //     def.word_casefold = case::to_casefolded_full(&def.def.word);
        //     return true;
        // }

        // false
        todo!()
    }

    fn set_global_format(&mut self, formatting: Formatting) -> bool {
        self.global_format = formatting;
        true
    }
}

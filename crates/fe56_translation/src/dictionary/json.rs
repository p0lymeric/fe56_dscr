use crate::dictionary::*;
use crate::trie;

use std::collections::BTreeMap;
use std::collections::HashMap;

use serde_json;

#[derive(Debug, Clone)]
pub struct JsonDictionary {
    json: serde_json::Value,
    lexicon: LexiconCompartment,
    trie_casefold: trie::Trie,
}

#[derive(Debug, Clone, PartialEq, Eq)]
struct CasefoldableDefinition {
    def: Definition,
    word_casefold: String,
}

#[derive(Debug, Clone)]
pub struct LexiconCompartment {
    defs: BTreeMap<SymbolT, CasefoldableDefinition>,
    revmap_exact: HashMap<String, Vec<SymbolT>>,
    revmap_casefold: HashMap<String, Vec<SymbolT>>,
    global_formatting: Formatting,
}

#[derive(Debug)]
pub enum JsonDictionaryError {
    SerdeJsonError(serde_json::error::Error),
}

fn to_uppercase_simple(s: &str) -> String {
    // Length-preserving, locale-independent uppercasing
    // using Unicode's Simple_Uppercase_Mapping table (TR44)
    s.chars().map(|x| {
        fe56_ucd_sys::UPPERCASE_SIMPLE.binary_search_by_key(&x, |(k, _v)| *k).ok().map(|i| fe56_ucd_sys::UPPERCASE_SIMPLE[i].1).unwrap_or(x)
    }).collect()
}

fn to_casefolded_full(s: &str) -> String {
    // Length-changing, locale-independent case folding
    // using Unicode's Case_Folding (C + F) table (TR44)
    let mut result = String::with_capacity(s.len());
    for x in s.chars() {
        match fe56_ucd_sys::CASEFOLD_FULL.binary_search_by_key(&x, |(k, _v)| *k) {
            Ok(i) => result.push_str(fe56_ucd_sys::CASEFOLD_FULL[i].1),
            _ => result.push(x)
        }
    }
    result
}

fn to_casefolded_full_offset(s: &str) -> Casefolded {
    // Length-changing, locale-independent case folding
    // using Unicode's Case_Folding (C + F) table (TR44)
    let mut text_norm = String::with_capacity(s.len());
    let mut offsets = Vec::with_capacity(s.len() + 1);
    for (orig, x) in s.char_indices() {
        offsets.push((orig, text_norm.len()));
        match fe56_ucd_sys::CASEFOLD_FULL.binary_search_by_key(&x, |(k, _v)| *k) {
            Ok(i) => text_norm.push_str(fe56_ucd_sys::CASEFOLD_FULL[i].1),
            _ => text_norm.push(x)
        }
    }
    offsets.push((s.len(), text_norm.len()));
    Casefolded { text_norm, offsets }
}

impl JsonDictionary {
    pub fn new(data: Option<&str>) -> Result<Self, JsonDictionaryError> {
        let json = if let Some(data_val) = data {
            serde_json::from_str(data_val).map_err(|e| JsonDictionaryError::SerdeJsonError(e))?
        } else {
            serde_json::json!(
                {
                    "wordDict": {
                        "keys": [],
                        "values": []
                    },
                    "descDict": {
                        "keys": [],
                        "values": []
                    },
                    "id": 1,
                    "beforeUserDefaultMode": 1,
                    "afterUserDefaultMode": 2
                }
            )
        };

        let lexicon = Self::init_lexicon(&json)?;
        let trie = Self::init_trie(&lexicon)?;

        Ok(Self {
            json,
            lexicon,
            trie_casefold: trie,
        })
    }

    fn init_lexicon(json: &serde_json::Value) -> Result<LexiconCompartment, JsonDictionaryError> {
        // Currently doesn't error but instead tries to parse as much of the file as possible
        let mut defs: BTreeMap<SymbolT, CasefoldableDefinition> = BTreeMap::new();

        let before = json["beforeUserDefaultMode"]
            .as_i64()
            .and_then(|m| SpacingKind::try_from(m).ok())
            .unwrap_or(SpacingKind::Space);
        let after = json["afterUserDefaultMode"]
            .as_i64()
            .and_then(|m| SpacingKind::try_from(m).ok())
            .unwrap_or(SpacingKind::Newline);
        let global_formatting = Formatting { before, after, break_on_double: false };

        let word_dict = &json["wordDict"];
        let desc_dict = &json["descDict"];

        // Ignore any descDict entries not in wordDict
        if let Some(word_keys) = word_dict["keys"].as_array() && let Some(word_values) = word_dict["values"].as_array() {
            let desc_map: HashMap<SymbolT, &serde_json::Value> = if let Some(desc_keys) = desc_dict["keys"].as_array() && let Some(desc_values) = desc_dict["values"].as_array() {
                desc_keys.iter().zip(desc_values).filter_map(|(k, v)| Some((k.as_i64()?, v))).collect()
            } else {
                HashMap::new()
            };

            for (maybe_id, maybe_word) in word_keys.iter().zip(word_values) {
                if let Some(id) = maybe_id.as_i64() && let Some(word) = maybe_word.as_str() {
                    let def = if let Some(desc) = desc_map.get(&id) {
                        Definition {
                            word: word.to_string(),
                            desc: desc["desc"].as_str().unwrap_or("??? ADD NOTES HERE ???").to_string(),
                            format: Formatting {
                                before: desc["formatMode"].as_i64().and_then(|m| SpacingKind::try_from(m).ok()).unwrap_or(global_formatting.before),
                                after: desc["formatModeAfter"].as_i64().and_then(|m| SpacingKind::try_from(m).ok()).unwrap_or(global_formatting.after),
                                break_on_double: desc["breakOnDouble"].as_bool().unwrap_or(global_formatting.break_on_double)
                            }
                        }
                    } else {
                        Definition { word: word.to_string(), desc: "??? ADD NOTES HERE ???".into(), format: global_formatting }
                    };
                    defs.insert(id, CasefoldableDefinition { def, word_casefold: to_casefolded_full(word) });
                }
            }

        }

        let mut revmap_exact: HashMap<String, Vec<SymbolT>> = HashMap::new();
        let mut revmap_casefold: HashMap<String, Vec<SymbolT>> = HashMap::new();
        for(&id, def) in &defs {
            revmap_exact.entry(def.def.word.clone()).or_default().push(id);
            revmap_casefold.entry(def.word_casefold.clone()).or_default().push(id);
        }

        Ok(LexiconCompartment { defs, revmap_exact, revmap_casefold, global_formatting })
    }

    fn init_trie(lexicon: &LexiconCompartment) -> Result<trie::Trie, JsonDictionaryError> {
        let mut result = trie::Trie::new();

        for (id, def) in &lexicon.defs {
            // TODO verify the BTreeMap's natural ID ordering is not the reverse expected by
            // result.insert to prevent needing to shift the entire vector by 1 on each insert
            result.insert(*id, &def.word_casefold);
        }

        Ok(result)
    }
}

impl Dictionary for JsonDictionary {
    fn lookup_id(&self, id: SymbolT) -> Option<&Definition> {
        self.lexicon.defs.get(&id).map(|x| &x.def)
    }

    fn lookup_word(&self, word: &str, case_sensitive: bool) -> &[SymbolT] {
        if case_sensitive {
            self.lexicon.revmap_exact.get(word).map(Vec::as_slice).unwrap_or(&[])
        } else {
            self.lexicon.revmap_casefold.get(&to_casefolded_full(word)).map(Vec::as_slice).unwrap_or(&[])
        }
    }

    fn global_formatting(&self) -> Formatting {
        self.lexicon.global_formatting
    }

    fn iter(&self) -> Box<dyn Iterator<Item = (&SymbolT, &Definition)> + '_> {
        Box::new(self.lexicon.defs.iter().map(|(k, v)| (k, &v.def)))
    }

    fn casefold(&self, text: &str) -> Casefolded {
        to_casefolded_full_offset(text)
    }

    fn tokenize_at(&self, text: &str, text_casefold: &Casefolded, start: usize) -> Vec<TokenizeResult> {
        // text is used for verifying exact matches
        if let Some(start_casefold) = text_casefold.to_casefold(start) {
            self.trie_casefold.tokenize_at(&text_casefold.text_norm, start_casefold).iter().filter_map(|&coords| {
                if let Some(end_orig) = text_casefold.to_orig(coords.end) {
                    let exact_case = if let Some(def) = self.lexicon.defs.get(&coords.id) {
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
impl DictionaryMut for JsonDictionary {
    fn add(&mut self, id: SymbolT, def: Definition) -> bool {
        let word_casefold = to_casefolded_full(&def.word);

        if let std::collections::btree_map::Entry::Vacant(entry) = self.lexicon.defs.entry(id) {
            let def = entry.insert(CasefoldableDefinition { def, word_casefold });
            self.lexicon.revmap_exact.entry(def.def.word.clone()).or_default().push(id);
            self.lexicon.revmap_casefold.entry(def.word_casefold.clone()).or_default().push(id);
            self.trie_casefold.insert(id, &def.word_casefold);

            // TODO update JSON view

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
        todo!()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_new_none() {
        JsonDictionary::new(None).expect("Failed to construct empty Dictionary");
    }

    // fn test_new_some() {
    //     JsonDictionary::new(Some())
    // }

    fn make_dict_untoward() -> JsonDictionary {
        let mut dict = JsonDictionary::new(None).expect("Failed to construct empty Dictionary");
        dict.add(-1, Definition { word: "UNTO".to_string(), desc: "desc".to_string(), format: dict.global_formatting() });
        dict.add(-2, Definition { word: "WARD".to_string(), desc: "desc".to_string(), format: dict.global_formatting() });
        dict.add(-3, Definition { word: "UNTOWARD".to_string(), desc: "desc".to_string(), format: dict.global_formatting() });
        dict
    }

    #[test]
    fn test_add() {
        let _dict = make_dict_untoward();
        // panic!("{:?}", _dict);
    }

    #[test]
    fn test_lookups() {
        let dict = make_dict_untoward();

        assert_eq!(dict.lookup_id(-1).expect("Can't find -1").word, "UNTO");
        assert_eq!(dict.lookup_id(-2).expect("Can't find -2").word, "WARD");
        assert_eq!(dict.lookup_id(-3).expect("Can't find -3").word, "UNTOWARD");

        assert_eq!(dict.lookup_word("uNTO", false), &[-1]);
        assert_eq!(dict.lookup_word("uNTO", true), &[] as &[SymbolT]);
        assert_eq!(dict.lookup_word("WARD", false), &[-2]);
        assert_eq!(dict.lookup_word("UNTOWARD", true), &[-3]);
    }

    #[test]
    fn test_tokenize_at_simple() {
        let dict = make_dict_untoward();

        let untoward_case_folded = dict.casefold("untoward");

        assert_eq!(dict.tokenize_at("UNTOWARD", &untoward_case_folded, 0), &[TokenizeResult { coords: TokenCoords { id: -1, start: 0, end: 4 }, exact_case: true }, TokenizeResult { coords: TokenCoords { id: -3, start: 0, end: 8 }, exact_case: true }]);
        assert_eq!(dict.tokenize_at("UNTOWARD", &untoward_case_folded, 1), &[] as &[TokenizeResult]);
        assert_eq!(dict.tokenize_at("UNTOWARD", &untoward_case_folded, 4), &[TokenizeResult { coords: TokenCoords { id: -2, start: 4, end: 8 }, exact_case: true }]);
        assert_eq!(dict.tokenize_at("UNTOWARD", &untoward_case_folded, 5), &[] as &[TokenizeResult]);

        // panic!("{:?}", dict.tokenize_at("untoWARD", Some(&untoward_case_folded), 0));
        assert_eq!(dict.tokenize_at("untoWARD", &untoward_case_folded, 0), &[TokenizeResult { coords: TokenCoords { id: -1, start: 0, end: 4 }, exact_case: false }, TokenizeResult { coords: TokenCoords { id: -3, start: 0, end: 8 }, exact_case: false }]);
        assert_eq!(dict.tokenize_at("untoWARD", &untoward_case_folded, 4), &[TokenizeResult { coords: TokenCoords { id: -2, start: 4, end: 8 }, exact_case: true }]);
    }

    #[test]
    fn test_tokenize_at_full() {
        let mut dict = JsonDictionary::new(None).expect("Failed to construct empty Dictionary");
        dict.add(-1, Definition { word: "fußy".to_string(), desc: "desc".to_string(), format: dict.global_formatting() });
        dict.add(-2, Definition { word: "FUSSY".to_string(), desc: "desc".to_string(), format: dict.global_formatting() });
        dict.add(-3, Definition { word: "fus".to_string(), desc: "desc".to_string(), format: dict.global_formatting() });

        let sus = "FUSSY fußy fus sus 🫪 susSy ß";
        let sus_case_folded = dict.casefold(sus);

        // panic!("{:?}", dict.tokenize_at(sus, &sus_case_folded, 0));
        assert_eq!(dict.tokenize_at(sus, &sus_case_folded, 0), &[TokenizeResult { coords: TokenCoords { id: -3, start: 0, end: 3 }, exact_case: false }, TokenizeResult { coords: TokenCoords { id: -2, start: 0, end: 5 }, exact_case: true }, TokenizeResult { coords: TokenCoords { id: -1, start: 0, end: 5 }, exact_case: false }]);
        // TODO more predictions
    }
}

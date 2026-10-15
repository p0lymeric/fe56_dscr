use crate::dictionary::*;
use crate::case;

use std::collections::HashMap;

use serde_json;

// JSON dictionary
// polymeric 2026

// Implementation of a dictionary as stored by TMfDS.

#[derive(Debug, Clone)]
pub struct JsonDictionary {
    json: serde_json::Value,
    shadow: shadow::ShadowDictionary,
}

#[derive(Debug)]
pub enum JsonDictionaryError {
    SerdeJsonError(serde_json::error::Error),
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

        let shadow = Self::init_shadow(&json)?;

        Ok(Self {
            json,
            shadow,
        })
    }

    fn init_shadow(json: &serde_json::Value) -> Result<shadow::ShadowDictionary, JsonDictionaryError> {
        let mut shadow = shadow::ShadowDictionary::new();

        let before = json["beforeUserDefaultMode"]
            .as_i64()
            .and_then(|m| SpacingKind::try_from(m).ok())
            .unwrap_or(SpacingKind::Space);
        let after = json["afterUserDefaultMode"]
            .as_i64()
            .and_then(|m| SpacingKind::try_from(m).ok())
            .unwrap_or(SpacingKind::Newline);
        let global_formatting = Formatting { before, after, break_on_double: false };
        shadow.set_global_format(global_formatting);

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
                    shadow.add(id, def);
                }
            }

        }

        Ok(shadow)
    }
}

impl Dictionary for JsonDictionary {
    fn get(&self, id: SymbolT) -> Option<&Definition> {
        self.shadow.get(id)
    }

    fn lookup(&self, word: &str) -> Option<SymbolT> {
        self.shadow.lookup(word)
    }

    fn lookup_casefold(&self, word: &str) -> &[SymbolT] {
        self.shadow.lookup_casefold(word)
    }

    fn global_format(&self) -> Formatting {
        self.shadow.global_format()
    }

    fn iter(&self) -> Box<dyn Iterator<Item = (&SymbolT, &Definition)> + '_> {
        self.shadow.iter()
    }

    fn casefold(&self, text: &str) -> case::Casefolded {
        self.shadow.casefold(text)
    }

    fn tokenize_at(&self, text: &str, text_casefold: &case::Casefolded, start: usize, out: &mut Vec<TokenizeResult>) {
        self.shadow.tokenize_at(text, text_casefold, start, out)
    }

}

impl DictionaryMut for JsonDictionary {
    fn add(&mut self, id: SymbolT, def: Definition) -> bool {
        if self.shadow.add(id, def) {
            // TODO actually implement this, commenting to allow shadow to be tested
            // todo!();
            return true;
        }
        false
    }

    fn remove(&mut self, id: SymbolT) -> bool {
        if self.shadow.remove(id) {
            todo!();
            return true;
        }
        false
    }

    fn relink(&mut self, from: SymbolT, to: SymbolT) -> bool {
        if self.shadow.relink(from, to) {
            todo!();
            return true;
        }
        false
    }

    fn edit(&mut self, id: SymbolT, f: &mut dyn FnMut(&mut Definition)) -> bool {
        // FIXME need to make this flush to the JSON file
        todo!()
    }

    fn set_global_format(&mut self, formatting: Formatting) -> bool {
        if self.shadow.set_global_format(formatting) {
            todo!();
            return true;
        }
        false
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
        dict.add(-1, Definition { word: "UNTO".to_string(), desc: "desc".to_string(), format: dict.global_format() });
        dict.add(-2, Definition { word: "WARD".to_string(), desc: "desc".to_string(), format: dict.global_format() });
        dict.add(-3, Definition { word: "UNTOWARD".to_string(), desc: "desc".to_string(), format: dict.global_format() });
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

        assert_eq!(dict.get(-1).expect("Can't find -1").word, "UNTO");
        assert_eq!(dict.get(-2).expect("Can't find -2").word, "WARD");
        assert_eq!(dict.get(-3).expect("Can't find -3").word, "UNTOWARD");

        assert_eq!(dict.lookup_casefold("uNTO"), &[-1]);
        assert_eq!(dict.lookup("uNTO"), None);
        assert_eq!(dict.lookup_casefold("WARD"), &[-2]);
        assert_eq!(dict.lookup("UNTOWARD"), Some(-3));
    }

    fn assert_eq_tokenize_at(dictionary: &dyn Dictionary, text: &str, text_casefold: &case::Casefolded, start: usize, lhs: &[TokenizeResult]) {
        let mut out = Vec::new();
        dictionary.tokenize_at(text, text_casefold, start, &mut out);
        assert_eq!(out, lhs);
    }

    #[test]
    fn test_tokenize_at_simple() {
        let dict = make_dict_untoward();

        let untoward_case_folded = dict.casefold("untoward");

        assert_eq_tokenize_at(&dict, "UNTOWARD", &untoward_case_folded, 0, &[TokenizeResult { kind: TokenizeResultKind::WordExact, id: -1, span: (0..4).into() }, TokenizeResult { kind: TokenizeResultKind::WordExact, id: -3, span: (0..8).into() }]);
        assert_eq_tokenize_at(&dict, "UNTOWARD", &untoward_case_folded, 1, &[] as &[TokenizeResult]);
        assert_eq_tokenize_at(&dict, "UNTOWARD", &untoward_case_folded, 4, &[TokenizeResult { kind: TokenizeResultKind::WordExact, id: -2, span: (4..8).into() }]);
        assert_eq_tokenize_at(&dict, "UNTOWARD", &untoward_case_folded, 5, &[] as &[TokenizeResult]);

        // panic!("{:?}", dict.tokenize_at("untoWARD", Some(&untoward_case_folded), 0));
        assert_eq_tokenize_at(&dict, "untoWARD", &untoward_case_folded, 0, &[TokenizeResult { kind: TokenizeResultKind::WordInexact, id: -1, span: (0..4).into() }, TokenizeResult { kind: TokenizeResultKind::WordInexact, id: -3, span: (0..8).into() }]);
        assert_eq_tokenize_at(&dict, "untoWARD", &untoward_case_folded, 4, &[TokenizeResult { kind: TokenizeResultKind::WordExact, id: -2, span: (4..8).into() }]);
    }

    #[test]
    fn test_tokenize_at_full() {
        let mut dict = JsonDictionary::new(None).expect("Failed to construct empty Dictionary");
        dict.add(-1, Definition { word: "fußy".to_string(), desc: "desc".to_string(), format: dict.global_format() });
        dict.add(-2, Definition { word: "FUSSY".to_string(), desc: "desc".to_string(), format: dict.global_format() });
        dict.add(-3, Definition { word: "fus".to_string(), desc: "desc".to_string(), format: dict.global_format() });

        let sus = "FUSSY fußy fus sus 🫪 susSy ß";
        let sus_case_folded = dict.casefold(sus);

        // panic!("{:?}", dict.tokenize_at(sus, &sus_case_folded, 0));
        assert_eq_tokenize_at(&dict, sus, &sus_case_folded, 0, &[TokenizeResult { kind: TokenizeResultKind::WordInexact, id: -3, span: (0..3).into() }, TokenizeResult { kind: TokenizeResultKind::WordExact, id: -2, span: (0..5).into() }, TokenizeResult { kind: TokenizeResultKind::WordInexact, id: -1, span: (0..5).into() }]);
        // TODO more predictions
    }
}

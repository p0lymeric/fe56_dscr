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
        shadow.set_global_formatting(global_formatting);

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
    fn lookup_id(&self, id: SymbolT) -> Option<&Definition> {
        self.shadow.lookup_id(id)
    }

    fn lookup_word(&self, word: &str, case_sensitive: bool) -> &[SymbolT] {
        self.shadow.lookup_word(word, case_sensitive)
    }

    fn global_formatting(&self) -> Formatting {
        self.shadow.global_formatting()
    }

    fn iter(&self) -> Box<dyn Iterator<Item = (&SymbolT, &Definition)> + '_> {
        self.shadow.iter()
    }

    fn casefold(&self, text: &str) -> case::Casefolded {
        self.shadow.casefold(text)
    }

    fn tokenize_at(&self, text: &str, text_casefold: &case::Casefolded, start: usize) -> Vec<TokenizeResult> {
        self.shadow.tokenize_at(text, text_casefold, start)
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

    fn rename(&mut self, from: SymbolT, to: SymbolT) -> bool {
        if self.shadow.rename(from, to) {
            todo!();
            return true;
        }
        false
    }

    fn lookup_id_mut(&mut self, id: SymbolT) -> Option<&mut Definition> {
        // FIXME need to make this flush to the JSON file
        todo!()
    }

    fn set_global_formatting(&mut self, formatting: Formatting) -> bool {
        if self.shadow.set_global_formatting(formatting) {
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

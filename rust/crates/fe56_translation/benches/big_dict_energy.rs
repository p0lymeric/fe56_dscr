use fe56_translation::compiler::Symbolizer;
use fe56_translation::compiler::mwis::MwisSymbolizer;
use fe56_translation::dictionary;
use fe56_translation::dictionary::Dictionary;
use fe56_translation::dictionary::DictionaryMut;
use fe56_translation::dictionary::shadow::ShadowDictionary;

use std::collections::HashMap;
use std::hint::black_box;
use std::io::BufRead;
use std::path::PathBuf;

use criterion::criterion_group;
use criterion::criterion_main;
use criterion::Criterion;

fn from_json(json: &serde_json::Value) -> ShadowDictionary {
    let mut shadow = ShadowDictionary::new();

    let before = json["beforeUserDefaultMode"]
        .as_i64()
        .and_then(|m| dictionary::SpacingKind::try_from(m).ok())
        .unwrap_or(dictionary::SpacingKind::Space);
    let after = json["afterUserDefaultMode"]
        .as_i64()
        .and_then(|m| dictionary::SpacingKind::try_from(m).ok())
        .unwrap_or(dictionary::SpacingKind::Newline);
    let global_formatting = dictionary::Formatting { before, after, break_on_double: false };
    shadow.set_global_format(global_formatting);

    let word_dict = &json["wordDict"];
    let desc_dict = &json["descDict"];

    // Ignore any descDict entries not in wordDict
    if let Some(word_keys) = word_dict["keys"].as_array() && let Some(word_values) = word_dict["values"].as_array() {
        let desc_map: HashMap<dictionary::SymbolT, &serde_json::Value> = if let Some(desc_keys) = desc_dict["keys"].as_array() && let Some(desc_values) = desc_dict["values"].as_array() {
            desc_keys.iter().zip(desc_values).filter_map(|(k, v)| Some((k.as_i64()?, v))).collect()
        } else {
            HashMap::new()
        };

        for (maybe_id, maybe_word) in word_keys.iter().zip(word_values) {
            if let Some(id) = maybe_id.as_i64() && let Some(word) = maybe_word.as_str() {
                let def = if let Some(desc) = desc_map.get(&id) {
                    dictionary::Definition {
                        word: word.to_string(),
                        desc: desc["desc"].as_str().unwrap_or("??? ADD NOTES HERE ???").to_string(),
                        format: dictionary::Formatting {
                            before: desc["formatMode"].as_i64().and_then(|m| dictionary::SpacingKind::try_from(m).ok()).unwrap_or(global_formatting.before),
                            after: desc["formatModeAfter"].as_i64().and_then(|m| dictionary::SpacingKind::try_from(m).ok()).unwrap_or(global_formatting.after),
                            break_on_double: desc["breakOnDouble"].as_bool().unwrap_or(global_formatting.break_on_double)
                        }
                    }
                } else {
                    dictionary::Definition { word: word.to_string(), desc: "??? ADD NOTES HERE ???".into(), format: global_formatting }
                };
                shadow.add(id, def);
            }
        }

    }

    shadow
}

fn criterion_benchmark(c: &mut Criterion) {
    let dict_contents = std::fs::read_to_string(PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("tests/artifacts/DICTIONARY-1.save")).unwrap();
    let mut dictionary = from_json(&serde_json::from_str(&dict_contents).unwrap());

    let esdb_src = std::fs::File::open(PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("tests/artifacts/third_party/esdb.txt")).unwrap();
    let esdb_rd = std::io::BufReader::new(esdb_src);
    let mut in_preamble = true;
    let mut id = -1000;
    for line in esdb_rd.lines() {
        let lineu = &line.unwrap();
        let linet = lineu.trim();
        if in_preamble {
            if linet == "---" {
                in_preamble = false;
            }
            continue;
        }
        let line_uppercase = fe56_translation::case::to_uppercase_simple(linet);
        if dictionary.lookup(&line_uppercase) == None {
            dictionary.add(id, dictionary::Definition { word: line_uppercase, desc: "".to_string(), format: dictionary::Formatting::DEFAULT });
            id -= 1;
        }
    }
    let pride_and_prejudice = std::fs::read_to_string(PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("tests/artifacts/third_party/pride_and_prejudice.txt")).unwrap();
    c.bench_function("casefold pride and prejudice", |b| b.iter(|| dictionary.casefold(black_box(&pride_and_prejudice))));
    c.bench_function("tokenize pride and prejudice", |b| b.iter(|| MwisSymbolizer.symbolize(black_box(&dictionary), black_box(&pride_and_prejudice))));

    let a_pickle_for_the_knowing_ones = std::fs::read_to_string(PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("tests/artifacts/third_party/a_pickle_for_the_knowing_ones.txt")).unwrap();
    c.bench_function("casefold a pickle for the knowing ones", |b| b.iter(|| dictionary.casefold(black_box(&a_pickle_for_the_knowing_ones))));
    c.bench_function("tokenize a pickle for the knowing ones", |b| b.iter(|| MwisSymbolizer.symbolize(black_box(&dictionary), black_box(&a_pickle_for_the_knowing_ones))));

    let human_second = std::fs::read_to_string(PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("tests/artifacts/human_second.txt")).unwrap();
    c.bench_function("casefold human second", |b| b.iter(|| dictionary.casefold(black_box(&human_second))));
    c.bench_function("tokenize human second", |b| b.iter(|| MwisSymbolizer.symbolize(black_box(&dictionary), black_box(&human_second))));
}

criterion_group!(benches, criterion_benchmark);
criterion_main!(benches);

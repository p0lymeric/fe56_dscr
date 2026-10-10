use std::{fs::{self, read_to_string}, hint::black_box, io::{BufRead, BufReader}, path::PathBuf};
use criterion::{criterion_group, criterion_main, Criterion};
use fe56_translation::{compiler::{Symbolizer, mwis::MwisSymbolizer}, dictionary::{Definition, Dictionary, DictionaryMut, Formatting, json::JsonDictionary}};

fn criterion_benchmark(c: &mut Criterion) {
    let mut dictionary = JsonDictionary::new(Some(&read_to_string(PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("tests/artifacts/DICTIONARY-1.save")).unwrap())).unwrap();

    let esdb_src = fs::File::open(PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("tests/artifacts/third_party/esdb.txt")).unwrap();
    let esdb_rd = BufReader::new(esdb_src);
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
            dictionary.add(id, Definition { word: line_uppercase, desc: "".to_string(), format: Formatting::DEFAULT });
            id -= 1;
        }
    }
    let pride_and_prejudice = read_to_string(PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("tests/artifacts/third_party/pride_and_prejudice.txt")).unwrap();
    c.bench_function("casefold pride and prejudice", |b| b.iter(|| dictionary.casefold(black_box(&pride_and_prejudice))));
    c.bench_function("tokenize pride and prejudice", |b| b.iter(|| MwisSymbolizer.symbolize(black_box(&dictionary), black_box(&pride_and_prejudice))));

    let a_pickle_for_the_knowing_ones = read_to_string(PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("tests/artifacts/third_party/a_pickle_for_the_knowing_ones.txt")).unwrap();
    c.bench_function("casefold a pickle for the knowing ones", |b| b.iter(|| dictionary.casefold(black_box(&a_pickle_for_the_knowing_ones))));
    c.bench_function("tokenize a pickle for the knowing ones", |b| b.iter(|| MwisSymbolizer.symbolize(black_box(&dictionary), black_box(&a_pickle_for_the_knowing_ones))));

    let human_second = read_to_string(PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("tests/artifacts/human_second.txt")).unwrap();
    c.bench_function("casefold human second", |b| b.iter(|| dictionary.casefold(black_box(&human_second))));
    c.bench_function("tokenize human second", |b| b.iter(|| MwisSymbolizer.symbolize(black_box(&dictionary), black_box(&human_second))));
}

criterion_group!(benches, criterion_benchmark);
criterion_main!(benches);

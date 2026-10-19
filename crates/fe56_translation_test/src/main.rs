use std::fs::{self, read_to_string};
use std::io::{BufRead, BufReader};

use eframe::egui;
use egui::{Color32, FontId};
use fe56_translation::compiler::SymbolizerErrorKind::{AmbiguousSpan, EntryNotFound};
use fe56_translation::compiler::mwis::MwisSymbolizer;
use fe56_translation::dictionary::json::JsonDictionary;
use fe56_translation::dictionary::{Definition, Dictionary, DictionaryMut, Formatting, TokenizeResultKind};
use fe56_translation::compiler::{Symbolized, Symbolizer, SymbolizerToken};

fn main() {
    let native_options = eframe::NativeOptions::default();
    eframe::run_native("My egui App", native_options, Box::new(|cc| Ok(Box::new(MyEguiApp::new(cc)))));
}

struct MyEguiApp {
    dictionary: JsonDictionary,
    my_string: String,
    my_tokenization: Symbolized,
}

impl MyEguiApp {
    fn new(cc: &eframe::CreationContext<'_>) -> Self {
        // Customize egui here with cc.egui_ctx.set_fonts and cc.egui_ctx.set_global_style.
        // Restore app state using cc.storage (requires the "persistence" feature).
        // Use the cc.gl (a glow::Context) to create graphics shaders and buffers that you can use
        // for e.g. egui::PaintCallback.
        let mut result = Self {
            dictionary: JsonDictionary::new(Some(include_str!("../../fe56_translation/tests/artifacts/DICTIONARY-1.save"))).unwrap(),
            my_string: String::new(),
            my_tokenization: Symbolized { tokens: Vec::new(), errors: Vec::new(), traces: Vec::new() },
        };
        let esdb_str = include_str!("../../fe56_translation/tests/artifacts/third_party/esdb.txt");
        let mut in_preamble = true;
        let mut id = -1000;
        for line in esdb_str.lines() {
            let linet = line.trim();
            if in_preamble {
                if linet == "---" {
                    in_preamble = false;
                }
                continue;
            }
            let line_uppercase = fe56_translation::case::to_uppercase_simple(linet);
            if result.dictionary.lookup(&line_uppercase) == None {
                result.dictionary.add(id, Definition { word: line_uppercase, desc: "".to_string(), format: Formatting::DEFAULT });
                id -= 1;
            }
        }
        result.dictionary.add(-10000000, Definition { word: "fußy".to_string(), desc: "desc".to_string(), format: Formatting::DEFAULT });
        result.dictionary.add(-10000001, Definition { word: "|-100|-200".to_string(), desc: "desc".to_string(), format: Formatting::DEFAULT });
        result.dictionary.add(-10000002, Definition { word: "|-300".to_string(), desc: "desc".to_string(), format: Formatting::DEFAULT });
        result
    }
}

impl eframe::App for MyEguiApp {
   fn ui(&mut self, ui: &mut egui::Ui, frame: &mut eframe::Frame) {
    ui.style_mut().override_font_id = Some(FontId { size: 18.0, family: egui::FontFamily::Monospace });
       egui::CentralPanel::default().show(ui, |ui| {
            // let mut dict = String::new();
            // for def in self.dictionary.iter() {
            //     let _ = write!(&mut dict, "{} {}\n", def.0, def.1.word);
            // }
            // let _ = ui.add(egui::TextEdit::multiline(&mut dict.as_str()).desired_width(f32::INFINITY));
            let response = ui.add(egui::TextEdit::multiline(&mut self.my_string).desired_width(f32::INFINITY));
            if response.changed() {
                // self.my_string = fe56_translation::case::to_uppercase_simple(&self.my_string);
                self.my_tokenization = MwisSymbolizer.symbolize(&self.dictionary, &self.my_string);
            }
            // ui.add(egui::TextEdit::singleline(&mut self.my_string.as_str()));
            let mut underline = vec![' '; self.my_string.len()];
            let mut parity = 0;
            for tk in &self.my_tokenization.tokens {
                for off in tk.span.start..tk.span.end {
                    // FIXME very much incorrect for multibyte characters
                    underline[off] = if tk.id >= 0 {
                        '#'
                    } else if parity == 0 {
                        '='
                    } else {
                        '~'
                    };
                }
                parity = if parity == 0 { 1 } else { 0 };
            }
            for err in &self.my_tokenization.errors {
                match &err.kind {
                    EntryNotFound => {
                        for i in err.span.start..err.span.end {
                            // FIXME very much incorrect for multibyte characters
                            underline[i] = '!';
                        }
                    },
                    AmbiguousSpan(lvec, rvec) => {
                        // for x in &vec[0] {
                        //     let entry = self.dictionary.get(x.id).unwrap();
                        //     for off in 0..entry.word.len() {
                        //         underline[off + x.span.start] = '?';
                        //     }
                        // }
                        for i in err.span.start..err.span.end {
                            underline[i] = '?';
                        }
                    },
                    _ => {}
                }
            }
            let mut trace = String::new();
            for tr in &self.my_tokenization.traces {
                match tr.kind {
                    fe56_translation::compiler::SymbolizerTraceKind::Token { id } => trace.push_str(&self.my_string[tr.span.start..tr.span.end]),
                    fe56_translation::compiler::SymbolizerTraceKind::TokenBig => trace.push_str(&self.my_string[tr.span.start..tr.span.end]),
                    fe56_translation::compiler::SymbolizerTraceKind::TokenAmbiguousAlt { id, id_alt, span_alt } => trace.push_str(&self.my_string[tr.span.start..tr.span.end]),
                    fe56_translation::compiler::SymbolizerTraceKind::TokenAmbiguous { id } => trace.push_str(&self.my_string[tr.span.start..tr.span.end]),
                    fe56_translation::compiler::SymbolizerTraceKind::Unconsumed => trace.push_str(&self.my_string[tr.span.start..tr.span.end]),
                    fe56_translation::compiler::SymbolizerTraceKind::Whitespace => trace.push_str(&self.my_string[tr.span.start..tr.span.end]),
                }
            }
            ui.add(egui::TextEdit::multiline(&mut trace.as_str()).desired_width(f32::INFINITY));
            ui.add(egui::TextEdit::multiline(&mut underline.iter().collect::<String>()).desired_width(f32::INFINITY));
            egui::ScrollArea::vertical().id_salt("tks").show(ui, |ui| {
                for tk in &self.my_tokenization.tokens {
                    if let Some(entry) = self.dictionary.get(tk.id) {
                        ui.add(egui::TextEdit::singleline(&mut format!("{} ({}) {}-{}", tk.id, entry.word, tk.span.start, tk.span.end)).desired_width(f32::INFINITY));
                    } else {
                        ui.add(egui::TextEdit::singleline(&mut format!("{} {}-{}", tk.id, tk.span.start, tk.span.end)).desired_width(f32::INFINITY));
                    }
                }
                for err in &self.my_tokenization.errors {
                    fn describe_stks(vec: &[SymbolizerToken], dictionary: &dyn Dictionary) -> String {
                        let mut builder: Vec<String> = Vec::new();
                        for tk in vec {
                            if let Some(entry) = dictionary.get(tk.id) {
                                builder.push(format!("{}", entry.word));
                            } else {
                                builder.push(format!("|{}", tk.id));
                            };
                        }
                        builder.join(" ")
                    }
                    match &err.kind {
                        AmbiguousSpan(lvec, rvec) => {
                            ui.add(egui::TextEdit::singleline(&mut format!("AmbiguousSpan ({} <=> {}) {}-{}", describe_stks(&lvec, &self.dictionary), describe_stks(&rvec, &self.dictionary), err.span.start, err.span.end)).desired_width(f32::INFINITY).background_color(Color32::LIGHT_YELLOW));
                        },
                        _ => {
                            ui.add(egui::TextEdit::singleline(&mut format!("{:?} ({}) {}-{}", err.kind, &self.my_string[err.span.start..err.span.end], err.span.start, err.span.end)).desired_width(f32::INFINITY).background_color(Color32::LIGHT_YELLOW));
                        }
                    }
                }
            });
       });
   }
}

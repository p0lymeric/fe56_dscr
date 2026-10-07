use std::fs::read_to_string;

use eframe::egui;
use egui::{Color32, FontId};
use fe56_translation::compiler::SymbolizerErrorKind::{AmbiguousSpan, EntryNotFound};
use fe56_translation::dictionary::json::JsonDictionary;
use fe56_translation::dictionary::{Definition, Dictionary, DictionaryMut, Formatting};
use fe56_translation::compiler::{MwisSymbolizer, Symbolized, Symbolizer};

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
            dictionary: JsonDictionary::new(Some(&read_to_string("C:\\Games\\TMDSDecomp\\save\\DICTIONARY-1.save").unwrap())).unwrap(),
            my_string: String::new(),
            my_tokenization: Symbolized { tokens: Vec::new(), errors: Vec::new() },
        };
        // result.dictionary.add(-1, Definition { word: "UNTO".to_string(), desc: "desc".to_string(), format: Formatting::DEFAULT });
        // result.dictionary.add(-2, Definition { word: "WARD".to_string(), desc: "desc".to_string(), format: Formatting::DEFAULT });
        // result.dictionary.add(-3, Definition { word: "UNTOWARD".to_string(), desc: "desc".to_string(), format: Formatting::DEFAULT });
        // result.dictionary.add(-4, Definition { word: "fußy".to_string(), desc: "desc".to_string(), format: Formatting::DEFAULT });
        // result.dictionary.add(-5, Definition { word: "FUSSY".to_string(), desc: "desc".to_string(), format: Formatting::DEFAULT });
        // result.dictionary.add(-6, Definition { word: "fus".to_string(), desc: "desc".to_string(), format: Formatting::DEFAULT });
        // result.dictionary.add(-7, Definition { word: "x".to_string(), desc: "desc".to_string(), format: Formatting::DEFAULT });
        // result.dictionary.add(-8, Definition { word: "xx".to_string(), desc: "desc".to_string(), format: Formatting::DEFAULT });
        // result.dictionary.add(-9, Definition { word: "esdo".to_string(), desc: "desc".to_string(), format: Formatting::DEFAULT });
        // result.dictionary.add(-10, Definition { word: "dodo".to_string(), desc: "desc".to_string(), format: Formatting::DEFAULT });
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
                self.my_tokenization = MwisSymbolizer.symbolize(&self.dictionary, &self.my_string);
            }
            // ui.add(egui::TextEdit::singleline(&mut self.my_string.as_str()));
            let mut underline = vec![' '; self.my_string.chars().count()];
            let mut parity = 0;
            for tk in &self.my_tokenization.tokens {
                let entry = self.dictionary.get(tk.id).unwrap();
                for off in 0..entry.word.len() {
                    // FIXME very much incorrect for multibyte characters
                    underline[off + tk.span.start] = if parity == 0 { '=' } else { '~' };
                }
                parity = if parity == 0 { 1 } else { 0 };
            }
            for err in &self.my_tokenization.errors {
                match &err.kind {
                    EntryNotFound => {
                        for i in err.span {
                            // FIXME very much incorrect for multibyte characters
                            underline[i] = '!';
                        }
                    },
                    AmbiguousSpan(vec) => {
                        for x in &vec[0] {
                            let entry = self.dictionary.get(x.id).unwrap();
                            for off in 0..entry.word.len() {
                                underline[off + x.span.start] = '?';
                            }
                        }
                    },
                    _ => {}
                }
            }
            ui.add(egui::TextEdit::singleline(&mut underline.iter().collect::<String>()).desired_width(f32::INFINITY));
            for tk in &self.my_tokenization.tokens {
                let entry = self.dictionary.get(tk.id).unwrap();
                ui.add(egui::TextEdit::singleline(&mut format!("{} ({}) {}-{}", tk.id, entry.word, tk.span.start, tk.span.end)).desired_width(f32::INFINITY));
            }
            for err in &self.my_tokenization.errors {
                ui.add(egui::TextEdit::singleline(&mut format!("{:?} ({}) {}-{}", err.kind, &self.my_string[err.span.start..err.span.end], err.span.start, err.span.end)).desired_width(f32::INFINITY).background_color(Color32::LIGHT_YELLOW));
            }
       });
   }
}


const CASEFOLDING_TXT : &str = "third_party/ucd/CaseFolding.txt";
const UNICODEDATA_TXT : &str = "third_party/ucd/UnicodeData.txt";

fn main() {
    println!("cargo:rerun-if-changed={}", CASEFOLDING_TXT);
    println!("cargo:rerun-if-changed={}", UNICODEDATA_TXT);

    #[cfg(any(feature = "run-codegen-and-use-output", feature = "run-codegen-and-copy-to-tree"))]
    {
        use std::fmt::Write;
        use std::fs;

        fn parse_casefolding_txt(src: &str, simple: bool) -> Vec<(String, String)> {
            let mut result = Vec::new();
            for line in src.lines() {
                let line = line.trim();
                if line.is_empty() || line.starts_with('#') {
                    continue;
                }
                let data: Vec<&str> = line.split(';').map(str::trim).collect();
                if let [code, status, mapping, _name] = data[..] {
                    let discriminant = if simple { status == "C" || status == "S" } else { status == "C" || status == "F" };
                    if discriminant {
                        let char_from = char::from_u32(u32::from_str_radix(code, 16).unwrap()).unwrap().to_string();
                        let chars_to = mapping.split_whitespace().map(|x| char::from_u32(u32::from_str_radix(x, 16).unwrap()).unwrap()).collect::<String>();
                        result.push((char_from, chars_to));
                    }
                }
            }
            result.sort();
            result
        }

        fn parse_unicodedata_txt(src: &str) -> Vec<(String, String)> {
            let mut result = Vec::new();
            for line in src.lines() {
                let line = line.trim();
                if line.is_empty() || line.starts_with('#') {
                    continue;
                }
                let data: Vec<&str> = line.split(';').map(str::trim).collect();
                if let (Some(code), Some(uppercode)) = (data.first(), data.get(12)) {
                    if uppercode.is_empty() {
                        continue;
                    }
                    let char_from = char::from_u32(u32::from_str_radix(code, 16).unwrap()).unwrap().to_string();
                    let char_to = char::from_u32(u32::from_str_radix(uppercode, 16).unwrap()).unwrap().to_string();
                    result.push((char_from, char_to));
                }
            }
            result.sort();
            result
        }

        let casefolding_txt = fs::read_to_string(CASEFOLDING_TXT).unwrap();
        let unicodedata_txt = fs::read_to_string(UNICODEDATA_TXT).unwrap();

        let mut ucd_rs = String::new();
        let _ = write!(ucd_rs, "{}", "// This is an auto-generated file, partially derived from the Unicode Character Database.\n");

        let _ = write!(ucd_rs, "{}", "pub static CASEFOLD_FULL: &[(char, &str)] = &[");
        for (c, n) in parse_casefolding_txt(&casefolding_txt, false) {
            let _ = write!(ucd_rs, "('{}',\"{}\"),", c.escape_default(), n.escape_default());
        }
        let _ = write!(ucd_rs, "{}", "];\n");

        // let _ = write!(ucd_rs, "{}", "pub static CASEFOLD_SIMPLE: &[(char, char)] = &[");
        // for (c, n) in parse_casefolding_txt(&casefolding_txt, true) {
        //     let _ = write!(ucd_rs, "('{}','{}'),", c.escape_default(), n.escape_default());
        // }
        // let _ = write!(ucd_rs, "{}", "];\n");

        let _ = write!(ucd_rs, "{}", "pub static UPPERCASE_SIMPLE: &[(char, char)] = &[");
        for (c, u) in parse_unicodedata_txt(&unicodedata_txt) {
            let _ = write!(ucd_rs, "('{}','{}'),", c.escape_default(), u.escape_default());
        }
        let _ = write!(ucd_rs, "{}", "];\n");

        #[cfg(feature = "run-codegen-and-use-output")]
        {
            use std::env;
            use std::path::PathBuf;
            let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
            fs::write(out_path.join("ucd.rs"), &ucd_rs).unwrap();
        }

        #[cfg(feature = "run-codegen-and-copy-to-tree")]
        {
            fs::write("src/ucd.rs", &ucd_rs).unwrap();
        }
    }
}

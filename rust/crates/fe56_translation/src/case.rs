// Unicode string transformation operations
// polymeric 2026

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Casefolded {
    pub text_norm: String,
    pub offset_orig_to_casefold: Vec<usize>,
    pub offset_casefold_to_orig: Vec<usize>,
}

impl Casefolded {
    pub fn new(s: &str) -> Casefolded {
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
        let mut offset_orig_to_casefold = vec![usize::MAX; s.len() + 1];
        let mut offset_casefold_to_orig = vec![usize::MAX; text_norm.len() + 1];
        for (orig, casefold) in offsets {
            offset_orig_to_casefold[orig] = casefold;
            offset_casefold_to_orig[casefold] = orig;
        }
        Casefolded { text_norm, offset_orig_to_casefold, offset_casefold_to_orig }
    }

    pub fn to_orig(&self, offset: usize) -> Option<usize> {
        if self.offset_casefold_to_orig[offset] == usize::MAX { None } else { Some(self.offset_casefold_to_orig[offset]) }
    }

    pub fn to_casefold(&self, offset: usize) -> usize {
        self.offset_orig_to_casefold[offset]
    }
}

pub fn to_uppercase_simple(s: &str) -> String {
    // Length-preserving, locale-independent uppercasing
    // using Unicode's Simple_Uppercase_Mapping table (TR44)
    s.chars().map(|x| {
        fe56_ucd_sys::UPPERCASE_SIMPLE.binary_search_by_key(&x, |(k, _v)| *k)
            .ok()
            .map(|i| fe56_ucd_sys::UPPERCASE_SIMPLE[i].1)
            .unwrap_or(x)
    }).collect()
}

pub fn to_casefolded_full(s: &str) -> String {
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

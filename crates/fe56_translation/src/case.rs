// Unicode string transformation operations
// polymeric 2026

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Casefolded {
    pub text_norm: String,
    pub offsets: Vec<(usize, usize)>,
    pub direct_mapping: bool,
}

impl Casefolded {
    pub fn new(s: &str) -> Casefolded {
        // Length-changing, locale-independent case folding
        // using Unicode's Case_Folding (C + F) table (TR44)
        let mut text_norm = String::with_capacity(s.len());
        let mut offsets = Vec::with_capacity(s.len() + 1);
        let mut direct_mapping = true;
        for (orig, x) in s.char_indices() {
            offsets.push((orig, text_norm.len()));
            if orig != text_norm.len() {
                direct_mapping = false;
            }
            match fe56_ucd_sys::CASEFOLD_FULL.binary_search_by_key(&x, |(k, _v)| *k) {
                Ok(i) => text_norm.push_str(fe56_ucd_sys::CASEFOLD_FULL[i].1),
                _ => text_norm.push(x)
            }
        }
        offsets.push((s.len(), text_norm.len()));
        if s.len() != text_norm.len() {
            direct_mapping = false;
        }
        Casefolded { text_norm, offsets, direct_mapping }
    }

    pub fn to_orig(&self, offset: usize) -> usize {
        if self.direct_mapping {
            offset
        } else {
            // TODO performance optimization: Use 2 index-to-index mapping arrays in Casefolded for the non-1-1 case.
            self.offsets.binary_search_by_key(&offset, |(_orig, casefold)| *casefold).ok().map(|i| self.offsets[i].0).unwrap_or(offset)
        }
    }

    pub fn to_casefold(&self, offset: usize) -> usize {
        if self.direct_mapping {
            offset
        } else {
            // TODO performance optimization: Use 2 index-to-index mapping arrays in Casefolded for the non-1-1 case.
            self.offsets.binary_search_by_key(&offset, |(orig, _casefold)| *orig).ok().map(|i| self.offsets[i].1).unwrap_or(offset)
        }
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

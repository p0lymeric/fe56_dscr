use fe56_translation::compiler::Desymbolizer;
use fe56_translation::compiler::Symbolizer;
use fe56_translation::compiler::SymbolizerSpan;
use fe56_translation::compiler::SymbolizerTrace;
use fe56_translation::compiler::mwis::MwisSymbolizer;
use fe56_translation::compiler::reformatter::CReformatterDesymbolizer;
use fe56_translation::dictionary::Definition;
use fe56_translation::dictionary::DictionaryMut;
use fe56_translation::dictionary::Formatting;
use fe56_translation::dictionary::shadow::ShadowDictionary;

#[derive(Debug, Clone)]
#[repr(C)]
pub struct RVec<T> {
    ptr: *mut T,
    size: usize,
    capacity: usize
}

#[derive(Debug, Clone)]
#[repr(C)]
pub struct RStr {
    ptr: *mut u8,
    size: usize,
    capacity: usize
}

#[unsafe(no_mangle)]
pub extern "C" fn fe56_dictionary_new() -> *mut ShadowDictionary {
    Box::into_raw(Box::new(ShadowDictionary::new()))
}

#[unsafe(no_mangle)]
pub extern "C" fn fe56_dictionary_add(p_dictionary: *mut ShadowDictionary, id: i64, p_word: *const u8, size: usize, format: Formatting) -> bool {
    let dictionary = unsafe { &mut *p_dictionary };
    let word = unsafe { str::from_utf8_unchecked(std::slice::from_raw_parts(p_word, size)) };
    dictionary.add(id, Definition {
        word: word.to_string(),
        desc: "".to_string(),
        format: format,
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn fe56_dictionary_remove(p_dictionary: *mut ShadowDictionary, id: i64) -> bool {
    let dictionary = unsafe { &mut *p_dictionary };
    dictionary.remove(id)
}

#[unsafe(no_mangle)]
pub extern "C" fn fe56_dictionary_set_global_format(p_dictionary: *mut ShadowDictionary, formatting: Formatting) -> bool {
    let dictionary = unsafe { &mut *p_dictionary };
    dictionary.set_global_format(formatting)
}

#[unsafe(no_mangle)]
pub extern "C" fn fe56_dictionary_free(p_dictionary: *mut ShadowDictionary) {
    unsafe { std::mem::drop(Box::from_raw(p_dictionary)); }
}

#[unsafe(no_mangle)]
pub extern "C" fn fe56_mwis_symbolize(p_dictionary: *const ShadowDictionary, p_text: *const u8, size: usize) -> RVec<SymbolizerTrace> {
    let dictionary = unsafe { &*p_dictionary };
    let text = unsafe { str::from_utf8_unchecked(std::slice::from_raw_parts(p_text, size)) };

    let symbolized = MwisSymbolizer.symbolize(dictionary, text);

    let (ptr, size, capacity) = symbolized.traces.into_raw_parts();

    RVec { ptr, size, capacity }
}

#[unsafe(no_mangle)]
pub extern "C" fn fe56_rvec_symbolizertrace_free(r_vec: RVec<SymbolizerTrace>) {
    unsafe { std::mem::drop(Vec::from_raw_parts(r_vec.ptr, r_vec.size, r_vec.capacity)); }
}

#[derive(Debug, Clone)]
#[repr(C)]
pub struct RDesymbolized {
    text: RStr,
    spans: RVec<SymbolizerSpan<usize>>,
}

#[unsafe(no_mangle)]
pub extern "C" fn fe56_reformatter_desymbolize(p_dictionary: *const ShadowDictionary, p_arr: *const i64, size: usize) -> RDesymbolized {
    let dictionary = unsafe { &*p_dictionary };
    let arr = unsafe { std::slice::from_raw_parts(p_arr, size) };

    let desymbolized = CReformatterDesymbolizer.desymbolize(dictionary, arr);

    let (text_ptr, text_size, text_capacity) = desymbolized.text.into_raw_parts();
    let (spans_ptr, spans_size, spans_capacity) = desymbolized.spans.into_raw_parts();

    RDesymbolized {
        text: RStr { ptr: text_ptr, size: text_size, capacity: text_capacity },
        spans: RVec { ptr: spans_ptr, size: spans_size, capacity: spans_capacity },
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn fe56_rdesymbolized_free(r_desymbolized: RDesymbolized) {
    unsafe {
        std::mem::drop(String::from_raw_parts(r_desymbolized.text.ptr, r_desymbolized.text.size, r_desymbolized.text.capacity));
        std::mem::drop(Vec::from_raw_parts(r_desymbolized.spans.ptr, r_desymbolized.spans.size, r_desymbolized.spans.capacity));
    }
}

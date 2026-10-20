use fe56_translation::compiler::Desymbolizer;
use fe56_translation::compiler::Symbolizer;
use fe56_translation::compiler::SymbolizerSpan;
use fe56_translation::compiler::SymbolizerTrace;
use fe56_translation::compiler::mwis::MwisSymbolizer;
use fe56_translation::compiler::reformatter::CReformatterDesymbolizer;
use fe56_translation::dictionary::json::JsonDictionary;

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
pub extern "C" fn fe56_jsondictionary_new(p_path: *const u8, size: usize) -> *mut JsonDictionary {
    if p_path != std::ptr::null() {
        let path = unsafe { str::from_utf8_unchecked(std::slice::from_raw_parts(p_path, size)) };
        if let Ok(dict) = JsonDictionary::new(Some(path)) {
            return Box::into_raw(Box::new(dict));
        }
    } else {
        if let Ok(dict) = JsonDictionary::new(None) {
            return Box::into_raw(Box::new(dict));
        }
    }
    std::ptr::null_mut()
}

#[unsafe(no_mangle)]
pub extern "C" fn fe56_jsondictionary_free(p_dictionary: *mut JsonDictionary) {
    unsafe { std::mem::drop(Box::from_raw(p_dictionary)); }
}

#[unsafe(no_mangle)]
pub extern "C" fn fe56_mwis_symbolize(p_jsondictionary: *const JsonDictionary, p_text: *const u8, size: usize) -> RVec<SymbolizerTrace> {
    let dictionary = unsafe { &*p_jsondictionary };
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
pub extern "C" fn fe56_reformatter_desymbolize(p_jsondictionary: *const JsonDictionary, p_arr: *const i64, size: usize) -> RDesymbolized {
    let dictionary = unsafe { &*p_jsondictionary };
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

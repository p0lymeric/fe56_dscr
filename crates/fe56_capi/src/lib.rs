use fe56_translation::compiler::Symbolizer;
use fe56_translation::compiler::SymbolizerTrace;
use fe56_translation::compiler::mwis::MwisSymbolizer;
use fe56_translation::dictionary::json::JsonDictionary;

#[derive(Debug, Clone)]
#[repr(C)]
pub struct RVec<T> {
    ptr: *mut T,
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

    let mut md_traces = std::mem::ManuallyDrop::new(symbolized.traces);

    RVec { ptr: md_traces.as_mut_ptr(), size: md_traces.len(), capacity: md_traces.capacity() }
}

#[unsafe(no_mangle)]
pub extern "C" fn fe56_rvec_symbolizertrace_free(r_vec: RVec<SymbolizerTrace>) {
    unsafe { std::mem::drop(Vec::from_raw_parts(r_vec.ptr, r_vec.size, r_vec.capacity)); }
}

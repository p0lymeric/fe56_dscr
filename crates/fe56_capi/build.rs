fn main() {
    #[cfg(any(feature = "run-cbindgen", feature = "run-cbindgen-and-copy-to-tree"))]
    {
        use std::env;
        use std::path::PathBuf;
        let crate_dir = env::var("CARGO_MANIFEST_DIR").unwrap();
        let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
        cbindgen::Builder::new()
            .with_crate(crate_dir)
            .with_parse_deps(true)
            .with_parse_include(&["fe56_translation"])
            .with_pragma_once(true)
            .with_tab_width(4)
            .with_item_prefix("Fe56")
            .with_language(cbindgen::Language::C)
            .with_cpp_compat(true)
            .generate()
            .map_or_else(
                |error| match error {
                    cbindgen::Error::ParseSyntaxError { .. } => {}
                    e => panic!("{:?}", e),
                },
                |bindings| {
                    bindings.write_to_file(out_path.join("include/fe56.h"));
                    #[cfg(feature = "run-cbindgen-and-copy-to-tree")]
                    {
                        bindings.write_to_file("include/fe56.h");
                    }
                },
            );
    }
}

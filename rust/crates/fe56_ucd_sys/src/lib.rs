#[cfg(feature = "run-codegen-and-use-output")]
include!(concat!(env!("OUT_DIR"), "/ucd.rs"));

#[cfg(not(feature = "run-codegen-and-use-output"))]
include!("ucd.rs");

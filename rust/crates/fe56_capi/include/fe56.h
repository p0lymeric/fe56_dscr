#pragma once

/* This is an auto-generated file, created by cbindgen. */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

typedef enum Fe56SpacingKind {
    None = 0,
    Space = 1,
    Newline = 2,
    TwoNewline = 3,
} Fe56SpacingKind;

typedef struct Fe56ShadowDictionary Fe56ShadowDictionary;

typedef struct Fe56Formatting {
    enum Fe56SpacingKind before;
    enum Fe56SpacingKind after;
    bool break_on_double;
} Fe56Formatting;
#define Fe56Formatting_DIGIT (Fe56Formatting){ .before = None, .after = None, .break_on_double = false }
#define Fe56Formatting_DEFAULT (Fe56Formatting){ .before = Space, .after = Newline, .break_on_double = false }

typedef struct Fe56SymbolizerSpan_usize {
    uintptr_t start;
    uintptr_t end;
} Fe56SymbolizerSpan_usize;

typedef int64_t Fe56SymbolT;

typedef enum Fe56SymbolizerTraceKind_Tag {
    Token,
    TokenBig,
    TokenAmbiguousAlt,
    TokenAmbiguous,
    Unconsumed,
    Whitespace,
} Fe56SymbolizerTraceKind_Tag;

typedef struct Fe56Token_Body {
    Fe56SymbolT id;
} Fe56Token_Body;

typedef struct Fe56TokenAmbiguousAlt_Body {
    Fe56SymbolT id;
    Fe56SymbolT id_alt;
    struct Fe56SymbolizerSpan_usize span_alt;
} Fe56TokenAmbiguousAlt_Body;

typedef struct Fe56TokenAmbiguous_Body {
    Fe56SymbolT id;
} Fe56TokenAmbiguous_Body;

typedef struct Fe56SymbolizerTraceKind {
    Fe56SymbolizerTraceKind_Tag tag;
    union {
        Fe56Token_Body token;
        Fe56TokenAmbiguousAlt_Body token_ambiguous_alt;
        Fe56TokenAmbiguous_Body token_ambiguous;
    };
} Fe56SymbolizerTraceKind;

typedef struct Fe56SymbolizerTrace {
    struct Fe56SymbolizerSpan_usize span;
    struct Fe56SymbolizerTraceKind kind;
} Fe56SymbolizerTrace;

typedef struct Fe56RVec_SymbolizerTrace {
    struct Fe56SymbolizerTrace *ptr;
    uintptr_t size;
    uintptr_t capacity;
} Fe56RVec_SymbolizerTrace;

typedef struct Fe56RStr {
    uint8_t *ptr;
    uintptr_t size;
    uintptr_t capacity;
} Fe56RStr;

typedef struct Fe56RVec_SymbolizerSpan_usize {
    struct Fe56SymbolizerSpan_usize *ptr;
    uintptr_t size;
    uintptr_t capacity;
} Fe56RVec_SymbolizerSpan_usize;

typedef struct Fe56RDesymbolized {
    struct Fe56RStr text;
    struct Fe56RVec_SymbolizerSpan_usize spans;
} Fe56RDesymbolized;

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

struct Fe56ShadowDictionary *fe56_dictionary_new(void);

bool fe56_dictionary_add(struct Fe56ShadowDictionary *p_dictionary,
                         int64_t id,
                         const uint8_t *p_word,
                         uintptr_t size,
                         struct Fe56Formatting format);

bool fe56_dictionary_remove(struct Fe56ShadowDictionary *p_dictionary, int64_t id);

bool fe56_dictionary_set_global_format(struct Fe56ShadowDictionary *p_dictionary,
                                       struct Fe56Formatting formatting);

void fe56_dictionary_free(struct Fe56ShadowDictionary *p_dictionary);

struct Fe56RVec_SymbolizerTrace fe56_mwis_symbolize(const struct Fe56ShadowDictionary *p_dictionary,
                                                    const uint8_t *p_text,
                                                    uintptr_t size);

void fe56_rvec_symbolizertrace_free(struct Fe56RVec_SymbolizerTrace r_vec);

struct Fe56RDesymbolized fe56_reformatter_desymbolize(const struct Fe56ShadowDictionary *p_dictionary,
                                                      const int64_t *p_arr,
                                                      uintptr_t size);

void fe56_rdesymbolized_free(struct Fe56RDesymbolized r_desymbolized);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

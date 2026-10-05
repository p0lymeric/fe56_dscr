use crate::dictionary;

use std::collections::HashMap;

// Trie for whole-dictionary prefix matching

// TODO The interface should be generalized to work with raw byte sequences
// to avoid needing to directly handle the fact that some UTF-8 codepoints span
// multiple bytes in the trie itself.

#[derive(Debug, Clone)]
pub struct Trie {
    root: TrieNode
}

#[derive(Debug, Clone)]
pub struct TrieNode {
    children: HashMap<char, TrieNode>,
    ids: Vec<dictionary::SymbolT>,
}

impl TrieNode {
    pub fn new() -> Self {
        TrieNode {
            children: HashMap::new(),
            ids: Vec::new(),
        }
    }
}

impl Default for TrieNode {
    fn default() -> Self {
        Self::new()
    }
}

impl Trie {
    pub fn new() -> Self {
        Self {
            root: TrieNode::new()
        }
    }

    pub fn insert(&mut self, id: dictionary::SymbolT, word: &str) -> bool {
        let mut terminal_node = &mut self.root;
        for c in word.chars() {
            terminal_node = terminal_node.children.entry(c).or_default();
        }
        match terminal_node.ids.binary_search(&id) {
            Err(pos) => { terminal_node.ids.insert(pos, id) },
            _ => { return false; }
        };
        true
    }

    pub fn search(&self, word: &str) -> &[dictionary::SymbolT] {
        let mut next_node = &self.root;
        for c in word.chars() {
            let maybe_next_node = next_node.children.get(&c);
            if let Some(nnext_node) = maybe_next_node {
                next_node = nnext_node;
            } else {
                return &[];
            }
        }
        next_node.ids.as_slice()
    }

    pub fn delete(&mut self, id: dictionary::SymbolT, word: &str) -> bool {
        let mut next_node = &self.root;
        let mut deepest_cousin_branch = 0;

        // TODO slice and use char_indices
        for (d, c) in word.chars().enumerate() {
            if !next_node.ids.is_empty() || next_node.children.len() > 1 {
                deepest_cousin_branch = d;
            }
            let maybe_next_node = next_node.children.get(&c);
            if let Some(nnext_node) = maybe_next_node {
                next_node = nnext_node;
            } else {
                return false;
            }
        }

        if next_node.ids.binary_search(&id).is_err() {
            return false;
        }

        let prune = !word.is_empty() && next_node.ids.len() == 1 && next_node.children.is_empty();
        let mut next_node = &mut self.root;
        if prune {
            for c in word.chars().take(deepest_cousin_branch) {
                next_node = next_node.children.get_mut(&c).expect("Should be impossible to fault here");
            }
            let delete_key = word.chars().nth(deepest_cousin_branch).expect("Should be impossible to fault here");
            next_node.children.remove(&delete_key);
        } else {
            for c in word.chars() {
                next_node = next_node.children.get_mut(&c).expect("Should be impossible to fault here");
            }
            match next_node.ids.binary_search(&id) {
                Ok(pos) => { next_node.ids.remove(pos) },
                _ => panic!("Should be impossible to fault here")
            };
        }

        true
    }

    pub fn tokenize_at(&self, text: &str, start: usize) -> Vec<dictionary::TokenCoords> {
        let mut result: Vec<dictionary::TokenCoords> = Vec::new();
        if start >= text.len() {
            return result;
        }
        let mut next_node = &self.root;
        for id in &next_node.ids {
            // Evil zero-length match. Perhaps this should be disallowed...
            result.push(dictionary::TokenCoords { id: *id, start, end: start });
        }
        for (off, c) in text[start..].char_indices() {
            let maybe_next_node = next_node.children.get(&c);
            if let Some(nnext_node) = maybe_next_node {
                next_node = nnext_node;
                for id in &next_node.ids {
                    // Remember that UTF-8 characters span multiple bytes (c.len_utf8() is past the end character)
                    result.push(dictionary::TokenCoords { id: *id, start, end: start + off + c.len_utf8() });
                }
            } else {
                break;
            }
        }
        result
    }
}

impl Default for Trie {
    fn default() -> Self {
        Self::new()
    }
}

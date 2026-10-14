use rustc_hash::FxHashMap;

// Trie for whole-dictionary prefix matching
// polymeric 2026

#[derive(Debug, Clone)]
pub struct Trie<T> {
    root: TrieNode<T>
}

#[derive(Debug, Clone)]
pub struct TrieNode<T> {
    children: FxHashMap<char, TrieNode<T>>,
    ids: Vec<T>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct TriePrefix<T> {
    pub id: T,
    pub length: usize,
}

impl<T> TrieNode<T> {
    pub fn new() -> Self {
        TrieNode {
            children: FxHashMap::default(),
            ids: Vec::new(),
        }
    }
}

impl<T> Default for TrieNode<T> {
    fn default() -> Self {
        Self::new()
    }
}

impl<T: Ord + Copy> Trie<T> {
    pub fn new() -> Self {
        Self {
            root: TrieNode::new()
        }
    }

    pub fn insert(&mut self, id: T, word: &str) -> bool {
        let mut node = &mut self.root;
        for c in word.chars() {
            node = node.children.entry(c).or_default();
        }
        match node.ids.binary_search(&id) {
            Err(pos) => { node.ids.insert(pos, id) },
            _ => { return false; }
        };
        true
    }

    pub fn search(&self, word: &str) -> &[T] {
        let mut node = &self.root;
        for c in word.chars() {
            if let Some(next_node) = node.children.get(&c) {
                node = next_node;
            } else {
                return &[];
            }
        }
        node.ids.as_slice()
    }

    pub fn present(&self, id: T, word: &str) -> bool {
        let mut node = &self.root;
        for c in word.chars() {
            if let Some(next_node) = node.children.get(&c) {
                node = next_node;
            } else {
                // Not found, trie does not contain the word
                return false;
            }
        }
        match node.ids.binary_search(&id) {
            Ok(_) => { return true; },
            _ => { return false; }
        }
    }

    pub fn delete(&mut self, id: T, word: &str) -> bool {
        // A deletion can result in one of three actions:
        // 1. If the ID is not found, nothing happens.
        // 2. If the ID exists in a node alongside other IDs or children, the ID is removed from the node's vector.
        // 3. If the ID exists alone in a node with no peer IDs or children,
        //    the node and its parents are pruned at the youngest common ancestor with which the node has a cousin
        //    or the root ancestor.

        // NB We use raw pointers to track two mutable pointers into the structure.
        // It's difficult to express a similar single-pass deletion routine using Safe Rust.
        let mut rnode = &raw mut self.root;
        let mut maybe_youngest_ca_with_cousin: Option<(*mut TrieNode<T>, char)> = None;

        // Seed the youngest common ancestor as the root
        if let Some(c) = word.chars().next() {
            maybe_youngest_ca_with_cousin = Some((rnode, c));
        }

        // Walk the trie to the requested word entry, recording any common ancestors of interest
        for c in word.chars() {
            let node = unsafe { &mut *rnode };
            if !node.ids.is_empty() || node.children.len() > 1 {
                maybe_youngest_ca_with_cousin = Some((rnode, c));
            }
            if let Some(next_node) = node.children.get_mut(&c) {
                rnode = next_node;
            } else {
                // Not found, trie does not contain the word
                return false;
            }
        }

        let node = unsafe { &mut *rnode };
        match node.ids.binary_search(&id) {
            Ok(vec_pos_to_delete) => {
                // If the entry holding the ID to be removed contains other IDs or has children, just remove the ID
                // Otherwise prune the trie at the common ancestor identified earlier
                if let Some(youngest_ca_with_cousin) = maybe_youngest_ca_with_cousin && node.ids.len() == 1 && node.children.is_empty() {
                    let (rca, c) = youngest_ca_with_cousin;
                    let ca = unsafe { &mut *rca };
                    ca.children.remove(&c);
                } else {
                    // By necessity these conditions will hold in this branch
                    debug_assert!(word.is_empty() || node.ids.len() > 1 || !node.children.is_empty());
                    node.ids.remove(vec_pos_to_delete);
                }
                true
            }
            // Not found, requested ID was not found in the word entry
            _ => {
                false
            },
        }
    }

    pub fn prefixes(&self, text: &str) -> Vec<TriePrefix<T>> {
        let mut result: Vec<TriePrefix<T>> = Vec::new();
        let mut node = &self.root;
        for id in &node.ids {
            // Evil zero-length match. Perhaps this should be disallowed...
            result.push(TriePrefix { id: *id, length: 0 });
        }
        for (off, c) in text.char_indices() {
            if let Some(next_node) = node.children.get(&c) {
                node = next_node;
                for id in &node.ids {
                    // Remember that UTF-8 characters span a variable number of bytes
                    // "off + c.len_utf8()" points to one past the end of the current character
                    result.push(TriePrefix { id: *id, length: off + c.len_utf8() });
                }
            } else {
                break;
            }
        }
        result
    }
}

impl<T: Ord + Copy> Default for Trie<T> {
    fn default() -> Self {
        Self::new()
    }
}

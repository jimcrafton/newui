#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace newui::text {

    namespace piecetree_detail {
        // Counts for a stretch of text, computed on its own (no context from what's around it) and
        // combinable: two neighbours' Stats give the Stats of their concatenation, including a
        // "\r\n" pair split across the join. A line break "starts" at every '\r' and at every '\n'
        // that doesn't follow a '\r', so breaks() = cr + lf - pairs.
        //@reflect ignore=true
        struct Stats {
            std::size_t length = 0;
            std::size_t cr = 0;
            std::size_t lf = 0;
            std::size_t pairs = 0;     // "\r\n" pairs with both characters inside
            wchar_t first = 0;         // valid when length > 0
            wchar_t last = 0;
            std::size_t breaks() const { return cr + lf - pairs; }
        };

        // Fixed-size chunk of characters. Only ever appended to, and only past what any published
        // piece covers, so the characters a piece refers to never change.
        //@reflect ignore=true
        struct Block {
            explicit Block(std::size_t capacity_) : data(new wchar_t[capacity_]), capacity(capacity_) {}
            std::unique_ptr<wchar_t[]> data;
            std::size_t capacity = 0;
            std::size_t used = 0;
        };

        // A run of characters in one Block.
        //@reflect ignore=true
        struct Piece {
            std::shared_ptr<Block> block;
            std::size_t start = 0;
            std::size_t length = 0;
            Stats stats;
            const wchar_t* data() const { return block->data.get() + start; }
        };

        // Treap node (a random priority keeps it balanced); the subtree's totals are in stats.
        //@reflect ignore=true
        struct Node {
            std::shared_ptr<const Node> left;
            std::shared_ptr<const Node> right;
            Piece piece;
            std::uint32_t priority = 0;
            Stats stats;
        };
    }

    // Text as a piece table kept in a persistent balanced tree: the characters live in
    // append-only blocks, and the document is a sequence of pieces (block, start, length) - an edit
    // splits and joins pieces instead of moving characters. Every node also carries the totals for
    // its subtree (length, line breaks), so offset <-> line queries and edits are O(log n) however
    // large the text, and there is no contiguous copy of it anywhere.
    //
    // Persistent: an edit copies the O(log n) nodes on its path and shares the rest, so a copy of a
    // PieceTree (O(1)) is a snapshot that later edits to the original never touch. That's what
    // undo/redo (keep the old tree) and a background thread (hand it a snapshot) need.
    //
    // Threads: any number of threads may read their own copies at the same time; one thread at a
    // time may edit a tree and the copies made from it (they share the block that new text is
    // appended to).
    //
    // Offsets and lengths are in wchar_t units. Line breaks are "\n", "\r\n" and a lone '\r'; a
    // line ends at a break, so there is always at least one line, and an empty one after a final
    // break. Everything clamps out-of-range arguments rather than throwing.
    //@reflect ignore=true
    class PieceTree {
    public:
        static constexpr std::size_t npos = static_cast<std::size_t>(-1);

        PieceTree() = default;
        explicit PieceTree(std::wstring_view text);

        std::size_t length() const;
        bool empty() const { return length() == 0; }
        // How many pieces the text is in now (diagnostics and tests).
        std::size_t pieceCount() const;

        // The character at offset, or L'\0' past the end.
        wchar_t at(std::size_t offset) const;

        // The characters [start, start + length), clamped.
        void appendTo(std::size_t start, std::size_t length, std::wstring& out) const;
        std::wstring substring(std::size_t start, std::size_t length) const;
        std::wstring str() const;

        // Calls visit with each contiguous stretch of [start, start + length), in order.
        void forEachChunk(std::size_t start, std::size_t length, const std::function<void(const wchar_t*, std::size_t)>& visit) const;

        // The offset of the first ch at or after from, or npos.
        std::size_t find(wchar_t ch, std::size_t from = 0) const;
        std::size_t count(wchar_t ch, std::size_t start, std::size_t length) const;

        // Against a plain string, without building this one: equality, the length of the shared
        // start, and of the shared end (at most limit characters).
        bool equals(const std::wstring& other) const;
        std::size_t commonPrefixLength(const std::wstring& other) const;
        std::size_t commonSuffixLength(const std::wstring& other, std::size_t limit) const;

        // The same, against another tree - stretches both share (an edit copies only what it
        // touches) are skipped without comparing. sameAs() is the O(1) "same tree" test, which
        // implies equal text; equals() also finds equal text in different trees.
        bool sameAs(const PieceTree& other) const { return root_ == other.root_; }
        bool equals(const PieceTree& other) const;
        std::size_t commonPrefixLength(const PieceTree& other) const;
        std::size_t commonSuffixLength(const PieceTree& other, std::size_t limit) const;

        std::size_t lineCount() const;
        // The offset line starts at (length() for a line past the last).
        std::size_t lineStart(std::size_t line) const;
        // The line offset is on; an offset between the '\r' and '\n' of a "\r\n" is on the line the
        // pair ends.
        std::size_t lineOfOffset(std::size_t offset) const;
        // The offset of the first break starting at or after from (not the '\n' of a "\r\n"), or
        // npos; *terminatorLength (if given) is 1 or 2.
        std::size_t findLineBreak(std::size_t from, std::size_t* terminatorLength = nullptr) const;
        // How many breaks start in [start, start + length).
        std::size_t countLineBreaks(std::size_t start, std::size_t length) const;

        void insert(std::size_t offset, std::wstring_view text);
        void erase(std::size_t start, std::size_t length);
        void replace(std::size_t start, std::size_t length, std::wstring_view text);
        void clear();

        // Checks the tree's own invariants (totals, balance order, no empty pieces) - for tests.
        bool validate() const;

    private:
        using NodePtr = std::shared_ptr<const piecetree_detail::Node>;

        NodePtr appendText(NodePtr left, std::wstring_view text);
        piecetree_detail::Stats statsBefore(std::size_t offset) const;
        std::size_t findBreakStart(std::size_t k) const;

        NodePtr root_;
        std::shared_ptr<piecetree_detail::Block> tail_;   // where new text is appended
    };

}

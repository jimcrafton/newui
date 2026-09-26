#include "newui/piecetree.h"

#include <algorithm>
#include <cwchar>
#include <utility>
#include <vector>

namespace newui::text {

    using namespace piecetree_detail;

    namespace {
        using NodePtr = std::shared_ptr<const Node>;

        // Small enough that scanning a whole piece (to split it, or to find a break in it) stays cheap.
        constexpr std::size_t kBlockSize = 2048;

        const Stats& statsOf(const NodePtr& node) {
            static const Stats empty;
            return node ? node->stats : empty;
        }

        Stats scan(const wchar_t* p, std::size_t n) {
            Stats s;
            s.length = n;
            if (n == 0) {
                return s;
            }
            s.first = p[0];
            s.last = p[n - 1];
            for (std::size_t i = 0; i < n; ++i) {
                if (p[i] == L'\r') {
                    ++s.cr;
                } else if (p[i] == L'\n') {
                    ++s.lf;
                    if (i > 0 && p[i - 1] == L'\r') {
                        ++s.pairs;
                    }
                }
            }
            return s;
        }

        Stats combine(const Stats& a, const Stats& b) {
            if (a.length == 0) {
                return b;
            }
            if (b.length == 0) {
                return a;
            }
            Stats r;
            r.length = a.length + b.length;
            r.cr = a.cr + b.cr;
            r.lf = a.lf + b.lf;
            r.pairs = a.pairs + b.pairs + (a.last == L'\r' && b.first == L'\n' ? 1 : 0);
            r.first = a.first;
            r.last = b.last;
            return r;
        }

        std::uint32_t randomPriority() {
            thread_local std::uint32_t state = 2463534242u;   // xorshift32
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return state;
        }

        NodePtr makeNode(NodePtr left, Piece piece, NodePtr right, std::uint32_t priority) {
            auto node = std::make_shared<Node>();
            node->stats = combine(combine(statsOf(left), piece.stats), statsOf(right));
            node->left = std::move(left);
            node->piece = std::move(piece);
            node->right = std::move(right);
            node->priority = priority;
            return node;
        }

        Piece slice(const Piece& piece, std::size_t offset, std::size_t length) {
            Piece out;
            out.block = piece.block;
            out.start = piece.start + offset;
            out.length = length;
            out.stats = scan(out.data(), length);
            return out;
        }

        NodePtr merge(const NodePtr& a, const NodePtr& b) {
            if (!a) {
                return b;
            }
            if (!b) {
                return a;
            }
            if (a->priority >= b->priority) {
                return makeNode(a->left, a->piece, merge(a->right, b), a->priority);
            }
            return makeNode(merge(a, b->left), b->piece, b->right, b->priority);
        }

        // The first offset characters, and the rest.
        std::pair<NodePtr, NodePtr> split(const NodePtr& node, std::size_t offset) {
            if (!node) {
                return { nullptr, nullptr };
            }
            const std::size_t leftLength = statsOf(node->left).length;
            if (offset <= leftLength) {
                auto parts = split(node->left, offset);
                return { std::move(parts.first), makeNode(std::move(parts.second), node->piece, node->right, node->priority) };
            }
            const std::size_t pieceEnd = leftLength + node->piece.length;
            if (offset >= pieceEnd) {
                auto parts = split(node->right, offset - pieceEnd);
                return { makeNode(node->left, node->piece, std::move(parts.first), node->priority), std::move(parts.second) };
            }
            const std::size_t inside = offset - leftLength;
            return {
                makeNode(node->left, slice(node->piece, 0, inside), nullptr, node->priority),
                makeNode(nullptr, slice(node->piece, inside, node->piece.length - inside), node->right, node->priority)
            };
        }

        NodePtr replaceRightmost(const NodePtr& node, const Piece& piece) {
            if (!node->right) {
                return makeNode(node->left, piece, nullptr, node->priority);
            }
            return makeNode(node->left, node->piece, replaceRightmost(node->right, piece), node->priority);
        }

        // Calls visit(data, length) with each stretch of [from, to) (relative to node's text), in
        // order, until it returns false. Returns whether it ran to the end.
        template <typename F>
        bool visitRange(const Node* node, std::size_t from, std::size_t to, F& visit) {
            if (node == nullptr || from >= to) {
                return true;
            }
            const std::size_t leftLength = statsOf(node->left).length;
            if (from < leftLength && !visitRange(node->left.get(), from, to < leftLength ? to : leftLength, visit)) {
                return false;
            }
            const std::size_t pieceEnd = leftLength + node->piece.length;
            const std::size_t s = from > leftLength ? from : leftLength;
            const std::size_t e = to < pieceEnd ? to : pieceEnd;
            if (s < e && !visit(node->piece.data() + (s - leftLength), e - s)) {
                return false;
            }
            if (to > pieceEnd) {
                return visitRange(node->right.get(), from > pieceEnd ? from - pieceEnd : 0, to - pieceEnd, visit);
            }
            return true;
        }

        bool validateNode(const Node* node) {
            if (node == nullptr) {
                return true;
            }
            const Piece& piece = node->piece;
            if (piece.length == 0 || !piece.block || piece.start + piece.length > piece.block->used) {
                return false;
            }
            const Stats own = scan(piece.data(), piece.length);
            if (own.length != piece.stats.length || own.cr != piece.stats.cr || own.lf != piece.stats.lf
                    || own.pairs != piece.stats.pairs || own.first != piece.stats.first || own.last != piece.stats.last) {
                return false;
            }
            if ((node->left && node->left->priority > node->priority) || (node->right && node->right->priority > node->priority)) {
                return false;
            }
            const Stats total = combine(combine(statsOf(node->left), piece.stats), statsOf(node->right));
            if (total.length != node->stats.length || total.cr != node->stats.cr || total.lf != node->stats.lf
                    || total.pairs != node->stats.pairs || total.first != node->stats.first || total.last != node->stats.last) {
                return false;
            }
            return validateNode(node->left.get()) && validateNode(node->right.get());
        }

        std::size_t countPieces(const Node* node) {
            return node == nullptr ? 0 : 1 + countPieces(node->left.get()) + countPieces(node->right.get());
        }
    }

    PieceTree::PieceTree(std::wstring_view text) {
        root_ = appendText(nullptr, text);
    }

    std::size_t PieceTree::length() const {
        return statsOf(root_).length;
    }

    std::size_t PieceTree::pieceCount() const {
        return countPieces(root_.get());
    }

    bool PieceTree::validate() const {
        return validateNode(root_.get());
    }

    wchar_t PieceTree::at(std::size_t offset) const {
        const Node* node = root_.get();
        while (node != nullptr) {
            const std::size_t leftLength = statsOf(node->left).length;
            if (offset < leftLength) {
                node = node->left.get();
            } else if (offset < leftLength + node->piece.length) {
                return node->piece.data()[offset - leftLength];
            } else {
                offset -= leftLength + node->piece.length;
                node = node->right.get();
            }
        }
        return L'\0';
    }

    void PieceTree::forEachChunk(std::size_t start, std::size_t length, const std::function<void(const wchar_t*, std::size_t)>& visit) const {
        const std::size_t total = this->length();
        const std::size_t from = start < total ? start : total;
        const std::size_t to = length < total - from ? from + length : total;
        auto each = [&](const wchar_t* data, std::size_t n) {
            visit(data, n);
            return true;
        };
        visitRange(root_.get(), from, to, each);
    }

    void PieceTree::appendTo(std::size_t start, std::size_t length, std::wstring& out) const {
        forEachChunk(start, length, [&](const wchar_t* data, std::size_t n) { out.append(data, n); });
    }

    std::wstring PieceTree::substring(std::size_t start, std::size_t length) const {
        std::wstring out;
        appendTo(start, length, out);
        return out;
    }

    std::wstring PieceTree::str() const {
        std::wstring out;
        out.reserve(length());
        appendTo(0, length(), out);
        return out;
    }

    std::size_t PieceTree::find(wchar_t ch, std::size_t from) const {
        const std::size_t total = length();
        if (from >= total) {
            return npos;
        }
        std::size_t result = npos;
        std::size_t position = from;
        auto each = [&](const wchar_t* data, std::size_t n) {
            if (const wchar_t* found = std::wmemchr(data, ch, n)) {
                result = position + static_cast<std::size_t>(found - data);
                return false;
            }
            position += n;
            return true;
        };
        visitRange(root_.get(), from, total, each);
        return result;
    }

    std::size_t PieceTree::count(wchar_t ch, std::size_t start, std::size_t length) const {
        std::size_t n = 0;
        forEachChunk(start, length, [&](const wchar_t* data, std::size_t size) {
            n += static_cast<std::size_t>(std::count(data, data + size, ch));
        });
        return n;
    }

    std::size_t PieceTree::commonPrefixLength(const std::wstring& other) const {
        const std::size_t total = length();
        const std::size_t limit = total < other.size() ? total : other.size();
        std::size_t matched = 0;
        auto each = [&](const wchar_t* data, std::size_t n) {
            std::size_t i = 0;
            while (i < n && data[i] == other[matched + i]) {
                ++i;
            }
            matched += i;
            return i == n;
        };
        visitRange(root_.get(), 0, limit, each);
        return matched;
    }

    bool PieceTree::equals(const std::wstring& other) const {
        return length() == other.size() && commonPrefixLength(other) == other.size();
    }

    std::size_t PieceTree::commonSuffixLength(const std::wstring& other, std::size_t limit) const {
        const std::size_t total = length();
        std::size_t max = total < other.size() ? total : other.size();
        max = limit < max ? limit : max;
        std::vector<std::pair<const wchar_t*, std::size_t>> chunks;
        auto each = [&](const wchar_t* data, std::size_t n) {
            chunks.emplace_back(data, n);
            return true;
        };
        visitRange(root_.get(), total - max, total, each);
        std::size_t matched = 0;
        for (std::size_t c = chunks.size(); c-- > 0;) {
            const wchar_t* data = chunks[c].first;
            const std::size_t n = chunks[c].second;
            std::size_t i = 0;
            while (i < n && data[n - 1 - i] == other[other.size() - 1 - matched - i]) {
                ++i;
            }
            matched += i;
            if (i < n) {
                break;
            }
        }
        return matched;
    }

    // Totals for the first offset characters.
    Stats PieceTree::statsBefore(std::size_t offset) const {
        Stats acc;
        const Node* node = root_.get();
        while (node != nullptr && offset > 0) {
            const std::size_t leftLength = statsOf(node->left).length;
            if (offset <= leftLength) {
                node = node->left.get();
                continue;
            }
            acc = combine(acc, statsOf(node->left));
            const std::size_t inPiece = offset - leftLength;
            if (inPiece <= node->piece.length) {
                return combine(acc, scan(node->piece.data(), inPiece));
            }
            acc = combine(acc, node->piece.stats);
            offset = inPiece - node->piece.length;
            node = node->right.get();
        }
        return acc;
    }

    // The offset the k-th (1-based) break starts at, or npos.
    std::size_t PieceTree::findBreakStart(std::size_t k) const {
        Stats acc;   // everything before the current subtree
        const Node* node = root_.get();
        while (node != nullptr) {
            const Stats leftAcc = combine(acc, statsOf(node->left));
            if (k <= leftAcc.breaks()) {
                node = node->left.get();
                continue;
            }
            const Stats pieceAcc = combine(leftAcc, node->piece.stats);
            if (k <= pieceAcc.breaks()) {
                std::size_t need = k - leftAcc.breaks();
                wchar_t previous = leftAcc.length > 0 ? leftAcc.last : L'\0';
                const wchar_t* p = node->piece.data();
                for (std::size_t i = 0; i < node->piece.length; ++i) {
                    const wchar_t c = p[i];
                    if ((c == L'\r' || (c == L'\n' && previous != L'\r')) && --need == 0) {
                        return leftAcc.length + i;
                    }
                    previous = c;
                }
                return npos;   // the totals said it's here
            }
            acc = pieceAcc;
            node = node->right.get();
        }
        return npos;
    }

    std::size_t PieceTree::lineCount() const {
        return statsOf(root_).breaks() + 1;
    }

    std::size_t PieceTree::lineStart(std::size_t line) const {
        if (line == 0) {
            return 0;
        }
        if (line > statsOf(root_).breaks()) {
            return length();
        }
        const std::size_t start = findBreakStart(line);
        return start + (at(start) == L'\r' && at(start + 1) == L'\n' ? 2 : 1);
    }

    std::size_t PieceTree::lineOfOffset(std::size_t offset) const {
        const std::size_t total = length();
        offset = offset < total ? offset : total;
        std::size_t line = statsBefore(offset).breaks();
        if (offset > 0 && offset < total && at(offset - 1) == L'\r' && at(offset) == L'\n') {
            --line;   // inside a "\r\n": the '\r' counted, but the line doesn't start until after the '\n'
        }
        return line;
    }

    std::size_t PieceTree::findLineBreak(std::size_t from, std::size_t* terminatorLength) const {
        if (from >= length()) {
            return npos;
        }
        const std::size_t start = findBreakStart(statsBefore(from).breaks() + 1);
        if (start == npos) {
            return npos;
        }
        if (terminatorLength != nullptr) {
            *terminatorLength = at(start) == L'\r' && at(start + 1) == L'\n' ? 2 : 1;
        }
        return start;
    }

    std::size_t PieceTree::countLineBreaks(std::size_t start, std::size_t length) const {
        const std::size_t total = this->length();
        const std::size_t from = start < total ? start : total;
        const std::size_t to = length < total - from ? from + length : total;
        return statsBefore(to).breaks() - statsBefore(from).breaks();
    }

    PieceTree::NodePtr PieceTree::appendText(NodePtr left, std::wstring_view text) {
        std::size_t done = 0;

        // Typing continues the last piece when it ends exactly where its block's used part does.
        if (left && tail_ && !text.empty()) {
            const Node* rightmost = left.get();
            while (rightmost->right) {
                rightmost = rightmost->right.get();
            }
            const Piece& last = rightmost->piece;
            if (last.block == tail_ && last.start + last.length == tail_->used && tail_->used < tail_->capacity) {
                const std::size_t room = tail_->capacity - tail_->used;
                const std::size_t n = text.size() < room ? text.size() : room;
                std::copy(text.data(), text.data() + n, tail_->data.get() + tail_->used);
                tail_->used += n;
                Piece extended = last;
                extended.length += n;
                extended.stats = scan(extended.data(), extended.length);
                left = replaceRightmost(left, extended);
                done = n;
            }
        }

        while (done < text.size()) {
            if (!tail_ || tail_->used == tail_->capacity) {
                tail_ = std::make_shared<Block>(kBlockSize);
            }
            const std::size_t room = tail_->capacity - tail_->used;
            const std::size_t n = text.size() - done < room ? text.size() - done : room;
            Piece piece;
            piece.block = tail_;
            piece.start = tail_->used;
            piece.length = n;
            std::copy(text.data() + done, text.data() + done + n, tail_->data.get() + tail_->used);
            tail_->used += n;
            piece.stats = scan(piece.data(), n);
            left = merge(left, makeNode(nullptr, std::move(piece), nullptr, randomPriority()));
            done += n;
        }
        return left;
    }

    void PieceTree::replace(std::size_t start, std::size_t length, std::wstring_view text) {
        const std::size_t total = this->length();
        start = start < total ? start : total;
        length = length < total - start ? length : total - start;
        if (length == 0 && text.empty()) {
            return;
        }
        auto head = split(root_, start);
        auto rest = split(head.second, length);   // rest.first is what's removed
        root_ = merge(appendText(std::move(head.first), text), rest.second);
    }

    void PieceTree::insert(std::size_t offset, std::wstring_view text) {
        replace(offset, 0, text);
    }

    void PieceTree::erase(std::size_t start, std::size_t length) {
        replace(start, length, std::wstring_view());
    }

    void PieceTree::clear() {
        root_.reset();
    }

}

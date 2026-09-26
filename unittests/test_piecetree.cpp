#include "newui/piecetree.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <random>
#include <thread>
#include <vector>

using newui::text::PieceTree;

namespace {
    struct RefBreak {
        std::size_t start;
        std::size_t length;
    };

    // The line-break rule, written the plain way against a std::wstring.
    std::vector<RefBreak> refBreaks(const std::wstring& s) {
        std::vector<RefBreak> out;
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (s[i] == L'\r') {
                out.push_back({ i, i + 1 < s.size() && s[i + 1] == L'\n' ? std::size_t(2) : std::size_t(1) });
            } else if (s[i] == L'\n' && (i == 0 || s[i - 1] != L'\r')) {
                out.push_back({ i, 1 });
            }
        }
        return out;
    }

    std::vector<std::size_t> refLineStarts(const std::wstring& s) {
        std::vector<std::size_t> starts{ 0 };
        for (const RefBreak& b : refBreaks(s)) {
            starts.push_back(b.start + b.length);
        }
        return starts;
    }

    // Everything the tree answers, against the reference; a sample of offsets for the queries that
    // take one (all of them when the text is small).
    void expectMatches(const PieceTree& tree, const std::wstring& ref, std::mt19937& rng, const char* what) {
        SCOPED_TRACE(what);
        ASSERT_EQ(tree.length(), ref.size());
        ASSERT_TRUE(tree.validate());
        ASSERT_EQ(tree.str(), ref);

        const std::vector<std::size_t> starts = refLineStarts(ref);
        const std::vector<RefBreak> breaks = refBreaks(ref);
        ASSERT_EQ(tree.lineCount(), starts.size());
        for (std::size_t line = 0; line < starts.size(); ++line) {
            ASSERT_EQ(tree.lineStart(line), starts[line]) << "line " << line;
        }
        EXPECT_EQ(tree.lineStart(starts.size()), ref.size());
        EXPECT_EQ(tree.lineStart(starts.size() + 50), ref.size());

        std::vector<std::size_t> offsets;
        if (ref.size() <= 400) {
            for (std::size_t i = 0; i <= ref.size(); ++i) {
                offsets.push_back(i);
            }
        } else {
            for (int i = 0; i < 150; ++i) {
                offsets.push_back(rng() % (ref.size() + 1));
            }
            for (std::size_t i = 0; i < starts.size(); i += (starts.size() / 20) + 1) {
                for (int d = -2; d <= 2; ++d) {   // around the line starts, where the pairs are
                    const long long at = static_cast<long long>(starts[i]) + d;
                    if (at >= 0 && at <= static_cast<long long>(ref.size())) {
                        offsets.push_back(static_cast<std::size_t>(at));
                    }
                }
            }
        }
        for (std::size_t offset : offsets) {
            // The last line starting at or before offset.
            const std::size_t expectedLine = static_cast<std::size_t>(
                std::upper_bound(starts.begin(), starts.end(), offset) - starts.begin()) - 1;
            ASSERT_EQ(tree.lineOfOffset(offset), expectedLine) << "offset " << offset;

            std::size_t terminator = 0;
            const std::size_t found = tree.findLineBreak(offset, &terminator);
            std::size_t expectedBreak = PieceTree::npos;
            std::size_t expectedLength = 0;
            const auto nextBreak = std::lower_bound(breaks.begin(), breaks.end(), offset,
                [](const RefBreak& b, std::size_t at) { return b.start < at; });
            if (nextBreak != breaks.end()) {
                expectedBreak = nextBreak->start;
                expectedLength = nextBreak->length;
            }
            ASSERT_EQ(found, expectedBreak) << "from " << offset;
            if (found != PieceTree::npos) {
                ASSERT_EQ(terminator, expectedLength);
            }

            ASSERT_EQ(tree.at(offset), offset < ref.size() ? ref[offset] : L'\0');
            const wchar_t ch = L"ab \r\n"[rng() % 5];
            const std::size_t expectedFind = ref.find(ch, offset);
            ASSERT_EQ(tree.find(ch, offset), expectedFind == std::wstring::npos ? PieceTree::npos : expectedFind);

            const std::size_t length = rng() % 300;
            const auto rangeEnd = std::lower_bound(breaks.begin(), breaks.end(), offset + length,
                [](const RefBreak& b, std::size_t at) { return b.start < at; });
            const std::size_t expectedCount = static_cast<std::size_t>(rangeEnd - nextBreak);
            ASSERT_EQ(tree.countLineBreaks(offset, length), expectedCount) << "range " << offset << "+" << length;
            ASSERT_EQ(tree.substring(offset, length), ref.substr(offset < ref.size() ? offset : ref.size(), length));
            ASSERT_EQ(tree.count(ch, offset, length),
                static_cast<std::size_t>(std::count(ref.begin() + (offset < ref.size() ? offset : ref.size()),
                    ref.begin() + (offset + length < ref.size() ? offset + length : ref.size()), ch)));
        }

        // Against a lightly edited copy.
        std::wstring other = ref;
        if (!other.empty()) {
            other[rng() % other.size()] = L'#';
        }
        std::size_t prefix = 0;
        while (prefix < ref.size() && prefix < other.size() && ref[prefix] == other[prefix]) {
            ++prefix;
        }
        ASSERT_EQ(tree.commonPrefixLength(other), prefix);
        ASSERT_EQ(tree.equals(other), other == ref);
        ASSERT_TRUE(tree.equals(ref));
        std::size_t suffix = 0;
        while (suffix < ref.size() && suffix < other.size() && ref[ref.size() - 1 - suffix] == other[other.size() - 1 - suffix]) {
            ++suffix;
        }
        ASSERT_EQ(tree.commonSuffixLength(other, ref.size()), suffix);
        ASSERT_EQ(tree.commonSuffixLength(other, 3), suffix < 3 ? suffix : 3);
    }

    std::wstring randomText(std::mt19937& rng, std::size_t length) {
        static const wchar_t alphabet[] = L"ab \r\n\r\nx";   // breaks are common, so pairs and lone '\r's are
        std::wstring s;
        for (std::size_t i = 0; i < length; ++i) {
            s += alphabet[rng() % (sizeof(alphabet) / sizeof(wchar_t) - 1)];
        }
        return s;
    }
}

TEST(PieceTree, EmptyTreeAnswersSensibly) {
    PieceTree tree;
    EXPECT_TRUE(tree.empty());
    EXPECT_EQ(tree.length(), 0u);
    EXPECT_EQ(tree.pieceCount(), 0u);
    EXPECT_EQ(tree.at(0), L'\0');
    EXPECT_EQ(tree.str(), L"");
    EXPECT_EQ(tree.lineCount(), 1u);
    EXPECT_EQ(tree.lineStart(0), 0u);
    EXPECT_EQ(tree.lineStart(3), 0u);
    EXPECT_EQ(tree.lineOfOffset(9), 0u);
    EXPECT_EQ(tree.find(L'a'), PieceTree::npos);
    EXPECT_EQ(tree.findLineBreak(0), PieceTree::npos);
    EXPECT_TRUE(tree.equals(L""));
    EXPECT_TRUE(tree.validate());
}

TEST(PieceTree, BuildsFromTextAndReadsItBackAcrossBlocks) {
    std::wstring text;
    for (int i = 0; i < 1000; ++i) {
        text += L"line " + std::to_wstring(i) + L"\n";
    }
    PieceTree tree(text);
    EXPECT_GT(tree.pieceCount(), 3u);   // more than one block
    EXPECT_EQ(tree.length(), text.size());
    EXPECT_EQ(tree.str(), text);
    EXPECT_EQ(tree.at(0), L'l');
    EXPECT_EQ(tree.at(text.size() - 1), L'\n');
    EXPECT_EQ(tree.at(text.size()), L'\0');
    EXPECT_EQ(tree.substring(5000, 40), text.substr(5000, 40));
    EXPECT_EQ(tree.lineCount(), 1001u);
    EXPECT_TRUE(tree.validate());
}

TEST(PieceTree, InsertEraseAndReplaceClampLikeAString) {
    PieceTree tree(L"hello world");
    tree.insert(5, L",");
    EXPECT_EQ(tree.str(), L"hello, world");
    tree.erase(5, 1);
    EXPECT_EQ(tree.str(), L"hello world");
    tree.replace(6, 5, L"there");
    EXPECT_EQ(tree.str(), L"hello there");
    tree.insert(1000, L"!");
    EXPECT_EQ(tree.str(), L"hello there!");
    tree.erase(10, 1000);
    EXPECT_EQ(tree.str(), L"hello ther");
    tree.erase(1000, 5);
    EXPECT_EQ(tree.str(), L"hello ther");
    tree.replace(0, 0, L"");
    EXPECT_EQ(tree.str(), L"hello ther");
    tree.replace(0, 100, L"");
    EXPECT_TRUE(tree.empty());
    EXPECT_EQ(tree.pieceCount(), 0u);
    tree.insert(0, L"back");
    EXPECT_EQ(tree.str(), L"back");
    tree.clear();
    EXPECT_TRUE(tree.empty());
    EXPECT_TRUE(tree.validate());
}

TEST(PieceTree, TypingContinuesOnePieceInsteadOfMakingOnePerCharacter) {
    PieceTree tree(L"start");
    for (int i = 0; i < 1500; ++i) {
        tree.insert(tree.length(), std::wstring(1, L'a'));
    }
    EXPECT_EQ(tree.length(), 1505u);
    EXPECT_LE(tree.pieceCount(), 2u);

    // In the middle too: type, then keep typing where the caret moved to.
    PieceTree middle(L"headtail");
    for (int i = 0; i < 500; ++i) {
        middle.insert(4 + static_cast<std::size_t>(i), std::wstring(1, L'x'));
    }
    EXPECT_EQ(middle.str(), L"head" + std::wstring(500, L'x') + L"tail");
    EXPECT_LE(middle.pieceCount(), 4u);
    EXPECT_TRUE(middle.validate());
}

TEST(PieceTree, ACopyIsASnapshotEditsToTheOriginalNeverTouch) {
    PieceTree tree(L"one two three");
    const PieceTree before = tree;
    tree.replace(4, 3, L"2");
    tree.insert(0, L">> ");
    tree.erase(tree.length() - 5, 5);
    EXPECT_EQ(before.str(), L"one two three");
    EXPECT_EQ(tree.str(), L">> one 2 ");

    // Undo: put the old tree back, then edit from there.
    const PieceTree edited = tree;
    tree = before;
    tree.insert(3, L"!");
    EXPECT_EQ(tree.str(), L"one! two three");
    EXPECT_EQ(edited.str(), L">> one 2 ");
    EXPECT_TRUE(tree.validate());
    EXPECT_TRUE(edited.validate());
    EXPECT_TRUE(before.validate());
}

TEST(PieceTree, TypingAfterGoingBackToAnOlderTreeDoesNotCorruptTheNewerOne) {
    PieceTree lineage(L"abc");
    const PieceTree snapshotAbc = lineage;
    lineage.insert(3, L"def");
    const PieceTree snapshotAbcdef = lineage;   // its last piece ends where the block's used part does
    lineage = snapshotAbc;                      // undo
    lineage.insert(3, L"XYZ");                  // must not overwrite what snapshotAbcdef refers to
    EXPECT_EQ(lineage.str(), L"abcXYZ");
    EXPECT_EQ(snapshotAbcdef.str(), L"abcdef");
    EXPECT_EQ(snapshotAbc.str(), L"abc");
    EXPECT_TRUE(lineage.validate());
}

TEST(PieceTree, ACrLfSplitAcrossPiecesIsStillOneBreak) {
    // '\r' and '\n' typed separately, so they are different pieces.
    PieceTree tree(L"a\rb");
    EXPECT_EQ(tree.lineCount(), 2u);
    tree.insert(2, L"\n");   // "a\r\nb"
    EXPECT_EQ(tree.str(), L"a\r\nb");
    EXPECT_EQ(tree.lineCount(), 2u);
    EXPECT_EQ(tree.lineStart(1), 3u);
    EXPECT_EQ(tree.lineOfOffset(2), 0u);   // between them
    EXPECT_EQ(tree.lineOfOffset(3), 1u);
    std::size_t length = 0;
    EXPECT_EQ(tree.findLineBreak(0, &length), 1u);
    EXPECT_EQ(length, 2u);
    EXPECT_EQ(tree.countLineBreaks(0, 4), 1u);
    EXPECT_EQ(tree.countLineBreaks(2, 2), 0u);   // just the '\n' half

    tree.erase(2, 1);   // "a\rb": a lone '\r' again
    EXPECT_EQ(tree.lineCount(), 2u);
    EXPECT_EQ(tree.lineStart(1), 2u);
    EXPECT_TRUE(tree.validate());
}

TEST(PieceTree, ACrLfSplitAcrossBlocksIsStillOneBreak) {
    std::wstring text(2047, L'x');   // the '\r' is the last character of a block
    text += L"\r\ny";
    PieceTree tree(text);
    EXPECT_EQ(tree.lineCount(), 2u);
    EXPECT_EQ(tree.lineStart(1), 2049u);
    EXPECT_EQ(tree.lineOfOffset(2048), 0u);
    EXPECT_EQ(tree.lineOfOffset(2049), 1u);
    std::size_t length = 0;
    EXPECT_EQ(tree.findLineBreak(0, &length), 2047u);
    EXPECT_EQ(length, 2u);
    EXPECT_TRUE(tree.validate());
}

TEST(PieceTree, LinesEndAtBreaksAndAFinalBreakStartsAnEmptyLine) {
    PieceTree tree(L"ab\ncd\r\nef\rgh\n");
    EXPECT_EQ(tree.lineCount(), 5u);
    EXPECT_EQ(tree.lineStart(0), 0u);
    EXPECT_EQ(tree.lineStart(1), 3u);
    EXPECT_EQ(tree.lineStart(2), 7u);
    EXPECT_EQ(tree.lineStart(3), 10u);
    EXPECT_EQ(tree.lineStart(4), 13u);
    EXPECT_EQ(tree.lineStart(5), 13u);
    EXPECT_EQ(tree.lineOfOffset(13), 4u);
    EXPECT_EQ(tree.lineOfOffset(1000), 4u);
    EXPECT_EQ(PieceTree(L"a\r").lineCount(), 2u);
    EXPECT_EQ(PieceTree(L"a\r\r").lineCount(), 3u);
    EXPECT_EQ(PieceTree(L"\n\r").lineCount(), 3u);
}

TEST(PieceTree, RandomEditsMatchAStringOnEveryQuery) {
    for (unsigned seed = 1; seed <= 4; ++seed) {
        std::mt19937 rng(seed);
        std::wstring ref = randomText(rng, seed % 2 == 0 ? 6000 : 30);
        PieceTree tree(ref);
        expectMatches(tree, ref, rng, "initial");
        if (HasFatalFailure()) {
            return;
        }
        for (int step = 0; step < 800; ++step) {
            const unsigned op = rng() % 10;
            const std::size_t at = ref.empty() ? 0 : rng() % (ref.size() + 1);
            const std::size_t span = rng() % 4 == 0 ? rng() % 200 : rng() % 6;
            const std::wstring text = randomText(rng, rng() % 40 == 0 ? 2000 + rng() % 4000 : rng() % 12);
            if (op < 4) {
                tree.insert(at, text);
                ref.insert(at, text);
            } else if (op < 7) {
                tree.erase(at, span);
                ref.erase(at, span);
            } else {
                tree.replace(at, span, text);
                ref.replace(at, span < ref.size() - at ? span : ref.size() - at, text);
            }
            if (step % 25 == 0 || step < 40) {
                expectMatches(tree, ref, rng, "step");
                if (HasFatalFailure()) {
                    ADD_FAILURE() << "seed " << seed << " step " << step << " op " << op << " at " << at << " span " << span;
                    return;
                }
            } else {
                ASSERT_EQ(tree.length(), ref.size());
            }
        }
        expectMatches(tree, ref, rng, "final");
    }
}

TEST(PieceTree, SnapshotsCanBeReadOnAnotherThreadWhileTheOriginalIsEdited) {
    std::mt19937 rng(7);
    std::wstring ref = randomText(rng, 20000);
    PieceTree tree(ref);
    const PieceTree snapshot = tree;
    const std::wstring expected = ref;

    std::atomic<bool> stop{ false };
    std::atomic<int> reads{ 0 };
    std::atomic<bool> ok{ true };
    std::thread reader([&]() {
        while (!stop) {
            if (snapshot.str() != expected || snapshot.lineCount() != refLineStarts(expected).size() || !snapshot.validate()) {
                ok = false;
            }
            ++reads;
        }
    });
    for (int i = 0; i < 2000; ++i) {
        tree.insert(rng() % (tree.length() + 1), randomText(rng, 1 + rng() % 8));
        if (i % 3 == 0 && tree.length() > 10) {
            tree.erase(rng() % (tree.length() - 5), 1 + rng() % 4);
        }
    }
    while (reads < 3) {
        std::this_thread::yield();
    }
    stop = true;
    reader.join();
    EXPECT_TRUE(ok);
    EXPECT_EQ(snapshot.str(), expected);
    EXPECT_TRUE(tree.validate());
}

TEST(PieceTree, AMultiMegabyteDocumentStaysBalancedThroughManyEdits) {
    std::mt19937 rng(11);
    std::wstring line = L"    int value = compute(alpha, beta, gamma); // a plausible line of C++\n";
    std::wstring ref;
    while (ref.size() < 2 * 1024 * 1024) {
        ref += line;
    }
    PieceTree tree(ref);
    EXPECT_TRUE(tree.validate());
    const std::size_t initialLines = tree.lineCount();
    for (int i = 0; i < 3000; ++i) {
        const std::size_t at = rng() % (ref.size() + 1);
        if (i % 3 == 0) {
            tree.erase(at, 40);
            ref.erase(at, 40);
        } else {
            tree.insert(at, L"x = 1;\r\n");
            ref.insert(at, L"x = 1;\r\n");
        }
    }
    EXPECT_TRUE(tree.validate());
    EXPECT_EQ(tree.length(), ref.size());
    EXPECT_EQ(tree.str(), ref);
    EXPECT_EQ(tree.lineCount(), refLineStarts(ref).size());
    EXPECT_NE(tree.lineCount(), initialLines);
    // Each edit adds a few pieces at most - nothing like one per character.
    EXPECT_LT(tree.pieceCount(), 2 * 1024 * 1024 / 2048 + 3000 * 3 + 10);
}

// Run with --gtest_also_run_disabled_tests to see the numbers.
TEST(PieceTree, DISABLED_Timings) {
    using clock = std::chrono::steady_clock;
    auto ms = [](clock::time_point a, clock::time_point b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };
    std::mt19937 rng(3);
    const std::wstring line = L"    int value = compute(alpha, beta, gamma); // a plausible line of C++\n";
    for (std::size_t megabytes : { 1u, 8u, 32u }) {
        std::wstring text;
        while (text.size() < megabytes * 1024 * 1024 / 2) {   // wchar_t: 2 bytes each
            text += line;
        }
        auto t0 = clock::now();
        PieceTree tree(text);
        auto t1 = clock::now();
        for (int i = 0; i < 10000; ++i) {
            tree.insert(rng() % (tree.length() + 1), L"x");
        }
        auto t2 = clock::now();
        std::size_t sink = 0;
        for (int i = 0; i < 10000; ++i) {
            sink += tree.lineStart(rng() % tree.lineCount());
            sink += tree.lineOfOffset(rng() % tree.length());
        }
        auto t3 = clock::now();
        const PieceTree snapshot = tree;
        auto t4 = clock::now();
        std::wstring flat = tree.str();
        auto t5 = clock::now();
        std::wstring flatCopy = text;
        flatCopy.insert(flatCopy.size() / 2, L"x");
        auto t6 = clock::now();
        std::printf("%zu MB (%zu chars, %zu lines): build %.1f ms, 10000 random inserts %.1f ms (%.2f us each), "
            "10000 lineStart+lineOfOffset %.1f ms, snapshot %.4f ms, str() %.1f ms, pieces %zu; "
            "one std::wstring insert into the middle %.3f ms (sink %zu %zu)\n",
            megabytes, text.size(), tree.lineCount(), ms(t0, t1), ms(t1, t2), ms(t1, t2) * 1000.0 / 10000.0,
            ms(t2, t3), ms(t3, t4), ms(t4, t5), tree.pieceCount(), ms(t5, t6), sink, flat.size() + flatCopy.size());
    }
}

#include "manyrowsdocument.h"
#include "manyrowssaver.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

using namespace manyrows;

namespace {
    std::string tempName(const std::string& name) {
        return (std::filesystem::temp_directory_path() / ("manyrows_save_" + name)).string();
    }

    std::string slurp(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        std::stringstream content;
        content << in.rdbuf();
        return content.str();
    }

    void removeAll(const std::string& path) {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        std::filesystem::remove(path + ".bak", ignored);
        std::filesystem::remove(ManyRowsSaver::tempPathFor(path), ignored);
    }

    bool waitFinished(const ManyRowsSaver& saver) {
        for (int i = 0; i < 2000 && !saver.finished(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return saver.finished();
    }
}

TEST(ManyRowsSaver, TempPathKeepsTheExtensionSoTheFormatIsTheSame) {
    EXPECT_EQ(std::filesystem::path(ManyRowsSaver::tempPathFor("C:\\data\\sales.tsv")).filename().string(), "sales.saving.tsv");
}

TEST(ManyRowsSaver, WritesATempFileThenTheDocumentMovesItIntoPlace) {
    std::string target = tempName("out.csv");
    removeAll(target);
    ManyRowsModel model(30000);   // the generated table: header + rows
    ManyRowsDocument document(model);
    {
        ManyRowsSaver saver(model, target);
        ASSERT_TRUE(waitFinished(saver));
        ASSERT_EQ(saver.state(), ManyRowsSaver::State::Done);
        EXPECT_FLOAT_EQ(saver.fraction(), 1.0f);
        EXPECT_FALSE(std::filesystem::exists(target));            // not written until handed over
        ASSERT_TRUE(std::filesystem::exists(saver.tempPath()));
        document.markModified();
        ASSERT_TRUE(document.saveStaged(saver.tempPath(), target));
        saver.release();
    }
    EXPECT_TRUE(std::filesystem::exists(target));
    EXPECT_FALSE(std::filesystem::exists(ManyRowsSaver::tempPathFor(target)));
    EXPECT_EQ(document.filePath(), target);
    EXPECT_FALSE(document.isModified());
    std::string text = slurp(target);
    EXPECT_EQ(text.rfind("Order,Item,Category", 0), 0u);
    EXPECT_EQ(std::count(text.begin(), text.end(), '\n'), 30001);
    removeAll(target);
}

TEST(ManyRowsSaver, AnExistingTargetIsBackedUpAndReplaced) {
    std::string target = tempName("replace.csv");
    removeAll(target);
    { std::ofstream(target, std::ios::binary) << "old\n"; }
    ManyRowsModel model;
    model.makeBlank(2, 2);
    model.setValueAt(0, 0, std::string("new"));
    ManyRowsDocument document(model);
    ManyRowsSaver saver(model, target);
    ASSERT_TRUE(waitFinished(saver));
    ASSERT_TRUE(document.saveStaged(saver.tempPath(), target));
    saver.release();
    EXPECT_EQ(slurp(target), "new,\r\n,\r\n");
    EXPECT_EQ(slurp(target + ".bak"), "old\n");
    removeAll(target);
}

TEST(ManyRowsSaver, CancelLeavesTheTargetAloneAndTheTempFileGoesAway) {
    std::string target = tempName("cancel.csv");
    removeAll(target);
    { std::ofstream(target, std::ios::binary) << "keep\n"; }
    ManyRowsModel model(2000000);
    {
        ManyRowsSaver saver(model, target);
        saver.cancel();
        ASSERT_TRUE(waitFinished(saver));
        EXPECT_TRUE(saver.state() == ManyRowsSaver::State::Cancelled || saver.state() == ManyRowsSaver::State::Done);
    }
    EXPECT_EQ(slurp(target), "keep\n");
    EXPECT_FALSE(std::filesystem::exists(ManyRowsSaver::tempPathFor(target)));   // destructor removed it
    removeAll(target);
}

TEST(ManyRowsSaver, AFailedSaveReportsWhy) {
    std::string target = (std::filesystem::temp_directory_path() / "manyrows_no_such_folder" / "x.csv").string();
    ManyRowsModel model(10);
    ManyRowsSaver saver(model, target);
    ASSERT_TRUE(waitFinished(saver));
    EXPECT_EQ(saver.state(), ManyRowsSaver::State::Failed);
    EXPECT_FALSE(saver.error().empty());
}

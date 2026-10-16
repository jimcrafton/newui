#include "manyrowsdocument.h"
#include "manyrowsloader.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace manyrows;

namespace {
    class TempFile {
    public:
        explicit TempFile(const std::string& name) {
            path_ = (std::filesystem::temp_directory_path() / ("manyrows_load_" + name)).string();
        }
        ~TempFile() {
            std::error_code ignored;
            std::filesystem::remove(path_, ignored);
        }
        const std::string& path() const { return path_; }
        void write(const std::string& content) const {
            std::ofstream file(path_, std::ios::binary | std::ios::trunc);
            file << content;
        }
    private:
        std::string path_;
    };

    // Waits (bounded) for the worker to finish; the tests must never hang.
    bool waitFinished(const ManyRowsLoader& loader) {
        for (int i = 0; i < 2000 && !loader.finished(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return loader.finished();
    }

    std::string bigCsv(std::size_t rows) {
        std::string text = "id,name,score\n";
        for (std::size_t r = 0; r < rows; ++r) {
            text += std::to_string(r) + ",row " + std::to_string(r) + "," + std::to_string(r % 97) + ".5\n";
        }
        return text;
    }
}

TEST(ManyRowsLoader, ReadsAFileOnAWorkerAndHandsOverTheTable) {
    TempFile file("ok.csv");
    file.write(bigCsv(50000));   // several 256 KB chunks
    ManyRowsLoader loader(file.path());
    ASSERT_TRUE(waitFinished(loader));
    ASSERT_EQ(loader.state(), ManyRowsLoader::State::Done);
    EXPECT_EQ(loader.rowsRead(), 50000u);
    EXPECT_FLOAT_EQ(loader.fraction(), 1.0f);

    ManyRowsModel model;
    model.adoptLoaded(loader.table());
    EXPECT_FALSE(model.isGenerated());
    EXPECT_EQ(model.rowCount(), 50000u);
    EXPECT_EQ(model.columnTitle(1), "name");
    EXPECT_EQ(model.cellText(49999, 1), "row 49999");
    EXPECT_EQ(loader.table().rowCount(), 0u);   // the staging table was emptied by the swap
}

TEST(ManyRowsLoader, ReportsAFailureWithItsReason) {
    TempFile missing("missing.csv");
    ManyRowsLoader loader(missing.path());
    ASSERT_TRUE(waitFinished(loader));
    EXPECT_EQ(loader.state(), ManyRowsLoader::State::Failed);
    EXPECT_FALSE(loader.error().empty());

    TempFile unknown("data.xyz");
    ManyRowsLoader other(unknown.path());
    ASSERT_TRUE(waitFinished(other));
    EXPECT_EQ(other.state(), ManyRowsLoader::State::Failed);
}

TEST(ManyRowsLoader, CancelStopsEarlyAndDestroyingARunningLoaderIsSafe) {
    TempFile file("big.csv");
    file.write(bigCsv(400000));
    {
        ManyRowsLoader loader(file.path());
        loader.cancel();
        ASSERT_TRUE(waitFinished(loader));
        // Cancelled normally; Done only if the worker beat the cancel to the end of a small file.
        EXPECT_TRUE(loader.state() == ManyRowsLoader::State::Cancelled || loader.state() == ManyRowsLoader::State::Done);
        if (loader.state() == ManyRowsLoader::State::Cancelled) {
            EXPECT_LT(loader.rowsRead(), 400000u);
        }
    }
    {
        ManyRowsLoader abandoned(file.path());   // destructor cancels and joins without hanging
    }
}

TEST(ManyRowsLoader, DocumentAdoptsTheStagedTableAndTheFilePath) {
    TempFile file("doc.csv");
    file.write("a,b\n1,2\n3,4\n");
    ManyRowsLoader loader(file.path());
    ASSERT_TRUE(waitFinished(loader));
    ASSERT_EQ(loader.state(), ManyRowsLoader::State::Done);

    ManyRowsModel model;
    ManyRowsDocument document(model);
    document.markModified();
    ASSERT_TRUE(document.loadStaged(loader.table(), file.path()));
    EXPECT_EQ(document.filePath(), file.path());
    EXPECT_FALSE(document.isModified());
    EXPECT_EQ(document.loadedRows(), 2u);
    EXPECT_EQ(model.cellText(1, 1), "4");
    EXPECT_EQ(model.columnTitle(0), "a");
}

TEST(ManyRowsLoader, AdoptingAGeneratedTableThrows) {
    ManyRowsModel model;
    ManyRowsModel generated(10);
    EXPECT_THROW(model.adoptLoaded(generated), std::runtime_error);
}

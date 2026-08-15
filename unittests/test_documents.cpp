#include "newui/controllers.h"
#include "newui/models.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

// Covers Document (models.h) and DocumentController (controllers.h) -
// fully headless, no real filesystem I/O (FakeDocument below simulates
// load/save success/failure via flags, same "test-local subclass"
// approach test_controllers.cpp's RecordingViewController already uses).

using namespace newui;

namespace {

int g_fakeDocumentDestructorCount = 0;

// A minimal concrete Document for testing the load()/save()/isModified()
// machinery Document itself provides - readFromFile()/writeToFile() don't
// touch a real file, just simulate success/failure and record how many
// times each was called.
class FakeDocument : public Document {
public:
    std::string content;
    bool nextLoadShouldFail = false;
    bool nextSaveShouldFail = false;
    bool setValueDuringLoad = false;
    int readFromFileCallCount = 0;
    int writeToFileCallCount = 0;

    ~FakeDocument() override { ++g_fakeDocumentDestructorCount; }

protected:
    bool readFromFile(const std::string& path) override {
        ++readFromFileCallCount;
        if (nextLoadShouldFail) {
            return false;
        }
        content = "loaded:" + path;
        if (setValueDuringLoad) {
            // Simulates a subclass that populates its own fields via
            // setValue() while loading - markModified() must not stick
            // from this (see Document::load()'s loading_ guard).
            setValue(std::any(content));
        }
        return true;
    }

    bool writeToFile(const std::string& path) override {
        ++writeToFileCallCount;
        return !nextSaveShouldFail;
    }
};

}  // namespace

// ---------------------------------------------------------------------
// Document
// ---------------------------------------------------------------------

TEST(Document, InitiallyNotModifiedNoFilePath) {
    FakeDocument doc;

    EXPECT_FALSE(doc.isModified());
    EXPECT_FALSE(doc.hasFilePath());
    EXPECT_EQ(doc.filePath(), "");
}

TEST(Document, LoadSuccessAdoptsPathAndClearsModified) {
    FakeDocument doc;

    EXPECT_TRUE(doc.load("C:/docs/a.txt"));

    EXPECT_EQ(doc.readFromFileCallCount, 1);
    EXPECT_EQ(doc.filePath(), "C:/docs/a.txt");
    EXPECT_TRUE(doc.hasFilePath());
    EXPECT_FALSE(doc.isModified());
    EXPECT_EQ(doc.content, "loaded:C:/docs/a.txt");
}

TEST(Document, LoadFailureLeavesPathAndModifiedUnchanged) {
    FakeDocument doc;
    doc.nextLoadShouldFail = true;

    EXPECT_FALSE(doc.load("C:/docs/missing.txt"));

    EXPECT_FALSE(doc.hasFilePath());
    EXPECT_FALSE(doc.isModified());
}

TEST(Document, SetValueMarksModifiedAndFiresBothDelegates) {
    FakeDocument doc;
    int onChangedCount = 0;
    int onModifiedChangedCount = 0;
    doc.onChanged.add([&onChangedCount](Model&) {
        ++onChangedCount;
        return SyncReturn::Handled;
    });
    doc.onModifiedChanged.add([&onModifiedChangedCount](Document&) {
        ++onModifiedChangedCount;
        return SyncReturn::Handled;
    });

    doc.setValue(std::any(std::string("hello")));

    EXPECT_EQ(onChangedCount, 1);
    EXPECT_EQ(onModifiedChangedCount, 1);
    EXPECT_TRUE(doc.isModified());
}

TEST(Document, MarkModifiedIsNoOpWhileLoading) {
    FakeDocument doc;
    doc.setValueDuringLoad = true;

    EXPECT_TRUE(doc.load("C:/docs/a.txt"));

    EXPECT_FALSE(doc.isModified());
}

TEST(Document, SaveWithExplicitPathAdoptsNewPathAndClearsModified) {
    FakeDocument doc;
    doc.setValue(std::any(std::string("x")));
    ASSERT_TRUE(doc.isModified());

    EXPECT_TRUE(doc.save("C:/docs/new.txt"));

    EXPECT_EQ(doc.writeToFileCallCount, 1);
    EXPECT_EQ(doc.filePath(), "C:/docs/new.txt");
    EXPECT_FALSE(doc.isModified());
}

TEST(Document, SaveUsesExistingFilePathWhenNoneGiven) {
    FakeDocument doc;
    ASSERT_TRUE(doc.load("C:/docs/a.txt"));

    EXPECT_TRUE(doc.save());

    EXPECT_EQ(doc.writeToFileCallCount, 1);
    EXPECT_EQ(doc.filePath(), "C:/docs/a.txt");
}

TEST(Document, SaveWithNoPathAndNoFilePathFailsWithoutCallingWriteToFile) {
    FakeDocument doc;

    EXPECT_FALSE(doc.save());
    EXPECT_EQ(doc.writeToFileCallCount, 0);
}

TEST(Document, SaveFailureLeavesModifiedAndPathUnchanged) {
    FakeDocument doc;
    doc.setValue(std::any(std::string("x")));
    doc.nextSaveShouldFail = true;

    EXPECT_FALSE(doc.save("C:/docs/new.txt"));

    EXPECT_TRUE(doc.isModified());
    EXPECT_FALSE(doc.hasFilePath());
}

TEST(Document, OnModifiedChangedOnlyFiresWhenFlagActuallyFlips) {
    FakeDocument doc;
    int count = 0;
    doc.onModifiedChanged.add([&count](Document&) {
        ++count;
        return SyncReturn::Handled;
    });

    doc.markModified();
    doc.markModified();
    EXPECT_EQ(count, 1);
}

// ---------------------------------------------------------------------
// DocumentController
// ---------------------------------------------------------------------

TEST(DocumentController, AddDocumentRegistersAndActivatesIt) {
    DocumentController controller;
    auto* doc = new FakeDocument();

    controller.addDocument(doc);

    ASSERT_EQ(controller.documents().size(), 1u);
    EXPECT_EQ(controller.documents()[0], doc);
    EXPECT_EQ(controller.activeDocument(), doc);
}

TEST(DocumentController, AddingSameDocumentTwiceIsNoOp) {
    DocumentController controller;
    auto* doc = new FakeDocument();

    controller.addDocument(doc);
    controller.addDocument(doc);

    EXPECT_EQ(controller.documents().size(), 1u);
}

TEST(DocumentController, OpenDocumentAddsThenLoads) {
    DocumentController controller;
    auto* doc = new FakeDocument();

    EXPECT_TRUE(controller.openDocument(doc, "C:/docs/a.txt"));

    EXPECT_EQ(controller.documents().size(), 1u);
    EXPECT_EQ(doc->filePath(), "C:/docs/a.txt");
}

TEST(DocumentController, OpenDocumentStillAddsDocumentEvenIfLoadFails) {
    DocumentController controller;
    auto* doc = new FakeDocument();
    doc->nextLoadShouldFail = true;

    EXPECT_FALSE(controller.openDocument(doc, "C:/docs/missing.txt"));

    EXPECT_EQ(controller.documents().size(), 1u);
}

TEST(DocumentController, CloseDocumentRemovesAndDeletesIt) {
    g_fakeDocumentDestructorCount = 0;
    DocumentController controller;
    auto* doc = new FakeDocument();
    controller.addDocument(doc);

    controller.closeDocument(doc);

    EXPECT_TRUE(controller.documents().empty());
    EXPECT_EQ(g_fakeDocumentDestructorCount, 1);
}

TEST(DocumentController, ClosingActiveDocumentMovesToLastRemaining) {
    DocumentController controller;
    auto* a = new FakeDocument();
    auto* b = new FakeDocument();
    controller.addDocument(a);
    controller.addDocument(b);
    ASSERT_EQ(controller.activeDocument(), b);

    controller.closeDocument(b);

    EXPECT_EQ(controller.activeDocument(), a);
}

TEST(DocumentController, ClosingLastDocumentClearsActiveDocument) {
    DocumentController controller;
    auto* doc = new FakeDocument();
    controller.addDocument(doc);

    controller.closeDocument(doc);

    EXPECT_EQ(controller.activeDocument(), nullptr);
}

TEST(DocumentController, ClosingInactiveDocumentLeavesActiveDocumentUnchanged) {
    DocumentController controller;
    auto* a = new FakeDocument();
    auto* b = new FakeDocument();
    controller.addDocument(a);
    controller.addDocument(b);
    controller.setActiveDocument(a);

    controller.closeDocument(b);

    EXPECT_EQ(controller.activeDocument(), a);
}

TEST(DocumentController, EventsFireInOrder) {
    std::vector<std::string> log;
    DocumentController controller;
    controller.onDocumentAdded.add([&log](DocumentController&, Document&) {
        log.push_back("added");
        return SyncReturn::Handled;
    });
    controller.onDocumentWillClose.add([&log](DocumentController&, Document&) {
        log.push_back("willClose");
        return SyncReturn::Handled;
    });
    controller.onActiveDocumentChanged.add([&log](DocumentController&) {
        log.push_back("activeChanged");
        return SyncReturn::Handled;
    });

    auto* doc = new FakeDocument();
    controller.addDocument(doc);
    controller.closeDocument(doc);

    EXPECT_EQ(log, (std::vector<std::string>{"added", "activeChanged", "willClose", "activeChanged"}));
}

TEST(DocumentController, DestructorDeletesRemainingDocumentsWithoutFiringEvents) {
    g_fakeDocumentDestructorCount = 0;
    bool willCloseFired = false;

    {
        DocumentController controller;
        controller.onDocumentWillClose.add([&willCloseFired](DocumentController&, Document&) {
            willCloseFired = true;
            return SyncReturn::Handled;
        });
        controller.addDocument(new FakeDocument());
        controller.addDocument(new FakeDocument());
    }

    EXPECT_EQ(g_fakeDocumentDestructorCount, 2);
    EXPECT_FALSE(willCloseFired);
}

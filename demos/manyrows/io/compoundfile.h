#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace manyrows {
    namespace io {

        // A Microsoft compound file (OLE2 "structured storage") held in memory - the container an .xls workbook
        // lives in. Read-only: it finds the named streams in the directory and follows their sector chains
        // (the regular FAT for streams of 4096 bytes and more, the mini FAT for smaller ones).
        //
        // The constructor reads the whole file and throws std::runtime_error ("CompoundFile::CompoundFile: ...")
        // when it cannot be opened or is not a compound file; readStream() throws for a missing stream or a
        // broken chain. Chains are bounded, so a corrupt (looping) file fails instead of hanging.
        // @reflect ignore=true
        class CompoundFile {
        public:
            explicit CompoundFile(const std::string& path);

            std::size_t fileBytes() const { return file_.size(); }

            // True when a stream called `name` (case-insensitive, top level) exists.
            bool hasStream(const std::string& name) const;
            // The whole stream.
            std::vector<std::uint8_t> readStream(const std::string& name) const;

            // True when the file starts with the compound file signature (a cheap sniff, nothing else is read).
            static bool hasSignature(const std::string& path);

        private:
            // @reflect ignore=true
            struct Entry {
                std::string name;
                std::uint8_t type = 0;   // 1 storage, 2 stream, 5 root
                std::uint32_t start = 0;
                std::uint64_t size = 0;
            };

            std::vector<std::uint8_t> readChain(std::uint32_t start, const std::vector<std::uint32_t>& fat, bool mini,
                std::uint64_t size, bool sizeKnown) const;

            std::vector<std::uint8_t> file_;
            std::size_t sectorSize_ = 512;
            std::size_t miniSectorSize_ = 64;
            std::uint32_t miniCutoff_ = 4096;
            std::vector<std::uint32_t> fat_;
            std::vector<std::uint32_t> miniFat_;
            std::vector<std::uint8_t> miniStream_;
            std::vector<Entry> entries_;
        };

    }
}

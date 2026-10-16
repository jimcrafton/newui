#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace manyrows {
    namespace io {

        // A ZIP archive on disk, read-only - the container of .xlsx files.
        //
        // The constructor reads the central directory (the entry list at the end of the file); entries are
        // then extracted on demand, streamed through a sink so a huge worksheet is never held whole in memory.
        // Supports stored and deflated entries (deflate through the vendored zlib's inflate). Not supported,
        // and reported with a clear std::runtime_error: ZIP64 archives, encrypted entries, other methods.
        // All errors are std::runtime_error ("ZipFile::method: ...").
        // @reflect ignore=true
        class ZipFile {
        public:
            // @reflect ignore=true
            struct Entry {
                std::string name;
                std::uint16_t method = 0;       // 0 stored, 8 deflated
                std::uint16_t flags = 0;
                std::uint32_t compressedSize = 0;
                std::uint32_t size = 0;         // uncompressed
                std::uint32_t localOffset = 0;
            };

            explicit ZipFile(const std::string& path);

            const std::vector<Entry>& entries() const { return entries_; }
            // The entry called `name` (case-insensitive, '/' separators), or nullptr.
            const Entry* find(const std::string& name) const;

            // Receives the entry's bytes in pieces, with how many of its compressed bytes were consumed so
            // far; return false to stop.
            typedef std::function<bool(const std::uint8_t* data, std::size_t size, std::size_t compressedConsumed)> Sink;
            // Returns true when the whole entry was delivered, false when the sink stopped it.
            bool extract(const Entry& entry, const Sink& sink) const;

            // True when the file starts with the ZIP signature ("PK\3\4") - a cheap sniff.
            static bool hasSignature(const std::string& path);

        private:
            std::string path_;
            std::vector<Entry> entries_;
        };

    }
}

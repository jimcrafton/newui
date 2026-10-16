#include "io/zipfile.h"

#include <newui/utils.h>

#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace manyrows {
    namespace io {

        namespace {
            std::uint16_t rd16(const std::uint8_t* p) { return std::uint16_t(p[0] | (p[1] << 8)); }
            std::uint32_t rd32(const std::uint8_t* p) {
                return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
            }

            std::string lower(std::string text) {
                for (char& c : text) {
                    c = char(std::tolower((unsigned char)c));
                }
                return text;
            }

            std::ifstream openFile(const std::string& path) {
                return std::ifstream(std::filesystem::path(newui::utf8ToWide(path)), std::ios::binary);
            }

            constexpr std::size_t kPiece = 64 * 1024;   // output delivered to the sink at most this much at a time
        }

        bool ZipFile::hasSignature(const std::string& path) {
            std::ifstream file = openFile(path);
            std::uint8_t head[4] = {};
            file.read(reinterpret_cast<char*>(head), 4);
            return file.gcount() == 4 && head[0] == 'P' && head[1] == 'K' && (head[2] == 3 || head[2] == 5);
        }

        ZipFile::ZipFile(const std::string& path) : path_(path) {
            std::ifstream file = openFile(path);
            if (!file) {
                throw std::runtime_error("ZipFile::ZipFile: cannot open \"" + path + "\"");
            }
            file.seekg(0, std::ios::end);
            std::uint64_t fileSize = std::uint64_t(file.tellg());
            if (fileSize < 22) {
                throw std::runtime_error("ZipFile::ZipFile: not a ZIP archive");
            }
            // The end-of-central-directory record is in the last 22 + 65535 bytes (a comment may follow it).
            std::size_t tail = std::size_t(fileSize < 22 + 65535 ? fileSize : 22 + 65535);
            std::vector<std::uint8_t> end(tail);
            file.seekg(std::streamoff(fileSize - tail));
            file.read(reinterpret_cast<char*>(end.data()), std::streamsize(tail));
            std::size_t at = tail - 22 + 1;
            for (;;) {
                if (at == 0) {
                    throw std::runtime_error("ZipFile::ZipFile: not a ZIP archive (no end record)");
                }
                --at;
                if (rd32(end.data() + at) == 0x06054B50u) {
                    break;
                }
            }
            std::uint16_t count = rd16(end.data() + at + 10);
            std::uint32_t directorySize = rd32(end.data() + at + 12);
            std::uint32_t directoryOffset = rd32(end.data() + at + 16);
            if (count == 0xFFFF || directoryOffset == 0xFFFFFFFFu || directorySize == 0xFFFFFFFFu) {
                throw std::runtime_error("ZipFile::ZipFile: ZIP64 archives are not supported");
            }
            if (std::uint64_t(directoryOffset) + directorySize > fileSize) {
                throw std::runtime_error("ZipFile::ZipFile: the central directory is outside the file");
            }
            std::vector<std::uint8_t> directory(directorySize);
            file.seekg(directoryOffset);
            file.read(reinterpret_cast<char*>(directory.data()), std::streamsize(directorySize));
            if (!file) {
                throw std::runtime_error("ZipFile::ZipFile: cannot read the central directory");
            }
            std::size_t pos = 0;
            for (std::uint16_t i = 0; i < count; ++i) {
                if (pos + 46 > directory.size() || rd32(directory.data() + pos) != 0x02014B50u) {
                    throw std::runtime_error("ZipFile::ZipFile: the central directory is damaged");
                }
                const std::uint8_t* d = directory.data() + pos;
                Entry entry;
                entry.flags = rd16(d + 8);
                entry.method = rd16(d + 10);
                entry.compressedSize = rd32(d + 20);
                entry.size = rd32(d + 24);
                std::size_t nameLength = rd16(d + 28);
                std::size_t extraLength = rd16(d + 30);
                std::size_t commentLength = rd16(d + 32);
                entry.localOffset = rd32(d + 42);
                if (pos + 46 + nameLength + extraLength + commentLength > directory.size()) {
                    throw std::runtime_error("ZipFile::ZipFile: the central directory is damaged");
                }
                entry.name.assign(reinterpret_cast<const char*>(d + 46), nameLength);
                std::replace(entry.name.begin(), entry.name.end(), '\\', '/');
                entries_.push_back(std::move(entry));
                pos += 46 + nameLength + extraLength + commentLength;
            }
        }

        const ZipFile::Entry* ZipFile::find(const std::string& name) const {
            std::string wanted = lower(name);
            for (const Entry& entry : entries_) {
                if (lower(entry.name) == wanted) {
                    return &entry;
                }
            }
            return nullptr;
        }

        bool ZipFile::extract(const Entry& entry, const Sink& sink) const {
            if ((entry.flags & 1) != 0) {
                throw std::runtime_error("ZipFile::extract: \"" + entry.name + "\" is encrypted");
            }
            if (entry.method != 0 && entry.method != 8) {
                throw std::runtime_error("ZipFile::extract: \"" + entry.name + "\" uses an unsupported compression method");
            }
            if (entry.compressedSize == 0xFFFFFFFFu || entry.size == 0xFFFFFFFFu) {
                throw std::runtime_error("ZipFile::extract: ZIP64 entries are not supported");
            }
            std::ifstream file = openFile(path_);
            std::uint8_t local[30];
            file.seekg(entry.localOffset);
            file.read(reinterpret_cast<char*>(local), 30);
            if (!file || rd32(local) != 0x04034B50u) {
                throw std::runtime_error("ZipFile::extract: the local header of \"" + entry.name + "\" is damaged");
            }
            std::streamoff dataAt = std::streamoff(entry.localOffset) + 30 + rd16(local + 26) + rd16(local + 28);
            std::vector<std::uint8_t> packed(entry.compressedSize);
            file.seekg(dataAt);
            if (entry.compressedSize > 0) {
                file.read(reinterpret_cast<char*>(packed.data()), std::streamsize(packed.size()));
            }
            if (!file) {
                throw std::runtime_error("ZipFile::extract: \"" + entry.name + "\" is truncated");
            }

            if (entry.method == 0) {
                for (std::size_t at = 0; at < packed.size(); at += kPiece) {
                    std::size_t take = packed.size() - at < kPiece ? packed.size() - at : kPiece;
                    if (!sink(packed.data() + at, take, at + take)) {
                        return false;
                    }
                }
                return true;
            }

            z_stream stream;
            std::memset(&stream, 0, sizeof(stream));
            // The vendored zlib is built Z_SOLO (no default allocator): supply one.
            stream.zalloc = [](voidpf, uInt items, uInt size) -> voidpf { return std::calloc(items, size); };
            stream.zfree = [](voidpf, voidpf address) { std::free(address); };
            int started = inflateInit2(&stream, -MAX_WBITS);   // raw deflate, no zlib header
            if (started != Z_OK) {
                throw std::runtime_error("ZipFile::extract: cannot start decompression (zlib error " + std::to_string(started) + ")");
            }
            std::vector<std::uint8_t> out(kPiece);
            stream.next_in = packed.data();
            stream.avail_in = uInt(packed.size());
            int status = Z_OK;
            bool stopped = false;
            while (status != Z_STREAM_END && !stopped) {
                stream.next_out = out.data();
                stream.avail_out = uInt(out.size());
                status = inflate(&stream, Z_NO_FLUSH);
                if (status != Z_OK && status != Z_STREAM_END) {
                    inflateEnd(&stream);
                    throw std::runtime_error("ZipFile::extract: \"" + entry.name + "\" is corrupt (zlib error " + std::to_string(status) + ")");
                }
                std::size_t produced = out.size() - stream.avail_out;
                if (produced > 0 && !sink(out.data(), produced, std::size_t(stream.total_in))) {
                    stopped = true;
                }
                if (status == Z_OK && produced == 0 && stream.avail_in == 0) {
                    inflateEnd(&stream);
                    throw std::runtime_error("ZipFile::extract: \"" + entry.name + "\" ends too soon");
                }
            }
            inflateEnd(&stream);
            return !stopped;
        }

    }
}

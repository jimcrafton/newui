#include "io/compoundfile.h"

#include <newui/utils.h>

#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace manyrows {
    namespace io {

        namespace {
            const std::uint8_t kSignature[8] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};
            constexpr std::uint32_t kEndOfChain = 0xFFFFFFFEu;
            constexpr std::uint32_t kFreeSector = 0xFFFFFFFFu;
            constexpr std::uint32_t kMaxRegularSector = 0xFFFFFFFAu;

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
        }

        bool CompoundFile::hasSignature(const std::string& path) {
            std::ifstream file(std::filesystem::path(newui::utf8ToWide(path)), std::ios::binary);
            std::uint8_t head[8] = {};
            file.read(reinterpret_cast<char*>(head), 8);
            return file.gcount() == 8 && std::memcmp(head, kSignature, 8) == 0;
        }

        CompoundFile::CompoundFile(const std::string& path) {
            std::ifstream file(std::filesystem::path(newui::utf8ToWide(path)), std::ios::binary | std::ios::ate);
            if (!file) {
                throw std::runtime_error("CompoundFile::CompoundFile: cannot open \"" + path + "\"");
            }
            std::streamoff size = file.tellg();
            file.seekg(0);
            file_.resize(std::size_t(size));
            if (size > 0) {
                file.read(reinterpret_cast<char*>(file_.data()), size);
            }
            if (file_.size() < 512 || std::memcmp(file_.data(), kSignature, 8) != 0) {
                throw std::runtime_error("CompoundFile::CompoundFile: not a compound (OLE2) file");
            }
            const std::uint8_t* header = file_.data();
            std::uint16_t sectorShift = rd16(header + 0x1E);
            std::uint16_t miniShift = rd16(header + 0x20);
            if (sectorShift < 7 || sectorShift > 16 || miniShift > sectorShift) {
                throw std::runtime_error("CompoundFile::CompoundFile: unsupported sector size");
            }
            sectorSize_ = std::size_t(1) << sectorShift;
            miniSectorSize_ = std::size_t(1) << miniShift;
            miniCutoff_ = rd32(header + 0x38);
            std::uint32_t directoryStart = rd32(header + 0x30);
            std::uint32_t miniFatStart = rd32(header + 0x3C);
            std::uint32_t difatStart = rd32(header + 0x44);
            std::uint32_t difatCount = rd32(header + 0x48);
            const std::size_t perSector = sectorSize_ / 4;

            // The FAT's own sectors: 109 in the header, then a chain of DIFAT sectors.
            std::vector<std::uint32_t> fatSectors;
            for (std::size_t i = 0; i < 109; ++i) {
                std::uint32_t sector = rd32(header + 0x4C + i * 4);
                if (sector != kFreeSector) {
                    fatSectors.push_back(sector);
                }
            }
            std::uint32_t difat = difatStart;
            for (std::uint32_t n = 0; n < difatCount && difat <= kMaxRegularSector; ++n) {
                std::size_t offset = (std::size_t(difat) + 1) * sectorSize_;
                if (offset + sectorSize_ > file_.size()) {
                    throw std::runtime_error("CompoundFile::CompoundFile: truncated file");
                }
                for (std::size_t i = 0; i + 1 < perSector; ++i) {
                    std::uint32_t sector = rd32(file_.data() + offset + i * 4);
                    if (sector != kFreeSector) {
                        fatSectors.push_back(sector);
                    }
                }
                difat = rd32(file_.data() + offset + (perSector - 1) * 4);
            }
            for (std::uint32_t sector : fatSectors) {
                std::size_t offset = (std::size_t(sector) + 1) * sectorSize_;
                if (offset + sectorSize_ > file_.size()) {
                    throw std::runtime_error("CompoundFile::CompoundFile: truncated file");
                }
                for (std::size_t i = 0; i < perSector; ++i) {
                    fat_.push_back(rd32(file_.data() + offset + i * 4));
                }
            }

            std::vector<std::uint8_t> directory = readChain(directoryStart, fat_, false, 0, false);
            for (std::size_t at = 0; at + 128 <= directory.size(); at += 128) {
                const std::uint8_t* raw = directory.data() + at;
                Entry entry;
                entry.type = raw[0x42];
                std::size_t nameBytes = rd16(raw + 0x40);
                for (std::size_t i = 0; i + 1 < nameBytes && i < 64; i += 2) {
                    std::uint16_t unit = rd16(raw + i);
                    if (unit == 0) {
                        break;
                    }
                    entry.name += unit < 128 ? char(unit) : '?';
                }
                entry.start = rd32(raw + 0x74);
                entry.size = std::uint64_t(rd32(raw + 0x78)) | (std::uint64_t(rd32(raw + 0x7C)) << 32);
                if (sectorSize_ == 512) {
                    entry.size &= 0xFFFFFFFFull;   // version 3 files: the high half is junk
                }
                entries_.push_back(std::move(entry));
            }
            if (entries_.empty() || entries_[0].type != 5) {
                throw std::runtime_error("CompoundFile::CompoundFile: no root entry");
            }
            if (miniFatStart <= kMaxRegularSector) {
                std::vector<std::uint8_t> raw = readChain(miniFatStart, fat_, false, 0, false);
                for (std::size_t i = 0; i + 4 <= raw.size(); i += 4) {
                    miniFat_.push_back(rd32(raw.data() + i));
                }
            }
            if (entries_[0].start <= kMaxRegularSector) {
                miniStream_ = readChain(entries_[0].start, fat_, false, entries_[0].size, true);
            }
        }

        std::vector<std::uint8_t> CompoundFile::readChain(std::uint32_t start, const std::vector<std::uint32_t>& fat, bool mini,
            std::uint64_t size, bool sizeKnown) const {
            std::vector<std::uint8_t> out;
            const std::size_t unit = mini ? miniSectorSize_ : sectorSize_;
            const std::vector<std::uint8_t>& source = mini ? miniStream_ : file_;
            std::uint32_t sector = start;
            // A chain cannot be longer than the table it indexes; this stops loops in a corrupt file.
            for (std::size_t steps = 0; sector <= kMaxRegularSector && steps <= fat.size(); ++steps) {
                std::size_t offset = mini ? std::size_t(sector) * unit : (std::size_t(sector) + 1) * unit;
                if (offset + unit > source.size()) {
                    if (offset >= source.size()) {
                        throw std::runtime_error("CompoundFile::readChain: sector beyond the end of the file");
                    }
                    out.insert(out.end(), source.begin() + std::ptrdiff_t(offset), source.end());   // a short last sector
                    break;
                }
                out.insert(out.end(), source.begin() + std::ptrdiff_t(offset), source.begin() + std::ptrdiff_t(offset + unit));
                if (sector >= fat.size()) {
                    break;
                }
                sector = fat[sector];
            }
            if (sizeKnown) {
                if (out.size() < size) {
                    throw std::runtime_error("CompoundFile::readChain: stream is shorter than its directory entry says");
                }
                out.resize(std::size_t(size));
            }
            return out;
        }

        bool CompoundFile::hasStream(const std::string& name) const {
            std::string wanted = lower(name);
            for (const Entry& entry : entries_) {
                if (entry.type == 2 && lower(entry.name) == wanted) {
                    return true;
                }
            }
            return false;
        }

        std::vector<std::uint8_t> CompoundFile::readStream(const std::string& name) const {
            std::string wanted = lower(name);
            for (const Entry& entry : entries_) {
                if (entry.type != 2 || lower(entry.name) != wanted) {
                    continue;
                }
                if (entry.size < miniCutoff_) {
                    return readChain(entry.start, miniFat_, true, entry.size, true);
                }
                return readChain(entry.start, fat_, false, entry.size, true);
            }
            throw std::runtime_error("CompoundFile::readStream: no stream named \"" + name + "\"");
        }

    }
}

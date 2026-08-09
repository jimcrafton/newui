#pragma once

#include <string>

#include <blend2d/blend2d.h>

#include "newui/fontmanager.h"

namespace newui {

    // A font description (name, size, and style flags) that resolves to an
    // actual BLFont lazily, through FontManager, only when blFont() is
    // called. Font itself owns none of the BLFont data - the pointer
    // returned by blFont() is owned by FontManager's cache and stays valid
    // for the rest of the process's lifetime; Font just remembers which
    // cached instance it last resolved to and re-resolves when name/size/
    // bold/italic change.
    //
    // strikeThrough/underlined aren't resolved into the BLFont at all -
    // blend2d has no such concept on BLFont itself, so these are plain
    // storage for whoever paints this font's text to act on (e.g. stroking
    // an extra line through/under the drawn glyphs).
    class Font {
    public:
        Font() = default;
        Font(const std::string& name, float size) : name_(name), size_(size) {}

        const std::string& name() const { return name_; }
        void setName(const std::string& name) {
            if (name_ != name) {
                name_ = name;
                dirty_ = true;
            }
        }

        float size() const { return size_; }
        void setSize(float size) {
            if (size_ != size) {
                size_ = size;
                dirty_ = true;
            }
        }

        bool bold() const { return bold_; }
        void setBold(bool bold) {
            if (bold_ != bold) {
                bold_ = bold;
                dirty_ = true;
            }
        }

        bool italic() const { return italic_; }
        void setItalic(bool italic) {
            if (italic_ != italic) {
                italic_ = italic;
                dirty_ = true;
            }
        }

        bool strikeThrough() const { return strikeThrough_; }
        void setStrikeThrough(bool strikeThrough) { strikeThrough_ = strikeThrough; }

        bool underlined() const { return underlined_; }
        void setUnderlined(bool underlined) { underlined_ = underlined; }

        // Resolves (if name/size/bold/italic changed since the last call)
        // and returns the underlying BLFont via FontManager, which owns
        // it. Returns nullptr if name doesn't resolve to a loadable font -
        // see FontManager::createFont(). bold/italic are folded into the
        // name FontManager is asked to resolve (e.g. "Arial" -> "Arial
        // Bold"), matching how Windows registers style-specific font
        // faces under their own names.
        BLFont* blFont() const {
            if (dirty_) {
                std::string lookupName = name_;
                if (bold_ && italic_) {
                    lookupName += " Bold Italic";
                } else if (bold_) {
                    lookupName += " Bold";
                } else if (italic_) {
                    lookupName += " Italic";
                }

                blFont_ = FontManager::instance().getFont(lookupName, size_);
                dirty_ = false;
            }
            return blFont_;
        }

    private:
        std::string name_;
        float size_ = 12.0f;
        bool bold_ = false;
        bool italic_ = false;
        bool strikeThrough_ = false;
        bool underlined_ = false;

        mutable BLFont* blFont_ = nullptr;
        mutable bool dirty_ = true;
    };

}

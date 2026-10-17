#include "sketchsvg.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace sketch {

    namespace {

        const double kPi = 3.14159265358979323846;

        // ------------------------------------------------------------------ writing

        std::string num(double v) {
            if (std::fabs(v) < 1e-9) {
                return "0";
            }
            char buffer[48];
            std::snprintf(buffer, sizeof(buffer), "%.6g", v);
            return buffer;
        }

        std::string escape(const std::string& text) {
            std::string out;
            for (char c : text) {
                switch (c) {
                    case '&': out += "&amp;"; break;
                    case '<': out += "&lt;"; break;
                    case '>': out += "&gt;"; break;
                    case '"': out += "&quot;"; break;
                    case '\n': out += "&#10;"; break;   // a line break inside an attribute (the words of a text object)
                    default: out += c; break;
                }
            }
            return out;
        }

        const char* spaceName(ColorSpace space) {
            switch (space) {
                case ColorSpace::CMYK: return "cmyk";
                case ColorSpace::Gray: return "gray";
                case ColorSpace::HSB: return "hsb";
                default: return "rgb";
            }
        }

        std::size_t componentCount(ColorSpace space) {
            switch (space) {
                case ColorSpace::CMYK: return 4;
                case ColorSpace::Gray: return 1;
                default: return 3;
            }
        }

        // "cmyk 0.1 0.2 0.3 0.4" - the color in its own space.
        std::string nativeColor(const SketchColor& color) {
            std::string out = spaceName(color.space());
            for (std::size_t i = 0; i < componentCount(color.space()); ++i) {
                out += " " + num(color.component(i));
            }
            return out;
        }

        std::string pathData(const BLPath& path) {
            std::string d;
            const uint8_t* cmd = path.command_data();
            const BLPoint* v = path.vertex_data();
            std::size_t n = path.size();
            auto pt = [&](std::size_t i) { return num(v[i].x) + " " + num(v[i].y); };
            for (std::size_t i = 0; i < n; ++i) {
                switch (cmd[i]) {
                    case BL_PATH_CMD_MOVE: d += "M" + pt(i) + " "; break;
                    case BL_PATH_CMD_ON: d += "L" + pt(i) + " "; break;
                    case BL_PATH_CMD_QUAD:
                        if (i + 1 < n) { d += "Q" + pt(i) + " " + pt(i + 1) + " "; }
                        i += 1;
                        break;
                    case BL_PATH_CMD_CUBIC:
                        if (i + 2 < n) { d += "C" + pt(i) + " " + pt(i + 1) + " " + pt(i + 2) + " "; }
                        i += 2;
                        break;
                    case BL_PATH_CMD_CLOSE: d += "Z "; break;
                    default: break;
                }
            }
            if (!d.empty()) {
                d.pop_back();
            }
            return d;
        }

        void stopAttrs(std::ostringstream& out, const GradientStop& stop) {
            out << "<stop offset=\"" << num(stop.offset) << "\" stop-color=\"" << stop.color.toHex() << "\"";
            if (stop.color.alpha() < 1.0f) {
                out << " stop-opacity=\"" << num(stop.color.alpha()) << "\"";
            }
            if (stop.color.space() != ColorSpace::RGB) {
                out << " data-color=\"" << nativeColor(stop.color) << "\"";
            }
            out << "/>\n";
        }

        // The attribute(s) for one paint ("fill" or "stroke"); a gradient adds its element to `defs`.
        std::string paintAttrs(const char* name, const SketchPaint& paint, const BLBox& box, std::ostringstream& defs, int& gradientCount) {
            std::string prefix = name;
            std::ostringstream out;
            if (paint.isNone()) {
                out << " " << prefix << "=\"none\"";
                return out.str();
            }
            double w = box.x1 - box.x0, h = box.y1 - box.y0;
            bool degenerate = w < 1e-6 && h < 1e-6;
            if (paint.isGradient() && !paint.gradient().stops.empty() && !degenerate) {
                const SketchGradient& g = paint.gradient();
                std::string id = "grad" + std::to_string(++gradientCount);
                const char* spread = g.spread == GradientSpread::Repeat ? "repeat" : (g.spread == GradientSpread::Reflect ? "reflect" : "pad");
                bool linear = paint.kind() == SketchPaint::Kind::Linear;
                BLPoint start, end;
                g.axisPoints(box, !linear, start, end);
                // data-axis: the explicit axis as box fractions, so reading it back keeps it tied to the object.
                std::string axis;
                if (g.hasAxis) {
                    axis = " data-axis=\"" + num(g.axisX1) + " " + num(g.axisY1) + " " + num(g.axisX2) + " " + num(g.axisY2) + "\"";
                }
                // The squash (radial aspect) and lean (skew) go out as a standard gradientTransform so other viewers
                // draw them too, plus our own numbers to read back exactly.
                std::string shape;
                if (g.hasAxis && (g.axisSkew != 0.0 || (!linear && g.axisAspect != 1.0))) {
                    BLMatrix2D m = g.axisMatrix(start, end, !linear);
                    shape = " gradientTransform=\"matrix(" + num(m.m00) + " " + num(m.m01) + " " + num(m.m10) + " " + num(m.m11) + " " +
                        num(m.m20) + " " + num(m.m21) + ")\" data-aspect=\"" + num(g.axisAspect) + "\" data-skew=\"" + num(g.axisSkew) + "\"";
                }
                if (g.alongStroke) {
                    shape += " data-along-stroke=\"1\"";
                }
                if (linear) {
                    defs << "<linearGradient id=\"" << id << "\" gradientUnits=\"userSpaceOnUse\" x1=\"" << num(start.x)
                         << "\" y1=\"" << num(start.y) << "\" x2=\"" << num(end.x) << "\" y2=\"" << num(end.y)
                         << "\" spreadMethod=\"" << spread << "\" data-angle=\"" << num(g.angleDegrees) << "\"" << axis << shape << ">\n";
                } else {
                    defs << "<radialGradient id=\"" << id << "\" gradientUnits=\"userSpaceOnUse\" cx=\"" << num(start.x) << "\" cy=\"" << num(start.y)
                         << "\" fx=\"" << num(start.x) << "\" fy=\"" << num(start.y) << "\" r=\"" << num(std::hypot(end.x - start.x, end.y - start.y))
                         << "\" spreadMethod=\"" << spread << "\"" << axis << shape << ">\n";
                }
                for (const GradientStop& stop : g.stops) {
                    defs << "  ";
                    stopAttrs(defs, stop);
                }
                defs << (paint.kind() == SketchPaint::Kind::Linear ? "</linearGradient>\n" : "</radialGradient>\n");
                out << " " << prefix << "=\"url(#" << id << ")\"";
                return out.str();
            }
            if (paint.isPattern()) {
                // A flat grey for other viewers; the tile is in the pattern library (<defs>) and found again by its id.
                out << " " << prefix << "=\"#808080\" data-" << prefix << "-pattern=\"" << paint.patternId() << "," << num(paint.patternScale()) << "," << num(paint.patternAngle()) << "\"";
                return out.str();
            }
            const SketchColor& color = paint.color();
            out << " " << prefix << "=\"" << color.toHex() << "\"";
            if (color.alpha() < 1.0f) {
                out << " " << prefix << "-opacity=\"" << num(color.alpha()) << "\"";
            }
            if (color.space() != ColorSpace::RGB) {
                out << " data-" << prefix << "-color=\"" << nativeColor(color) << "\"";
            }
            return out.str();
        }

        // The ruler guides as the root's data-guides attribute: "v100 h50.5" (v = a vertical line at x, h = a horizontal one at y).
        std::string guideAttr(const SketchDocument& document) {
            if (document.guides().empty()) {
                return std::string();
            }
            std::string text = " data-guides=\"";
            for (std::size_t i = 0; i < document.guides().size(); ++i) {
                text += (i ? " " : "") + std::string(document.guides()[i].vertical ? "v" : "h") + num(document.guides()[i].position);
            }
            return text + "\"";
        }

        // The layer tree as the root's data-layers attribute: entries "id,parent,flags,name" separated by ';' (flags: h hidden, l locked,
        // c closed in the panel, - none; the name has every character but letters and digits written as %XX). Siblings are in stacking
        // order. A document with just the one untouched layer writes nothing.
        std::string encodeName(const std::string& name) {
            static const char* digits = "0123456789ABCDEF";
            std::string out;
            for (unsigned char c : name) {
                if (std::isalnum(c)) {
                    out += char(c);
                } else {
                    out += '%';
                    out += digits[c >> 4];
                    out += digits[c & 15];
                }
            }
            return out;
        }

        std::string decodeName(const std::string& text) {
            std::string out;
            for (std::size_t i = 0; i < text.size(); ++i) {
                if (text[i] == '%' && i + 2 < text.size() && std::isxdigit((unsigned char)text[i + 1]) && std::isxdigit((unsigned char)text[i + 2])) {
                    out += char(std::stoi(text.substr(i + 1, 2), nullptr, 16));
                    i += 2;
                } else {
                    out += text[i];
                }
            }
            return out;
        }

        std::string layerAttr(const SketchDocument& document) {
            const std::vector<Layer>& layers = document.layers();
            if (layers.size() == 1 && layers[0].name == "Layer 1" && layers[0].visible && !layers[0].locked) {
                return std::string();
            }
            std::string text = " data-layers=\"";
            for (std::size_t i = 0; i < layers.size(); ++i) {
                const Layer& l = layers[i];
                std::string flags = std::string(l.visible ? "" : "h") + (l.locked ? "l" : "") + (l.expanded ? "" : "c");
                text += (i ? ";" : "") + std::to_string(l.id) + "," + std::to_string(l.parent) + "," + (flags.empty() ? "-" : flags) + "," + encodeName(l.name);
            }
            return text + "\"";
        }

        std::vector<Layer> parseLayers(const std::string& text) {
            std::vector<Layer> layers;
            std::istringstream in(text);
            std::string entry;
            while (std::getline(in, entry, ';')) {
                std::vector<std::string> parts;
                std::istringstream fields(entry);
                std::string part;
                while (std::getline(fields, part, ',')) {
                    parts.push_back(part);
                }
                if (parts.size() < 3) {
                    continue;
                }
                Layer l;
                try {
                    l.id = std::size_t(std::stoull(parts[0]));
                    l.parent = std::size_t(std::stoull(parts[1]));
                } catch (...) {
                    continue;
                }
                if (l.id == 0) {
                    continue;
                }
                l.visible = parts[2].find('h') == std::string::npos;
                l.locked = parts[2].find('l') != std::string::npos;
                l.expanded = parts[2].find('c') == std::string::npos;
                l.name = parts.size() > 3 ? decodeName(parts[3]) : "Layer " + std::to_string(l.id);
                layers.push_back(l);
            }
            // A parent the list does not have (or itself) makes the layer a top-level one.
            for (Layer& l : layers) {
                bool known = false;
                for (const Layer& p : layers) {
                    known = known || (p.id == l.parent && p.id != l.id);
                }
                if (!known) {
                    l.parent = 0;
                }
            }
            return layers;
        }

        // The swatches as the root's data-swatches attribute ("#aabbcc #112233"); nothing is written while they are the defaults.
        std::string swatchAttr(const SketchDocument& document) {
            if (document.swatches() == defaultSwatches()) {
                return std::string();
            }
            std::string text = " data-swatches=\"";
            for (std::size_t i = 0; i < document.swatches().size(); ++i) {
                text += (i ? " " : "") + document.swatches()[i];
            }
            return text + "\"";
        }

        std::vector<std::string> parseSwatches(const std::string& text) {
            std::vector<std::string> swatches;
            std::istringstream in(text);
            std::string token;
            while (in >> token) {
                bool ok = token.size() == 7 && token[0] == '#';
                for (std::size_t i = 1; ok && i < 7; ++i) {
                    ok = std::isxdigit((unsigned char)token[i]) != 0;
                }
                if (ok) {
                    swatches.push_back(token);
                }
            }
            return swatches;
        }

        // The artboards as the root's data-artboards attribute: entries "name,x,y,width,height" separated by ';' (the name written with
        // encodeName), and data-active-artboard (an index). The root's viewBox is the box round all of them.
        std::string artboardAttr(const SketchDocument& document) {
            std::string text = " data-artboards=\"";
            for (std::size_t i = 0; i < document.artboards().size(); ++i) {
                const Artboard& b = document.artboards()[i];
                text += (i ? ";" : "") + encodeName(b.name) + "," + num(b.x) + "," + num(b.y) + "," + num(b.width) + "," + num(b.height);
            }
            return text + "\" data-active-artboard=\"" + std::to_string(document.activeArtboard()) + "\"";
        }

        std::vector<Artboard> parseArtboards(const std::string& text) {
            std::vector<Artboard> boards;
            std::istringstream entries(text);
            std::string entry;
            while (std::getline(entries, entry, ';')) {
                std::vector<std::string> parts;
                std::istringstream in(entry);
                std::string part;
                while (std::getline(in, part, ',')) {
                    parts.push_back(part);
                }
                if (parts.size() != 5) {
                    continue;
                }
                Artboard b;
                b.name = decodeName(parts[0]);
                b.x = std::strtod(parts[1].c_str(), nullptr);
                b.y = std::strtod(parts[2].c_str(), nullptr);
                b.width = std::strtod(parts[3].c_str(), nullptr);
                b.height = std::strtod(parts[4].c_str(), nullptr);
                if (std::isfinite(b.x) && std::isfinite(b.y) && std::isfinite(b.width) && std::isfinite(b.height) && b.width >= 1.0 && b.height >= 1.0) {
                    boards.push_back(b);
                }
            }
            return boards;
        }

        std::vector<Guide> parseGuides(const std::string& text) {
            std::vector<Guide> guides;
            std::istringstream in(text);
            std::string token;
            while (in >> token) {
                if (token.size() >= 2 && (token[0] == 'v' || token[0] == 'h')) {
                    char* end = nullptr;
                    double position = std::strtod(token.c_str() + 1, &end);
                    if (end != token.c_str() + 1 && std::isfinite(position)) {
                        guides.push_back(Guide{token[0] == 'v', position});
                    }
                }
            }
            return guides;
        }

        const char* typeName(SketchObjectType type) {
            switch (type) {
                case SketchObjectType::Line: return "Line";
                case SketchObjectType::Rect: return "Rect";
                case SketchObjectType::RoundRect: return "RoundRect";
                case SketchObjectType::Ellipse: return "Ellipse";
                case SketchObjectType::Polygon: return "Polygon";
                case SketchObjectType::Polyline: return "Polyline";
                case SketchObjectType::Text: return "Text";
                case SketchObjectType::Symbol: return "Symbol";
                default: return "Path";
            }
        }

        // ------------------------------------------------------------------ XML

        struct XmlNode {
            std::string name;
            std::map<std::string, std::string> attrs;
            std::vector<std::unique_ptr<XmlNode>> children;

            const std::string* attr(const std::string& key) const {
                auto it = attrs.find(key);
                return it == attrs.end() ? nullptr : &it->second;
            }
        };

        std::string unescape(const std::string& text) {
            std::string out;
            for (std::size_t i = 0; i < text.size(); ++i) {
                if (text[i] != '&') {
                    out += text[i];
                    continue;
                }
                std::size_t end = text.find(';', i);
                if (end == std::string::npos || end - i > 10) {
                    out += text[i];
                    continue;
                }
                std::string entity = text.substr(i + 1, end - i - 1);
                if (entity == "amp") { out += '&'; }
                else if (entity == "lt") { out += '<'; }
                else if (entity == "gt") { out += '>'; }
                else if (entity == "quot") { out += '"'; }
                else if (entity == "apos") { out += '\''; }
                else if (!entity.empty() && entity[0] == '#') {
                    unsigned long code = entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X')
                        ? std::strtoul(entity.c_str() + 2, nullptr, 16) : std::strtoul(entity.c_str() + 1, nullptr, 10);
                    if (code < 0x80) {
                        out += char(code);
                    } else if (code < 0x800) {
                        out += char(0xC0 | (code >> 6));
                        out += char(0x80 | (code & 0x3F));
                    } else if (code < 0x10000) {
                        out += char(0xE0 | (code >> 12));
                        out += char(0x80 | ((code >> 6) & 0x3F));
                        out += char(0x80 | (code & 0x3F));
                    } else {
                        out += char(0xF0 | (code >> 18));
                        out += char(0x80 | ((code >> 12) & 0x3F));
                        out += char(0x80 | ((code >> 6) & 0x3F));
                        out += char(0x80 | (code & 0x3F));
                    }
                } else {
                    out += text.substr(i, end - i + 1);
                }
                i = end;
            }
            return out;
        }

        // Elements and attributes only: text, comments, declarations, DOCTYPE and CDATA are skipped.
        class XmlParser {
        public:
            explicit XmlParser(const std::string& text) : s_(text) {}

            std::unique_ptr<XmlNode> parseDocument() {
                std::unique_ptr<XmlNode> root;
                while (pos_ < s_.size()) {
                    skipMisc();
                    if (pos_ >= s_.size()) {
                        break;
                    }
                    if (s_[pos_] != '<') {
                        ++pos_;
                        continue;
                    }
                    root = parseElement();
                    if (!root) {
                        return nullptr;
                    }
                    break;
                }
                return root;
            }

        private:
            bool startsWith(const char* prefix) const { return s_.compare(pos_, std::char_traits<char>::length(prefix), prefix) == 0; }

            // Skips whitespace, comments, <?...?>, <!DOCTYPE ...> and text (outside of elements).
            void skipMisc() {
                for (;;) {
                    while (pos_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[pos_]))) { ++pos_; }
                    if (startsWith("<!--")) {
                        std::size_t end = s_.find("-->", pos_ + 4);
                        pos_ = end == std::string::npos ? s_.size() : end + 3;
                    } else if (startsWith("<?")) {
                        std::size_t end = s_.find("?>", pos_ + 2);
                        pos_ = end == std::string::npos ? s_.size() : end + 2;
                    } else if (startsWith("<![CDATA[")) {
                        std::size_t end = s_.find("]]>", pos_ + 9);
                        pos_ = end == std::string::npos ? s_.size() : end + 3;
                    } else if (startsWith("<!")) {
                        // DOCTYPE, possibly with an internal [ ... ] subset.
                        int depth = 0;
                        while (pos_ < s_.size()) {
                            char c = s_[pos_++];
                            if (c == '[') { ++depth; }
                            else if (c == ']') { --depth; }
                            else if (c == '>' && depth <= 0) { break; }
                        }
                    } else {
                        return;
                    }
                }
            }

            static bool nameChar(char c) {
                return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == ':' || c == '.';
            }

            std::unique_ptr<XmlNode> parseElement() {
                ++pos_;  // '<'
                std::unique_ptr<XmlNode> node(new XmlNode());
                while (pos_ < s_.size() && nameChar(s_[pos_])) { node->name += s_[pos_++]; }
                if (node->name.empty()) {
                    return nullptr;
                }
                // Namespace prefixes (svg:rect) are dropped; xlink:href is kept as written.
                std::size_t colon = node->name.find(':');
                if (colon != std::string::npos) {
                    node->name = node->name.substr(colon + 1);
                }
                for (;;) {
                    while (pos_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[pos_]))) { ++pos_; }
                    if (pos_ >= s_.size()) {
                        return nullptr;
                    }
                    if (s_[pos_] == '/') {
                        if (pos_ + 1 < s_.size() && s_[pos_ + 1] == '>') {
                            pos_ += 2;
                            return node;
                        }
                        return nullptr;
                    }
                    if (s_[pos_] == '>') {
                        ++pos_;
                        break;
                    }
                    std::string key;
                    while (pos_ < s_.size() && nameChar(s_[pos_])) { key += s_[pos_++]; }
                    if (key.empty()) {
                        return nullptr;
                    }
                    while (pos_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[pos_]))) { ++pos_; }
                    if (pos_ >= s_.size() || s_[pos_] != '=') {
                        return nullptr;
                    }
                    ++pos_;
                    while (pos_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[pos_]))) { ++pos_; }
                    if (pos_ >= s_.size() || (s_[pos_] != '"' && s_[pos_] != '\'')) {
                        return nullptr;
                    }
                    char quote = s_[pos_++];
                    std::size_t end = s_.find(quote, pos_);
                    if (end == std::string::npos) {
                        return nullptr;
                    }
                    node->attrs[key] = unescape(s_.substr(pos_, end - pos_));
                    pos_ = end + 1;
                }
                // Content until the matching close tag.
                for (;;) {
                    std::size_t lt = s_.find('<', pos_);
                    if (lt == std::string::npos) {
                        return nullptr;
                    }
                    pos_ = lt;
                    if (startsWith("<!--") || startsWith("<?") || startsWith("<![CDATA[")) {
                        skipMisc();
                    } else if (startsWith("</")) {
                        std::size_t end = s_.find('>', pos_);
                        if (end == std::string::npos) {
                            return nullptr;
                        }
                        pos_ = end + 1;
                        return node;
                    } else {
                        std::unique_ptr<XmlNode> child = parseElement();
                        if (!child) {
                            return nullptr;
                        }
                        node->children.push_back(std::move(child));
                    }
                }
            }

            const std::string& s_;
            std::size_t pos_ = 0;
        };

        // ------------------------------------------------------------------ values

        std::string trim(const std::string& text) {
            std::size_t a = 0, b = text.size();
            while (a < b && std::isspace(static_cast<unsigned char>(text[a]))) { ++a; }
            while (b > a && std::isspace(static_cast<unsigned char>(text[b - 1]))) { --b; }
            return text.substr(a, b - a);
        }

        // A number with an optional unit; `percentBase` scales "50%". Returns false when there is no number.
        bool parseLength(const std::string* text, double& out, double percentBase = 1.0) {
            if (text == nullptr) {
                return false;
            }
            std::string t = trim(*text);
            if (t.empty()) {
                return false;
            }
            char* end = nullptr;
            double v = std::strtod(t.c_str(), &end);
            if (end == t.c_str()) {
                return false;
            }
            std::string unit = trim(std::string(end));
            if (unit == "%") { v = v / 100.0 * percentBase; }
            else if (unit == "px") { v *= 0.75; }
            else if (unit == "in") { v *= 72.0; }
            else if (unit == "mm") { v *= 72.0 / 25.4; }
            else if (unit == "cm") { v *= 72.0 / 2.54; }
            else if (unit == "pc") { v *= 12.0; }
            out = v;
            return true;
        }

        // A plain number, no unit conversion (user units: coordinates inside shapes, opacities, offsets).
        double number(const std::string* text, double fallback) {
            if (text == nullptr) {
                return fallback;
            }
            std::string t = trim(*text);
            char* end = nullptr;
            double v = std::strtod(t.c_str(), &end);
            if (end == t.c_str()) {
                return fallback;
            }
            if (*end == '%') {
                v /= 100.0;
            }
            return v;
        }

        std::vector<double> numberList(const std::string& text) {
            std::vector<double> values;
            const char* p = text.c_str();
            while (*p != '\0') {
                while (*p != '\0' && (std::isspace(static_cast<unsigned char>(*p)) || *p == ',')) { ++p; }
                if (*p == '\0') {
                    break;
                }
                char* end = nullptr;
                double v = std::strtod(p, &end);
                if (end == p) {
                    break;
                }
                values.push_back(v);
                p = end;
            }
            return values;
        }

        struct NamedColor { const char* name; const char* hex; };
        const NamedColor kNamedColors[] = {
            {"black", "#000000"}, {"white", "#ffffff"}, {"red", "#ff0000"}, {"green", "#008000"}, {"lime", "#00ff00"},
            {"blue", "#0000ff"}, {"yellow", "#ffff00"}, {"cyan", "#00ffff"}, {"aqua", "#00ffff"}, {"magenta", "#ff00ff"},
            {"fuchsia", "#ff00ff"}, {"gray", "#808080"}, {"grey", "#808080"}, {"silver", "#c0c0c0"}, {"maroon", "#800000"},
            {"olive", "#808000"}, {"navy", "#000080"}, {"purple", "#800080"}, {"teal", "#008080"}, {"orange", "#ffa500"},
            {"pink", "#ffc0cb"}, {"brown", "#a52a2a"}, {"gold", "#ffd700"}, {"lightgray", "#d3d3d3"}, {"lightgrey", "#d3d3d3"},
            {"darkgray", "#a9a9a9"}, {"darkgrey", "#a9a9a9"}, {"transparent", "#00000000"},
        };

        // "#rgb", "#rrggbb", "#rrggbbaa", "rgb(r,g,b)" / "rgb(r%,g%,b%)", or a CSS color name.
        bool parseColor(const std::string& text, SketchColor& out) {
            std::string t = trim(text);
            if (t.empty()) {
                return false;
            }
            try {
                if (t[0] == '#') {
                    if (t.size() == 4) {
                        std::string full = "#";
                        for (std::size_t i = 1; i < 4; ++i) { full += t[i]; full += t[i]; }
                        out = SketchColor::fromHex(full);
                    } else {
                        out = SketchColor::fromHex(t);
                    }
                    return true;
                }
                if (t.compare(0, 4, "rgb(") == 0 || t.compare(0, 5, "rgba(") == 0) {
                    std::size_t open = t.find('(');
                    std::size_t close = t.find(')');
                    if (close == std::string::npos) {
                        return false;
                    }
                    std::string body = t.substr(open + 1, close - open - 1);
                    bool percent = body.find('%') != std::string::npos;
                    std::vector<double> v = numberList(body);
                    // numberList() stops at '%', so strip them first.
                    if (percent) {
                        std::string stripped;
                        for (char c : body) { stripped += c == '%' ? ' ' : c; }
                        v = numberList(stripped);
                    }
                    if (v.size() < 3) {
                        return false;
                    }
                    double scale = percent ? 100.0 : 255.0;
                    double alpha = v.size() > 3 ? v[3] : 1.0;
                    out = SketchColor::rgb(float(v[0] / scale), float(v[1] / scale), float(v[2] / scale), float(alpha));
                    return true;
                }
                std::string lower;
                for (char c : t) { lower += char(std::tolower(static_cast<unsigned char>(c))); }
                for (const NamedColor& named : kNamedColors) {
                    if (lower == named.name) {
                        out = SketchColor::fromHex(named.hex);
                        return true;
                    }
                }
            } catch (const std::exception&) {
                return false;
            }
            return false;
        }

        // The color in its own space from data-*-color ("cmyk 0.1 0.2 0.3 0.4"), keeping `alpha`.
        bool parseNativeColor(const std::string* text, float alpha, SketchColor& out) {
            if (text == nullptr) {
                return false;
            }
            std::istringstream in(*text);
            std::string space;
            in >> space;
            std::vector<double> v;
            double x;
            while (in >> x) { v.push_back(x); }
            auto f = [&](std::size_t i) { return i < v.size() ? float(v[i]) : 0.0f; };
            if (space == "cmyk" && v.size() >= 4) { out = SketchColor::cmyk(f(0), f(1), f(2), f(3), alpha); return true; }
            if (space == "gray" && v.size() >= 1) { out = SketchColor::gray(f(0), alpha); return true; }
            if (space == "hsb" && v.size() >= 3) { out = SketchColor::hsb(f(0), f(1), f(2), alpha); return true; }
            if (space == "rgb" && v.size() >= 3) { out = SketchColor::rgb(f(0), f(1), f(2), alpha); return true; }
            return false;
        }

        // ------------------------------------------------------------------ transforms

        BLMatrix2D identity() { return BLMatrix2D::make_identity(); }

        // result = first applied, then `then` (SVG lists transforms left to right as nested, outermost first).
        BLMatrix2D concat(const BLMatrix2D& outer, const BLMatrix2D& inner) {
            BLMatrix2D m = outer;
            m.transform(inner);  // Blend2D's transform() applies the argument first: inner, then outer
            return m;
        }

        BLMatrix2D parseTransform(const std::string& text) {
            BLMatrix2D result = identity();
            std::size_t pos = 0;
            while (pos < text.size()) {
                std::size_t open = text.find('(', pos);
                if (open == std::string::npos) {
                    break;
                }
                std::size_t close = text.find(')', open);
                if (close == std::string::npos) {
                    break;
                }
                std::string name = trim(text.substr(pos, open - pos));
                while (!name.empty() && (name[0] == ',' || std::isspace(static_cast<unsigned char>(name[0])))) { name.erase(0, 1); }
                std::vector<double> a = numberList(text.substr(open + 1, close - open - 1));
                BLMatrix2D m = identity();
                if (name == "translate" && !a.empty()) {
                    m = BLMatrix2D::make_translation(a[0], a.size() > 1 ? a[1] : 0.0);
                } else if (name == "scale" && !a.empty()) {
                    m = BLMatrix2D::make_scaling(a[0], a.size() > 1 ? a[1] : a[0]);
                } else if (name == "rotate" && !a.empty()) {
                    double angle = a[0] * kPi / 180.0;
                    double cx = a.size() > 2 ? a[1] : 0.0, cy = a.size() > 2 ? a[2] : 0.0;
                    m = BLMatrix2D::make_translation(-cx, -cy);
                    m.post_rotate(angle);
                    m.post_translate(cx, cy);
                } else if (name == "skewX" && !a.empty()) {
                    m = BLMatrix2D(1.0, 0.0, std::tan(a[0] * kPi / 180.0), 1.0, 0.0, 0.0);
                } else if (name == "skewY" && !a.empty()) {
                    m = BLMatrix2D(1.0, std::tan(a[0] * kPi / 180.0), 0.0, 1.0, 0.0, 0.0);
                } else if (name == "matrix" && a.size() >= 6) {
                    m = BLMatrix2D(a[0], a[1], a[2], a[3], a[4], a[5]);
                }
                result = concat(result, m);
                pos = close + 1;
            }
            return result;
        }

        // ------------------------------------------------------------------ path data

        bool parsePathData(const std::string& d, BLPath& path) {
            const char* p = d.c_str();
            auto skip = [&]() { while (*p != '\0' && (std::isspace(static_cast<unsigned char>(*p)) || *p == ',')) { ++p; } };
            auto isNumberStart = [&]() { skip(); return std::isdigit(static_cast<unsigned char>(*p)) || *p == '-' || *p == '+' || *p == '.'; };
            auto readNumber = [&](double& out) {
                skip();
                char* end = nullptr;
                out = std::strtod(p, &end);
                if (end == p) {
                    return false;
                }
                p = end;
                return true;
            };
            auto readFlag = [&](bool& out) {
                skip();
                if (*p != '0' && *p != '1') {
                    return false;
                }
                out = *p == '1';
                ++p;
                return true;
            };

            double cx = 0, cy = 0, sx = 0, sy = 0;     // current point, subpath start
            double lcx = 0, lcy = 0;                    // last control point (for S/T)
            char prev = 0;
            char cmd = 0;
            bool needMove = true;                       // a drawing command after Z starts at the subpath start
            skip();
            while (*p != '\0') {
                if (std::isalpha(static_cast<unsigned char>(*p))) {
                    cmd = *p++;
                } else if (cmd == 0) {
                    return false;
                } else if (cmd == 'M') {
                    cmd = 'L';
                } else if (cmd == 'm') {
                    cmd = 'l';
                } else if (cmd == 'Z' || cmd == 'z') {
                    return false;
                }
                bool rel = std::islower(static_cast<unsigned char>(cmd)) != 0;
                char up = char(std::toupper(static_cast<unsigned char>(cmd)));
                double ox = rel ? cx : 0.0, oy = rel ? cy : 0.0;
                if (up == 'Z') {
                    path.close();
                    cx = sx;
                    cy = sy;
                    needMove = true;
                    prev = 'Z';
                    skip();
                    continue;
                }
                if (!isNumberStart()) {
                    return false;
                }
                auto beginDraw = [&]() {
                    if (needMove) {
                        path.move_to(cx, cy);
                        sx = cx;
                        sy = cy;
                        needMove = false;
                    }
                };
                double x1, y1, x2, y2, x, y;
                switch (up) {
                    case 'M':
                        if (!readNumber(x) || !readNumber(y)) { return false; }
                        cx = ox + x; cy = oy + y;
                        path.move_to(cx, cy);
                        sx = cx; sy = cy;
                        needMove = false;
                        break;
                    case 'L':
                        if (!readNumber(x) || !readNumber(y)) { return false; }
                        beginDraw();
                        cx = ox + x; cy = oy + y;
                        path.line_to(cx, cy);
                        break;
                    case 'H':
                        if (!readNumber(x)) { return false; }
                        beginDraw();
                        cx = ox + x;
                        path.line_to(cx, cy);
                        break;
                    case 'V':
                        if (!readNumber(y)) { return false; }
                        beginDraw();
                        cy = oy + y;
                        path.line_to(cx, cy);
                        break;
                    case 'C':
                        if (!readNumber(x1) || !readNumber(y1) || !readNumber(x2) || !readNumber(y2) || !readNumber(x) || !readNumber(y)) { return false; }
                        beginDraw();
                        path.cubic_to(ox + x1, oy + y1, ox + x2, oy + y2, ox + x, oy + y);
                        lcx = ox + x2; lcy = oy + y2;
                        cx = ox + x; cy = oy + y;
                        break;
                    case 'S':
                        if (!readNumber(x2) || !readNumber(y2) || !readNumber(x) || !readNumber(y)) { return false; }
                        beginDraw();
                        x1 = (prev == 'C' || prev == 'S') ? 2 * cx - lcx : cx;
                        y1 = (prev == 'C' || prev == 'S') ? 2 * cy - lcy : cy;
                        path.cubic_to(x1, y1, ox + x2, oy + y2, ox + x, oy + y);
                        lcx = ox + x2; lcy = oy + y2;
                        cx = ox + x; cy = oy + y;
                        break;
                    case 'Q':
                        if (!readNumber(x1) || !readNumber(y1) || !readNumber(x) || !readNumber(y)) { return false; }
                        beginDraw();
                        path.quad_to(ox + x1, oy + y1, ox + x, oy + y);
                        lcx = ox + x1; lcy = oy + y1;
                        cx = ox + x; cy = oy + y;
                        break;
                    case 'T':
                        if (!readNumber(x) || !readNumber(y)) { return false; }
                        beginDraw();
                        x1 = (prev == 'Q' || prev == 'T') ? 2 * cx - lcx : cx;
                        y1 = (prev == 'Q' || prev == 'T') ? 2 * cy - lcy : cy;
                        path.quad_to(x1, y1, ox + x, oy + y);
                        lcx = x1; lcy = y1;
                        cx = ox + x; cy = oy + y;
                        break;
                    case 'A': {
                        double rx, ry, rot;
                        bool large, sweep;
                        if (!readNumber(rx) || !readNumber(ry) || !readNumber(rot) || !readFlag(large) || !readFlag(sweep) || !readNumber(x) || !readNumber(y)) { return false; }
                        beginDraw();
                        cx = ox + x; cy = oy + y;
                        if (std::fabs(rx) < 1e-12 || std::fabs(ry) < 1e-12) {
                            path.line_to(cx, cy);
                        } else {
                            path.elliptic_arc_to(std::fabs(rx), std::fabs(ry), rot * kPi / 180.0, large, sweep, cx, cy);
                        }
                        break;
                    }
                    default:
                        return false;
                }
                prev = up;
                skip();
            }
            return true;
        }

        // ------------------------------------------------------------------ reading

        struct Style {
            std::map<std::string, std::string> props;

            const std::string* get(const std::string& key) const {
                auto it = props.find(key);
                return it == props.end() ? nullptr : &it->second;
            }
        };

        // Adds the element's presentation attributes and style="" declarations on top of the inherited ones.
        Style styleFor(const XmlNode& node, const Style& inherited) {
            Style style = inherited;
            // Not inherited.
            style.props.erase("opacity");
            style.props.erase("display");
            style.props.erase("mix-blend-mode");
            static const char* kKeys[] = {
                "fill", "fill-opacity", "stroke", "stroke-width", "stroke-opacity", "opacity", "display",
                "stroke-linecap", "stroke-linejoin", "stroke-miterlimit", "stroke-dasharray", "data-width-profile",
                "data-fill-color", "data-stroke-color", "data-fill-pattern", "data-stroke-pattern",
            };
            for (const char* key : kKeys) {
                if (const std::string* v = node.attr(key)) {
                    style.props[key] = *v;
                }
            }
            if (const std::string* css = node.attr("style")) {
                std::size_t pos = 0;
                while (pos < css->size()) {
                    std::size_t end = css->find(';', pos);
                    if (end == std::string::npos) {
                        end = css->size();
                    }
                    std::string decl = css->substr(pos, end - pos);
                    std::size_t colon = decl.find(':');
                    if (colon != std::string::npos) {
                        style.props[trim(decl.substr(0, colon))] = trim(decl.substr(colon + 1));
                    }
                    pos = end + 1;
                }
            }
            return style;
        }

        struct ReadContext {
            std::map<std::string, const XmlNode*> byId;
            SketchDocument* document = nullptr;
            float pageWidth = 612.0f, pageHeight = 792.0f;
            std::vector<std::size_t> groupStack;  // the <g> elements being read (document group ids, outermost first)
        };

        void collectIds(const XmlNode& node, std::map<std::string, const XmlNode*>& out) {
            if (const std::string* id = node.attr("id")) {
                out[*id] = &node;
            }
            for (const auto& child : node.children) {
                collectIds(*child, out);
            }
        }

        // The <stop> children of a gradient, following href chains for stops it does not define itself.
        void gradientStops(const ReadContext& ctx, const XmlNode& gradient, SketchGradient& out, int depth = 0) {
            for (const auto& child : gradient.children) {
                if (child->name != "stop") {
                    continue;
                }
                Style style = styleFor(*child, Style());
                GradientStop stop;
                stop.offset = std::min(1.0, std::max(0.0, number(child->attr("offset"), 0.0)));
                std::string colorText = child->attr("stop-color") ? *child->attr("stop-color") : std::string("#000000");
                // stop-color may also arrive through style="".
                std::string cssText;
                if (const std::string* css = child->attr("style")) {
                    std::size_t at = css->find("stop-color");
                    if (at != std::string::npos) {
                        std::size_t colon = css->find(':', at);
                        std::size_t end = css->find(';', colon);
                        if (colon != std::string::npos) {
                            colorText = trim(css->substr(colon + 1, end == std::string::npos ? std::string::npos : end - colon - 1));
                        }
                    }
                    std::size_t op = css->find("stop-opacity");
                    if (op != std::string::npos) {
                        std::size_t colon = css->find(':', op);
                        std::size_t end = css->find(';', colon);
                        if (colon != std::string::npos) {
                            cssText = trim(css->substr(colon + 1, end == std::string::npos ? std::string::npos : end - colon - 1));
                        }
                    }
                }
                SketchColor color = SketchColor::gray(0.0f);
                parseColor(colorText, color);
                float alpha = float(number(child->attr("stop-opacity") != nullptr ? child->attr("stop-opacity") : (cssText.empty() ? nullptr : &cssText), double(color.alpha())));
                SketchColor native;
                if (parseNativeColor(child->attr("data-color"), alpha, native)) {
                    color = native;
                } else {
                    color = SketchColor::rgb(color.component(0), color.component(1), color.component(2), alpha);
                }
                stop.color = color;
                out.stops.push_back(stop);
            }
            if (out.stops.empty() && depth < 8) {
                const std::string* href = gradient.attr("href");
                if (href == nullptr) { href = gradient.attr("xlink:href"); }
                if (href != nullptr && !href->empty() && (*href)[0] == '#') {
                    auto it = ctx.byId.find(href->substr(1));
                    if (it != ctx.byId.end()) {
                        gradientStops(ctx, *it->second, out, depth + 1);
                    }
                }
            }
        }

        // A paint value ("none", color, "url(#id)") for an object whose outline bounds are `box`.
        SketchPaint readPaint(const ReadContext& ctx, const Style& style, const char* name, const BLBox& box) {
            std::string prefix = name;
            if (const std::string* own = style.get("data-" + prefix + "-pattern")) {
                std::istringstream in(*own);
                std::string idText, scaleText, angleText;
                std::getline(in, idText, ',');
                std::getline(in, scaleText, ',');
                std::getline(in, angleText, ',');
                double scale = std::strtod(scaleText.c_str(), nullptr), angle = std::strtod(angleText.c_str(), nullptr);
                std::shared_ptr<const SymbolDef> def = ctx.document != nullptr ? ctx.document->findPattern(std::size_t(std::strtoull(idText.c_str(), nullptr, 10))) : nullptr;
                if (def && scale > 0.0 && std::isfinite(scale) && std::isfinite(angle)) {
                    return SketchPaint::pattern(def, scale, angle);
                }
                // The tile is missing: the flat grey the pattern was written with.
                return SketchPaint::solid(SketchColor::gray(0.5f));
            }
            const std::string* value = style.get(prefix);
            std::string text = value != nullptr ? trim(*value) : (prefix == "fill" ? std::string("#000000") : std::string("none"));
            if (text == "none" || text.empty()) {
                return SketchPaint::none();
            }
            double w = box.x1 - box.x0, h = box.y1 - box.y0;
            if (text.compare(0, 4, "url(") == 0) {
                std::size_t hash = text.find('#');
                std::size_t close = text.find(')');
                if (hash != std::string::npos && close != std::string::npos && close > hash) {
                    std::string id = text.substr(hash + 1, close - hash - 1);
                    while (!id.empty() && (id.back() == '\'' || id.back() == '"')) { id.pop_back(); }
                    auto it = ctx.byId.find(id);
                    if (it != ctx.byId.end()) {
                        const XmlNode& g = *it->second;
                        SketchGradient gradient;
                        gradientStops(ctx, g, gradient);
                        if (!gradient.stops.empty()) {
                            gradient.sortStops();
                            const std::string* spread = g.attr("spreadMethod");
                            if (spread != nullptr) {
                                gradient.spread = *spread == "repeat" ? GradientSpread::Repeat
                                    : (*spread == "reflect" ? GradientSpread::Reflect : GradientSpread::Pad);
                            }
                            bool linear = g.name == "linearGradient";
                            if (linear || g.name == "radialGradient") {
                                if (const std::string* angle = g.attr("data-angle")) {
                                    gradient.angleDegrees = number(angle, 0.0);
                                }
                                if (const std::string* along = g.attr("data-along-stroke")) {
                                    gradient.alongStroke = *along == "1";
                                }
                                std::vector<double> axis;
                                if (const std::string* own = g.attr("data-axis")) {
                                    axis = numberList(*own);
                                }
                                if (axis.size() == 4) {  // our own file: the axis as box fractions
                                    gradient.hasAxis = true;
                                    gradient.axisX1 = axis[0];
                                    gradient.axisY1 = axis[1];
                                    gradient.axisX2 = axis[2];
                                    gradient.axisY2 = axis[3];
                                    if (const std::string* aspect = g.attr("data-aspect")) {
                                        gradient.axisAspect = number(aspect, 1.0);
                                    }
                                    if (const std::string* skew = g.attr("data-skew")) {
                                        gradient.axisSkew = number(skew, 0.0);
                                    }
                                    if (const std::string* along = g.attr("data-along-stroke")) {
                                        gradient.alongStroke = *along == "1";
                                    }
                                } else if (g.attr("data-angle") == nullptr && w > 1e-9 && h > 1e-9) {
                                    // A foreign gradient: keep its vector, as box fractions. Everything is taken to user space
                                    // first (through gradientTransform, and the box for objectBoundingBox units).
                                    bool bbox = !(g.attr("gradientUnits") != nullptr && *g.attr("gradientUnits") == "userSpaceOnUse");
                                    BLMatrix2D t = identity();
                                    if (const std::string* text = g.attr("gradientTransform")) {
                                        t = parseTransform(*text);
                                    }
                                    auto user = [&](double x, double y) {
                                        BLPoint p = t.map_point(x, y);
                                        return bbox ? BLPoint(box.x0 + p.x * w, box.y0 + p.y * h) : p;
                                    };
                                    auto userVector = [&](double x, double y) {
                                        BLPoint v = t.map_vector(x, y);
                                        return bbox ? BLPoint(v.x * w, v.y * h) : v;
                                    };
                                    gradient.hasAxis = true;
                                    BLPoint start, end, across;  // across: where the gradient's perpendicular goes in user space
                                    if (linear) {
                                        double x1 = number(g.attr("x1"), 0.0), y1 = number(g.attr("y1"), 0.0);
                                        double x2 = number(g.attr("x2"), bbox ? 1.0 : box.x1), y2 = number(g.attr("y2"), 0.0);
                                        start = user(x1, y1);
                                        end = user(x2, y2);
                                        across = userVector(-(y2 - y1), x2 - x1);
                                        gradient.angleDegrees = std::atan2(end.y - start.y, end.x - start.x) * 180.0 / kPi;
                                    } else {
                                        double cx = number(g.attr("cx"), bbox ? 0.5 : box.x0 + w * 0.5);
                                        double cy = number(g.attr("cy"), bbox ? 0.5 : box.y0 + h * 0.5);
                                        double r = number(g.attr("r"), bbox ? 0.5 : std::max(w, h) * 0.5);
                                        start = user(cx, cy);
                                        BLPoint alongVector = userVector(r, 0.0);
                                        across = userVector(0.0, r);
                                        end = BLPoint(start.x + alongVector.x, start.y + alongVector.y);
                                    }
                                    // Split the perpendicular into a part along the axis (the lean) and a part square to it (the
                                    // radial aspect): a skewing or non-uniform transform shows up as lean.
                                    double axisLength = std::hypot(end.x - start.x, end.y - start.y);
                                    if (axisLength > 1e-9) {
                                        double ux = (end.x - start.x) / axisLength, uy = (end.y - start.y) / axisLength;
                                        double wu = across.x * ux + across.y * uy, wv = -across.x * uy + across.y * ux;
                                        if (std::fabs(wv) > 1e-9) {
                                            gradient.axisSkew = wu / wv;
                                            if (!linear) {
                                                gradient.axisAspect = std::fabs(wv) / axisLength;
                                            }
                                        }
                                    }
                                    gradient.axisX1 = (start.x - box.x0) / w;
                                    gradient.axisY1 = (start.y - box.y0) / h;
                                    gradient.axisX2 = (end.x - box.x0) / w;
                                    gradient.axisY2 = (end.y - box.y0) / h;
                                }
                                return linear ? SketchPaint::linear(gradient) : SketchPaint::radial(gradient);
                            }
                        }
                    }
                }
                return SketchPaint::none();
            }
            SketchColor color;
            if (!parseColor(text, color)) {
                return SketchPaint::none();
            }
            float alpha = color.alpha();
            if (const std::string* op = style.get(prefix + "-opacity")) {
                alpha *= float(number(op, 1.0));
            }
            SketchColor native;
            if (parseNativeColor(style.get("data-" + prefix + "-color"), alpha, native)) {
                color = native;
            } else {
                color = SketchColor::rgb(color.component(0), color.component(1), color.component(2), alpha);
            }
            return SketchPaint::solid(color);
        }

        // Text objects: the words, font, size and placement matrix their outline was made from (other viewers just see the
        // outline path). Empty for every other kind of object.
        std::string textAttrs(const SketchObject& object) {
            if (object.isSymbol() && object.symbol()) {
                return " data-symbol=\"" + std::to_string(object.symbol()->id) + "\"";   // the artwork is in the symbol library (<defs>)
            }
            if (!object.isText()) {
                return std::string();
            }
            const BLMatrix2D& m = object.textMatrix();
            const TextStyle& style = object.textStyle();
            std::ostringstream out;
            if (style.bold) { out << " data-text-bold=\"true\""; }
            if (style.italic) { out << " data-text-italic=\"true\""; }
            if (style.align != TextStyle::Left) { out << " data-text-align=\"" << (style.align == TextStyle::Center ? "center" : "right") << "\""; }
            if (style.tracking != 0.0) { out << " data-text-tracking=\"" << num(style.tracking) << "\""; }
            if (style.leading != 1.0) { out << " data-text-leading=\"" << num(style.leading) << "\""; }
            out << " data-text=\"" << escape(object.text()) << "\" data-font-name=\"" << escape(object.fontName()) << "\" data-font-size=\""
                << num(object.fontSize()) << "\" data-text-matrix=\"" << num(m.m00) << " " << num(m.m01) << " " << num(m.m10) << " "
                << num(m.m11) << " " << num(m.m20) << " " << num(m.m21) << "\"";
            return out.str();
        }

        // The reverse: a Text object keeps its words and font when the attributes are there (the file's own `matrix` is folded into
        // the placement); without them the outline is all there is, so it becomes a plain path.
        // A Symbol object finds its definition by the data-symbol id (the library is read first); without one it is just an outline.
        SketchObject* restoreSymbol(const ReadContext& ctx, const XmlNode& node, SketchObject* object) {
            if (object->type() != SketchObjectType::Symbol) {
                return object;
            }
            const std::string* id = node.attr("data-symbol");
            std::shared_ptr<const SymbolDef> def = id != nullptr && ctx.document != nullptr ? ctx.document->findSymbol(std::size_t(std::strtoull(id->c_str(), nullptr, 10))) : nullptr;
            if (def) {
                object->setSymbol(def);
                return object;
            }
            SketchObject* plain = SketchObject::make(SketchObjectType::Path, object->geometry());
            delete object;
            return plain;
        }

        SketchObject* restoreText(const XmlNode& node, SketchObject* object, const BLMatrix2D& matrix) {
            if (object->type() != SketchObjectType::Text) {
                return object;
            }
            const std::string* words = node.attr("data-text");
            const std::string* size = node.attr("data-font-size");
            if (words == nullptr || size == nullptr) {
                SketchObject* plain = SketchObject::make(SketchObjectType::Path, object->geometry());
                delete object;
                return plain;
            }
            BLMatrix2D placed = BLMatrix2D::make_identity();
            TextStyle textStyle;
            if (const std::string* v = node.attr("data-text-bold")) { textStyle.bold = *v == "true"; }
            if (const std::string* v = node.attr("data-text-italic")) { textStyle.italic = *v == "true"; }
            if (const std::string* v = node.attr("data-text-align")) { textStyle.align = *v == "center" ? TextStyle::Center : (*v == "right" ? TextStyle::Right : TextStyle::Left); }
            if (const std::string* v = node.attr("data-text-tracking")) { textStyle.tracking = number(v, 0.0); }
            if (const std::string* v = node.attr("data-text-leading")) { textStyle.leading = std::max(0.1, number(v, 1.0)); }
            if (const std::string* stored = node.attr("data-text-matrix")) {
                double v[6] = {1, 0, 0, 1, 0, 0};
                std::istringstream in(*stored);
                for (double& value : v) {
                    in >> value;
                }
                placed = BLMatrix2D(v[0], v[1], v[2], v[3], v[4], v[5]);
            }
            placed.post_transform(matrix);   // the file's own transform applies after the stored placement
            const std::string* font = node.attr("data-font-name");
            double fontSize = number(size, 24.0);
            object->setTextState(*words, font != nullptr ? *font : std::string(), fontSize > 0.0 ? fontSize : 24.0, placed);
            object->setTextStyle(textStyle);
            return object;
        }

        bool parseObjectType(const std::string& name, SketchObjectType& out) {
            static const std::pair<const char*, SketchObjectType> kTypes[] = {
                {"Line", SketchObjectType::Line}, {"Rect", SketchObjectType::Rect}, {"RoundRect", SketchObjectType::RoundRect},
                {"Ellipse", SketchObjectType::Ellipse}, {"Polygon", SketchObjectType::Polygon},
                {"Polyline", SketchObjectType::Polyline}, {"Path", SketchObjectType::Path}, {"Text", SketchObjectType::Text},
                {"Symbol", SketchObjectType::Symbol},
            };
            for (const auto& entry : kTypes) {
                if (name == entry.first) {
                    out = entry.second;
                    return true;
                }
            }
            return false;
        }

        std::vector<BLPoint> pointList(const std::string* text) {
            std::vector<BLPoint> points;
            if (text == nullptr) {
                return points;
            }
            std::vector<double> v = numberList(*text);
            for (std::size_t i = 0; i + 1 < v.size(); i += 2) {
                points.push_back(BLPoint(v[i], v[i + 1]));
            }
            return points;
        }

        // Builds the object for a shape element, or nullptr when it is not one we draw.
        SketchObject* makeShape(const XmlNode& node, SketchObjectType& type) {
            BLPath path;
            if (node.name == "path") {
                const std::string* d = node.attr("d");
                if (d == nullptr || !parsePathData(*d, path) || path.size() == 0) {
                    return nullptr;
                }
                type = SketchObjectType::Path;
            } else if (node.name == "rect") {
                double x = number(node.attr("x"), 0.0), y = number(node.attr("y"), 0.0);
                double w = number(node.attr("width"), 0.0), h = number(node.attr("height"), 0.0);
                if (w <= 0.0 || h <= 0.0) {
                    return nullptr;
                }
                double rx = number(node.attr("rx"), -1.0), ry = number(node.attr("ry"), -1.0);
                if (rx < 0.0) { rx = ry; }
                if (ry < 0.0) { ry = rx; }
                if (rx > 0.0 && ry > 0.0) {
                    path.add_round_rect(BLRoundRect(x, y, w, h, std::min(rx, w * 0.5), std::min(ry, h * 0.5)));
                    type = SketchObjectType::RoundRect;
                } else {
                    path.add_rect(BLRect(x, y, w, h));
                    type = SketchObjectType::Rect;
                }
            } else if (node.name == "circle" || node.name == "ellipse") {
                double cx = number(node.attr("cx"), 0.0), cy = number(node.attr("cy"), 0.0);
                double rx = node.name == "circle" ? number(node.attr("r"), 0.0) : number(node.attr("rx"), 0.0);
                double ry = node.name == "circle" ? rx : number(node.attr("ry"), 0.0);
                if (rx <= 0.0 || ry <= 0.0) {
                    return nullptr;
                }
                path.add_ellipse(BLEllipse(cx, cy, rx, ry));
                type = SketchObjectType::Ellipse;
            } else if (node.name == "line") {
                path.move_to(number(node.attr("x1"), 0.0), number(node.attr("y1"), 0.0));
                path.line_to(number(node.attr("x2"), 0.0), number(node.attr("y2"), 0.0));
                type = SketchObjectType::Line;
            } else if (node.name == "polygon" || node.name == "polyline") {
                std::vector<BLPoint> points = pointList(node.attr("points"));
                if (points.size() < 2) {
                    return nullptr;
                }
                path.move_to(points[0]);
                for (std::size_t i = 1; i < points.size(); ++i) {
                    path.line_to(points[i]);
                }
                if (node.name == "polygon") {
                    path.close();
                    type = SketchObjectType::Polygon;
                } else {
                    type = SketchObjectType::Polyline;
                }
            } else {
                return nullptr;
            }
            return SketchObject::make(type, path);
        }

        // The stroke options (cap, join, miter limit, dashes, width profile) a style declares; lengths are multiplied by `scale`.
        StrokeOptions readStrokeOptions(const Style& style, double scale) {
        StrokeOptions strokeOptions;
        if (const std::string* cap = style.get("stroke-linecap")) {
            strokeOptions.cap = *cap == "round" ? StrokeCap::Round : (*cap == "square" ? StrokeCap::Square : StrokeCap::Butt);
        }
        if (const std::string* join = style.get("stroke-linejoin")) {
            strokeOptions.join = *join == "round" ? StrokeJoin::Round : (*join == "bevel" ? StrokeJoin::Bevel : StrokeJoin::Miter);
        }
        strokeOptions.miterLimit = std::max(1.0, number(style.get("stroke-miterlimit"), 4.0));
        if (const std::string* dashes = style.get("stroke-dasharray")) {
            std::string text = *dashes;
            for (char& ch : text) {
                if (ch == ',') { ch = ' '; }
            }
            std::istringstream in(text);
            std::vector<double> values;
            double value = 0.0;
            double total = 0.0;
            while (in >> value) {
                values.push_back(std::max(0.0, value * (scale > 1e-9 ? scale : 1.0)));
                total += values.back();
            }
            if (total > 0.0) {
                strokeOptions.dashes = values;
            }
        }
        if (const std::string* profile = style.get("data-width-profile")) {
            std::string text = *profile;
            for (char& ch : text) {
                if (ch == ';') { ch = '\n'; }
            }
            std::istringstream lines(text);
            std::string row;
            while (std::getline(lines, row)) {
                std::istringstream in(row);
                WidthPoint point;
                if (in >> point.t >> point.left >> point.right) {
                    strokeOptions.widths.push_back(point);
                }
            }
            std::sort(strokeOptions.widths.begin(), strokeOptions.widths.end(),
                [](const WidthPoint& a, const WidthPoint& b) { return a.t < b.t; });
        }
        return strokeOptions;
        }

        // A stroke's own opacity folded into a solid color's alpha.
        SketchPaint foldStrokeOpacity(const SketchPaint& paint, const Style& style) {
            const std::string* strokeOpacity = style.get("stroke-opacity");
            if (strokeOpacity == nullptr || paint.kind() != SketchPaint::Kind::Solid) {
                return paint;
            }
            SketchColor c = paint.color();
            c.setAlpha(c.alpha() * float(number(strokeOpacity, 1.0)));
            return SketchPaint::solid(c);
        }

        // <g data-sketch="appearance">: one object whose appearance is the stack of child paths (bottom to top).
        void readAppearanceObject(ReadContext& ctx, const XmlNode& node, const BLMatrix2D& matrix, const Style& style) {
            const XmlNode* first = nullptr;
            for (const auto& child : node.children) {
                if (child->name == "path") {
                    first = child.get();
                    break;
                }
            }
            if (first == nullptr) {
                return;
            }
            SketchObjectType type = SketchObjectType::Path;
            std::unique_ptr<SketchObject> object(makeShape(*first, type));
            if (!object) {
                return;
            }
            if (const std::string* dt = node.attr("data-type")) {
                SketchObjectType stored;
                if (parseObjectType(*dt, stored)) {
                    object.reset(SketchObject::make(stored, object->geometry()));
                }
            }
            BLPath path = object->geometry();
            path.transform(matrix);
            object->setGeometry(path);
            if (const std::string* dt = node.attr("data-type"); dt != nullptr && *dt == "Text") {
                object.reset(restoreText(node, object.release(), matrix));
            }
            object.reset(restoreSymbol(ctx, node, object.release()));
            BLBox box = object->geometryBounds();
            double scale = std::sqrt(std::fabs(matrix.m00 * matrix.m11 - matrix.m01 * matrix.m10));
            if (scale <= 1e-9) {
                scale = 1.0;
            }
            std::vector<AppearanceItem> items;
            for (const auto& child : node.children) {
                const std::string* kind = child->attr("data-item");
                if (child->name != "path" || kind == nullptr) {
                    continue;
                }
                Style cs = styleFor(*child, style);
                AppearanceItem item;
                if (*kind == "effect") {
                    const std::string* code = child->attr("data-effect");
                    EffectKind effect = EffectKind::DropShadow;
                    if (code != nullptr) {
                        static const std::pair<const char*, EffectKind> kEffects[] = {
                            {"drop-shadow", EffectKind::DropShadow}, {"outer-glow", EffectKind::OuterGlow}, {"gaussian-blur", EffectKind::GaussianBlur},
                            {"round-corners", EffectKind::RoundCorners}, {"inner-shadow", EffectKind::InnerShadow},
                            {"offset-path", EffectKind::OffsetPath}, {"roughen", EffectKind::Roughen}};
                        for (const auto& entry : kEffects) {
                            if (*code == entry.first) {
                                effect = entry.second;
                            }
                        }
                    }
                    item = AppearanceItem::makeEffect(effect);
                    item.softness = number(child->attr("data-softness"), item.softness);
                    item.offsetX = number(child->attr("data-dx"), item.offsetX);
                    item.offsetY = number(child->attr("data-dy"), item.offsetY);
                    item.radius = number(child->attr("data-radius"), item.radius);
                    SketchPaint tint = readPaint(ctx, cs, "fill", box);
                    if (tint.kind() == SketchPaint::Kind::Solid) {
                        item.color = tint.color();
                    }
                    item.opacity = 1.0;
                    item.blendMode = BlendMode::Normal;  // absent means Normal (makeEffect's default is the editing default)
                } else if (*kind == "stroke") {
                    item = AppearanceItem::makeStroke(foldStrokeOpacity(readPaint(ctx, cs, "stroke", box), cs),
                        std::max(0.0, number(cs.get("stroke-width"), 1.0) * scale), readStrokeOptions(cs, scale));
                } else {
                    item = AppearanceItem::makeFill(readPaint(ctx, cs, "fill", box));
                }
                item.opacity = std::min(1.0, std::max(0.0, number(cs.get("opacity"), 1.0)));
                if (const std::string* blend = cs.get("mix-blend-mode")) {
                    item.blendMode = blendModeFromCss(*blend);
                }
                if (const std::string* display = cs.get("display")) {
                    item.visible = *display != "none";
                }
                items.push_back(item);
            }
            object->setAppearance(items);
            object->setOpacity(std::min(1.0, std::max(0.0, number(style.get("opacity"), 1.0))));
            if (const std::string* blend = style.get("mix-blend-mode")) {
                object->setBlendMode(blendModeFromCss(*blend));
            }
            if (const std::string* display = style.get("display")) {
                object->setVisible(*display != "none");
            }
            if (const std::string* locked = node.attr("data-locked")) {
                object->setLocked(*locked == "true");
            }
            if (const std::string* layer = node.attr("data-layer")) {
                object->setLayer(std::size_t(std::strtoull(layer->c_str(), nullptr, 10)));
            }
            if (const std::string* name = node.attr("data-name")) {
                object->setName(*name);
            } else if (const std::string* id = node.attr("id")) {
                object->setName(*id);
            }
            if (const std::string* angle = node.attr("data-frame-angle")) {
                object->setFrameAngle(number(angle, 0.0));
            }
            object->setGroups(ctx.groupStack);
            ctx.document->add(object.release());
        }

        void readNode(ReadContext& ctx, const XmlNode& node, const BLMatrix2D& parentMatrix, const Style& inherited) {
            if (node.name == "defs" || node.name == "linearGradient" || node.name == "radialGradient" || node.name == "clipPath"
                || node.name == "mask" || node.name == "pattern" || node.name == "symbol" || node.name == "style" || node.name == "metadata") {
                return;
            }
            BLMatrix2D matrix = parentMatrix;
            if (const std::string* t = node.attr("transform")) {
                matrix = concat(parentMatrix, parseTransform(*t));
            }
            Style style = styleFor(node, inherited);
            if (node.name == "g" && node.attr("data-sketch") != nullptr && *node.attr("data-sketch") == "appearance") {
                readAppearanceObject(ctx, node, matrix, style);
                return;
            }
            if (node.name == "g" && node.attr("data-sketch") != nullptr && *node.attr("data-sketch") == "graphic-styles") {
                // Each child is an object wearing one style: read them into a scratch document, keep their appearances.
                SketchDocument scratch;
                ReadContext inner;
                inner.byId = ctx.byId;
                inner.document = &scratch;
                for (const auto& child : node.children) {
                    readNode(inner, *child, identity(), Style());
                }
                std::vector<GraphicStyle> styles;
                for (std::size_t i = 0; i < scratch.objectCount(); ++i) {
                    const SketchObject& carrier = scratch.object(i);
                    GraphicStyle loaded;
                    loaded.name = carrier.name();
                    loaded.items = carrier.appearance();
                    loaded.opacity = carrier.opacity();
                    loaded.blendMode = carrier.blendMode();
                    styles.push_back(loaded);
                }
                if (ctx.document != nullptr) {
                    ctx.document->setGraphicStyles(styles);
                }
                return;
            }
            if (node.name == "g" || node.name == "svg" || node.name == "a" || node.name == "switch") {
                bool group = node.name == "g" && ctx.document != nullptr;
                if (group) {
                    ctx.groupStack.push_back(ctx.document->newGroupId());
                    GroupProps props;
                    props.opacity = std::min(1.0, std::max(0.0, number(style.get("opacity"), 1.0)));
                    if (const std::string* blend = style.get("mix-blend-mode")) {
                        props.blendMode = blendModeFromCss(*blend);
                    }
                    if (const std::string* clip = node.attr("data-clip")) {
                        props.clip = *clip == "true";
                    }
                    if (!props.isDefault()) {
                        ctx.document->setGroupProps(ctx.groupStack.back(), props);
                    }
                }
                for (const auto& child : node.children) {
                    readNode(ctx, *child, matrix, style);
                }
                if (group) {
                    ctx.groupStack.pop_back();
                }
                return;
            }
            if (const std::string* id = node.attr("id"); id != nullptr && id->rfind("pageBackground", 0) == 0) {
                return;  // the page color is carried by data-page-background
            }
            SketchObjectType type = SketchObjectType::Path;
            std::unique_ptr<SketchObject> object(makeShape(node, type));
            if (!object) {
                return;
            }
            // data-type restores what the object was (a path whose outline is a rounded rectangle, say).
            if (const std::string* dt = node.attr("data-type")) {
                SketchObjectType stored;
                if (parseObjectType(*dt, stored)) {
                    object.reset(SketchObject::make(stored, object->geometry()));
                }
            }
            BLPath path = object->geometry();
            path.transform(matrix);
            object->setGeometry(path);
            if (const std::string* dt = node.attr("data-type"); dt != nullptr && *dt == "Text") {
                object.reset(restoreText(node, object.release(), matrix));
            }
            object.reset(restoreSymbol(ctx, node, object.release()));
            BLBox box = object->geometryBounds();

            object->setFill(readPaint(ctx, style, "fill", box));
            object->setStroke(readPaint(ctx, style, "stroke", box));
            double strokeWidth = number(style.get("stroke-width"), 1.0);
            // A scaling transform scales the stroke too.
            double scale = std::sqrt(std::fabs(matrix.m00 * matrix.m11 - matrix.m01 * matrix.m10));
            object->setStrokeWidth(std::max(0.0, strokeWidth * (scale > 1e-9 ? scale : 1.0)));
            object->setStrokeOptions(readStrokeOptions(style, scale));
            object->setStroke(foldStrokeOpacity(object->stroke(), style));
            object->setOpacity(std::min(1.0, std::max(0.0, number(style.get("opacity"), 1.0))));
            if (const std::string* blend = style.get("mix-blend-mode")) {
                object->setBlendMode(blendModeFromCss(*blend));
            }
            if (const std::string* display = style.get("display")) {
                object->setVisible(*display != "none");
            }
            if (const std::string* locked = node.attr("data-locked")) {
                object->setLocked(*locked == "true");
            }
            if (const std::string* layer = node.attr("data-layer")) {
                object->setLayer(std::size_t(std::strtoull(layer->c_str(), nullptr, 10)));
            }
            if (const std::string* name = node.attr("data-name")) {
                object->setName(*name);
            } else if (const std::string* id = node.attr("id")) {
                object->setName(*id);
            }
            if (const std::string* angle = node.attr("data-frame-angle")) {
                object->setFrameAngle(number(angle, 0.0));
            }
            object->setGroups(ctx.groupStack);
            ctx.document->add(object.release());
        }

    }

    namespace {
        // stroke-width / cap / join / dashes / width profile / miter limit attributes of a stroke (nothing for a none paint).
        std::string strokeAttrs(const SketchPaint& paint, double width, const StrokeOptions& options) {
            std::ostringstream out;
            if (paint.isNone()) {
                return std::string();
            }
            out << " stroke-width=\"" << num(width) << "\"";
                            if (options.cap != StrokeCap::Butt) {
                out << " stroke-linecap=\"" << (options.cap == StrokeCap::Round ? "round" : "square") << "\"";
            }
            if (options.join != StrokeJoin::Miter) {
                out << " stroke-linejoin=\"" << (options.join == StrokeJoin::Round ? "round" : "bevel") << "\"";
            }
            if (!options.dashes.empty()) {
                out << " stroke-dasharray=\"";
                for (size_t i = 0; i < options.dashes.size(); ++i) {
                    out << (i > 0 ? " " : "") << num(options.dashes[i]);
                }
                out << "\"";
            }
            if (!options.widths.empty()) {  // other viewers ignore this and show the plain stroke
                out << " data-width-profile=\"";
                for (std::size_t i = 0; i < options.widths.size(); ++i) {
                    out << (i > 0 ? ";" : "") << num(options.widths[i].t) << " " << num(options.widths[i].left) << " " << num(options.widths[i].right);
                }
                out << "\"";
            }
            if (options.miterLimit != 4.0) {  // 4 is SVG's default
                out << " stroke-miterlimit=\"" << num(options.miterLimit) << "\"";
            }
            return out.str();
        }

    }

    namespace {
    // One object as SVG: a plain <path>, or - for several fills / strokes, per-item opacity / blend / visibility or effects - a
    // <g data-sketch="appearance"> with one path per item. `idText` is its id attribute.
    void writeObject(const SketchObject& object, const std::string& idText, std::ostringstream& body, std::ostringstream& defs, int& gradientCount, bool withLayers = false) {
        BLBox box = object.geometryBounds();
            {
                const std::vector<AppearanceItem>& items = object.appearance();
                bool plain = items.size() == 2 && items[0].kind == AppearanceKind::Fill && items[1].kind == AppearanceKind::Stroke;
                for (const AppearanceItem& item : items) {
                    plain = plain && item.visible && item.opacity == 1.0 && item.blendMode == BlendMode::Normal;
                }
                if (!plain) {
                    // Several fills/strokes (or per-item opacity/blend/visibility): the object is a <g> with one path per item.
                    body << "<g data-sketch=\"appearance\" id=\"" << idText << "\" data-type=\"" << typeName(object.type()) << "\"" << textAttrs(object);
                    if (!object.name().empty()) {
                        body << " data-name=\"" << escape(object.name()) << "\"";
                    }
                    if (object.frameAngle() != 0.0) {
                        body << " data-frame-angle=\"" << num(object.frameAngle()) << "\"";
                    }
                    if (object.opacity() < 1.0) {
                        body << " opacity=\"" << num(object.opacity()) << "\"";
                    }
                    if (object.blendMode() != BlendMode::Normal) {
                        body << " style=\"mix-blend-mode:" << blendModeCss(object.blendMode()) << "\"";
                    }
                    if (!object.ownVisible()) {
                        body << " display=\"none\"";
                    }
                    if (object.ownLocked()) {
                        body << " data-locked=\"true\"";
                    }
                    if (withLayers) {
                        body << " data-layer=\"" << object.layer() << "\"";
                    }
                    body << ">\n";
                    for (const AppearanceItem& item : items) {
                        bool stroke = item.kind == AppearanceKind::Stroke;
                        if (item.kind == AppearanceKind::Effect) {
                            // Other viewers draw nothing for it; the reader restores it from these attributes.
                            static const char* kCodes[] = {"drop-shadow", "outer-glow", "gaussian-blur", "round-corners", "inner-shadow", "offset-path", "roughen"};
                            body << "<path d=\"M0 0\" data-item=\"effect\" data-effect=\"" << kCodes[std::size_t(item.effect)] << "\" data-softness=\""
                                 << num(item.softness) << "\" data-dx=\"" << num(item.offsetX) << "\" data-dy=\"" << num(item.offsetY)
                                 << "\" data-radius=\"" << num(item.radius) << "\" stroke=\"none\""
                                 << paintAttrs("fill", SketchPaint::solid(item.color), box, defs, gradientCount);
                            if (item.opacity < 1.0) {
                                body << " opacity=\"" << num(item.opacity) << "\"";
                            }
                            if (item.blendMode != BlendMode::Normal) {
                                body << " style=\"mix-blend-mode:" << blendModeCss(item.blendMode) << "\"";
                            }
                            if (!item.visible) {
                                body << " display=\"none\"";
                            }
                            body << "/>\n";
                            continue;
                        }
                        body << "<path d=\"" << pathData(object.geometry()) << "\" data-item=\"" << (stroke ? "stroke" : "fill") << "\"";
                        if (stroke) {
                            body << " fill=\"none\"" << paintAttrs("stroke", item.paint, box, defs, gradientCount)
                                 << strokeAttrs(item.paint, item.strokeWidth, item.strokeOptions);
                        } else {
                            body << paintAttrs("fill", object.isClosed() ? item.paint : SketchPaint::none(), box, defs, gradientCount) << " stroke=\"none\"";
                        }
                        if (item.opacity < 1.0) {
                            body << " opacity=\"" << num(item.opacity) << "\"";
                        }
                        if (item.blendMode != BlendMode::Normal) {
                            body << " style=\"mix-blend-mode:" << blendModeCss(item.blendMode) << "\"";
                        }
                        if (!item.visible) {
                            body << " display=\"none\"";
                        }
                        body << "/>\n";
                    }
                    body << "</g>\n";
                    return;
                }
            }
            body << "<path id=\"" << idText << "\" d=\"" << pathData(object.geometry()) << "\" data-type=\"" << typeName(object.type()) << "\"" << textAttrs(object);
            if (!object.name().empty()) {
                body << " data-name=\"" << escape(object.name()) << "\"";
            }
            if (object.frameAngle() != 0.0) {
                body << " data-frame-angle=\"" << num(object.frameAngle()) << "\"";
            }
            SketchPaint fill = object.isClosed() ? object.fill() : SketchPaint::none();
            body << paintAttrs("fill", fill, box, defs, gradientCount);
            body << paintAttrs("stroke", object.stroke(), box, defs, gradientCount);
            body << strokeAttrs(object.stroke(), object.strokeWidth(), object.strokeOptions());
            if (object.opacity() < 1.0) {
                body << " opacity=\"" << num(object.opacity()) << "\"";
            }
            if (object.blendMode() != BlendMode::Normal) {
                body << " style=\"mix-blend-mode:" << blendModeCss(object.blendMode()) << "\"";
            }
            if (!object.ownVisible()) {
                body << " display=\"none\"";
            }
            if (object.ownLocked()) {
                body << " data-locked=\"true\"";
            }
            if (withLayers) {
                body << " data-layer=\"" << object.layer() << "\"";
            }
            body << "/>\n";
    }

    }

    // ---------------------------------------------------------------------- public

    std::string svgFromDocument(const SketchDocument& document) {
        std::ostringstream defs;
        std::ostringstream body;
        int gradientCount = 0;
        std::vector<std::size_t> openGroups;  // <g> elements currently open (group ids)
        for (std::size_t i = 0; i < document.objectCount(); ++i) {
            const SketchObject& object = document.object(i);
            BLBox box = object.geometryBounds();
            {
                const std::vector<std::size_t>& groups = object.groups();
                std::size_t common = 0;
                while (common < openGroups.size() && common < groups.size() && openGroups[common] == groups[common]) {
                    ++common;
                }
                for (std::size_t k = openGroups.size(); k > common; --k) {
                    body << "</g>\n";
                }
                openGroups.resize(common);
                for (std::size_t k = common; k < groups.size(); ++k) {
                    GroupProps props = document.groupProps(groups[k]);
                    body << "<g";
                    if (props.clip) {
                        body << " data-clip=\"true\"";
                    }
                    if (props.opacity < 1.0) {
                        body << " opacity=\"" << num(props.opacity) << "\"";
                    }
                    if (props.blendMode != BlendMode::Normal) {
                        body << " style=\"mix-blend-mode:" << blendModeCss(props.blendMode) << "\"";
                    }
                    body << ">\n";
                    openGroups.push_back(groups[k]);
                }
            }
            writeObject(object, "obj" + std::to_string(object.id()), body, defs, gradientCount, document.layers().size() > 1);
        }
        for (std::size_t k = openGroups.size(); k > 0; --k) {
            body << "</g>\n";
        }
        // Graphic styles: hidden, each one a 100 x 100 rectangle wearing the style, so the ordinary object reader / writer
        // carries the whole appearance. The group is written even when empty, so "no styles" survives a round trip.
        body << "<g data-sketch=\"graphic-styles\" display=\"none\">\n";
        for (std::size_t i = 0; i < document.graphicStyles().size(); ++i) {
            const GraphicStyle& style = document.graphicStyles()[i];
            std::unique_ptr<SketchObject> carrier(SketchObject::makeRect(0, 0, 100, 100));
            carrier->setAppearance(style.items);
            carrier->setOpacity(style.opacity);
            carrier->setBlendMode(style.blendMode);
            carrier->setName(style.name);
            writeObject(*carrier, "style" + std::to_string(i + 1), body, defs, gradientCount);
        }
        body << "</g>\n";

        std::ostringstream out;
        out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        const BLBox all = document.artboardsBounds();
        out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << num(all.x1 - all.x0) << "pt\" height=\"" << num(all.y1 - all.y0)
            << "pt\" viewBox=\"" << num(all.x0) << " " << num(all.y0) << " " << num(all.x1 - all.x0) << " " << num(all.y1 - all.y0) << "\" data-ppi=\"" << num(document.ppi())
            << "\" data-color-mode=\"" << (document.colorMode() == ColorSpace::CMYK ? "cmyk" : "rgb")
            << "\" data-page-background=\"" << (document.pageBackground() ? nativeColor(*document.pageBackground()) : std::string("none")) << "\"" << artboardAttr(document) << guideAttr(document) << layerAttr(document) << swatchAttr(document) << ">\n";
        std::string defsText = defs.str();
        // The symbol library: each definition's artwork as a nested <svg> of its own (written by this same function).
        for (const std::shared_ptr<const SymbolDef>& def : document.symbols()) {
            if (!def->content) {
                continue;
            }
            std::string inner = svgFromDocument(*def->content);
            std::size_t at = inner.find("<svg ");
            if (at == std::string::npos) {
                continue;
            }
            defsText += "<svg data-sketch-symbol=\"" + std::to_string(def->id) + "\" data-name=\"" + escape(def->name) + "\" data-box=\"" + num(def->box.x0) + " " +
                num(def->box.y0) + " " + num(def->box.x1) + " " + num(def->box.y1) + "\" " + inner.substr(at + 5);
        }
        for (const std::shared_ptr<const SymbolDef>& def : document.patterns()) {
            if (!def->content) {
                continue;
            }
            std::string inner = svgFromDocument(*def->content);
            std::size_t at = inner.find("<svg ");
            if (at == std::string::npos) {
                continue;
            }
            defsText += "<svg data-sketch-pattern=\"" + std::to_string(def->id) + "\" data-name=\"" + escape(def->name) + "\" data-box=\"" + num(def->box.x0) + " " +
                num(def->box.y0) + " " + num(def->box.x1) + " " + num(def->box.y1) + "\" " + inner.substr(at + 5);
        }
        if (!defsText.empty()) {
            out << "<defs>\n" << defsText << "</defs>\n";
        }
        if (document.pageBackground()) {
            // A real rectangle per artboard so other viewers show the page color; the reader recognizes them by their ids and skips them.
            for (std::size_t i = 0; i < document.artboards().size(); ++i) {
                const Artboard& board = document.artboards()[i];
                std::ostringstream unused;
                int none = 0;
                out << "<rect id=\"pageBackground" << (i ? std::to_string(i + 1) : std::string()) << "\" x=\"" << num(board.x) << "\" y=\"" << num(board.y)
                    << "\" width=\"" << num(board.width) << "\" height=\"" << num(board.height) << "\""
                    << paintAttrs("fill", SketchPaint::solid(*document.pageBackground()), BLBox(0, 0, 0, 0), unused, none) << "/>\n";
            }
        }
        out << body.str();
        out << "</svg>\n";
        return out.str();
    }

    static bool documentFromXml(const XmlNode* root, SketchDocument& document);

    bool documentFromSvg(const std::string& text, SketchDocument& document) {
        std::unique_ptr<XmlNode> root = XmlParser(text).parseDocument();
        if (!root || root->name != "svg") {
            return false;
        }
        return documentFromXml(root.get(), document);
    }

    static bool documentFromXml(const XmlNode* root, SketchDocument& document) {
        ReadContext ctx;
        collectIds(*root, ctx.byId);

        double width = 0.0, height = 0.0;
        bool haveWidth = parseLength(root->attr("width"), width);
        bool haveHeight = parseLength(root->attr("height"), height);
        double vbX = 0.0, vbY = 0.0, vbW = 0.0, vbH = 0.0;
        bool haveViewBox = false;
        if (const std::string* vb = root->attr("viewBox")) {
            std::vector<double> v = numberList(*vb);
            if (v.size() == 4 && v[2] > 0.0 && v[3] > 0.0) {
                vbX = v[0]; vbY = v[1]; vbW = v[2]; vbH = v[3];
                haveViewBox = true;
            }
        }
        if (!haveWidth || width <= 0.0) { width = haveViewBox ? vbW : 612.0; }
        if (!haveHeight || height <= 0.0) { height = haveViewBox ? vbH : 792.0; }

        std::vector<Artboard> boards;
        if (const std::string* list = root->attr("data-artboards")) {
            boards = parseArtboards(*list);
        }
        BLMatrix2D base = identity();
        if (haveViewBox && boards.empty()) {
            double sx = width / vbW, sy = height / vbH;
            base = BLMatrix2D(sx, 0.0, 0.0, sy, -vbX * sx, -vbY * sy);
        }

        document.clearObjects();
        document.setGraphicStyles(defaultGraphicStyles());   // replaced by the file's own group, when it has one
        document.setPageSize(float(width), float(height));
        if (!boards.empty()) {
            std::size_t active = 0;
            if (const std::string* index = root->attr("data-active-artboard")) {
                double value = number(index, 0.0);
                active = value >= 0.0 && value < double(boards.size()) ? std::size_t(value) : 0;
            }
            document.setArtboards(boards, active);
        }
        if (const std::string* ppi = root->attr("data-ppi")) {
            double value = number(ppi, 72.0);
            if (value > 0.0) {
                document.setPpi(float(value));
            }
        }
        if (const std::string* background = root->attr("data-page-background")) {
            SketchColor color;
            if (*background == "none") {
                document.setPageBackground(std::nullopt);
            } else if (parseNativeColor(background, 1.0f, color)) {
                document.setPageBackground(color);
            }
        } else {
            document.setPageBackground(SketchColor::rgb(1.0f, 1.0f, 1.0f));
        }
        if (const std::string* mode = root->attr("data-color-mode")) {
            document.setColorMode(*mode == "cmyk" ? ColorSpace::CMYK : ColorSpace::RGB);
        } else {
            document.setColorMode(ColorSpace::RGB);
        }
        document.setGuides(root->attr("data-guides") != nullptr ? parseGuides(*root->attr("data-guides")) : std::vector<Guide>());
        document.setLayers(root->attr("data-layers") != nullptr ? parseLayers(*root->attr("data-layers")) : std::vector<Layer>());
        document.setSwatches(root->attr("data-swatches") != nullptr ? parseSwatches(*root->attr("data-swatches")) : defaultSwatches());
        {
            // The symbol library comes first: the instances are resolved against it as they are read.
            std::vector<std::shared_ptr<const SymbolDef>> library, patterns;
            for (const auto& child : root->children) {
                if (child->name != "defs") {
                    continue;
                }
                for (const auto& entry : child->children) {
                    const std::string* id = entry->attr("data-sketch-symbol");
                    const bool isPattern = id == nullptr;
                    if (isPattern) {
                        id = entry->attr("data-sketch-pattern");
                    }
                    if (entry->name != "svg" || id == nullptr) {
                        continue;
                    }
                    auto def = std::make_shared<SymbolDef>();
                    def->id = std::size_t(std::strtoull(id->c_str(), nullptr, 10));
                    if (const std::string* name = entry->attr("data-name")) {
                        def->name = *name;
                    }
                    std::vector<double> box = entry->attr("data-box") != nullptr ? numberList(*entry->attr("data-box")) : std::vector<double>();
                    if (box.size() != 4 || !(box[2] > box[0]) || !(box[3] > box[1]) || def->id == 0) {
                        continue;
                    }
                    def->box = BLBox(box[0], box[1], box[2], box[3]);
                    def->content = std::make_shared<SketchDocument>();
                    if (documentFromXml(entry.get(), *def->content)) {
                        (isPattern ? patterns : library).push_back(def);
                    }
                }
            }
            document.setSymbols(library);
            document.setPatterns(patterns);
        }
        ctx.document = &document;
        Style rootStyle = styleFor(*root, Style());
        for (const auto& child : root->children) {
            readNode(ctx, *child, base, rootStyle);
        }
        // A <g> holding a single object (or none that survived) is not worth a group.
        std::map<std::size_t, std::size_t> members;
        for (std::size_t i = 0; i < document.objectCount(); ++i) {
            for (std::size_t g : document.object(i).groups()) {
                ++members[g];
            }
        }
        for (std::size_t i = 0; i < document.objectCount(); ++i) {
            std::vector<std::size_t> kept;
            for (std::size_t g : document.object(i).groups()) {
                if (members[g] > 1) {
                    kept.push_back(g);
                }
            }
            if (kept.size() != document.object(i).groups().size()) {
                document.object(i).setGroups(kept);
            }
        }
        document.undoStack().clear();
        document.clearSelection();
        return true;
    }

}

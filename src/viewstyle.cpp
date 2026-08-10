





#include "newui/viewstyle.h"
#include "newui/view.h"
#include "newui/rootview.h"
#include "newui/json5_helpers.h"

#include <json5/json5.hpp>
#include <json5/json5_builder.hpp>

namespace {

	const char* edge3DStyleToString(newui::Edge3DStyle s) {
		switch (s) {
			case newui::Edge3DStyle::Raised: return "Raised";
			case newui::Edge3DStyle::Sunken: return "Sunken";
			case newui::Edge3DStyle::Etched: return "Etched";
			case newui::Edge3DStyle::Bump: return "Bump";
		}
		return "Raised";
	}

	newui::Edge3DStyle edge3DStyleFromString(const std::string& s, newui::Edge3DStyle defaultValue) {
		if (s == "Raised") return newui::Edge3DStyle::Raised;
		if (s == "Sunken") return newui::Edge3DStyle::Sunken;
		if (s == "Etched") return newui::Edge3DStyle::Etched;
		if (s == "Bump") return newui::Edge3DStyle::Bump;
		return defaultValue;
	}

}

namespace newui {

	void ViewStyle::markDirty()
	{
		if (nullptr != view_) {
			view_->rootView()->markDirty();
		}
	}

	void ViewStyle::writeFields(json5::builder& w) const {
		writeColor(w, "backgroundFill", backgroundFill);
		writeColor(w, "borderFill", borderFill);
		w["borderWidth"] = borderWidth;
		writeColor(w, "highlightFill", highlightFill);
		w["opacity"] = opacity;
		// Raw int, not a name table: BLCompOp has ~30 blend2d composite-op
		// values and this field is essentially always left at its
		// SRC_OVER default - not worth a full string table for v1.
		w["compositingOp"] = int(compositingOp);
		writeFont(w, "font", font);
	}

	void ViewStyle::readFields(const json5::value& obj) {
		readColor(obj, "backgroundFill", backgroundFill);
		readColor(obj, "borderFill", borderFill);
		borderWidth = obj["borderWidth"].get<float>(borderWidth);
		readColor(obj, "highlightFill", highlightFill);
		opacity = obj["opacity"].get<float>(opacity);
		compositingOp = BLCompOp(obj["compositingOp"].get<int>(int(compositingOp)));
		font = readFont(obj["font"], font);
	}

	void ButtonStyle::writeFields(json5::builder& w) const {
		ViewStyle::writeFields(w);
		w["edgeStyle"] = w.new_string(edge3DStyleToString(edgeStyle));
		w["edgeWidth"] = edgeWidth;
		writeColor(w, "edgeHighlightColor", edgeHighlightColor);
		writeColor(w, "edgeShadowColor", edgeShadowColor);
	}

	void ButtonStyle::readFields(const json5::value& obj) {
		ViewStyle::readFields(obj);
		edgeStyle = edge3DStyleFromString(obj["edgeStyle"].get_c_str(""), edgeStyle);
		edgeWidth = obj["edgeWidth"].get<float>(edgeWidth);
		readColor(obj, "edgeHighlightColor", edgeHighlightColor);
		readColor(obj, "edgeShadowColor", edgeShadowColor);
	}

	void LabelStyle::writeFields(json5::builder& w) const {
		ViewStyle::writeFields(w);
		w["text"] = w.new_string(text);
		writeColor(w, "textColor", textColor);
	}

	void LabelStyle::readFields(const json5::value& obj) {
		ViewStyle::readFields(obj);
		text = obj["text"].get_c_str(text.c_str());
		readColor(obj, "textColor", textColor);
	}

	void CheckBoxStyle::writeFields(json5::builder& w) const {
		ViewStyle::writeFields(w);
		w["checked"] = checked;
		w["boxSize"] = boxSize;
		w["boxEdgeStyle"] = w.new_string(edge3DStyleToString(boxEdgeStyle));
		w["boxEdgeWidth"] = boxEdgeWidth;
		writeColor(w, "boxFill", boxFill);
		writeColor(w, "boxEdgeHighlightColor", boxEdgeHighlightColor);
		writeColor(w, "boxEdgeShadowColor", boxEdgeShadowColor);
		writeColor(w, "checkColor", checkColor);
		w["checkWidth"] = checkWidth;
		w["boxLabelSpacing"] = boxLabelSpacing;
	}

	void CheckBoxStyle::readFields(const json5::value& obj) {
		ViewStyle::readFields(obj);
		checked = obj["checked"].get_bool(checked);
		boxSize = obj["boxSize"].get<float>(boxSize);
		boxEdgeStyle = edge3DStyleFromString(obj["boxEdgeStyle"].get_c_str(""), boxEdgeStyle);
		boxEdgeWidth = obj["boxEdgeWidth"].get<float>(boxEdgeWidth);
		readColor(obj, "boxFill", boxFill);
		readColor(obj, "boxEdgeHighlightColor", boxEdgeHighlightColor);
		readColor(obj, "boxEdgeShadowColor", boxEdgeShadowColor);
		readColor(obj, "checkColor", checkColor);
		checkWidth = obj["checkWidth"].get<float>(checkWidth);
		boxLabelSpacing = obj["boxLabelSpacing"].get<float>(boxLabelSpacing);
	}

}
#pragma once

#include "LayoutTypes.h"
#include "Node.h"

#include <nlohmann/json.hpp>

namespace app_logic {

	using namespace hypergraph_logic;

	// ============================================================================
	// ProjectSettings
	//
	// What a project decides for all its diagrams (see Project::setSettings):
	//
	//   - The look of new boxes: background colour, text colour, font size and
	//     shape. Existing boxes keep theirs.
	//   - Whether new equivalences are drawn with a continuous line.
	//   - The layout's distances and the default size of each shape's boxes
	//     (LayoutSettings), which every diagram and the joint are drawn with.
	//     Whether boxes already using a default size take a new one is chosen
	//     when it changes (Project::setSettings).
	//
	// Saved with the project. The application keeps one more set, the defaults
	// new projects start with.
	// ============================================================================
	struct ProjectSettings {
		Color colour = NodeAttributes().colour;
		Color font_colour = NodeAttributes().font_colour;
		int font_size = NodeAttributes::DEFAULT_FONT_SIZE;
		NodeShape shape = NodeShape::Rectangle;
		bool equivalence_continuous = true;
		LayoutSettings layout;

		static constexpr int MIN_FONT_SIZE = 1;
		static constexpr int MAX_FONT_SIZE = 400;

		// The same settings with every value inside its range.
		ProjectSettings clamped() const;

		bool operator==(const ProjectSettings& other) const noexcept {
			return colour == other.colour && font_colour == other.font_colour && font_size == other.font_size
				&& shape == other.shape && equivalence_continuous == other.equivalence_continuous
				&& layout == other.layout;
		}
		bool operator!=(const ProjectSettings& other) const noexcept { return !(*this == other); }

		// ── JSON ──────────────────────────────────────────────────────────────────
		//
		// toJSON writes two members of j:
		//   "defaults": { "colour": "#RRGGBBAA", "font_colour": "#RRGGBBAA",
		//                 "font_size": <int>, "shape": "rectangle" | ...,
		//                 "equivalence_continuous": <bool> }
		//   "layout":   { "block_sep": <number>, "layer_gap": <number>,
		//                 "horizontal_sep": <number>,
		//                 "box_sizes": { "rectangle": { "width": <number>, "height": <number> },
		//                                "circle": {...}, "rhombus": {...} } }
		// fromJSON reads them back from j; any missing member or field keeps its
		// default, so files saved before a setting existed still load. Values out
		// of range are brought inside it. Throws std::runtime_error on a malformed
		// value.
		void toJSON(nlohmann::json& j) const;
		static ProjectSettings fromJSON(const nlohmann::json& j);
	};

} // namespace app_logic

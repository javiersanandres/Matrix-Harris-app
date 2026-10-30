#include "ProjectSettings.h"

#include <algorithm>
#include <stdexcept>
#include <string>

using json = nlohmann::json;

namespace app_logic {

	namespace {
		const char* shapeKey(NodeShape shape) {
			switch (shape) {
			case NodeShape::Circle:           return "circle";
			case NodeShape::Rhombus:          return "rhombus";
			case NodeShape::RoundedRectangle: return "rounded_rectangle";
			default:                          return "rectangle";
			}
		}

		NodeShape shapeFromKey(const std::string& key) {
			for (NodeShape s : { NodeShape::Rectangle, NodeShape::Circle, NodeShape::Rhombus, NodeShape::RoundedRectangle })
				if (key == shapeKey(s)) return s;
			throw std::runtime_error("Forma desconocida: " + key);
		}

		json boxToJSON(const BoxSize& b) {
			return { { "width", b.width }, { "height", b.height } };
		}

		void boxFromJSON(const json& j, const char* key, BoxSize& b) {
			if (!j.contains(key)) return;
			const json& box = j.at(key);
			if (box.contains("width"))  b.width = box.at("width").get<double>();
			if (box.contains("height")) b.height = box.at("height").get<double>();
		}
	}

	ProjectSettings ProjectSettings::clamped() const {
		ProjectSettings s = *this;
		s.font_size = std::clamp(font_size, MIN_FONT_SIZE, MAX_FONT_SIZE);
		s.layout = layout.clamped();
		return s;
	}

	void ProjectSettings::toJSON(json& j) const {
		j["defaults"] = {
			{ "colour", colorToHex(colour) },
			{ "font_colour", colorToHex(font_colour) },
			{ "font_size", font_size },
			{ "shape", shapeKey(shape) },
			{ "equivalence_continuous", equivalence_continuous } };
		j["layout"] = {
			{ "block_sep", layout.block_sep },
			{ "layer_gap", layout.layer_gap },
			{ "horizontal_sep", layout.horizontal_sep },
			{ "box_sizes", {
				{ "rectangle", boxToJSON(layout.box_sizes.rectangle) },
				{ "circle", boxToJSON(layout.box_sizes.circle) },
				{ "rhombus", boxToJSON(layout.box_sizes.rhombus) } } } };
	}

	ProjectSettings ProjectSettings::fromJSON(const json& j) {
		ProjectSettings s;
		try {
			if (j.contains("defaults")) {
				const json& d = j.at("defaults");
				if (d.contains("colour"))      s.colour = colorFromHex(d.at("colour").get<std::string>());
				if (d.contains("font_colour")) s.font_colour = colorFromHex(d.at("font_colour").get<std::string>());
				if (d.contains("font_size"))   s.font_size = d.at("font_size").get<int>();
				if (d.contains("shape"))       s.shape = shapeFromKey(d.at("shape").get<std::string>());
				if (d.contains("equivalence_continuous"))
					s.equivalence_continuous = d.at("equivalence_continuous").get<bool>();
			}
			if (j.contains("layout")) {
				const json& l = j.at("layout");
				if (l.contains("block_sep"))      s.layout.block_sep = l.at("block_sep").get<double>();
				if (l.contains("layer_gap"))      s.layout.layer_gap = l.at("layer_gap").get<double>();
				if (l.contains("horizontal_sep")) s.layout.horizontal_sep = l.at("horizontal_sep").get<double>();
				if (l.contains("box_sizes")) {
					const json& b = l.at("box_sizes");
					boxFromJSON(b, "rectangle", s.layout.box_sizes.rectangle);
					boxFromJSON(b, "circle", s.layout.box_sizes.circle);
					boxFromJSON(b, "rhombus", s.layout.box_sizes.rhombus);
				}
			}
		}
		catch (const std::runtime_error&) { throw; }
		catch (const std::exception& e) {
			throw std::runtime_error(std::string("Ajustes del proyecto no válidos: ") + e.what());
		}
		return s.clamped();
	}

} // namespace app_logic

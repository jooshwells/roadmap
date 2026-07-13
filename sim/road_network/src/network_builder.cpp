#include "network_builder.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <vector>
#include "json.hpp"

using json = nlohmann::json;

// Parse an OSM turn:lanes value ("left|through|through;right") into per-lane
// TurnLane masks. Tokens run left to right, matching lane 0 = leftmost lane.
// Returns empty when the tag doesn't describe exactly 'lanes' lanes -- a
// mismatched map is worse than none, since the inference pass covers the gap.
static std::vector<uint8_t> parseOsmTurnLanes(const std::string& spec, int lanes)
{
    std::vector<uint8_t> masks;
    if (spec.empty() || lanes <= 0) return masks;

    std::vector<std::string> tokens;
    size_t start = 0;
    while (true)
    {
        size_t bar = spec.find('|', start);
        tokens.push_back(spec.substr(start, bar == std::string::npos ? bar : bar - start));
        if (bar == std::string::npos) break;
        start = bar + 1;
    }
    if (static_cast<int>(tokens.size()) != lanes) return masks;

    for (const std::string& token : tokens)
    {
        uint8_t mask = 0;
        // A token can carry multiple movements ("through;slight_right").
        size_t pos = 0;
        while (pos <= token.size())
        {
            size_t semi = token.find(';', pos);
            std::string part = token.substr(pos, semi == std::string::npos ? semi : semi - pos);

            // merge_to_* are merge hints, not turns; none/empty means an
            // unmarked lane, which in practice is a through lane.
            if (part.rfind("merge", 0) == 0 || part == "none" || part.empty() || part == "through")
                mask |= TurnLane::Through;
            else if (part.find("left") != std::string::npos || part == "reverse")
                mask |= TurnLane::Left;
            else if (part.find("right") != std::string::npos)
                mask |= TurnLane::Right;
            else
                mask |= TurnLane::Through; // unknown token: fail open

            if (semi == std::string::npos) break;
            pos = semi + 1;
        }
        masks.push_back(mask);
    }
    return masks;
}

namespace {

// OSM-style truthiness: any value except "no"/false counts as tagged
// (bridge can be "yes", "viaduct", true, or a merged list of those).
bool isTruthyTag(const json& v)
{
    if (v.is_boolean()) return v.get<bool>();
    if (v.is_string())
    {
        const std::string s = v.get<std::string>();
        return !s.empty() && s != "no";
    }
    if (v.is_array())
    {
        for (const auto& item : v)
            if (isTruthyTag(item)) return true;
    }
    return false;
}

// Effective vertical layer of an edge: the explicit OSM "layer" when present,
// otherwise bridge implies +1 and tunnel implies -1. The fallback keeps
// overpasses elevated in older exports that carry bridge tags but a null
// layer. Clamped to a sane range against junk data.
int parseEdgeLayer(const json& j)
{
    auto it = j.find("layer");
    if (it != j.end() && !it->is_null())
    {
        if (it->is_number()) return std::clamp(static_cast<int>(it->get<double>()), -5, 5);
        if (it->is_string())
        {
            try { return std::clamp(std::stoi(it->get<std::string>()), -5, 5); }
            catch (...) {}
        }
    }

    auto b = j.find("bridge");
    if (b != j.end() && isTruthyTag(*b)) return 1;

    auto t = j.find("tunnel");
    if (t != j.end() && isTruthyTag(*t)) return -1;

    return 0;
}

} // namespace

Network NetworkBuilder::buildNetworkFromJSONL(const std::string& nodePath, const std::string& edgePath)
{
    Network roadNetwork;

    std::ifstream nodeFile(nodePath);
    std::ifstream edgeFile(edgePath);

    for (int i = 0; i < 2; i++)   
    {

        if(!nodeFile.is_open() || !edgeFile.is_open())
        {
            std::cerr << "Fatal Error: Could not open map data at one or both files\n";
            return roadNetwork;
        }

        std::string line;
        int lineNumber = 0;
        // add nodes
        while (std::getline(nodeFile, line)) // each line an individual JSON object
        {
            lineNumber++;
            if (line.empty()) continue;
            try
            {
                json j = json::parse(line); // parse object
                std::string controlType = "none";
                if (j.contains("traffic_control") && !j["traffic_control"].is_null()) {
                    controlType = j["traffic_control"].get<std::string>();
                }
                roadNetwork.addNode(
                    j["id"],
                    j["lat"],
                    j["lon"],
                    j["x"],
                    -j["y"].get<double>(),
                    controlType
                );
            }
            catch(const json::exception& e)
            {
                std::cerr << "NODES: JSON Error on line " << lineNumber << ": " << e.what() << '\n';
            }
            catch(const std::invalid_argument& e)
            {
                std::cerr << "NODES: Graph Logic Error on line " << lineNumber << ": " << e.what() << "\n";
            }
        }
        lineNumber = 0;
        // add edges
        while (std::getline(edgeFile, line)) // each line an individual JSON object
        {
            lineNumber++;
            if (line.empty()) continue;
            // std::cout << "current line: " << line << std::endl;
            try
            {
                json j = json::parse(line); // parse object
                int lanes = j.value("lanes", 1);

                // Optional real-world centerline shape. The y flip matches the
                // node convention above (-j["y"]).
                std::vector<RoadGeomPoint> geometry;
                auto geomIt = j.find("geometry_xy");
                if (geomIt != j.end() && geomIt->is_array())
                {
                    geometry.reserve(geomIt->size());
                    for (const auto& p : *geomIt)
                    {
                        if (!p.contains("x") || !p.contains("y")) continue;
                        geometry.push_back({
                            p["x"].get<double>(),
                            -p["y"].get<double>(),
                            0.0
                        });
                    }
                }

                // Turn-lane data: most edges carry null here; parse what
                // exists and let assignInferredTurnLanes cover the rest.
                std::string turnSpec;
                for (const char* key : { "turn_lanes", "turn_lanes_forward" })
                {
                    auto it = j.find(key);
                    if (it != j.end() && it->is_string())
                    {
                        turnSpec = it->get<std::string>();
                        break;
                    }
                }

                roadNetwork.addDirectedEdge(
                    j["u"],
                    j["v"],
                    j["length_m"],
                    j["speed_mps"],
                    lanes,
                    std::move(geometry),
                    parseEdgeLayer(j),
                    parseOsmTurnLanes(turnSpec, lanes)
                );
            }
            catch(const json::exception& e)
            {
                std::cerr << "EDGES: JSON Error on line " << lineNumber << ": " << e.what() << '\n';
            }
            catch(const std::invalid_argument& e)
            {
                std::cerr << "EDGES: Graph Logic Error on line " << lineNumber << ": " << e.what() << "\n";
            }
        }
    }

    // With every edge loaded, turn the OSM layer tags into actual elevations
    // (bridge decks up, underpasses down, smooth ramps at their ends).
    roadNetwork.applyVerticality();

    roadNetwork.applyDefaultTrafficControls();
    roadNetwork.calculateIntersectionPriorities();
    roadNetwork.assignInferredTurnLanes();

    return roadNetwork; // successfully loaded network
}
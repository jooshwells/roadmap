#include "network_builder.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <vector>
#include "json.hpp"

using json = nlohmann::json;

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

                roadNetwork.addNode(
                    j["id"],
                    j["lat"],
                    j["lon"],
                    j["x"],
                    -j["y"].get<double>()
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
                std::string name = j.value("name", ""); // default to empty string if "name" key is missing

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

                roadNetwork.addDirectedEdge(
                    j["u"],
                    j["v"],
                    j["length_m"],
                    j["speed_mps"],
                    lanes,
                    std::move(geometry),
					parseEdgeLayer(j),
                    name
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

    return roadNetwork; // successfully loaded network
}
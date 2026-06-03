#include "network_builder.h"

// ── 1. UNREAL HEADERS FIRST ──────────────────────────────────────────────────
#include "CoreMinimal.h"

// ── 2. WRAP THIRD-PARTY & STD LIBRARIES TO PREVENT 43,000 MACRO ERRORS ───────
THIRD_PARTY_INCLUDES_START
#include <fstream>
#include <iostream>
#include "json.hpp"
THIRD_PARTY_INCLUDES_END

using json = nlohmann::json;

Network NetworkBuilder::buildNetworkFromJSONL(const std::string& nodePath, const std::string& edgePath)
{
    Network roadNetwork;

    std::ifstream nodeFile(nodePath);
    std::ifstream edgeFile(edgePath);

    if (!nodeFile.is_open() || !edgeFile.is_open())
    {
        UE_LOG(LogTemp, Error, TEXT("Fatal Error: Could not open map data at one or both files"));
        return roadNetwork;
    }

    std::string line;
    int lineNumber = 0;

    // ── 3. ADD NODES ─────────────────────────────────────────────────────────
    UE_LOG(LogTemp, Log, TEXT("RoadMap Sim: Parsing Nodes..."));
    while (std::getline(nodeFile, line))
    {
        lineNumber++;
        if (line.empty()) continue;

        try
        {
            json j = json::parse(line);

            // Use .value() to provide safe defaults if lat/lon are missing
            roadNetwork.addNode(
                j["id"].get<uint64_t>(),
                j.value("lat", 0.0),
                j.value("lon", 0.0),
                j.value("x", 0.0),
                j.value("y", 0.0)
            );
        }
        catch (const json::exception& e)
        {
            UE_LOG(LogTemp, Error, TEXT("NODES: JSON Error on line %d: %hs"), lineNumber, e.what());
        }
        catch (const std::invalid_argument& e)
        {
            UE_LOG(LogTemp, Error, TEXT("NODES: Graph Logic Error on line %d: %hs"), lineNumber, e.what());
        }
    }

    // ── 4. ADD EDGES ─────────────────────────────────────────────────────────
    lineNumber = 0;
    UE_LOG(LogTemp, Log, TEXT("RoadMap Sim: Parsing Edges..."));
    while (std::getline(edgeFile, line))
    {
        lineNumber++;
        if (line.empty()) continue;

        try
        {
            json j = json::parse(line);

            // Check for both u/v AND source/target naming conventions
            uint64_t fromId = 0;
            uint64_t toId = 0;

            if (j.contains("u") && j.contains("v"))
            {
                fromId = j["u"].get<uint64_t>();
                toId = j["v"].get<uint64_t>();
            }
            else if (j.contains("source") && j.contains("target"))
            {
                fromId = j["source"].get<uint64_t>();
                toId = j["target"].get<uint64_t>();
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("EDGES: Missing source/target IDs on line %d"), lineNumber);
                continue; // Skip this edge since it has no valid connections
            }

            // Safely map lengths and speeds, falling back to defaults if keys don't exist
            double length = j.contains("length_m") ? j["length_m"].get<double>() : (j.contains("length") ? j["length"].get<double>() : 1.0);
            double speed = j.contains("speed_mps") ? j["speed_mps"].get<double>() : (j.contains("speed") ? j["speed"].get<double>() : 13.4);
            int lanes = j.value("lanes", 1);

            roadNetwork.addDirectedEdge(fromId, toId, length, speed, lanes);
        }
        catch (const json::exception& e)
        {
            UE_LOG(LogTemp, Error, TEXT("EDGES: JSON Error on line %d: %hs"), lineNumber, e.what());
        }
        catch (const std::invalid_argument& e)
        {
            UE_LOG(LogTemp, Error, TEXT("EDGES: Graph Logic Error on line %d: %hs"), lineNumber, e.what());
        }
    }

    UE_LOG(LogTemp, Log, TEXT("RoadMap Sim: Successfully parsed map data."));
    return roadNetwork;
}
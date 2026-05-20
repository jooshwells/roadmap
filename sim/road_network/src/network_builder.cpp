#include "network_builder.h"
#include <fstream>
#include <iostream>
#include "json.hpp"

using json = nlohmann::json;

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
                std::string nodeType = j.value("type", "PASS_THROUGH"); // get node type, default to pass through
                roadNetwork.addNode(
                    j["id"],
                    j["lat"],
                    j["lon"],
                    j["x"],
                    j["y"],
                    nodeType
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
               
                roadNetwork.addDirectedEdge(
                    j["u"],
                    j["v"],
                    j["length_m"],
                    j["speed_mps"],
                    lanes
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

    return roadNetwork; // successfully loaded network
}
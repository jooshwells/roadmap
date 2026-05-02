# Python Data Pipeline – RoadMap Project

## Overview

This folder contains the Python code I am writing for the RoadMap traffic simulator project.

My role on the team is to:
- Import raw OpenStreetMap (OSM) data
- Clean and filter roadway data
- Build a graph structure (nodes and edges)
- Export processed data to JSON for the C++ simulation team

The goal is to create a repeatable data pipeline so the team can regenerate road network data whenever needed.

## Raw Data

The raw OSM .pbf file is NOT included in this repository.

To download Florida OSM data:
https://download.geofabrik.de/north-america/us/florida.html


## What This Pipeline Does

The Python scripts will:
1. Load the Florida OSM extract
2. Filter relevant roadway types
3. Convert the data into a graph structure
4. Export nodes.json and edges.json

These JSON files will be used by the C++ simulation engine.

## Status

This pipeline is currently a work in progress.

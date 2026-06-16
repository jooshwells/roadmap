from pathlib import Path
import geopandas as gpd

"""
inspect_fdot.py

Quick utility script I use to inspect the FDOT GeoJSON file and
see what attribute fields are available before matching FDOT data
to RoadMap edges.
"""

BASE_DIR = Path(__file__).resolve().parents[2]

fdot = gpd.read_file(
    BASE_DIR
    / "data/fdot/Annual_Average_Daily_Traffic_TDA_-2830269347537693947.geojson"
)

for col in fdot.columns:
    print(col)
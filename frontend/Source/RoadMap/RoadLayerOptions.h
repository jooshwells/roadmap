#pragma once

#include "CoreMinimal.h"

// Shared elevation choices for the road toolbar and road editor panels,
// mapping a human-readable option to the OSM vertical layer the sim's
// elevation pass consumes (0 ground, +1 overpass, -1 underpass).
namespace RoadLayerOptions
{
    inline const TCHAR* Options[] =
    {
        TEXT("Ground"),
        TEXT("Bridge (+1)"),
        TEXT("High bridge (+2)"),
        TEXT("Underpass (-1)"),
    };

    inline int32 OptionToLayer(const FString& Option)
    {
        if (Option == TEXT("Bridge (+1)"))      return 1;
        if (Option == TEXT("High bridge (+2)")) return 2;
        if (Option == TEXT("Underpass (-1)"))   return -1;
        if (Option.StartsWith(TEXT("Layer ")))  return FCString::Atoi(*Option.Mid(6));
        return 0;
    }

    // Layers outside the dropdown list (rare OSM data like +3) get a generic
    // entry so the editor round-trips them without clamping.
    inline FString LayerToOption(int32 Layer)
    {
        switch (Layer)
        {
        case 0:  return TEXT("Ground");
        case 1:  return TEXT("Bridge (+1)");
        case 2:  return TEXT("High bridge (+2)");
        case -1: return TEXT("Underpass (-1)");
        default: return FString::Printf(TEXT("Layer %d"), Layer);
        }
    }
}

#pragma once

#include "CoreMinimal.h"
#include "Algo/Reverse.h"

// Shared per-lane turn choices for the road toolbar and road editor panels,
// in OSM turn:lanes syntax. "(none)" maps to an empty entry (no restriction
// on that lane).
namespace RoadTurnLaneOptions
{
    inline const TCHAR* Options[] =
    {
        TEXT("(none)"),
        TEXT("left"),
        TEXT("through"),
        TEXT("right"),
        TEXT("left;through"),
        TEXT("through;right"),
        TEXT("left;right"),
        TEXT("left;through;right"),
    };

    inline FString OptionToTurnValue(const FString& Option)
    {
        return Option == TEXT("(none)") ? FString() : Option;
    }

    inline FString TurnValueToOption(const FString& Value)
    {
        return Value.IsEmpty() ? FString(TEXT("(none)")) : Value;
    }

    // Mirrors a turn:lanes string for the opposite direction of travel.
    // turn:lanes is ordered left-to-right as seen by the driver, so the
    // reverse edge flips the lane order AND swaps left/right in each value
    // (including slight_/sharp_ variants): "left|through" -> "through|right".
    inline FString MirrorTurnLanes(const FString& Value)
    {
        if (Value.IsEmpty()) return Value;

        TArray<FString> Lanes;
        Value.ParseIntoArray(Lanes, TEXT("|"), /*CullEmpty*/ false);
        Algo::Reverse(Lanes);

        for (FString& Lane : Lanes)
        {
            TArray<FString> Turns;
            Lane.ParseIntoArray(Turns, TEXT(";"), /*CullEmpty*/ false);
            Algo::Reverse(Turns); // keep values left-to-right after the swap
            for (FString& Turn : Turns)
            {
                Turn.ReplaceInline(TEXT("left"), TEXT("\x01"));
                Turn.ReplaceInline(TEXT("right"), TEXT("left"));
                Turn.ReplaceInline(TEXT("\x01"), TEXT("right"));
            }
            Lane = FString::Join(Turns, TEXT(";"));
        }
        return FString::Join(Lanes, TEXT("|"));
    }
}

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Engine/Texture2D.h"
#include "HeatmapImageLoader.generated.h"

UCLASS()
class ROADMAP_API UHeatmapImageLoader : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Heatmaps")
    static UTexture2D* LoadTextureFromFile(const FString& FilePath);
};
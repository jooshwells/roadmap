#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "RoadmapGameInstance.generated.h"

// One selectable roadmap: either a read-only template shipped with the game or
// a user save. Always a pair of JSONL files (nodes + edges).
USTRUCT(BlueprintType)
struct FRoadmapEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Roadmap") FString DisplayName;
	UPROPERTY(BlueprintReadOnly, Category = "Roadmap") FString NodesPath;
	UPROPERTY(BlueprintReadOnly, Category = "Roadmap") FString EdgesPath;
	UPROPERTY(BlueprintReadOnly, Category = "Roadmap") FDateTime LastModified;
	UPROPERTY(BlueprintReadOnly, Category = "Roadmap") int64 SizeBytes = 0;
};

/**
 * Holds which roadmap the player is working on across level transitions.
 *
 * Templates are read-only JSONL pairs in Content/ThirdParty/MapTemplates
 * (<Name>_nodes.jsonl + <Name>_edges.jsonl). Starting a new roadmap copies the
 * pair into Saved/RoadMaps/<SaveName>/ so the shipped template data is never
 * written to; the road editor's exports append to the user's copy.
 */
UCLASS()
class SD1TEST_API URoadmapGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	static FString GetTemplatesDir();
	static FString GetSavesDir();

	// Templates sorted by name. Only pairs with both files present are listed.
	UFUNCTION(BlueprintCallable, Category = "Roadmap")
	TArray<FRoadmapEntry> GetAvailableTemplates() const;

	// User saves sorted most recently modified first.
	UFUNCTION(BlueprintCallable, Category = "Roadmap")
	TArray<FRoadmapEntry> GetSavedRoadmaps() const;

	// Copies the template pair into a new save folder and makes it the active
	// roadmap. Fails (with a user-facing reason) on bad names or collisions.
	UFUNCTION(BlueprintCallable, Category = "Roadmap")
	bool CreateRoadmapFromTemplate(const FRoadmapEntry& Template, const FString& NewName, FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "Roadmap")
	bool LoadRoadmap(const FRoadmapEntry& Saved, FString& OutError);

	UFUNCTION(BlueprintPure, Category = "Roadmap")
	bool HasActiveRoadmap() const { return bHasActiveRoadmap; }

	UFUNCTION(BlueprintPure, Category = "Roadmap")
	const FString& GetActiveRoadmapName() const { return ActiveRoadmapName; }

	UFUNCTION(BlueprintPure, Category = "Roadmap")
	const FString& GetActiveNodesPath() const { return ActiveNodesPath; }

	UFUNCTION(BlueprintPure, Category = "Roadmap")
	const FString& GetActiveEdgesPath() const { return ActiveEdgesPath; }

	// Strips characters that can't appear in a folder name; may return empty.
	static FString SanitizeRoadmapName(const FString& InName);

	// "Grid City", "Grid City 2", ... first name with no existing save folder.
	UFUNCTION(BlueprintCallable, Category = "Roadmap")
	FString MakeUniqueRoadmapName(const FString& BaseName) const;

private:
	void SetActiveRoadmap(const FString& Name, const FString& NodesPath, const FString& EdgesPath);

	bool bHasActiveRoadmap = false;
	FString ActiveRoadmapName;
	FString ActiveNodesPath;
	FString ActiveEdgesPath;
};

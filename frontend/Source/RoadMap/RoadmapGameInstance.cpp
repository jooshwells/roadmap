#include "RoadmapGameInstance.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

namespace
{
	const TCHAR* NodesFileName = TEXT("nodes.jsonl");
	const TCHAR* EdgesFileName = TEXT("edges.jsonl");
	const TCHAR* NodesSuffix = TEXT("_nodes.jsonl");
	const TCHAR* EdgesSuffix = TEXT("_edges.jsonl");

	int64 SafeFileSize(const FString& Path)
	{
		const int64 Size = IFileManager::Get().FileSize(*Path);
		return Size > 0 ? Size : 0;
	}
}

FString URoadmapGameInstance::GetTemplatesDir()
{
	FString Dir = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("ThirdParty/MapTemplates"));
	return FPaths::ConvertRelativePathToFull(Dir);
}

FString URoadmapGameInstance::GetSavesDir()
{
	FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("RoadMaps"));
	return FPaths::ConvertRelativePathToFull(Dir);
}

TArray<FRoadmapEntry> URoadmapGameInstance::GetAvailableTemplates() const
{
	TArray<FRoadmapEntry> Templates;
	const FString Dir = GetTemplatesDir();

	TArray<FString> NodeFiles;
	IFileManager::Get().FindFiles(NodeFiles, *FPaths::Combine(Dir, FString(TEXT("*")) + NodesSuffix), true, false);

	for (const FString& NodeFile : NodeFiles)
	{
		const FString Stem = NodeFile.LeftChop(FCString::Strlen(NodesSuffix));
		const FString NodesPath = FPaths::Combine(Dir, NodeFile);
		const FString EdgesPath = FPaths::Combine(Dir, Stem + EdgesSuffix);
		if (!IFileManager::Get().FileExists(*EdgesPath))
		{
			UE_LOG(LogTemp, Warning, TEXT("MapTemplates: '%s' has no matching %s%s, skipping."), *NodeFile, *Stem, EdgesSuffix);
			continue;
		}

		FRoadmapEntry Entry;
		Entry.DisplayName = Stem.Replace(TEXT("_"), TEXT(" "));
		Entry.NodesPath = NodesPath;
		Entry.EdgesPath = EdgesPath;
		Entry.LastModified = IFileManager::Get().GetTimeStamp(*NodesPath);
		Entry.SizeBytes = SafeFileSize(NodesPath) + SafeFileSize(EdgesPath);
		Templates.Add(MoveTemp(Entry));
	}

	Templates.Sort([](const FRoadmapEntry& A, const FRoadmapEntry& B) { return A.DisplayName < B.DisplayName; });
	return Templates;
}

TArray<FRoadmapEntry> URoadmapGameInstance::GetSavedRoadmaps() const
{
	TArray<FRoadmapEntry> Saves;
	const FString Dir = GetSavesDir();

	TArray<FString> SaveDirs;
	IFileManager::Get().FindFiles(SaveDirs, *FPaths::Combine(Dir, TEXT("*")), false, true);

	// File timestamps come back as UTC; shift them so the menu shows local time.
	const FTimespan UtcToLocal = FDateTime::Now() - FDateTime::UtcNow();

	for (const FString& SaveDir : SaveDirs)
	{
		const FString NodesPath = FPaths::Combine(Dir, SaveDir, NodesFileName);
		const FString EdgesPath = FPaths::Combine(Dir, SaveDir, EdgesFileName);
		if (!IFileManager::Get().FileExists(*NodesPath) || !IFileManager::Get().FileExists(*EdgesPath))
		{
			continue;
		}

		FRoadmapEntry Entry;
		Entry.DisplayName = SaveDir;
		Entry.NodesPath = NodesPath;
		Entry.EdgesPath = EdgesPath;
		const FDateTime NodesTime = IFileManager::Get().GetTimeStamp(*NodesPath);
		const FDateTime EdgesTime = IFileManager::Get().GetTimeStamp(*EdgesPath);
		Entry.LastModified = (NodesTime > EdgesTime ? NodesTime : EdgesTime) + UtcToLocal;
		Entry.SizeBytes = SafeFileSize(NodesPath) + SafeFileSize(EdgesPath);
		Saves.Add(MoveTemp(Entry));
	}

	Saves.Sort([](const FRoadmapEntry& A, const FRoadmapEntry& B) { return A.LastModified > B.LastModified; });
	return Saves;
}

FString URoadmapGameInstance::SanitizeRoadmapName(const FString& InName)
{
	FString Clean;
	Clean.Reserve(InName.Len());
	for (const TCHAR C : InName)
	{
		if (FChar::IsAlnum(C) || C == TEXT(' ') || C == TEXT('-') || C == TEXT('_'))
		{
			Clean.AppendChar(C);
		}
	}
	Clean.TrimStartAndEndInline();
	return Clean;
}

FString URoadmapGameInstance::MakeUniqueRoadmapName(const FString& BaseName) const
{
	const FString Base = SanitizeRoadmapName(BaseName);
	const FString SavesDir = GetSavesDir();

	FString Candidate = Base;
	for (int32 Suffix = 2; IFileManager::Get().DirectoryExists(*FPaths::Combine(SavesDir, Candidate)); ++Suffix)
	{
		Candidate = FString::Printf(TEXT("%s %d"), *Base, Suffix);
	}
	return Candidate;
}

bool URoadmapGameInstance::CreateRoadmapFromTemplate(const FRoadmapEntry& Template, const FString& NewName, FString& OutError)
{
	const FString CleanName = SanitizeRoadmapName(NewName);
	if (CleanName.IsEmpty())
	{
		OutError = TEXT("Enter a name (letters, numbers, spaces, - and _).");
		return false;
	}

	if (!IFileManager::Get().FileExists(*Template.NodesPath) || !IFileManager::Get().FileExists(*Template.EdgesPath))
	{
		OutError = FString::Printf(TEXT("Template '%s' is missing its data files."), *Template.DisplayName);
		return false;
	}

	const FString SaveDir = FPaths::Combine(GetSavesDir(), CleanName);
	if (IFileManager::Get().DirectoryExists(*SaveDir))
	{
		OutError = FString::Printf(TEXT("A roadmap named '%s' already exists."), *CleanName);
		return false;
	}

	if (!IFileManager::Get().MakeDirectory(*SaveDir, /*Tree=*/true))
	{
		OutError = TEXT("Could not create the save folder.");
		return false;
	}

	// Copy the template pair; the user only ever edits this copy.
	const FString NodesDest = FPaths::Combine(SaveDir, NodesFileName);
	const FString EdgesDest = FPaths::Combine(SaveDir, EdgesFileName);
	if (IFileManager::Get().Copy(*NodesDest, *Template.NodesPath) != COPY_OK ||
		IFileManager::Get().Copy(*EdgesDest, *Template.EdgesPath) != COPY_OK)
	{
		IFileManager::Get().DeleteDirectory(*SaveDir, /*RequireExists=*/false, /*Tree=*/true);
		OutError = TEXT("Failed to copy the template data.");
		return false;
	}

	SetActiveRoadmap(CleanName, NodesDest, EdgesDest);
	UE_LOG(LogTemp, Log, TEXT("Roadmap '%s' created from template '%s' at %s"), *CleanName, *Template.DisplayName, *SaveDir);
	return true;
}

bool URoadmapGameInstance::LoadRoadmap(const FRoadmapEntry& Saved, FString& OutError)
{
	if (!IFileManager::Get().FileExists(*Saved.NodesPath) || !IFileManager::Get().FileExists(*Saved.EdgesPath))
	{
		OutError = FString::Printf(TEXT("'%s' is missing its data files on disk."), *Saved.DisplayName);
		return false;
	}

	SetActiveRoadmap(Saved.DisplayName, Saved.NodesPath, Saved.EdgesPath);
	return true;
}

bool URoadmapGameInstance::DeleteRoadmap(const FRoadmapEntry& Saved, FString& OutError)
{
	FString SaveDir = FPaths::ConvertRelativePathToFull(FPaths::GetPath(Saved.NodesPath));
	const FString SavesRoot = GetSavesDir();
	if (SaveDir == SavesRoot || !FPaths::IsUnderDirectory(SaveDir, SavesRoot))
	{
		OutError = FString::Printf(TEXT("'%s' is not a user save and cannot be deleted."), *Saved.DisplayName);
		return false;
	}

	if (!IFileManager::Get().DeleteDirectory(*SaveDir, /*RequireExists=*/false, /*Tree=*/true))
	{
		OutError = FString::Printf(TEXT("Could not delete '%s' \u2014 a file may be in use."), *Saved.DisplayName);
		return false;
	}

	if (bHasActiveRoadmap && ActiveNodesPath == Saved.NodesPath)
	{
		bHasActiveRoadmap = false;
		ActiveRoadmapName.Empty();
		ActiveNodesPath.Empty();
		ActiveEdgesPath.Empty();
	}

	UE_LOG(LogTemp, Log, TEXT("Roadmap '%s' deleted (%s)"), *Saved.DisplayName, *SaveDir);
	return true;
}

void URoadmapGameInstance::SetActiveRoadmap(const FString& Name, const FString& NodesPath, const FString& EdgesPath)
{
	bHasActiveRoadmap = true;
	ActiveRoadmapName = Name;
	ActiveNodesPath = NodesPath;
	ActiveEdgesPath = EdgesPath;
}

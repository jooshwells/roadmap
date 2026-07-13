#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "RoadmapGameInstance.h"
#include "MainMenuWidget.generated.h"

class UBorder;
class UEditableText;
class UScrollBox;
class UTextBlock;
class UVerticalBox;
class UWidgetSwitcher;

DECLARE_DELEGATE_OneParam(FOnMenuRowSelected, int32);

// UButton::OnClicked carries no payload, so list rows use this thin subclass
// that remembers its index and re-broadcasts clicks with it.
UCLASS()
class ROADMAP_API URoadmapRowButton : public UButton
{
	GENERATED_BODY()

public:
	void InitRow(int32 InIndex);

	FOnMenuRowSelected OnRowSelected;

private:
	UFUNCTION()
	void HandleClicked();

	int32 RowIndex = INDEX_NONE;
};

/**
 * The whole main menu, built in C++ (no Blueprint widget needed):
 *  - Start New Road Map: pick a template, name the copy, play on the copy
 *  - Load Road Map: pick one of the user's saves
 *  - Quit
 * Template/save discovery and copying live in URoadmapGameInstance; this class
 * is presentation + flow only.
 */
UCLASS()
class ROADMAP_API UMainMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual bool Initialize() override;

	// Level opened after a roadmap is created or loaded.
	UPROPERTY(EditDefaultsOnly, Category = "Main Menu")
	FName SimulationLevelName = TEXT("MainLevel");

protected:
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	// --- Page flow ---------------------------------------------------------
	enum class EMenuPage : int32 { Root = 0, NewRoadmap = 1, LoadRoadmap = 2 };

	void ShowPage(EMenuPage Page);
	void RefreshTemplateList();
	void RefreshSaveList();

	UFUNCTION() void OnNewRoadmapClicked();
	UFUNCTION() void OnLoadRoadmapClicked();
	UFUNCTION() void OnQuitClicked();
	UFUNCTION() void OnBackClicked();
	UFUNCTION() void OnCreateAndStartClicked();
	UFUNCTION() void OnOpenSaveClicked();
	UFUNCTION() void OnNameCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	void OnTemplateRowSelected(int32 Index);
	void OnSaveRowSelected(int32 Index);

	void ShowError(const FString& Message);
	void ClearError();
	void StartSimulation();

	URoadmapGameInstance* GetRoadmapGameInstance() const;

	// --- Construction helpers ----------------------------------------------
	void BuildTree();
	UWidget* BuildRootPage();
	UWidget* BuildNewPage();
	UWidget* BuildLoadPage();

	UTextBlock* MakeText(const FString& Text, int32 Size, const FLinearColor& Color,
		const FName& Typeface = FName("Regular"), int32 LetterSpacing = 0);
	UButton* MakeActionButton(const FString& Label, bool bPrimary);
	UWidget* WrapMinWidth(UWidget* Inner, float MinWidth);
	URoadmapRowButton* MakeListRow(int32 Index, const FString& Title, const FString& Subtitle);
	void ApplyRowSelection(const TArray<TObjectPtr<URoadmapRowButton>>& Rows, int32 SelectedIndex);

	// --- State --------------------------------------------------------------
	TArray<FRoadmapEntry> Templates;
	TArray<FRoadmapEntry> Saves;
	int32 SelectedTemplateIndex = INDEX_NONE;
	int32 SelectedSaveIndex = INDEX_NONE;

	// --- Widgets kept for updates after construction -------------------------
	UPROPERTY() TObjectPtr<UWidgetSwitcher> PageSwitcher;
	UPROPERTY() TObjectPtr<UVerticalBox> TemplateListBox;
	UPROPERTY() TObjectPtr<UVerticalBox> SaveListBox;
	UPROPERTY() TObjectPtr<UScrollBox> SaveScrollBox;
	UPROPERTY() TObjectPtr<UTextBlock> NoSavesText;
	UPROPERTY() TObjectPtr<UEditableText> NameField;
	UPROPERTY() TObjectPtr<UTextBlock> NewPageErrorText;
	UPROPERTY() TObjectPtr<UTextBlock> LoadPageErrorText;
	UPROPERTY() TObjectPtr<UButton> CreateButton;
	UPROPERTY() TObjectPtr<UButton> OpenButton;
	UPROPERTY() TArray<TObjectPtr<URoadmapRowButton>> TemplateRows;
	UPROPERTY() TArray<TObjectPtr<URoadmapRowButton>> SaveRows;
};

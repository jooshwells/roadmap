#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/SlateWrapperTypes.h" // FEventReply
#include "SimulationManager.h" // FIntersectionNodeInfo
#include "IntersectionInspectorWidget.generated.h"

class UVerticalBox;
class UTextBlock;
class UButton;
class UBorder;
class UCanvasPanelSlot;

// Self-contained intersection inspector panel: the entire widget tree is
// built in C++ (no UMG asset needed), like VehicleStatsWidget. Opened by
// AMapPlayerController when a junction / node is clicked outside draw mode.
// Shows the node's topology (control type, roads in/out, junction box) and
// the roads feeding it, and while open draws a per-frame world overlay at
// the node (setback circle + per-approach stop-line bars) so misplaced signs
// and clamped stop lines are visible where they actually are.
UCLASS()
class ROADMAP_API UIntersectionInspectorWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    // Targets the panel at a node. Also retargets an already-open panel when
    // another intersection is clicked.
    void InitWithInfo(ASimulationManager* InSimManager, const FIntersectionNodeInfo& InInfo);

    // Fired when the user closes the panel with the X button.
    FSimpleMulticastDelegate OnClosed;

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
    UFUNCTION()
    void HandleCloseClicked();

    // Title-bar drag: press starts, move repositions the window, release ends.
    UFUNCTION()
    FEventReply HandleTitleBarMouseDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);

    UFUNCTION()
    FEventReply HandleTitleBarMouseMove(FGeometry MyGeometry, const FPointerEvent& MouseEvent);

    UFUNCTION()
    FEventReply HandleTitleBarMouseUp(FGeometry MyGeometry, const FPointerEvent& MouseEvent);

    // Static topology rows + rebuilds the per-approach list. Run on retarget.
    void RefreshStaticFields();

    // Adds a "Label      value" row and returns the value text block.
    UTextBlock* AddValueRow(UVerticalBox* Parent, const FText& Label);

    // Adds a dim uppercase-style section header row.
    void AddSectionHeader(UVerticalBox* Parent, const FText& Label);

    FIntersectionNodeInfo Info;
    TWeakObjectPtr<ASimulationManager> SimManager;

    // Title-bar drag state (mirrors the other panels).
    bool bDraggingWindow = false;
    FVector2D DragStartScreenPos = FVector2D::ZeroVector;
    FVector2D DragStartPanelPos = FVector2D::ZeroVector;

    UPROPERTY() UCanvasPanelSlot* PanelSlot = nullptr;
    UPROPERTY() UBorder* TitleBar = nullptr;
    UPROPERTY() UTextBlock* HeaderText = nullptr;
    UPROPERTY() UButton* CloseButton = nullptr;
    UPROPERTY() UVerticalBox* BodyBox = nullptr;

    // Topology section
    UPROPERTY() UTextBlock* ControlValue = nullptr;
    UPROPERTY() UTextBlock* GeometryValue = nullptr;   // "4 in / 4 out"
    UPROPERTY() UTextBlock* SetbackValue = nullptr;    // junction box radius
    UPROPERTY() UTextBlock* MaxLanesValue = nullptr;

    // Per-approach rows. The count varies by intersection, so rows are reused
    // across retargets -- grown on demand, collapsed when surplus, never
    // removed. See RefreshStaticFields for why they are not rebuilt.
    UPROPERTY() UVerticalBox* ApproachBox = nullptr;
    UPROPERTY() TArray<UTextBlock*> ApproachRows;
    UPROPERTY() UTextBlock* NoApproachesText = nullptr;

    // Set by the X button so the panel closes on the next tick instead of
    // removing itself while its own click is still being routed.
    bool bPendingClose = false;

    static constexpr float MpsToMph = 2.23694f;
};

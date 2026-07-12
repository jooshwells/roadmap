#include "RoadEditorWidget.h"
#include "MapPlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/EditableTextBox.h"
#include "Components/CheckBox.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Misc/DefaultValueHelper.h"
#include "Styling/CoreStyle.h"
#include "RoadTurnLaneOptions.h"
#include "RoadLayerOptions.h"
#include "RoadPanelStyle.h"

void URoadEditorWidget::InitWithEdgeInfo(const FRoadEdgeInfo& Info)
{
    EdgeInfo = Info;
    CurrentLanes = FMath::Clamp(EdgeInfo.Lanes, MinLanes, MaxLanes);
    CurrentSpeedMph = FMath::Clamp(FMath::RoundToInt(EdgeInfo.SpeedLimitMps * MpsToMph), MinSpeedMph, MaxSpeedMph);
    CurrentTurnLanes = EdgeInfo.TurnLanes;
    CurrentLayer = EdgeInfo.Layer;

    // While cars are moving the panel is a read-only info card.
    AMapPlayerController* PC = Cast<AMapPlayerController>(GetOwningPlayer());
    bReadOnly = PC && !PC->IsRoadEditingAllowed();

    RefreshFields();
}

TSharedRef<SWidget> URoadEditorWidget::RebuildWidget()
{
    // Build the tree once, before the underlying Slate widget is created.
    if (WidgetTree && !WidgetTree->RootWidget)
    {
        UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
        WidgetTree->RootWidget = Canvas;

        // Dark movable panel, opening near the right edge of the screen (the
        // road toolbar opens on the left, so both can be open at once).
        // Visible border so clicks over the panel don't fall through to the map.
        UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
        Panel->SetBrushColor(FLinearColor(0.015f, 0.015f, 0.02f, 0.95f));
        Panel->SetPadding(FMargin(0.0f));
        Panel->SetVisibility(ESlateVisibility::Visible);

        PanelSlot = Canvas->AddChildToCanvas(Panel);
        PanelSlot->SetAnchors(FAnchors(1.0f, 0.5f, 1.0f, 0.5f));
        PanelSlot->SetAlignment(FVector2D(1.0f, 0.5f));
        PanelSlot->SetPosition(FVector2D(-40.0f, 0.0f));
        PanelSlot->SetAutoSize(true);

        UVerticalBox* Outer = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Outer"));
        Panel->SetContent(Outer);

        // Title bar: drag handle for moving the window.
        TitleBar = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("TitleBar"));
        TitleBar->SetBrushColor(FLinearColor(0.06f, 0.06f, 0.09f, 1.0f));
        TitleBar->SetPadding(FMargin(18.0f, 10.0f));

        HeaderText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Header"));
        HeaderText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 16));
        HeaderText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
        TitleBar->SetContent(HeaderText);

        UVerticalBoxSlot* TitleSlot = Outer->AddChildToVerticalBox(TitleBar);
        TitleSlot->SetHorizontalAlignment(HAlign_Fill);

        UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Rows"));
        UVerticalBoxSlot* BoxSlot = Outer->AddChildToVerticalBox(Box);
        BoxSlot->SetPadding(FMargin(18.0f, 12.0f, 18.0f, 18.0f));

        // Lanes: plain number entry, clamped to 1-6 on commit.
        LanesBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("Lanes"));
        RoadPanelStyle::StyleNumberField(LanesBox);
        AddRow(Box, NSLOCTEXT("RoadEditor", "Lanes", "Lanes (1-6)"), LanesBox);

        // Speed limit: plain number entry in mph, clamped to 5-80 on commit
        // (m/s in the sim).
        SpeedBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("Speed"));
        RoadPanelStyle::StyleNumberField(SpeedBox);
        AddRow(Box, NSLOCTEXT("RoadEditor", "Speed", "Speed limit (mph, max 80)"), SpeedBox);

        // Elevation: applying a non-ground layer turns the road into a
        // bridge/underpass in place (ramps, deck, and pillars included).
        LayerCombo = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("LayerCombo"));
        RoadPanelStyle::StyleTurnLaneCombo(LayerCombo);
        LayerCombo->OnGenerateWidgetEvent.BindUFunction(this, FName("MakeTurnLaneEntry"));
        for (const TCHAR* Option : RoadLayerOptions::Options)
        {
            LayerCombo->AddOption(Option);
        }
        AddRow(Box, NSLOCTEXT("RoadEditor", "Elevation", "Elevation"), LayerCombo);

        // Turn lanes: one dropdown per lane.
        UTextBlock* TurnHeader = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TurnHeader"));
        TurnHeader->SetText(NSLOCTEXT("RoadEditor", "TurnHeader", "Turn lanes (left lane first)"));
        TurnHeader->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 10));
        TurnHeader->SetColorAndOpacity(FSlateColor(RoadPanelStyle::RowLabel));
        UVerticalBoxSlot* TurnHeaderSlot = Box->AddChildToVerticalBox(TurnHeader);
        TurnHeaderSlot->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 2.0f));

        TurnLaneRows = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("TurnLaneRows"));
        Box->AddChildToVerticalBox(TurnLaneRows);

        // Apply to both directions
        BothDirectionsCheck = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), TEXT("BothDirs"));
        AddRow(Box, NSLOCTEXT("RoadEditor", "BothDirs", "Apply to both directions"), BothDirectionsCheck);

        // Apply / Cancel buttons
        UHorizontalBox* ButtonRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Buttons"));

        auto MakeButton = [this](const FText& Label, UTextBlock** OutText = nullptr) -> UButton*
        {
            UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
            UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
            Text->SetText(Label);
            Text->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
            Button->AddChild(Text);
            if (OutText) *OutText = Text;
            return Button;
        };

        // Delete sits on the left, away from Apply/Cancel, and is tinted red.
        DeleteButton = MakeButton(NSLOCTEXT("RoadEditor", "Delete", "  Delete road  "), &DeleteButtonText);
        DeleteButton->SetBackgroundColor(FLinearColor(1.0f, 0.35f, 0.35f, 1.0f));
        UHorizontalBoxSlot* DeleteSlot = ButtonRow->AddChildToHorizontalBox(DeleteButton);
        DeleteSlot->SetPadding(FMargin(0.0f, 0.0f, 24.0f, 0.0f));

        ApplyButton = MakeButton(NSLOCTEXT("RoadEditor", "Apply", "  Apply  "));
        UHorizontalBoxSlot* ApplySlot = ButtonRow->AddChildToHorizontalBox(ApplyButton);
        ApplySlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));

        CancelButton = MakeButton(NSLOCTEXT("RoadEditor", "Cancel", "  Cancel  "));
        ButtonRow->AddChildToHorizontalBox(CancelButton);

        UVerticalBoxSlot* ButtonRowSlot = Box->AddChildToVerticalBox(ButtonRow);
        ButtonRowSlot->SetPadding(FMargin(0.0f, 12.0f, 0.0f, 0.0f));
        ButtonRowSlot->SetHorizontalAlignment(HAlign_Right);
    }

    return Super::RebuildWidget();
}

void URoadEditorWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (ApplyButton) ApplyButton->OnClicked.AddUniqueDynamic(this, &URoadEditorWidget::HandleApplyClicked);
    if (CancelButton) CancelButton->OnClicked.AddUniqueDynamic(this, &URoadEditorWidget::HandleCancelClicked);
    if (DeleteButton) DeleteButton->OnClicked.AddUniqueDynamic(this, &URoadEditorWidget::HandleDeleteClicked);
    if (LanesBox) LanesBox->OnTextCommitted.AddUniqueDynamic(this, &URoadEditorWidget::HandleLanesCommitted);
    if (SpeedBox) SpeedBox->OnTextCommitted.AddUniqueDynamic(this, &URoadEditorWidget::HandleSpeedCommitted);
    if (LayerCombo) LayerCombo->OnSelectionChanged.AddUniqueDynamic(this, &URoadEditorWidget::HandleLayerComboChanged);

    if (TitleBar)
    {
        TitleBar->OnMouseButtonDownEvent.BindUFunction(this, FName("HandleTitleBarMouseDown"));
        TitleBar->OnMouseMoveEvent.BindUFunction(this, FName("HandleTitleBarMouseMove"));
        TitleBar->OnMouseButtonUpEvent.BindUFunction(this, FName("HandleTitleBarMouseUp"));
    }

    RefreshFields();
}

void URoadEditorWidget::RefreshFields()
{
    // Called both from InitWithEdgeInfo (which can run before the tree is
    // built) and from NativeConstruct (after it is) -- whichever comes last
    // sees the controls and wins.
    if (!HeaderText || !LanesBox || !SpeedBox || !BothDirectionsCheck) return;

    HeaderText->SetText(FText::FromString(FString::Printf(TEXT("%s  -  %.0f m, %s"),
        bReadOnly ? TEXT("Road Info (sim running)") : TEXT("Edit Road"),
        EdgeInfo.LengthMeters, EdgeInfo.bTwoWay ? TEXT("two-way") : TEXT("one-way"))));

    LanesBox->SetText(FText::AsNumber(CurrentLanes));
    SpeedBox->SetText(FText::AsNumber(CurrentSpeedMph));
    BothDirectionsCheck->SetIsChecked(EdgeInfo.bTwoWay);
    if (LayerCombo)
    {
        // A layer outside the dropdown list (e.g. +3 from OSM data) gets a
        // generic entry so retargeting the panel round-trips it unchanged.
        const FString Wanted = RoadLayerOptions::LayerToOption(CurrentLayer);
        if (LayerCombo->FindOptionIndex(Wanted) == INDEX_NONE)
        {
            LayerCombo->AddOption(Wanted);
        }
        LayerCombo->SetSelectedOption(Wanted);
    }
    RebuildTurnLaneCombos();

    // Read-only while the simulation runs: values stay visible, inputs lock,
    // and the mutating buttons disappear (Cancel still closes the panel).
    LanesBox->SetIsReadOnly(bReadOnly);
    SpeedBox->SetIsReadOnly(bReadOnly);
    BothDirectionsCheck->SetIsEnabled(!bReadOnly);
    if (LayerCombo) LayerCombo->SetIsEnabled(!bReadOnly);
    if (ApplyButton) ApplyButton->SetVisibility(bReadOnly ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
    if (DeleteButton) DeleteButton->SetVisibility(bReadOnly ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);

    // Retargeting to another road must not keep a half-confirmed delete.
    bDeleteArmed = false;
    if (DeleteButtonText) DeleteButtonText->SetText(NSLOCTEXT("RoadEditor", "Delete", "  Delete road  "));
}

void URoadEditorWidget::HandleLanesCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
    if (CommitMethod == ETextCommit::OnCleared) return;

    int32 Parsed = CurrentLanes; // non-numeric input keeps the previous value
    FDefaultValueHelper::ParseInt(Text.ToString(), Parsed);
    CurrentLanes = FMath::Clamp(Parsed, MinLanes, MaxLanes);
    if (LanesBox) LanesBox->SetText(FText::AsNumber(CurrentLanes));

    // Lane count changed: the per-lane dropdown list must match it.
    RebuildTurnLaneCombos();
    CurrentTurnLanes = ComposeTurnLanesFromCombos();
}

void URoadEditorWidget::HandleSpeedCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
    if (CommitMethod == ETextCommit::OnCleared) return;

    float Parsed = static_cast<float>(CurrentSpeedMph);
    FDefaultValueHelper::ParseFloat(Text.ToString(), Parsed);
    CurrentSpeedMph = FMath::Clamp(FMath::RoundToInt(Parsed), MinSpeedMph, MaxSpeedMph);
    if (SpeedBox) SpeedBox->SetText(FText::AsNumber(CurrentSpeedMph));
}

void URoadEditorWidget::HandleTurnLaneComboChanged(FString /*SelectedItem*/, ESelectInfo::Type SelectionType)
{
    if (SelectionType == ESelectInfo::Direct) return; // programmatic; avoids feedback loops

    CurrentTurnLanes = ComposeTurnLanesFromCombos();
}

void URoadEditorWidget::HandleLayerComboChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
    if (SelectionType == ESelectInfo::Direct) return; // programmatic; avoids feedback loops

    CurrentLayer = RoadLayerOptions::OptionToLayer(SelectedItem);
}

UWidget* URoadEditorWidget::MakeTurnLaneEntry(FString Item)
{
    UTextBlock* Entry = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
    Entry->SetText(FText::FromString(Item));
    Entry->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 10));
    Entry->SetColorAndOpacity(FSlateColor(RoadPanelStyle::ControlText));
    return Entry;
}

FEventReply URoadEditorWidget::HandleTitleBarMouseDown(FGeometry /*MyGeometry*/, const FPointerEvent& MouseEvent)
{
    if (!PanelSlot || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
    {
        return UWidgetBlueprintLibrary::Unhandled();
    }

    bDraggingWindow = true;
    DragStartScreenPos = MouseEvent.GetScreenSpacePosition();
    DragStartPanelPos = PanelSlot->GetPosition();

    // Capture so the drag keeps tracking even when the cursor outruns the bar.
    FEventReply Reply = UWidgetBlueprintLibrary::Handled();
    return UWidgetBlueprintLibrary::CaptureMouse(Reply, TitleBar);
}

FEventReply URoadEditorWidget::HandleTitleBarMouseMove(FGeometry /*MyGeometry*/, const FPointerEvent& MouseEvent)
{
    if (!bDraggingWindow || !PanelSlot) return UWidgetBlueprintLibrary::Unhandled();

    // Screen-space delta -> canvas units (viewport scale covers DPI scaling).
    const float Scale = FMath::Max(UWidgetLayoutLibrary::GetViewportScale(this), KINDA_SMALL_NUMBER);
    PanelSlot->SetPosition(DragStartPanelPos + (MouseEvent.GetScreenSpacePosition() - DragStartScreenPos) / Scale);
    return UWidgetBlueprintLibrary::Handled();
}

FEventReply URoadEditorWidget::HandleTitleBarMouseUp(FGeometry /*MyGeometry*/, const FPointerEvent& MouseEvent)
{
    if (!bDraggingWindow || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
    {
        return UWidgetBlueprintLibrary::Unhandled();
    }

    bDraggingWindow = false;
    FEventReply Reply = UWidgetBlueprintLibrary::Handled();
    return UWidgetBlueprintLibrary::ReleaseMouseCapture(Reply);
}

void URoadEditorWidget::RebuildTurnLaneCombos()
{
    if (!WidgetTree || !TurnLaneRows) return;

    TArray<FString> Existing;
    CurrentTurnLanes.ParseIntoArray(Existing, TEXT("|"), /*CullEmpty*/ false);

    TurnLaneRows->ClearChildren();
    TurnLaneCombos.Reset();

    for (int32 LaneIdx = 0; LaneIdx < CurrentLanes; LaneIdx++)
    {
        UComboBoxString* Combo = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass());
        RoadPanelStyle::StyleTurnLaneCombo(Combo);
        Combo->OnGenerateWidgetEvent.BindUFunction(this, FName("MakeTurnLaneEntry"));
        for (const TCHAR* Option : RoadTurnLaneOptions::Options)
        {
            Combo->AddOption(Option);
        }

        FString Wanted = TEXT("(none)");
        if (Existing.IsValidIndex(LaneIdx))
        {
            Wanted = RoadTurnLaneOptions::TurnValueToOption(Existing[LaneIdx].TrimStartAndEnd());
        }
        if (Combo->FindOptionIndex(Wanted) == INDEX_NONE)
        {
            Combo->AddOption(Wanted); // value the dropdown list doesn't cover, e.g. "slight_right"
        }
        Combo->SetSelectedOption(Wanted);
        Combo->OnSelectionChanged.AddUniqueDynamic(this, &URoadEditorWidget::HandleTurnLaneComboChanged);
        Combo->SetIsEnabled(!bReadOnly);

        TurnLaneCombos.Add(Combo);
        AddRow(TurnLaneRows, FText::FromString(FString::Printf(TEXT("Lane %d"), LaneIdx + 1)), Combo);
    }
}

FString URoadEditorWidget::ComposeTurnLanesFromCombos() const
{
    TArray<FString> Parts;
    bool bAnySet = false;
    for (const UComboBoxString* Combo : TurnLaneCombos)
    {
        const FString Value = Combo ? RoadTurnLaneOptions::OptionToTurnValue(Combo->GetSelectedOption()) : FString();
        bAnySet |= !Value.IsEmpty();
        Parts.Add(Value);
    }
    // All "(none)" means the road has no turn-lane tagging at all.
    return bAnySet ? FString::Join(Parts, TEXT("|")) : FString();
}

void URoadEditorWidget::AddRow(UVerticalBox* Parent, const FText& Label, UWidget* Input)
{
    UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

    UTextBlock* LabelText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
    LabelText->SetText(Label);
    LabelText->SetColorAndOpacity(FSlateColor(RoadPanelStyle::RowLabel));

    UHorizontalBoxSlot* LabelSlot = Row->AddChildToHorizontalBox(LabelText);
    LabelSlot->SetPadding(FMargin(0.0f, 0.0f, 14.0f, 0.0f));
    LabelSlot->SetVerticalAlignment(VAlign_Center);
    LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

    USizeBox* InputSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
    InputSize->SetWidthOverride(190.0f);
    InputSize->AddChild(Input);

    UHorizontalBoxSlot* InputSlot = Row->AddChildToHorizontalBox(InputSize);
    InputSlot->SetVerticalAlignment(VAlign_Center);

    UVerticalBoxSlot* RowSlot = Parent->AddChildToVerticalBox(Row);
    RowSlot->SetPadding(FMargin(0.0f, 4.0f));
}

void URoadEditorWidget::HandleApplyClicked()
{
    FRoadEdgeInfo Edited = EdgeInfo;
    Edited.Lanes = CurrentLanes;
    Edited.SpeedLimitMps = static_cast<float>(CurrentSpeedMph) / MpsToMph;
    Edited.TurnLanes = ComposeTurnLanesFromCombos();
    Edited.Layer = CurrentLayer;

    if (AMapPlayerController* PC = Cast<AMapPlayerController>(GetOwningPlayer()))
    {
        if (PC->ApplyRoadEdit(Edited, BothDirectionsCheck->IsChecked()))
        {
            RemoveFromParent();
            return;
        }
    }

    if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red, TEXT("Road edit failed to apply."));
}

void URoadEditorWidget::HandleCancelClicked()
{
    RemoveFromParent();
}

void URoadEditorWidget::HandleDeleteClicked()
{
    if (!bDeleteArmed)
    {
        bDeleteArmed = true;
        if (DeleteButtonText) DeleteButtonText->SetText(NSLOCTEXT("RoadEditor", "ConfirmDelete", "  Confirm delete?  "));
        return;
    }

    if (AMapPlayerController* PC = Cast<AMapPlayerController>(GetOwningPlayer()))
    {
        // The checkbox doubles as "delete both directions" for two-way roads.
        if (PC->DeleteRoad(EdgeInfo, BothDirectionsCheck && BothDirectionsCheck->IsChecked()))
        {
            RemoveFromParent();
            return;
        }
    }

    bDeleteArmed = false;
    if (DeleteButtonText) DeleteButtonText->SetText(NSLOCTEXT("RoadEditor", "Delete", "  Delete road  "));
    if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red, TEXT("Road delete failed."));
}

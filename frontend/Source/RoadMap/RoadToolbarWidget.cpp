#include "RoadToolbarWidget.h"
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
#include "MenuPalette.h"

namespace
{
    // Local copy of SimControlBarWidget's compact button padding, so this
    // file's toolbar buttons match the transport bar's smaller size instead
    // of MenuPalette::ActionButtonStyle's default (larger) menu padding.
    FButtonStyle CompactButtonStyle(bool bPrimary)
    {
        FButtonStyle Style = MenuPalette::ActionButtonStyle(bPrimary);
        Style.SetNormalPadding(FMargin(14.0f, 8.0f))
            .SetPressedPadding(FMargin(14.0f, 9.0f, 14.0f, 7.0f));
        return Style;
    }
}

TSharedRef<SWidget> URoadToolbarWidget::RebuildWidget()
{
    if (WidgetTree && !WidgetTree->RootWidget)
    {
        UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
        WidgetTree->RootWidget = Canvas;

        // Small always-visible button that opens/closes the tool window.
        ToggleButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("ToggleButton"));
        ToggleButton->SetStyle(CompactButtonStyle(true));

        UTextBlock* ToggleLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ToggleLabel"));
        ToggleLabel->SetText(NSLOCTEXT("RoadToolbar", "Toggle", "Road Tools"));
        ToggleLabel->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 14));
        ToggleLabel->SetColorAndOpacity(FSlateColor(MenuPalette::TextOnAccent));
        ToggleLabel->SetJustification(ETextJustify::Center);
        ToggleButton->AddChild(ToggleLabel);

        // Fixed size so it lines up with the Telemetry button (a UMG Blueprint) --
        // set the same 140x40 on Telemetry's button in its Designer to match.
        USizeBox* ToggleSizer = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
        ToggleSizer->SetWidthOverride(140.0f);
        ToggleSizer->SetHeightOverride(40.0f);
        ToggleSizer->AddChild(ToggleButton);

        UCanvasPanelSlot* ToggleSlot = Canvas->AddChildToCanvas(ToggleSizer);
        ToggleSlot->SetAnchors(FAnchors(0.0f, 0.0f, 0.0f, 0.0f));
        ToggleSlot->SetAlignment(FVector2D(0.0f, 0.0f));
        ToggleSlot->SetPosition(FVector2D(40.0f, 18.0f)); // Y was 40.0f — now matches the center bar's top
        ToggleSlot->SetAutoSize(true);

        // The tool window: dark movable panel, hidden until the user opens it.
        // Visible border so clicks over the panel don't fall through to the map.
        Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
        FLinearColor PanelFill = MenuPalette::Background;
        PanelFill.A = 0.95f;
        Panel->SetBrush(MenuPalette::RoundedBrush(PanelFill, 14.0f, MenuPalette::Outline, 1.0f)); // NEW
        Panel->SetPadding(FMargin(0.0f));
        Panel->SetVisibility(ESlateVisibility::Collapsed);

        PanelSlot = Canvas->AddChildToCanvas(Panel);
        PanelSlot->SetAnchors(FAnchors(0.0f, 0.0f, 0.0f, 0.0f));
        PanelSlot->SetAlignment(FVector2D(0.0f, 0.0f));
        PanelSlot->SetPosition(FVector2D(40.0f, 90.0f));
        PanelSlot->SetAutoSize(true);

        UVerticalBox* Outer = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Outer"));
        Panel->SetContent(Outer);

        // Title bar: drag handle for moving the window.
        TitleBar = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("TitleBar"));
        TitleBar->SetBrush(MenuPalette::RoundedBrush(MenuPalette::CardFillHover, 14.0f, MenuPalette::Outline, 1.0f));
        TitleBar->SetPadding(FMargin(18.0f, 10.0f));

        UTextBlock* Header = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Header"));
        Header->SetText(NSLOCTEXT("RoadToolbar", "Header", "Road Tools"));
        Header->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 16));
        Header->SetColorAndOpacity(FSlateColor(FLinearColor::White));
        TitleBar->SetContent(Header);

        UVerticalBoxSlot* TitleSlot = Outer->AddChildToVerticalBox(TitleBar);
        TitleSlot->SetHorizontalAlignment(HAlign_Fill);

        UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Rows"));
        UVerticalBoxSlot* BoxSlot = Outer->AddChildToVerticalBox(Box);
        BoxSlot->SetPadding(FMargin(18.0f, 12.0f, 18.0f, 18.0f));

        // Draw Road toggle
        DrawButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("DrawButton"));
        DrawButton->SetStyle(CompactButtonStyle(true));
        DrawButtonLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DrawLabel"));
        DrawButtonLabel->SetText(NSLOCTEXT("RoadToolbar", "StartDrawing", "Start Drawing Road"));
        DrawButtonLabel->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 12));
        DrawButtonLabel->SetColorAndOpacity(FSlateColor(MenuPalette::TextOnAccent));
        DrawButtonLabel->SetJustification(ETextJustify::Center);
        DrawButton->AddChild(DrawButtonLabel);

        USizeBox* DrawSizer = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
        DrawSizer->SetWidthOverride(170.0f);  // smaller than before, no longer full-width
        DrawSizer->SetHeightOverride(36.0f);
        DrawSizer->AddChild(DrawButton);

        UVerticalBoxSlot* DrawSlot = Box->AddChildToVerticalBox(DrawSizer);
        DrawSlot->SetPadding(FMargin(0.0f, 14.0f, 0.0f, 16.0f)); // top gap separating it from the rows above
        DrawSlot->SetHorizontalAlignment(HAlign_Center); // or HAlign_Right if you'd rather it hug the edge

        // Status / hint line under the button.
        StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Status"));
        StatusText->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 9));
        StatusText->SetColorAndOpacity(FSlateColor(RoadPanelStyle::StatusLabel));
        StatusText->SetAutoWrapText(true);
        UVerticalBoxSlot* StatusSlot = Box->AddChildToVerticalBox(StatusText);
        StatusSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 20.0f));

        // Lanes: plain number entry, clamped to 1-6 on commit.
        LanesBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("Lanes"));
        LanesBox->SetText(FText::AsNumber(CurrentLanes));
        RoadPanelStyle::StyleNumberField(LanesBox);
        AddRow(Box, NSLOCTEXT("RoadToolbar", "Lanes", "Lanes (1-6)"), LanesBox, 14);

        // Speed limit: plain number entry in mph, clamped to 5-80 on commit
        // (m/s in the sim).
        SpeedBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("Speed"));
        SpeedBox->SetText(FText::AsNumber(CurrentSpeedMph));
        RoadPanelStyle::StyleNumberField(SpeedBox);
        AddRow(Box, NSLOCTEXT("RoadToolbar", "Speed", "Speed limit (mph, max 80)"), SpeedBox, 14);

        // Two-way
        TwoWayCheck = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), TEXT("TwoWay"));
        TwoWayCheck->SetIsChecked(true);
        AddRow(Box, NSLOCTEXT("RoadToolbar", "TwoWay", "Two-way street"), TwoWayCheck);

        // Elevation: new roads become bridges/underpasses via the sim's
        // vertical-layer pass (ramps, deck slabs, and pillars come free).
        LayerCombo = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("LayerCombo"));
        RoadPanelStyle::StyleTurnLaneCombo(LayerCombo);
        LayerCombo->OnGenerateWidgetEvent.BindUFunction(this, FName("MakeTurnLaneEntry"));
        for (const TCHAR* Option : RoadLayerOptions::Options)
        {
            LayerCombo->AddOption(Option);
        }
        LayerCombo->SetSelectedOption(RoadLayerOptions::LayerToOption(CurrentLayer));
        AddRow(Box, NSLOCTEXT("RoadToolbar", "Elevation", "Elevation"), LayerCombo, 14);

        // Turn lanes: one dropdown per lane.
        UTextBlock* TurnHeader = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TurnHeader"));
        TurnHeader->SetText(NSLOCTEXT("RoadToolbar", "TurnHeader", "Turn lanes (left lane first)"));
        TurnHeader->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 10));
        TurnHeader->SetColorAndOpacity(FSlateColor(RoadPanelStyle::RowLabel));
        UVerticalBoxSlot* TurnHeaderSlot = Box->AddChildToVerticalBox(TurnHeader);
        TurnHeaderSlot->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 2.0f));

        TurnLaneRows = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("TurnLaneRows"));
        Box->AddChildToVerticalBox(TurnLaneRows);
    }

    return Super::RebuildWidget();
}

void URoadToolbarWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (ToggleButton) ToggleButton->OnClicked.AddUniqueDynamic(this, &URoadToolbarWidget::HandleToggleWindowClicked);
    if (DrawButton) DrawButton->OnClicked.AddUniqueDynamic(this, &URoadToolbarWidget::HandleDrawClicked);
    if (LanesBox) LanesBox->OnTextCommitted.AddUniqueDynamic(this, &URoadToolbarWidget::HandleLanesCommitted);
    if (SpeedBox) SpeedBox->OnTextCommitted.AddUniqueDynamic(this, &URoadToolbarWidget::HandleSpeedCommitted);
    if (TwoWayCheck) TwoWayCheck->OnCheckStateChanged.AddUniqueDynamic(this, &URoadToolbarWidget::HandleTwoWayChanged);
    if (LayerCombo) LayerCombo->OnSelectionChanged.AddUniqueDynamic(this, &URoadToolbarWidget::HandleLayerComboChanged);

    if (TitleBar)
    {
        TitleBar->OnMouseButtonDownEvent.BindUFunction(this, FName("HandleTitleBarMouseDown"));
        TitleBar->OnMouseMoveEvent.BindUFunction(this, FName("HandleTitleBarMouseMove"));
        TitleBar->OnMouseButtonUpEvent.BindUFunction(this, FName("HandleTitleBarMouseUp"));
    }

    RebuildTurnLaneCombos();
    RefreshDrawStateVisuals();
}

void URoadToolbarWidget::HandleToggleWindowClicked()
{
    bWindowOpen = !bWindowOpen;
    if (Panel)
    {
        Panel->SetVisibility(bWindowOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    }
}

void URoadToolbarWidget::HandleDrawClicked()
{
    // Drawing is pre-run only; while the simulation runs the map is view-only.
    if (!bDrawing)
    {
        AMapPlayerController* PC = Cast<AMapPlayerController>(GetOwningPlayer());
        if (PC && !PC->IsRoadEditingAllowed())
        {
            if (StatusText)
            {
                StatusText->SetText(NSLOCTEXT("RoadToolbar", "StatusBlocked", "Road editing is disabled while the simulation is running. Stop it first."));
            }
            return;
        }
    }

    bDrawing = !bDrawing;
    PushDrawParams();
    RefreshDrawStateVisuals();
}

void URoadToolbarWidget::SyncDrawState(bool bInDrawing)
{
    bDrawing = bInDrawing;
    RefreshDrawStateVisuals();
}

void URoadToolbarWidget::HandleLanesCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
    if (CommitMethod == ETextCommit::OnCleared) return;

    int32 Parsed = CurrentLanes; // non-numeric input keeps the previous value
    FDefaultValueHelper::ParseInt(Text.ToString(), Parsed);
    CurrentLanes = FMath::Clamp(Parsed, MinLanes, MaxLanes);
    if (LanesBox) LanesBox->SetText(FText::AsNumber(CurrentLanes));

    // Lane count changed: the per-lane dropdown list must match it.
    RebuildTurnLaneCombos();
    CurrentTurnLanes = ComposeTurnLanesFromCombos();
    if (bDrawing) PushDrawParams();
}

void URoadToolbarWidget::HandleSpeedCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
    if (CommitMethod == ETextCommit::OnCleared) return;

    float Parsed = static_cast<float>(CurrentSpeedMph);
    FDefaultValueHelper::ParseFloat(Text.ToString(), Parsed);
    CurrentSpeedMph = FMath::Clamp(FMath::RoundToInt(Parsed), MinSpeedMph, MaxSpeedMph);
    if (SpeedBox) SpeedBox->SetText(FText::AsNumber(CurrentSpeedMph));

    if (bDrawing) PushDrawParams();
}

void URoadToolbarWidget::HandleTwoWayChanged(bool /*bIsChecked*/)
{
    if (bDrawing) PushDrawParams();
}

void URoadToolbarWidget::HandleLayerComboChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
    if (SelectionType == ESelectInfo::Direct) return; // programmatic; avoids feedback loops

    CurrentLayer = RoadLayerOptions::OptionToLayer(SelectedItem);
    if (bDrawing) PushDrawParams();
}

void URoadToolbarWidget::HandleTurnLaneComboChanged(FString /*SelectedItem*/, ESelectInfo::Type SelectionType)
{
    if (SelectionType == ESelectInfo::Direct) return; // programmatic; avoids feedback loops

    CurrentTurnLanes = ComposeTurnLanesFromCombos();
    if (bDrawing) PushDrawParams();
}

UWidget* URoadToolbarWidget::MakeTurnLaneEntry(FString Item)
{
    UTextBlock* Entry = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
    Entry->SetText(FText::FromString(Item));
    Entry->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 10));
    Entry->SetColorAndOpacity(FSlateColor(RoadPanelStyle::ControlText));
    return Entry;
}

FEventReply URoadToolbarWidget::HandleTitleBarMouseDown(FGeometry /*MyGeometry*/, const FPointerEvent& MouseEvent)
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

FEventReply URoadToolbarWidget::HandleTitleBarMouseMove(FGeometry /*MyGeometry*/, const FPointerEvent& MouseEvent)
{
    if (!bDraggingWindow || !PanelSlot) return UWidgetBlueprintLibrary::Unhandled();

    // Screen-space delta -> canvas units (viewport scale covers DPI scaling).
    const float Scale = FMath::Max(UWidgetLayoutLibrary::GetViewportScale(this), KINDA_SMALL_NUMBER);
    PanelSlot->SetPosition(DragStartPanelPos + (MouseEvent.GetScreenSpacePosition() - DragStartScreenPos) / Scale);
    return UWidgetBlueprintLibrary::Handled();
}

FEventReply URoadToolbarWidget::HandleTitleBarMouseUp(FGeometry /*MyGeometry*/, const FPointerEvent& MouseEvent)
{
    if (!bDraggingWindow || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
    {
        return UWidgetBlueprintLibrary::Unhandled();
    }

    bDraggingWindow = false;
    FEventReply Reply = UWidgetBlueprintLibrary::Handled();
    return UWidgetBlueprintLibrary::ReleaseMouseCapture(Reply);
}

void URoadToolbarWidget::PushDrawParams()
{
    AMapPlayerController* PC = Cast<AMapPlayerController>(GetOwningPlayer());
    if (!PC || !TwoWayCheck) return;

    PC->SetDrawMode(
        bDrawing,
        CurrentLanes,
        TwoWayCheck->IsChecked(),
        static_cast<float>(CurrentSpeedMph) / MpsToMph,
        CurrentTurnLanes,
        CurrentLayer);
}

void URoadToolbarWidget::RebuildTurnLaneCombos()
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
        Combo->OnSelectionChanged.AddUniqueDynamic(this, &URoadToolbarWidget::HandleTurnLaneComboChanged);

        TurnLaneCombos.Add(Combo);
        AddRow(TurnLaneRows, FText::FromString(FString::Printf(TEXT("Lane %d"), LaneIdx + 1)), Combo);
    }
}

FString URoadToolbarWidget::ComposeTurnLanesFromCombos() const
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

void URoadToolbarWidget::RefreshDrawStateVisuals()
{
    if (DrawButtonLabel)
    {
        DrawButtonLabel->SetText(bDrawing
            ? NSLOCTEXT("RoadToolbar", "StopDrawing", "  Stop Drawing  ")
            : NSLOCTEXT("RoadToolbar", "StartDrawing", "  Start Drawing Road  "));
    }
    if (StatusText)
    {
        StatusText->SetText(bDrawing
            ? NSLOCTEXT("RoadToolbar", "StatusOn", "Draw mode ON: click a road/intersection to start, click again to end. Right-click cancels.")
            : NSLOCTEXT("RoadToolbar", "StatusOff", "Click any road on the map to edit its speed limit and turn lanes."));
    }
    if (Panel)
    {
        Panel->SetBrushColor(bDrawing
            ? FLinearColor(0.02f, 0.06f, 0.02f, 0.95f)
            : FLinearColor(0.015f, 0.015f, 0.02f, 0.95f));
    }
}

void URoadToolbarWidget::AddRow(UVerticalBox* Parent, const FText& Label, UWidget* Input, int32 LabelFontSize)
{
    UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

    UTextBlock* LabelText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
    LabelText->SetText(Label);
    LabelText->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", LabelFontSize));
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

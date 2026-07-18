#include "TelemetryPanelWidget.h"

#include "Async/Async.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateImageBrush.h"
#include "Components/Border.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ComboBoxString.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Misc/Paths.h"
#include "RoadPanelStyle.h"
#include "RoadmapGameInstance.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"

// Shared telemetry colors and brushes match the C++ main menu visual language.
namespace TelemetryPalette
{
    // Converts a hexadecimal color into the linear color Unreal widgets use.
    FLinearColor Hex(const TCHAR* Code, float Alpha = 1.0f)
    {
        FLinearColor Color = FLinearColor::FromSRGBColor(FColor::FromHex(Code));
        Color.A = Alpha;
        return Color;
    }

    const FLinearColor Background = Hex(TEXT("0B0F16"));
    const FLinearColor PanelFill = Hex(TEXT("101620"), 0.98f);
    const FLinearColor CardFill = Hex(TEXT("FFFFFF"), 0.035f);
    const FLinearColor CardHover = Hex(TEXT("FFFFFF"), 0.07f);
    const FLinearColor Outline = Hex(TEXT("273140"));
    const FLinearColor OutlineHover = Hex(TEXT("3A4658"));
    const FLinearColor Accent = Hex(TEXT("F5B93E"));
    const FLinearColor AccentHover = Hex(TEXT("FFCE5C"));
    const FLinearColor AccentPressed = Hex(TEXT("D99F27"));
    const FLinearColor AccentSoft = Hex(TEXT("F5B93E"), 0.11f);
    const FLinearColor TextPrimary = Hex(TEXT("EEF2F7"));
    const FLinearColor TextSecondary = Hex(TEXT("8A94A6"));
    const FLinearColor TextFaint = Hex(TEXT("566072"));
    const FLinearColor TextOnAccent = Hex(TEXT("17130A"));
    const FLinearColor ErrorColor = Hex(TEXT("FF7A66"));
    const FLinearColor Success = Hex(TEXT("70D6A3"));
    constexpr float MinimumHeatmapZoom = 1.0f;
    constexpr float MaximumHeatmapZoom = 5.0f;
    constexpr float HeatmapZoomStep = 0.25f;

    // Creates a reusable rounded brush with an optional outline.
    FSlateBrush RoundedBrush(
        const FLinearColor& Fill,
        float Radius,
        const FLinearColor& OutlineColor = FLinearColor::Transparent,
        float OutlineWidth = 0.0f
    )
    {
        FSlateBrush Brush;
        Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
        Brush.TintColor = Fill;
        Brush.OutlineSettings = FSlateBrushOutlineSettings(
            FVector4(Radius, Radius, Radius, Radius),
            OutlineColor,
            OutlineWidth
        );
        Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
        return Brush;
    }

    // Creates the amber primary style or dark secondary button style.
    FButtonStyle ActionButtonStyle(bool bPrimary)
    {
        FButtonStyle Style;

        if (bPrimary)
        {
            Style.SetNormal(RoundedBrush(Accent, 8.0f))
                .SetHovered(RoundedBrush(AccentHover, 8.0f))
                .SetPressed(RoundedBrush(AccentPressed, 8.0f))
                .SetNormalForeground(TextOnAccent)
                .SetHoveredForeground(TextOnAccent)
                .SetPressedForeground(TextOnAccent);
        }
        else
        {
            Style.SetNormal(RoundedBrush(CardFill, 8.0f, Outline, 1.0f))
                .SetHovered(RoundedBrush(CardHover, 8.0f, OutlineHover, 1.0f))
                .SetPressed(RoundedBrush(AccentSoft, 8.0f, Accent, 1.0f))
                .SetNormalForeground(TextPrimary)
                .SetHoveredForeground(AccentHover)
                .SetPressedForeground(AccentPressed);
        }

        Style.SetNormalPadding(FMargin(18.0f, 11.0f));
        Style.SetPressedPadding(FMargin(18.0f, 12.0f, 18.0f, 10.0f));
        return Style;
    }

    // Creates the red style used only for deleting a saved run.
    FButtonStyle DestructiveButtonStyle()
    {
        FButtonStyle Style;
        Style.SetNormal(RoundedBrush(Hex(TEXT("7F1D1D")), 8.0f, Hex(TEXT("DC2626")), 1.0f))
            .SetHovered(RoundedBrush(Hex(TEXT("B91C1C")), 8.0f, Hex(TEXT("F87171")), 1.0f))
            .SetPressed(RoundedBrush(Hex(TEXT("991B1B")), 8.0f, Hex(TEXT("FCA5A5")), 1.0f))
            .SetNormalForeground(TextPrimary)
            .SetHoveredForeground(FLinearColor::White)
            .SetPressedForeground(FLinearColor::White);
        Style.SetNormalPadding(FMargin(18.0f, 11.0f));
        Style.SetPressedPadding(FMargin(18.0f, 12.0f, 18.0f, 10.0f));
        return Style;
    }

    // Creates a saved-run row style for its selected or unselected state.
    FButtonStyle RunRowStyle(bool bSelected)
    {
        FButtonStyle Style;

        if (bSelected)
        {
            Style.SetNormal(RoundedBrush(AccentSoft, 8.0f, Accent, 1.5f))
                .SetHovered(RoundedBrush(AccentSoft, 8.0f, AccentHover, 1.5f))
                .SetPressed(RoundedBrush(AccentSoft, 8.0f, AccentPressed, 1.5f));
        }
        else
        {
            Style.SetNormal(RoundedBrush(CardFill, 8.0f, Outline, 1.0f))
                .SetHovered(RoundedBrush(CardHover, 8.0f, OutlineHover, 1.0f))
                .SetPressed(RoundedBrush(CardHover, 8.0f, Accent, 1.0f));
        }

        Style.SetNormalPadding(FMargin(14.0f, 11.0f));
        Style.SetPressedPadding(FMargin(14.0f, 11.0f));
        return Style;
    }
}

// NOTE: no file-scope `using namespace TelemetryPalette;` here — in unity
// builds it leaks into every .cpp compiled after this one and collides with
// MenuPalette. Use function-scope directives instead.

// Saved-run row events

// Saves which run this row represents and connects its click event.
void UTelemetryRunButton::InitializeRow(int32 InRunIndex)
{
    RunIndex = InRunIndex;
    OnClicked.AddUniqueDynamic(this, &UTelemetryRunButton::HandleClicked);
}

// Sends this row's run number back to the main panel.
void UTelemetryRunButton::HandleClicked()
{
    OnRunSelected.ExecuteIfBound(RunIndex);
}

// Builds the panel the first time Unreal creates it, then loads saved runs.
bool UTelemetryPanelWidget::Initialize()
{
    if (!Super::Initialize())
    {
        return false;
    }

    if (WidgetTree && !WidgetTree->RootWidget)
    {
        BuildWidgetTree();
    }

    RefreshRunList();
    return true;
}

// Input handling
// The panel takes control of the mouse while it is open so map controls do not
// move the game camera at the same time.

// Locks game controls when the panel becomes visible.
void UTelemetryPanelWidget::NativeConstruct()
{
    Super::NativeConstruct();
    SuppressGameInput();
}

// Gives game controls back when the panel is removed.
void UTelemetryPanelWidget::NativeDestruct()
{
    RestoreGameInput();
    Super::NativeDestruct();
}

// Gives Slate a few frames to finish moving the map before placing its markers.
void UTelemetryPanelWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);
    if (HeatmapMarkerLayoutFramesRemaining > 0 && HeatmapViewer && HeatmapViewer->IsVisible())
    {
        UpdateHeatmapRoadMarker();
        UpdateHeatmapExtremeMarker();
        --HeatmapMarkerLayoutFramesRemaining;
    }
}

// Stops camera movement while still allowing the user to click the panel.
void UTelemetryPanelWidget::SuppressGameInput()
{
    APlayerController* PlayerController = GetOwningPlayer();
    if (!PlayerController || bTelemetryInputModeActive)
    {
        return;
    }

    bAddedMoveInputIgnore = !PlayerController->IsMoveInputIgnored();
    bAddedLookInputIgnore = !PlayerController->IsLookInputIgnored();
    if (bAddedMoveInputIgnore)
    {
        PlayerController->SetIgnoreMoveInput(true);
    }
    if (bAddedLookInputIgnore)
    {
        PlayerController->SetIgnoreLookInput(true);
    }

    FInputModeUIOnly InputMode;
    InputMode.SetWidgetToFocus(TakeWidget());
    InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    PlayerController->SetInputMode(InputMode);
    PlayerController->bShowMouseCursor = true;
    bTelemetryInputModeActive = true;
}

// Restores only the controls that this panel turned off.
void UTelemetryPanelWidget::RestoreGameInput()
{
    APlayerController* PlayerController = GetOwningPlayer();
    if (!PlayerController || !bTelemetryInputModeActive)
    {
        return;
    }

    if (bAddedMoveInputIgnore)
    {
        PlayerController->SetIgnoreMoveInput(false);
    }
    if (bAddedLookInputIgnore)
    {
        PlayerController->SetIgnoreLookInput(false);
    }

    FInputModeGameAndUI InputMode;
    InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    InputMode.SetHideCursorDuringCapture(false);
    PlayerController->SetInputMode(InputMode);
    PlayerController->bShowMouseCursor = true;

    bTelemetryInputModeActive = false;
    bAddedMoveInputIgnore = false;
    bAddedLookInputIgnore = false;
}

// Uses the mouse wheel to zoom only when the pointer is over the heatmap.
// Other panel areas still handle the wheel so the game camera does not move.
FReply UTelemetryPanelWidget::NativeOnMouseWheel(
    const FGeometry& InGeometry,
    const FPointerEvent& InMouseEvent
)
{
    using namespace TelemetryPalette;

    const FVector2D ScreenPosition = InMouseEvent.GetScreenSpacePosition();
    if (!IsPointerOverHeatmap(ScreenPosition))
    {
        return FReply::Handled();
    }

    SetHeatmapZoom(
        HeatmapZoom + InMouseEvent.GetWheelDelta() * HeatmapZoomStep,
        &ScreenPosition
    );
    UpdateHeatmapRoadHover(ScreenPosition);
    return FReply::Handled();
}

// Starts a map click or drag when the left mouse button is pressed.
FReply UTelemetryPanelWidget::NativeOnMouseButtonDown(
    const FGeometry& InGeometry,
    const FPointerEvent& InMouseEvent
)
{
    using namespace TelemetryPalette;

    if (InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
    {
        return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
    }

    // Blank panel areas do not have their own button to consume the click.
    // Handle it here so the road behind the panel is not selected too.
    if (!IsPointerOverHeatmap(InMouseEvent.GetScreenSpacePosition()))
    {
        return FReply::Handled();
    }

    bHeatmapPointerPressed = true;
    bHeatmapDragMoved = false;
    bIsHeatmapPanning = HeatmapZoom > MinimumHeatmapZoom;
    HeatmapPointerDownScreenPosition = InMouseEvent.GetScreenSpacePosition();
    LastPanPointerScreenPosition = HeatmapPointerDownScreenPosition;
    UpdateHeatmapRoadHover(HeatmapPointerDownScreenPosition);
    return FReply::Handled().CaptureMouse(TakeWidget());
}

// Finishes a drag, or pins a road when the user only clicked.
FReply UTelemetryPanelWidget::NativeOnMouseButtonUp(
    const FGeometry& InGeometry,
    const FPointerEvent& InMouseEvent
)
{
    if (InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
    {
        return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
    }

    // Match the handled press on blank panel space. Real child controls receive
    // their own click first, so this does not block buttons or drop-down menus.
    if (!bHeatmapPointerPressed)
    {
        return FReply::Handled();
    }

    if (!bHeatmapDragMoved && HoveredHeatmapRoadIndex != INDEX_NONE)
    {
        if (PinnedHeatmapRoadIndex == HoveredHeatmapRoadIndex)
        {
            PinnedHeatmapRoadIndex = INDEX_NONE;
            ShowHeatmapRoadDetails(HoveredHeatmapRoadIndex, false);
        }
        else
        {
            PinnedHeatmapRoadIndex = HoveredHeatmapRoadIndex;
            PinnedRoadNormalizedPoint = HoveredRoadNormalizedPoint;
            ShowHeatmapRoadDetails(PinnedHeatmapRoadIndex, true);
        }
        UpdateHeatmapRoadMarker();
    }
    else if (!bHeatmapDragMoved && HoveredHeatmapRoadIndex == INDEX_NONE)
    {
        PinnedHeatmapRoadIndex = INDEX_NONE;
        ClearHeatmapRoadInteraction();
    }

    bHeatmapPointerPressed = false;
    bIsHeatmapPanning = false;
    return FReply::Handled().ReleaseMouseCapture();
}

// Moves the map during a drag and updates the road under the pointer.
FReply UTelemetryPanelWidget::NativeOnMouseMove(
    const FGeometry& InGeometry,
    const FPointerEvent& InMouseEvent
)
{
    const FVector2D ScreenPosition = InMouseEvent.GetScreenSpacePosition();
    LastHeatmapPointerScreenPosition = ScreenPosition;

    if (!bHeatmapPointerPressed)
    {
        UpdateHeatmapRoadHover(ScreenPosition);
        return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
    }

    if (FVector2D::Distance(ScreenPosition, HeatmapPointerDownScreenPosition) > 4.0f)
    {
        bHeatmapDragMoved = true;
    }

    if (bIsHeatmapPanning && bHeatmapDragMoved)
    {
        HeatmapPan = ClampHeatmapPan(
            HeatmapPan + ScreenPosition - LastPanPointerScreenPosition
        );
        ApplyHeatmapViewTransform();
    }

    LastPanPointerScreenPosition = ScreenPosition;
    UpdateHeatmapRoadHover(ScreenPosition);
    return FReply::Handled();
}

// Small widget helpers used while building the panel in C++

// Creates text with the shared panel font and color settings.
UTextBlock* UTelemetryPanelWidget::MakeText(
    const FString& Text,
    int32 Size,
    const FLinearColor& Color,
    const FName& Typeface,
    int32 LetterSpacing
)
{
    UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>();
    FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(Typeface, Size);
    Font.LetterSpacing = LetterSpacing;
    Block->SetFont(Font);
    Block->SetText(FText::FromString(Text));
    Block->SetColorAndOpacity(FSlateColor(Color));
    return Block;
}

// Creates one readable item for a drop-down menu.
UWidget* UTelemetryPanelWidget::MakeComboEntry(FString Item)
{
    UTextBlock* Entry = MakeText(Item, 11, RoadPanelStyle::ControlText);
    Entry->SetAutoWrapText(false);
    return Entry;
}

// Creates either an amber main button or a dark normal button.
UButton* UTelemetryPanelWidget::MakeActionButton(const FString& Label, bool bPrimary)
{
    using namespace TelemetryPalette;
    UButton* Button = WidgetTree->ConstructWidget<UButton>();
    Button->SetStyle(ActionButtonStyle(bPrimary));

    UTextBlock* LabelText = MakeText(Label.ToUpper(), 12, TextPrimary, FName("Bold"), 100);
    LabelText->SetColorAndOpacity(FSlateColor::UseForeground());
    Button->AddChild(LabelText);

    if (UButtonSlot* LabelSlot = Cast<UButtonSlot>(LabelText->Slot))
    {
        LabelSlot->SetHorizontalAlignment(HAlign_Center);
        LabelSlot->SetVerticalAlignment(VAlign_Center);
    }

    return Button;
}

// Creates one saved-run row with its date and short summary.
UTelemetryRunButton* UTelemetryPanelWidget::MakeRunRow(
    int32 RunIndex,
    const FTelemetryRunInfo& RunInfo
)
{
    using namespace TelemetryPalette;
    UTelemetryRunButton* Row = WidgetTree->ConstructWidget<UTelemetryRunButton>();
    Row->InitializeRow(RunIndex);
    Row->OnRunSelected.BindUObject(this, &UTelemetryPanelWidget::HandleRunSelected);
    Row->SetStyle(RunRowStyle(false));

    UVerticalBox* Labels = WidgetTree->ConstructWidget<UVerticalBox>();
    Labels->AddChildToVerticalBox(MakeText(RunInfo.CreatedAt, 14, TextPrimary, FName("Medium")));

    const FString Summary = FString::Printf(
        TEXT("%d vehicles   |   %.1f mph recorded avg"),
        RunInfo.TotalVehicles,
        RunInfo.AverageSpeedMph
    );

    UTextBlock* SummaryText = MakeText(Summary, 11, TextSecondary);
    if (UVerticalBoxSlot* SummarySlot = Labels->AddChildToVerticalBox(SummaryText))
    {
        SummarySlot->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 0.0f));
    }

    Row->AddChild(Labels);
    if (UButtonSlot* LabelsSlot = Cast<UButtonSlot>(Labels->Slot))
    {
        LabelsSlot->SetHorizontalAlignment(HAlign_Fill);
        LabelsSlot->SetVerticalAlignment(VAlign_Center);
    }

    return Row;
}

// Builds the full panel, its four tabs, and the heatmap viewer.
void UTelemetryPanelWidget::BuildWidgetTree()
{
    // The whole panel is created here instead of depending on a large widget file.
    // Keeping each tab in one tree also makes it easy to share the selected run.
    using namespace TelemetryPalette;
    UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
    WidgetTree->RootWidget = Canvas;

    // Darken the game behind the panel so its text stays easy to read.
    UBorder* ScreenShade = WidgetTree->ConstructWidget<UBorder>();
    ScreenShade->SetBrush(FSlateColorBrush(Hex(TEXT("05070B"), 0.78f)));
    if (UCanvasPanelSlot* ShadeSlot = Canvas->AddChildToCanvas(ScreenShade))
    {
        ShadeSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
        ShadeSlot->SetOffsets(FMargin(0.0f));
    }

    // Place the main panel in the center at one steady size.
    UBorder* Panel = WidgetTree->ConstructWidget<UBorder>();
    Panel->SetBrush(RoundedBrush(PanelFill, 16.0f, Outline, 1.0f));
    Panel->SetPadding(FMargin(32.0f));
    if (UCanvasPanelSlot* PanelSlot = Canvas->AddChildToCanvas(Panel))
    {
        PanelSlot->SetAnchors(FAnchors(0.5f, 0.5f));
        PanelSlot->SetAlignment(FVector2D(0.5f, 0.5f));
        PanelSlot->SetSize(FVector2D(1240.0f, 760.0f));
    }

    UVerticalBox* PanelColumn = WidgetTree->ConstructWidget<UVerticalBox>();
    Panel->SetContent(PanelColumn);

    // Build the title row with refresh and close buttons.
    UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>();
    UVerticalBox* HeaderLabels = WidgetTree->ConstructWidget<UVerticalBox>();
    HeaderLabels->AddChildToVerticalBox(MakeText(
        TEXT("TELEMETRY ANALYSIS"),
        11,
        Accent,
        FName("Medium"),
        280
    ));
    UTextBlock* Heading = MakeText(TEXT("Simulation Runs"), 28, TextPrimary, FName("Bold"));
    if (UVerticalBoxSlot* HeadingSlot = HeaderLabels->AddChildToVerticalBox(Heading))
    {
        HeadingSlot->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 0.0f));
    }

    if (UHorizontalBoxSlot* LabelsSlot = Header->AddChildToHorizontalBox(HeaderLabels))
    {
        LabelsSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    }

    UButton* RefreshButton = MakeActionButton(TEXT("Refresh Runs"), false);
    RefreshButton->OnClicked.AddDynamic(this, &UTelemetryPanelWidget::HandleRefreshClicked);
    if (UHorizontalBoxSlot* RefreshSlot = Header->AddChildToHorizontalBox(RefreshButton))
    {
        RefreshSlot->SetPadding(FMargin(0.0f, 0.0f, 12.0f, 0.0f));
        RefreshSlot->SetVerticalAlignment(VAlign_Center);
    }

    UButton* CloseButton = MakeActionButton(TEXT("Close"), false);
    CloseButton->OnClicked.AddDynamic(this, &UTelemetryPanelWidget::HandleCloseClicked);
    if (UHorizontalBoxSlot* CloseSlot = Header->AddChildToHorizontalBox(CloseButton))
    {
        CloseSlot->SetVerticalAlignment(VAlign_Center);
    }

    PanelColumn->AddChildToVerticalBox(Header);

    // Split the body into the saved-run list and the active workspace.
    UHorizontalBox* Body = WidgetTree->ConstructWidget<UHorizontalBox>();
    if (UVerticalBoxSlot* BodySlot = PanelColumn->AddChildToVerticalBox(Body))
    {
        BodySlot->SetPadding(FMargin(0.0f, 26.0f, 0.0f, 0.0f));
        BodySlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    }

    // The left card scrolls when a map has many saved runs.
    UBorder* RunsCard = WidgetTree->ConstructWidget<UBorder>();
    RunsCard->SetBrush(RoundedBrush(CardFill, 10.0f, Outline, 1.0f));
    RunsCard->SetPadding(FMargin(18.0f));
    if (UHorizontalBoxSlot* RunsSlot = Body->AddChildToHorizontalBox(RunsCard))
    {
        RunsSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        RunsSlot->SetPadding(FMargin(0.0f, 0.0f, 16.0f, 0.0f));
    }

    UVerticalBox* RunsColumn = WidgetTree->ConstructWidget<UVerticalBox>();
    RunsCard->SetContent(RunsColumn);
    RunsColumn->AddChildToVerticalBox(MakeText(TEXT("SAVED RUNS"), 11, TextSecondary, FName("Medium"), 180));

    UScrollBox* RunScrollBox = WidgetTree->ConstructWidget<UScrollBox>();
    RunListBox = WidgetTree->ConstructWidget<UVerticalBox>();
    RunScrollBox->AddChild(RunListBox);
    if (UVerticalBoxSlot* ScrollSlot = RunsColumn->AddChildToVerticalBox(RunScrollBox))
    {
        ScrollSlot->SetPadding(FMargin(0.0f, 14.0f, 0.0f, 0.0f));
        ScrollSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    }

    // The right card holds the four telemetry workspaces.
    UBorder* DetailsCard = WidgetTree->ConstructWidget<UBorder>();
    DetailsCard->SetBrush(RoundedBrush(CardFill, 10.0f, Outline, 1.0f));
    DetailsCard->SetPadding(FMargin(22.0f));
    if (UHorizontalBoxSlot* DetailsSlot = Body->AddChildToHorizontalBox(DetailsCard))
    {
        DetailsSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    }

    UVerticalBox* DetailsColumn = WidgetTree->ConstructWidget<UVerticalBox>();
    DetailsCard->SetContent(DetailsColumn);

    // Keep the workspace choices in one row above their changing content.
    UHorizontalBox* WorkspaceTabs = WidgetTree->ConstructWidget<UHorizontalBox>();
    if (UVerticalBoxSlot* TabsSlot = DetailsColumn->AddChildToVerticalBox(WorkspaceTabs))
    {
        TabsSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 18.0f));
    }

    OverviewTabButton = MakeActionButton(TEXT("Overview"), true);
    OverviewTabButton->OnClicked.AddDynamic(this, &UTelemetryPanelWidget::HandleOverviewTabClicked);
    if (UHorizontalBoxSlot* OverviewSlot = WorkspaceTabs->AddChildToHorizontalBox(OverviewTabButton))
    {
        OverviewSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        OverviewSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
    }

    HeatmapsTabButton = MakeActionButton(TEXT("Heatmaps"), false);
    HeatmapsTabButton->OnClicked.AddDynamic(this, &UTelemetryPanelWidget::HandleHeatmapsTabClicked);
    if (UHorizontalBoxSlot* HeatmapsSlot = WorkspaceTabs->AddChildToHorizontalBox(HeatmapsTabButton))
    {
        HeatmapsSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        HeatmapsSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
    }

    FDOTTabButton = MakeActionButton(TEXT("FDOT"), false);
    FDOTTabButton->OnClicked.AddDynamic(this, &UTelemetryPanelWidget::HandleFDOTTabClicked);
    if (UHorizontalBoxSlot* FDOTSlot = WorkspaceTabs->AddChildToHorizontalBox(FDOTTabButton))
    {
        FDOTSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        FDOTSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
    }

    CompareTabButton = MakeActionButton(TEXT("Compare"), false);
    CompareTabButton->OnClicked.AddDynamic(this, &UTelemetryPanelWidget::HandleCompareTabClicked);
    if (UHorizontalBoxSlot* CompareSlot = WorkspaceTabs->AddChildToHorizontalBox(CompareTabButton))
    {
        CompareSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    }

    WorkspaceSwitcher = WidgetTree->ConstructWidget<UWidgetSwitcher>();
    if (UVerticalBoxSlot* SwitcherSlot = DetailsColumn->AddChildToVerticalBox(WorkspaceSwitcher))
    {
        SwitcherSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    }

    // Overview keeps the common run information approachable and free of tool controls.
    UVerticalBox* OverviewPanel = WidgetTree->ConstructWidget<UVerticalBox>();
    WorkspaceSwitcher->AddChild(OverviewPanel);
    OverviewPanel->AddChildToVerticalBox(MakeText(TEXT("RUN OVERVIEW"), 11, TextSecondary, FName("Medium"), 180));

    UTextBlock* OverviewHelp = MakeText(
        TEXT("A plain-language summary of what happened during the selected simulation."),
        12,
        TextFaint
    );
    OverviewHelp->SetAutoWrapText(true);
    if (UVerticalBoxSlot* OverviewHelpSlot = OverviewPanel->AddChildToVerticalBox(OverviewHelp))
    {
        OverviewHelpSlot->SetPadding(FMargin(0.0f, 7.0f, 0.0f, 0.0f));
    }

    RunDetailsText = MakeText(TEXT("Select a saved run to view its telemetry summary."), 14, TextSecondary);
    RunDetailsText->SetAutoWrapText(true);
    RunDetailsText->SetLineHeightPercentage(1.35f);

    // Keep long summaries inside their own scrollable area so the controls stay visible.
    UScrollBox* DetailsScrollBox = WidgetTree->ConstructWidget<UScrollBox>();
    DetailsScrollBox->AddChild(RunDetailsText);
    if (UVerticalBoxSlot* DetailsTextSlot = OverviewPanel->AddChildToVerticalBox(DetailsScrollBox))
    {
        DetailsTextSlot->SetPadding(FMargin(0.0f, 16.0f, 0.0f, 0.0f));
        DetailsTextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    }

    // Keep deletion on Overview and use red so it is not mistaken for a normal action.
    DeleteRunButton = MakeActionButton(TEXT("Delete Run"), false);
    DeleteRunButton->SetStyle(DestructiveButtonStyle());
    DeleteRunButtonText = Cast<UTextBlock>(DeleteRunButton->GetContent());
    DeleteRunButton->SetIsEnabled(false);
    DeleteRunButton->OnClicked.AddDynamic(this, &UTelemetryPanelWidget::HandleDeleteRunClicked);
    if (UVerticalBoxSlot* DeleteSlot = OverviewPanel->AddChildToVerticalBox(DeleteRunButton))
    {
        DeleteSlot->SetPadding(FMargin(0.0f, 12.0f, 0.0f, 0.0f));
        DeleteSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
    }

    // Heatmap tools stay hidden until the user chooses the Heatmaps tab.
    UVerticalBox* HeatmapsPanel = WidgetTree->ConstructWidget<UVerticalBox>();
    WorkspaceSwitcher->AddChild(HeatmapsPanel);
    HeatmapsPanel->AddChildToVerticalBox(MakeText(TEXT("HEATMAPS"), 11, TextSecondary, FName("Medium"), 180));

    UTextBlock* HeatmapHelp = MakeText(
        TEXT("Visualize where traffic conditions occurred. Choose a measurement, then limit the map to the roads that matter most."),
        12,
        TextFaint
    );
    HeatmapHelp->SetAutoWrapText(true);
    if (UVerticalBoxSlot* HeatmapHelpSlot = HeatmapsPanel->AddChildToVerticalBox(HeatmapHelp))
    {
        HeatmapHelpSlot->SetPadding(FMargin(0.0f, 7.0f, 0.0f, 0.0f));
    }

    UTextBlock* MetricLabel = MakeText(TEXT("HEATMAP METRIC"), 11, TextSecondary, FName("Medium"), 180);
    if (UVerticalBoxSlot* MetricLabelSlot = HeatmapsPanel->AddChildToVerticalBox(MetricLabel))
    {
        MetricLabelSlot->SetPadding(FMargin(0.0f, 18.0f, 0.0f, 8.0f));
    }

    // Let the user choose what road value the colors represent.
    MetricComboBox = WidgetTree->ConstructWidget<UComboBoxString>();
    RoadPanelStyle::StyleTurnLaneCombo(MetricComboBox);
    MetricComboBox->OnGenerateWidgetEvent.BindUFunction(this, FName("MakeComboEntry"));
    MetricComboBox->AddOption(TEXT("RoadMap Bottleneck Index"));
    MetricComboBox->AddOption(TEXT("Estimated Hourly Traffic Flow"));
    MetricComboBox->AddOption(TEXT("Average Recorded Speed"));
    MetricComboBox->AddOption(TEXT("Average Stopped Time per Vehicle Entry"));
    MetricComboBox->OnSelectionChanged.AddDynamic(
        this,
        &UTelemetryPanelWidget::HandleMetricSelectionChanged
    );
    MetricComboBox->SetSelectedOption(TEXT("RoadMap Bottleneck Index"));
    HeatmapsPanel->AddChildToVerticalBox(MetricComboBox);

    MetricHelpText = MakeText(GetMetricHelpText(TEXT("bottleneck_score")), 11, TextFaint);
    MetricHelpText->SetAutoWrapText(true);
    MetricHelpText->SetLineHeightPercentage(1.2f);
    if (UVerticalBoxSlot* MetricHelpSlot = HeatmapsPanel->AddChildToVerticalBox(MetricHelpText))
    {
        MetricHelpSlot->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 0.0f));
    }

    UTextBlock* FocusLabel = MakeText(TEXT("ROAD FOCUS"), 11, TextSecondary, FName("Medium"), 180);
    if (UVerticalBoxSlot* FocusLabelSlot = HeatmapsPanel->AddChildToVerticalBox(FocusLabel))
    {
        FocusLabelSlot->SetPadding(FMargin(0.0f, 14.0f, 0.0f, 8.0f));
    }

    // Let the user reduce clutter by showing only the worst roads.
    FocusComboBox = WidgetTree->ConstructWidget<UComboBoxString>();
    RoadPanelStyle::StyleTurnLaneCombo(FocusComboBox);
    FocusComboBox->OnGenerateWidgetEvent.BindUFunction(this, FName("MakeComboEntry"));
    FocusComboBox->AddOption(TEXT("All Roads"));
    FocusComboBox->AddOption(TEXT("Focused 25%"));
    FocusComboBox->AddOption(TEXT("Focused 10%"));
    FocusComboBox->AddOption(TEXT("Focused 5%"));
    FocusComboBox->OnSelectionChanged.AddDynamic(
        this,
        &UTelemetryPanelWidget::HandleFocusSelectionChanged
    );
    FocusComboBox->SetSelectedOption(TEXT("All Roads"));
    HeatmapsPanel->AddChildToVerticalBox(FocusComboBox);

    // Put the main heatmap action and optional regenerate action together.
    UHorizontalBox* Actions = WidgetTree->ConstructWidget<UHorizontalBox>();
    if (UVerticalBoxSlot* ActionsSlot = HeatmapsPanel->AddChildToVerticalBox(Actions))
    {
        ActionsSlot->SetPadding(FMargin(0.0f, 16.0f, 0.0f, 0.0f));
    }

    PrimaryHeatmapButton = MakeActionButton(TEXT("Generate & View"), true);
    PrimaryHeatmapButtonText = Cast<UTextBlock>(PrimaryHeatmapButton->GetContent());
    PrimaryHeatmapButton->OnClicked.AddDynamic(this, &UTelemetryPanelWidget::HandlePrimaryHeatmapClicked);
    if (UHorizontalBoxSlot* GenerateSlot = Actions->AddChildToHorizontalBox(PrimaryHeatmapButton))
    {
        GenerateSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        GenerateSlot->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 0.0f));
    }

    RegenerateHeatmapButton = MakeActionButton(TEXT("Regenerate"), false);
    RegenerateHeatmapButton->OnClicked.AddDynamic(this, &UTelemetryPanelWidget::HandleGenerateHeatmapClicked);
    if (UHorizontalBoxSlot* ViewSlot = Actions->AddChildToHorizontalBox(RegenerateHeatmapButton))
    {
        ViewSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    }
    RefreshHeatmapActionState();

    // The FDOT reference comparison uses plain language first and keeps GEH as a technical detail.
    UVerticalBox* FDOTPanel = WidgetTree->ConstructWidget<UVerticalBox>();
    WorkspaceSwitcher->AddChild(FDOTPanel);

    UTextBlock* FDOTLabel = MakeText(TEXT("FDOT REFERENCE COMPARISON"), 11, TextSecondary, FName("Medium"), 180);
    FDOTPanel->AddChildToVerticalBox(FDOTLabel);

    UTextBlock* FDOTHelp = MakeText(
        TEXT("Compare RoadMap's estimated hourly flow with 2025 Florida Department of Transportation reference estimates. This is a model check with stated assumptions, not a final validation."),
        12,
        TextFaint
    );
    FDOTHelp->SetAutoWrapText(true);
    FDOTHelp->SetLineHeightPercentage(1.2f);
    if (UVerticalBoxSlot* FDOTHelpSlot = FDOTPanel->AddChildToVerticalBox(FDOTHelp))
    {
        FDOTHelpSlot->SetPadding(FMargin(0.0f, 7.0f, 0.0f, 18.0f));
    }

    CompareFDOTButton = MakeActionButton(TEXT("Run FDOT Comparison"), true);
    CompareFDOTButton->OnClicked.AddDynamic(this, &UTelemetryPanelWidget::HandleCompareFDOTClicked);
    FDOTPanel->AddChildToVerticalBox(CompareFDOTButton);

    ViewFDOTMapButton = MakeActionButton(TEXT("View Comparison Map"), false);
    ViewFDOTMapButton->OnClicked.AddDynamic(this, &UTelemetryPanelWidget::HandleViewFDOTHeatmapClicked);
    if (UVerticalBoxSlot* ViewFDOTMapSlot = FDOTPanel->AddChildToVerticalBox(ViewFDOTMapButton))
    {
        ViewFDOTMapSlot->SetPadding(FMargin(0.0f, 10.0f, 0.0f, 0.0f));
    }
    RefreshHeatmapActionState();

    FDOTValidationText = MakeText(
        TEXT("Select a saved run, then start the comparison. Technical measures will be explained alongside the result."),
        12,
        TextSecondary
    );
    FDOTValidationText->SetAutoWrapText(true);
    FDOTValidationText->SetLineHeightPercentage(1.25f);

    // Long FDOT comparison results stay inside the tab instead of extending beyond the panel.
    UScrollBox* FDOTResultsScrollBox = WidgetTree->ConstructWidget<UScrollBox>();
    FDOTResultsScrollBox->AddChild(FDOTValidationText);
    if (UVerticalBoxSlot* FDOTTextSlot = FDOTPanel->AddChildToVerticalBox(FDOTResultsScrollBox))
    {
        FDOTTextSlot->SetPadding(FMargin(0.0f, 18.0f, 0.0f, 0.0f));
        FDOTTextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    }

    // Run comparison keeps the selected run as the baseline and explains each change.
    UScrollBox* ComparisonPageScroll = WidgetTree->ConstructWidget<UScrollBox>();
    WorkspaceSwitcher->AddChild(ComparisonPageScroll);
    UVerticalBox* ComparisonPanel = WidgetTree->ConstructWidget<UVerticalBox>();
    ComparisonPageScroll->AddChild(ComparisonPanel);
    ComparisonPanel->AddChildToVerticalBox(
        MakeText(TEXT("COMPARE SIMULATION RUNS"), 11, TextSecondary, FName("Medium"), 180)
    );

    UTextBlock* ComparisonHelp = MakeText(
        TEXT("Use one run as the baseline, then see whether traffic conditions improved or worsened in another run from the same map."),
        12,
        TextFaint
    );
    ComparisonHelp->SetAutoWrapText(true);
    if (UVerticalBoxSlot* HelpSlot = ComparisonPanel->AddChildToVerticalBox(ComparisonHelp))
    {
        HelpSlot->SetPadding(FMargin(0.0f, 7.0f, 0.0f, 14.0f));
    }

    ComparisonBaselineText = MakeText(TEXT("Baseline: Select a saved run"), 12, TextPrimary, FName("Medium"));
    ComparisonBaselineText->SetAutoWrapText(true);
    ComparisonPanel->AddChildToVerticalBox(ComparisonBaselineText);

    UTextBlock* CompareWithLabel = MakeText(TEXT("COMPARE WITH"), 10, TextSecondary, FName("Medium"), 140);
    if (UVerticalBoxSlot* LabelSlot = ComparisonPanel->AddChildToVerticalBox(CompareWithLabel))
    {
        LabelSlot->SetPadding(FMargin(0.0f, 14.0f, 0.0f, 7.0f));
    }

    // Only runs from this map are added to the second-run list.
    ComparisonRunComboBox = WidgetTree->ConstructWidget<UComboBoxString>();
    RoadPanelStyle::StyleTurnLaneCombo(ComparisonRunComboBox);
    ComparisonRunComboBox->OnGenerateWidgetEvent.BindUFunction(this, FName("MakeComboEntry"));
    ComparisonRunComboBox->OnSelectionChanged.AddDynamic(
        this,
        &UTelemetryPanelWidget::HandleComparisonRunChanged
    );
    ComparisonPanel->AddChildToVerticalBox(ComparisonRunComboBox);

    UHorizontalBox* ComparisonActions = WidgetTree->ConstructWidget<UHorizontalBox>();
    if (UVerticalBoxSlot* ActionSlot = ComparisonPanel->AddChildToVerticalBox(ComparisonActions))
    {
        ActionSlot->SetPadding(FMargin(0.0f, 12.0f, 0.0f, 0.0f));
    }
    CompareRunsButton = MakeActionButton(TEXT("Compare Runs"), true);
    CompareRunsButton->OnClicked.AddDynamic(this, &UTelemetryPanelWidget::HandleCompareRunsClicked);
    if (UHorizontalBoxSlot* CompareButtonSlot = ComparisonActions->AddChildToHorizontalBox(CompareRunsButton))
    {
        CompareButtonSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        CompareButtonSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
    }
    SwapComparisonRunsButton = MakeActionButton(TEXT("Swap"), false);
    SwapComparisonRunsButton->OnClicked.AddDynamic(this, &UTelemetryPanelWidget::HandleSwapComparisonRunsClicked);
    if (UHorizontalBoxSlot* SwapSlot = ComparisonActions->AddChildToHorizontalBox(SwapComparisonRunsButton))
    {
        SwapSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
    }

    // Map options are separate from the short written comparison above them.
    UTextBlock* ComparisonMapLabel = MakeText(TEXT("MAP COMPARISON"), 10, TextSecondary, FName("Medium"), 140);
    if (UVerticalBoxSlot* MapLabelSlot = ComparisonPanel->AddChildToVerticalBox(ComparisonMapLabel))
    {
        MapLabelSlot->SetPadding(FMargin(0.0f, 16.0f, 0.0f, 7.0f));
    }

    ComparisonMetricComboBox = WidgetTree->ConstructWidget<UComboBoxString>();
    RoadPanelStyle::StyleTurnLaneCombo(ComparisonMetricComboBox);
    ComparisonMetricComboBox->OnGenerateWidgetEvent.BindUFunction(this, FName("MakeComboEntry"));
    for (const FString& Option : {TEXT("RoadMap Bottleneck Index"), TEXT("Estimated Hourly Traffic Flow"), TEXT("Average Recorded Speed"), TEXT("Average Stopped Time per Vehicle Entry")})
    {
        ComparisonMetricComboBox->AddOption(Option);
    }
    ComparisonMetricComboBox->SetSelectedOption(TEXT("RoadMap Bottleneck Index"));
    ComparisonMetricComboBox->OnSelectionChanged.AddDynamic(this, &UTelemetryPanelWidget::HandleComparisonMetricChanged);
    ComparisonPanel->AddChildToVerticalBox(ComparisonMetricComboBox);

    ComparisonFocusComboBox = WidgetTree->ConstructWidget<UComboBoxString>();
    RoadPanelStyle::StyleTurnLaneCombo(ComparisonFocusComboBox);
    ComparisonFocusComboBox->OnGenerateWidgetEvent.BindUFunction(this, FName("MakeComboEntry"));
    for (const FString& Option : {TEXT("All Roads"), TEXT("Focused 25%"), TEXT("Focused 10%"), TEXT("Focused 5%")})
    {
        ComparisonFocusComboBox->AddOption(Option);
    }
    ComparisonFocusComboBox->SetSelectedOption(TEXT("All Roads"));
    ComparisonFocusComboBox->OnSelectionChanged.AddDynamic(this, &UTelemetryPanelWidget::HandleComparisonFocusChanged);
    if (UVerticalBoxSlot* FocusSlot = ComparisonPanel->AddChildToVerticalBox(ComparisonFocusComboBox))
    {
        FocusSlot->SetPadding(FMargin(0.0f, 7.0f, 0.0f, 0.0f));
    }

    UHorizontalBox* ComparisonMapActions = WidgetTree->ConstructWidget<UHorizontalBox>();
    if (UVerticalBoxSlot* MapActionSlot = ComparisonPanel->AddChildToVerticalBox(ComparisonMapActions))
    {
        MapActionSlot->SetPadding(FMargin(0.0f, 10.0f, 0.0f, 0.0f));
    }
    GenerateComparisonHeatmapsButton = MakeActionButton(TEXT("Generate Maps"), true);
    GenerateComparisonHeatmapsButton->OnClicked.AddDynamic(this, &UTelemetryPanelWidget::HandleGenerateComparisonHeatmapsClicked);
    if (UHorizontalBoxSlot* GenerateSlot = ComparisonMapActions->AddChildToHorizontalBox(GenerateComparisonHeatmapsButton))
    {
        GenerateSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        GenerateSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
    }
    ViewComparisonHeatmapsButton = MakeActionButton(TEXT("View Maps"), false);
    ViewComparisonHeatmapsButton->SetIsEnabled(false);
    ViewComparisonHeatmapsButton->OnClicked.AddDynamic(this, &UTelemetryPanelWidget::HandleViewComparisonHeatmapsClicked);
    if (UHorizontalBoxSlot* ViewMapsSlot = ComparisonMapActions->AddChildToHorizontalBox(ViewComparisonHeatmapsButton))
    {
        ViewMapsSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    }

    // Results grow down the page, and the full Compare tab handles scrolling.
    ComparisonResultsBox = WidgetTree->ConstructWidget<UVerticalBox>();
    ComparisonResultsBox->AddChildToVerticalBox(
        MakeText(TEXT("Select a baseline and another run to see the differences."), 12, TextSecondary)
    );
    if (UVerticalBoxSlot* ResultsSlot = ComparisonPanel->AddChildToVerticalBox(ComparisonResultsBox))
    {
        ResultsSlot->SetPadding(FMargin(0.0f, 18.0f, 0.0f, 12.0f));
        ResultsSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
    }

    SetWorkspaceTab(0);

    // Keep one status line below the two main cards for success and error messages.
    StatusText = MakeText(TEXT("Select a saved run to begin."), 12, TextSecondary);
    StatusText->SetAutoWrapText(true);
    if (UVerticalBoxSlot* StatusSlot = PanelColumn->AddChildToVerticalBox(StatusText))
    {
        StatusSlot->SetPadding(FMargin(2.0f, 18.0f, 0.0f, 0.0f));
    }

    // Build the full-screen viewer once, but keep it hidden until a map opens.
    HeatmapViewer = WidgetTree->ConstructWidget<UOverlay>();
    HeatmapViewer->SetVisibility(ESlateVisibility::Collapsed);
    if (UCanvasPanelSlot* ViewerSlot = Canvas->AddChildToCanvas(HeatmapViewer))
    {
        ViewerSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
        ViewerSlot->SetOffsets(FMargin(0.0f));
        ViewerSlot->SetZOrder(100);
    }

    UBorder* ViewerShade = WidgetTree->ConstructWidget<UBorder>();
    ViewerShade->SetBrush(FSlateColorBrush(Hex(TEXT("0B0F16"))));
    HeatmapViewer->AddChildToOverlay(ViewerShade);

    UVerticalBox* ViewerColumn = WidgetTree->ConstructWidget<UVerticalBox>();
    if (UOverlaySlot* ViewerColumnSlot = HeatmapViewer->AddChildToOverlay(ViewerColumn))
    {
        ViewerColumnSlot->SetPadding(FMargin(48.0f, 30.0f));
        ViewerColumnSlot->SetHorizontalAlignment(HAlign_Fill);
        ViewerColumnSlot->SetVerticalAlignment(VAlign_Fill);
    }

    // Use two header rows so long map titles do not overlap the controls.
    UBorder* ViewerHeaderFrame = WidgetTree->ConstructWidget<UBorder>();
    ViewerHeaderFrame->SetBrush(RoundedBrush(Hex(TEXT("101620")), 8.0f, Outline, 1.0f));
    ViewerHeaderFrame->SetPadding(FMargin(18.0f, 12.0f));
    if (UVerticalBoxSlot* HeaderFrameSlot = ViewerColumn->AddChildToVerticalBox(ViewerHeaderFrame))
    {
        HeaderFrameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
    }

    UVerticalBox* ViewerHeaderColumn = WidgetTree->ConstructWidget<UVerticalBox>();
    ViewerHeaderFrame->SetContent(ViewerHeaderColumn);

    UHorizontalBox* ViewerTitleRow = WidgetTree->ConstructWidget<UHorizontalBox>();
    ViewerHeaderColumn->AddChildToVerticalBox(ViewerTitleRow);
    HeatmapTitleText = MakeText(TEXT("Telemetry Heatmap"), 22, TextPrimary, FName("Bold"));
    if (UHorizontalBoxSlot* TitleSlot = ViewerTitleRow->AddChildToHorizontalBox(HeatmapTitleText))
    {
        TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        TitleSlot->SetVerticalAlignment(VAlign_Center);
    }

    UButton* CloseViewerButton = MakeActionButton(TEXT("Close Viewer"), false);
    CloseViewerButton->OnClicked.AddDynamic(this, &UTelemetryPanelWidget::HandleCloseHeatmapClicked);
    ViewerTitleRow->AddChildToHorizontalBox(CloseViewerButton);

    UHorizontalBox* ViewerControlsRow = WidgetTree->ConstructWidget<UHorizontalBox>();
    if (UVerticalBoxSlot* ControlsRowSlot = ViewerHeaderColumn->AddChildToVerticalBox(ViewerControlsRow))
    {
        ControlsRowSlot->SetPadding(FMargin(0.0f, 10.0f, 0.0f, 0.0f));
    }

    // These three buttons only appear while viewing run comparison maps.
    ComparisonHeatmapModeBar = WidgetTree->ConstructWidget<UHorizontalBox>();
    ComparisonHeatmapModeBar->SetVisibility(ESlateVisibility::Collapsed);
    if (UHorizontalBoxSlot* ModeBarSlot = ViewerControlsRow->AddChildToHorizontalBox(ComparisonHeatmapModeBar))
    {
        ModeBarSlot->SetPadding(FMargin(12.0f, 0.0f, 14.0f, 0.0f));
        ModeBarSlot->SetVerticalAlignment(VAlign_Center);
    }
    BaselineHeatmapModeButton = MakeActionButton(TEXT("Baseline"), false);
    BaselineHeatmapModeButton->OnClicked.AddDynamic(this, &UTelemetryPanelWidget::HandleBaselineHeatmapModeClicked);
    ComparisonHeatmapModeBar->AddChildToHorizontalBox(BaselineHeatmapModeButton);
    ComparisonHeatmapModeButton = MakeActionButton(TEXT("Comparison"), false);
    ComparisonHeatmapModeButton->OnClicked.AddDynamic(this, &UTelemetryPanelWidget::HandleComparisonHeatmapModeClicked);
    ComparisonHeatmapModeBar->AddChildToHorizontalBox(ComparisonHeatmapModeButton);
    ChangeHeatmapModeButton = MakeActionButton(TEXT("Change"), true);
    ChangeHeatmapModeButton->OnClicked.AddDynamic(this, &UTelemetryPanelWidget::HandleChangeHeatmapModeClicked);
    ComparisonHeatmapModeBar->AddChildToHorizontalBox(ChangeHeatmapModeButton);

    HeatmapInteractionHint = MakeText(
        TEXT("SCROLL TO ZOOM  |  DRAG TO PAN"),
        9,
        TextSecondary,
        FName("Medium"),
        80
    );
    if (UHorizontalBoxSlot* HintSlot = ViewerControlsRow->AddChildToHorizontalBox(HeatmapInteractionHint))
    {
        HintSlot->SetPadding(FMargin(0.0f, 0.0f, 14.0f, 0.0f));
        HintSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        HintSlot->SetVerticalAlignment(VAlign_Center);
    }

    UButton* ZoomOutButton = MakeActionButton(TEXT("-"), false);
    ZoomOutButton->OnClicked.AddDynamic(this, &UTelemetryPanelWidget::HandleZoomOutClicked);
    ViewerControlsRow->AddChildToHorizontalBox(ZoomOutButton);

    HeatmapZoomText = MakeText(TEXT("100%"), 11, TextPrimary, FName("Bold"));
    HeatmapZoomText->SetJustification(ETextJustify::Center);
    USizeBox* ZoomTextSizer = WidgetTree->ConstructWidget<USizeBox>();
    ZoomTextSizer->SetWidthOverride(58.0f);
    ZoomTextSizer->SetContent(HeatmapZoomText);
    if (UHorizontalBoxSlot* ZoomTextSlot = ViewerControlsRow->AddChildToHorizontalBox(ZoomTextSizer))
    {
        ZoomTextSlot->SetVerticalAlignment(VAlign_Center);
    }

    UButton* ZoomInButton = MakeActionButton(TEXT("+"), false);
    ZoomInButton->OnClicked.AddDynamic(this, &UTelemetryPanelWidget::HandleZoomInClicked);
    if (UHorizontalBoxSlot* ZoomInSlot = ViewerControlsRow->AddChildToHorizontalBox(ZoomInButton))
    {
        ZoomInSlot->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 0.0f));
    }

    UButton* ResetViewButton = MakeActionButton(TEXT("Reset View"), false);
    ResetViewButton->OnClicked.AddDynamic(this, &UTelemetryPanelWidget::HandleResetHeatmapViewClicked);
    if (UHorizontalBoxSlot* ResetSlot = ViewerControlsRow->AddChildToHorizontalBox(ResetViewButton))
    {
        ResetSlot->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 0.0f));
    }

    // Keep the summary, map, and legend inside one shared frame.
    UBorder* UnifiedMapFrame = WidgetTree->ConstructWidget<UBorder>();
    UnifiedMapFrame->SetBrush(RoundedBrush(Hex(TEXT("070B10")), 10.0f, Outline, 1.0f));
    UnifiedMapFrame->SetPadding(FMargin(14.0f));
    if (UVerticalBoxSlot* UnifiedFrameSlot = ViewerColumn->AddChildToVerticalBox(UnifiedMapFrame))
    {
        UnifiedFrameSlot->SetPadding(FMargin(0.0f, 14.0f, 0.0f, 0.0f));
        UnifiedFrameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    }

    UHorizontalBox* ViewerContent = WidgetTree->ConstructWidget<UHorizontalBox>();
    UnifiedMapFrame->SetContent(ViewerContent);

    // The summary card shows a few important values without covering the map.
    UBorder* SummaryCard = WidgetTree->ConstructWidget<UBorder>();
    SummaryCard->SetBrush(RoundedBrush(Hex(TEXT("0B1220")), 8.0f, Outline, 1.0f));
    SummaryCard->SetPadding(FMargin(18.0f));
    HeatmapSummaryCard = SummaryCard;
    if (UHorizontalBoxSlot* SummarySlot = ViewerContent->AddChildToHorizontalBox(SummaryCard))
    {
        SummarySlot->SetPadding(FMargin(0.0f, 0.0f, 14.0f, 0.0f));
        SummarySlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
        SummarySlot->SetVerticalAlignment(VAlign_Top);
    }

    USizeBox* SummarySizer = WidgetTree->ConstructWidget<USizeBox>();
    SummarySizer->SetWidthOverride(260.0f);
    SummaryCard->SetContent(SummarySizer);
    HeatmapSummaryBox = WidgetTree->ConstructWidget<UVerticalBox>();
    SummarySizer->SetContent(HeatmapSummaryBox);

    // Clip the map here so zooming and panning stay inside the viewer.
    UBorder* ImageFrame = WidgetTree->ConstructWidget<UBorder>();
    ImageFrame->SetBrush(RoundedBrush(Hex(TEXT("05080D")), 8.0f));
    ImageFrame->SetPadding(FMargin(10.0f));
    ImageFrame->SetClipping(EWidgetClipping::ClipToBounds);
    HeatmapViewport = ImageFrame;

    // Limit the displayed map so it stays inside the viewer area.
    USizeBox* ImageSizer = WidgetTree->ConstructWidget<USizeBox>();
    ImageSizer->SetMaxDesiredWidth(1600.0f);
    ImageSizer->SetMaxDesiredHeight(850.0f);
    ImageSizer->SetContent(ImageFrame);
    if (UHorizontalBoxSlot* ImageFrameSlot = ViewerContent->AddChildToHorizontalBox(ImageSizer))
    {
        ImageFrameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        ImageFrameSlot->SetHorizontalAlignment(HAlign_Center);
        ImageFrameSlot->SetVerticalAlignment(VAlign_Center);
    }

    UOverlay* HeatmapCanvas = WidgetTree->ConstructWidget<UOverlay>();
    HeatmapCanvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    ImageFrame->SetContent(HeatmapCanvas);

    UScaleBox* HeatmapScaleBox = WidgetTree->ConstructWidget<UScaleBox>();
    HeatmapScaleBox->SetStretch(EStretch::ScaleToFit);
    HeatmapScaleBox->SetStretchDirection(EStretchDirection::Both);
    if (UOverlaySlot* ScaleSlot = HeatmapCanvas->AddChildToOverlay(HeatmapScaleBox))
    {
        ScaleSlot->SetHorizontalAlignment(HAlign_Fill);
        ScaleSlot->SetVerticalAlignment(VAlign_Fill);
    }

    HeatmapImage = WidgetTree->ConstructWidget<UImage>();
    HeatmapScaleBox->SetContent(HeatmapImage);

    // Unreal displays the SVG as one image, so the road marker is drawn above it.
    HeatmapMarkerLayer = WidgetTree->ConstructWidget<UCanvasPanel>();
    HeatmapMarkerLayer->SetVisibility(ESlateVisibility::HitTestInvisible);
    if (UOverlaySlot* MarkerLayerSlot = HeatmapCanvas->AddChildToOverlay(HeatmapMarkerLayer))
    {
        MarkerLayerSlot->SetHorizontalAlignment(HAlign_Fill);
        MarkerLayerSlot->SetVerticalAlignment(VAlign_Fill);
    }

    USizeBox* MarkerSizer = WidgetTree->ConstructWidget<USizeBox>();
    MarkerSizer->SetWidthOverride(18.0f);
    MarkerSizer->SetHeightOverride(18.0f);
    UBorder* Marker = WidgetTree->ConstructWidget<UBorder>();
    Marker->SetBrush(RoundedBrush(AccentSoft, 9.0f, Accent, 2.0f));
    MarkerSizer->SetContent(Marker);
    MarkerSizer->SetVisibility(ESlateVisibility::Collapsed);
    HeatmapRoadMarker = MarkerSizer;
    if (UCanvasPanelSlot* MarkerSlot = HeatmapMarkerLayer->AddChildToCanvas(MarkerSizer))
    {
        MarkerSlot->SetAutoSize(true);
        MarkerSlot->SetZOrder(5);
    }

    // This small arrow points to the summary road without covering the road itself.
    USizeBox* ExtremeMarkerSizer = WidgetTree->ConstructWidget<USizeBox>();
    ExtremeMarkerSizer->SetWidthOverride(18.0f);
    ExtremeMarkerSizer->SetHeightOverride(20.0f);
    // Cyan stays easy to see over the red, yellow, and green heatmap colors.
    const FLinearColor ExtremeMarkerColor = Hex(TEXT("32D5FF"));
    UTextBlock* ExtremeMarkerArrow = MakeText(TEXT("\u25BC"), 15, ExtremeMarkerColor, FName("Bold"));
    ExtremeMarkerArrow->SetJustification(ETextJustify::Center);
    ExtremeMarkerArrow->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.9f));
    ExtremeMarkerArrow->SetShadowOffset(FVector2D(1.0f, 1.0f));
    ExtremeMarkerSizer->SetContent(ExtremeMarkerArrow);
    ExtremeMarkerSizer->SetVisibility(ESlateVisibility::Collapsed);
    HeatmapExtremeMarker = ExtremeMarkerSizer;
    if (UCanvasPanelSlot* ExtremeSlot = HeatmapMarkerLayer->AddChildToCanvas(ExtremeMarkerSizer))
    {
        ExtremeSlot->SetAutoSize(true);
        ExtremeSlot->SetZOrder(4);
    }

    // This card appears when the user hovers over or pins a road.
    HeatmapRoadCard = WidgetTree->ConstructWidget<UBorder>();
    HeatmapRoadCard->SetBrush(RoundedBrush(Hex(TEXT("0B1220"), 0.97f), 8.0f, Accent, 1.0f));
    HeatmapRoadCard->SetPadding(FMargin(14.0f, 11.0f));
    HeatmapRoadCard->SetVisibility(ESlateVisibility::Collapsed);
    UVerticalBox* RoadCardContent = WidgetTree->ConstructWidget<UVerticalBox>();
    HeatmapRoadCard->SetContent(RoadCardContent);
    HeatmapRoadNameText = MakeText(TEXT("Road"), 13, TextPrimary, FName("Bold"));
    RoadCardContent->AddChildToVerticalBox(HeatmapRoadNameText);
    HeatmapRoadDetailsText = MakeText(TEXT("Metric"), 11, TextSecondary, FName("Medium"));
    if (UVerticalBoxSlot* DetailsSlot = RoadCardContent->AddChildToVerticalBox(HeatmapRoadDetailsText))
    {
        DetailsSlot->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 0.0f));
    }
    HeatmapRoadPinText = MakeText(TEXT("Click to pin"), 9, Accent, FName("Medium"), 50);
    if (UVerticalBoxSlot* PinSlot = RoadCardContent->AddChildToVerticalBox(HeatmapRoadPinText))
    {
        PinSlot->SetPadding(FMargin(0.0f, 7.0f, 0.0f, 0.0f));
    }
    if (UOverlaySlot* RoadCardSlot = HeatmapCanvas->AddChildToOverlay(HeatmapRoadCard))
    {
        RoadCardSlot->SetPadding(FMargin(14.0f));
        RoadCardSlot->SetHorizontalAlignment(HAlign_Left);
        RoadCardSlot->SetVerticalAlignment(VAlign_Bottom);
    }

    // The legend explains what the road colors mean for the current metric.
    UBorder* LegendCard = WidgetTree->ConstructWidget<UBorder>();
    LegendCard->SetBrush(RoundedBrush(Hex(TEXT("0B1220")), 8.0f, Outline, 1.0f));
    LegendCard->SetPadding(FMargin(14.0f));
    HeatmapLegendCard = LegendCard;
    if (UHorizontalBoxSlot* LegendSlot = ViewerContent->AddChildToHorizontalBox(LegendCard))
    {
        LegendSlot->SetPadding(FMargin(14.0f, 0.0f, 0.0f, 0.0f));
        LegendSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
        LegendSlot->SetVerticalAlignment(VAlign_Center);
    }

    UVerticalBox* LegendColumn = WidgetTree->ConstructWidget<UVerticalBox>();
    LegendCard->SetContent(LegendColumn);
    HeatmapLegendLabel = MakeText(TEXT("Metric"), 11, TextSecondary, FName("Medium"));
    HeatmapLegendLabel->SetAutoWrapText(true);
    LegendColumn->AddChildToVerticalBox(HeatmapLegendLabel);

    UHorizontalBox* LegendScale = WidgetTree->ConstructWidget<UHorizontalBox>();
    if (UVerticalBoxSlot* ScaleSlot = LegendColumn->AddChildToVerticalBox(LegendScale))
    {
        ScaleSlot->SetPadding(FMargin(0.0f, 12.0f, 0.0f, 0.0f));
    }

    USizeBox* LegendBarSizer = WidgetTree->ConstructWidget<USizeBox>();
    LegendBarSizer->SetWidthOverride(34.0f);
    LegendBarSizer->SetHeightOverride(420.0f);
    LegendScale->AddChildToHorizontalBox(LegendBarSizer);
    HeatmapLegendGradient = WidgetTree->ConstructWidget<UImage>();
    LegendBarSizer->SetContent(HeatmapLegendGradient);

    USizeBox* LegendTickSizer = WidgetTree->ConstructWidget<USizeBox>();
    LegendTickSizer->SetHeightOverride(420.0f);
    LegendTickSizer->SetWidthOverride(52.0f);
    HeatmapLegendTicks = WidgetTree->ConstructWidget<UVerticalBox>();
    LegendTickSizer->SetContent(HeatmapLegendTicks);
    if (UHorizontalBoxSlot* TickSlot = LegendScale->AddChildToHorizontalBox(LegendTickSizer))
    {
        TickSlot->SetPadding(FMargin(9.0f, 0.0f, 0.0f, 0.0f));
    }
}

// Saved run list and details

// Reloads runs for the current map and rebuilds the list on the left.
void UTelemetryPanelWidget::RefreshRunList()
{
    using namespace TelemetryPalette;
    ResetDeleteConfirmation();
    if (DeleteRunButton)
    {
        DeleteRunButton->SetIsEnabled(false);
    }
    ComparisonHeatmapPaths = FTelemetryHeatmapComparisonPaths();
    if (ViewComparisonHeatmapsButton)
    {
        ViewComparisonHeatmapsButton->SetIsEnabled(false);
    }
    if (!RunListBox)
    {
        return;
    }

    // Rescanning the run list is the one moment folders can have moved or
    // been renamed, so stale resolutions must not survive it.
    UTelemetryPanelBridge::InvalidateRunFolderCache();

    RunListBox->ClearChildren();
    RunRows.Empty();
    SavedRuns.Empty();
    SelectedRunIndex = INDEX_NONE;
    ComparisonRunIndex = INDEX_NONE;
    ComparisonRunIndices.Empty();
    if (ComparisonRunComboBox)
    {
        ComparisonRunComboBox->ClearOptions();
    }
    if (ComparisonBaselineText)
    {
        ComparisonBaselineText->SetText(FText::FromString(TEXT("Baseline: Select a saved run")));
    }
    if (CompareRunsButton)
    {
        CompareRunsButton->SetIsEnabled(false);
    }
    if (SwapComparisonRunsButton)
    {
        SwapComparisonRunsButton->SetIsEnabled(false);
    }
    RefreshHeatmapActionState();

    if (FDOTValidationText)
    {
        FDOTValidationText->SetText(FText::FromString(
            TEXT("Select a saved run, then start the comparison. Technical measures will be explained alongside the result.")
        ));
        FDOTValidationText->SetColorAndOpacity(FSlateColor(TextSecondary));
    }

    ActiveMapName = TEXT("Waterford (Default)");
    if (const UWorld* World = GetWorld())
    {
        if (const URoadmapGameInstance* GameInstance = World->GetGameInstance<URoadmapGameInstance>())
        {
            if (GameInstance->HasActiveRoadmap())
            {
                ActiveMapName = GameInstance->GetActiveRoadmapName();
            }
        }
    }

    FString ErrorMessage;
    if (!UTelemetryPanelBridge::GetSavedRuns(ActiveMapName, SavedRuns, ErrorMessage))
    {
        RunListBox->AddChildToVerticalBox(MakeText(ErrorMessage, 12, ErrorColor, FName("Medium")));
        SetStatus(ErrorMessage, true);
        return;
    }

    if (SavedRuns.IsEmpty())
    {
        UTextBlock* EmptyText = MakeText(
            FString::Printf(TEXT("No saved simulation runs were found for %s."), *ActiveMapName),
            13,
            TextSecondary
        );
        EmptyText->SetAutoWrapText(true);
        RunListBox->AddChildToVerticalBox(EmptyText);
        SetStatus(FString::Printf(TEXT("No saved telemetry runs are available for %s."), *ActiveMapName));
        return;
    }

    for (int32 RunIndex = 0; RunIndex < SavedRuns.Num(); ++RunIndex)
    {
        UTelemetryRunButton* Row = MakeRunRow(RunIndex, SavedRuns[RunIndex]);
        RunRows.Add(Row);

        if (UVerticalBoxSlot* RowSlot = RunListBox->AddChildToVerticalBox(Row))
        {
            RowSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 9.0f));
        }
    }

    RefreshComparisonOptions();

    RunDetailsText->SetText(FText::FromString(TEXT("Select a saved run to view its telemetry summary.")));
    SetStatus(FString::Printf(
        TEXT("Loaded %d saved run%s for %s."),
        SavedRuns.Num(),
        SavedRuns.Num() == 1 ? TEXT("") : TEXT("s"),
        *ActiveMapName
    ));
}

// Loads the selected run's values and shows them on the Overview tab.
void UTelemetryPanelWidget::RefreshSelectedRunDetails()
{
    using namespace TelemetryPalette;
    if (!SavedRuns.IsValidIndex(SelectedRunIndex))
    {
        return;
    }

    FTelemetryRunDetails Details;
    FString ErrorMessage;

    if (!UTelemetryPanelBridge::GetSavedRunDetails(
            SavedRuns[SelectedRunIndex].RunId,
            Details,
            ErrorMessage
        ))
    {
        RunDetailsText->SetText(FText::FromString(ErrorMessage));
        RunDetailsText->SetColorAndOpacity(FSlateColor(ErrorColor));
        SetStatus(ErrorMessage, true);
        return;
    }

    const FString BottleneckRoad = Details.WorstBottleneckRoad.IsEmpty()
        ? TEXT("Not available")
        : Details.WorstBottleneckRoad;
    const FString BottleneckSegment = Details.WorstBottleneckEdgeId >= 0
        ? FString::Printf(TEXT("%s - Edge %d"), *BottleneckRoad, Details.WorstBottleneckEdgeId)
        : BottleneckRoad;
    const FString BottleneckValue = Details.BottleneckIndexVersion >= 2
        ? FString::Printf(TEXT("%.1f / 100"), Details.WorstBottleneckScore)
        : FString::Printf(TEXT("%.2f (legacy score; not comparable with new runs)"), Details.WorstBottleneckScore);

    // These labels state exactly what the saved totals measure. In particular,
    // stopped time includes normal signal stops and is not official traffic delay.
    const FString DetailsString = FString::Printf(
        TEXT("Vehicles\n%d\n\nDuration\n%.1f seconds\n\nRoad directions used\n%d\n\nAverage recorded speed\n%.1f mph\n\nTotal stopped time across all vehicles\n%.1f seconds\n\nHighest stopped time for one vehicle\n%.1f seconds\n\nHighest bottleneck-index segment\n%s\nRoadMap index: %s"),
        Details.TotalVehicles,
        Details.SimulationDurationSeconds,
        Details.EdgesUsed,
        Details.AverageSpeedMph,
        Details.TotalWaitAddedSeconds,
        Details.MaximumWaitSeconds,
        *BottleneckSegment,
        *BottleneckValue
    );

    RunDetailsText->SetText(FText::FromString(DetailsString));
    RunDetailsText->SetColorAndOpacity(FSlateColor(TextPrimary));
}

// Highlights the selected run and leaves the other rows dark.
void UTelemetryPanelWidget::RefreshRunRowStyles()
{
    using namespace TelemetryPalette;
    for (int32 RowIndex = 0; RowIndex < RunRows.Num(); ++RowIndex)
    {
        if (RunRows[RowIndex])
        {
            RunRows[RowIndex]->SetStyle(RunRowStyle(RowIndex == SelectedRunIndex));
        }
    }
}

// Run comparison setup and result cards

// Fills the comparison list with other runs from the same map.
void UTelemetryPanelWidget::RefreshComparisonOptions(int32 PreferredRunIndex)
{
    ComparisonRunIndices.Empty();
    ComparisonRunIndex = INDEX_NONE;
    if (!ComparisonRunComboBox)
    {
        return;
    }

    ComparisonRunComboBox->ClearOptions();
    if (SavedRuns.IsValidIndex(SelectedRunIndex))
    {
        ComparisonBaselineText->SetText(FText::FromString(FString::Printf(
            TEXT("Baseline: %s"), *SavedRuns[SelectedRunIndex].CreatedAt
        )));
    }
    else
    {
        ComparisonBaselineText->SetText(FText::FromString(TEXT("Baseline: Select a saved run")));
    }

    for (int32 RunIndex = 0; RunIndex < SavedRuns.Num(); ++RunIndex)
    {
        if (RunIndex == SelectedRunIndex)
        {
            continue;
        }
        ComparisonRunIndices.Add(RunIndex);
        ComparisonRunComboBox->AddOption(SavedRuns[RunIndex].CreatedAt);
    }

    int32 OptionIndex = ComparisonRunIndices.IndexOfByKey(PreferredRunIndex);
    if (OptionIndex == INDEX_NONE)
    {
        OptionIndex = 0;
    }
    if (ComparisonRunIndices.IsValidIndex(OptionIndex))
    {
        ComparisonRunIndex = ComparisonRunIndices[OptionIndex];
        ComparisonRunComboBox->SetSelectedOption(SavedRuns[ComparisonRunIndex].CreatedAt);
    }

    const bool bCanCompare = SavedRuns.IsValidIndex(SelectedRunIndex) &&
        SavedRuns.IsValidIndex(ComparisonRunIndex) && !bTelemetryTaskRunning;
    CompareRunsButton->SetIsEnabled(bCanCompare);
    SwapComparisonRunsButton->SetIsEnabled(bCanCompare);
    if (GenerateComparisonHeatmapsButton)
    {
        GenerateComparisonHeatmapsButton->SetIsEnabled(bCanCompare);
    }
    ComparisonHeatmapPaths = FTelemetryHeatmapComparisonPaths();
    if (ViewComparisonHeatmapsButton)
    {
        ViewComparisonHeatmapsButton->SetIsEnabled(false);
    }
}

// Turns the comparison result into simple cards and road-change rows.
void UTelemetryPanelWidget::RenderComparisonResult(const FTelemetryRunComparisonResult& Result)
{
    using namespace TelemetryPalette;

    if (!ComparisonResultsBox)
    {
        return;
    }
    ComparisonResultsBox->ClearChildren();

    UTextBlock* Heading = MakeText(
        FString::Printf(
            TEXT("BASELINE\n%s\n\nCOMPARISON\n%s"),
            *Result.BaselineCreatedAt.Replace(TEXT("_"), TEXT(" ")),
            *Result.ComparisonCreatedAt.Replace(TEXT("_"), TEXT(" "))
        ),
        12,
        TextPrimary,
        FName("Medium")
    );
    Heading->SetAutoWrapText(true);
    ComparisonResultsBox->AddChildToVerticalBox(Heading);
    ComparisonResultsBox->AddChildToVerticalBox(MakeText(
        FString::Printf(
            TEXT("\n%d shared road directions (%.1f%% coverage)"),
            Result.SharedRoads,
            Result.SharedCoveragePercent
        ),
        11,
        TextSecondary
    ));

    if (!Result.Warnings.IsEmpty())
    {
        ComparisonResultsBox->AddChildToVerticalBox(
            MakeText(TEXT("\nPRELIMINARY RESULT"), 11, Accent, FName("Bold"))
        );
        for (const FString& Warning : Result.Warnings)
        {
            UTextBlock* WarningText = MakeText(FString::Printf(TEXT("- %s"), *Warning), 11, TextSecondary);
            WarningText->SetAutoWrapText(true);
            ComparisonResultsBox->AddChildToVerticalBox(WarningText);
        }
    }

    ComparisonResultsBox->AddChildToVerticalBox(
        MakeText(TEXT("\nOVERALL CHANGES"), 11, TextSecondary, FName("Medium"), 150)
    );
    for (const FTelemetryRunComparisonMetric& Metric : Result.Metrics)
    {
        const FLinearColor StatusColor = Metric.Status == TEXT("improved")
            ? Success
            : Metric.Status == TEXT("worsened") ? ErrorColor
            : Metric.Status == TEXT("little_change") ? Accent : TextSecondary;
        const FString StatusLabel = Metric.Status.Replace(TEXT("_"), TEXT(" ")).ToUpper();
        const FString PercentText = Metric.bHasPercentChange
            ? FString::Printf(TEXT("  |  %+.1f%%"), Metric.PercentChange)
            : TEXT("");
        UTextBlock* MetricText = MakeText(
            FString::Printf(
                TEXT("\n%s  -  %s\n%.2f -> %.2f %s  |  change %+.2f%s\n%s"),
                *Metric.Label,
                *StatusLabel,
                Metric.BaselineValue,
                Metric.ComparisonValue,
                *Metric.Unit,
                Metric.Delta,
                *PercentText,
                *Metric.Explanation
            ),
            11,
            StatusColor,
            FName("Medium")
        );
        MetricText->SetAutoWrapText(true);
        ComparisonResultsBox->AddChildToVerticalBox(MetricText);
    }

    ComparisonResultsBox->AddChildToVerticalBox(
        MakeText(TEXT("\nLARGEST ROAD CHANGES"), 11, TextSecondary, FName("Medium"), 150)
    );
    if (Result.TopRoadChanges.IsEmpty())
    {
        ComparisonResultsBox->AddChildToVerticalBox(
            MakeText(TEXT("No shared road-level metrics were available."), 11, TextSecondary)
        );
    }
    for (const FTelemetryRunRoadChange& Road : Result.TopRoadChanges)
    {
        const FLinearColor StatusColor = Road.Status == TEXT("improved")
            ? Success : Road.Status == TEXT("worsened") ? ErrorColor : Accent;
        UTextBlock* RoadText = MakeText(
            FString::Printf(
                TEXT("\n%s  -  %s\nRecorded speed: %.1f -> %.1f mph (%+.1f)\nStopped time per entry: %.1f -> %.1f s (%+.1f)\nBottleneck-index change: %+.2f"),
                *Road.RoadName,
                *Road.Status.Replace(TEXT("_"), TEXT(" ")).ToUpper(),
                Road.BaselineSpeedMph,
                Road.ComparisonSpeedMph,
                Road.SpeedDeltaMph,
                Road.BaselineWaitSeconds,
                Road.ComparisonWaitSeconds,
                Road.WaitDeltaSeconds,
                Road.BottleneckDelta
            ),
            11,
            StatusColor
        );
        RoadText->SetAutoWrapText(true);
        ComparisonResultsBox->AddChildToVerticalBox(RoadText);
    }
}

// Text shown to the user is kept separate from the short IDs sent to Python.

// Changes a friendly metric name into the short name used by Python.
FString UTelemetryPanelWidget::GetMetricId(const FString& DisplayName) const
{
    if (DisplayName == TEXT("RoadMap Bottleneck Index"))
    {
        return TEXT("bottleneck_score");
    }

    if (DisplayName == TEXT("Estimated Hourly Traffic Flow"))
    {
        return TEXT("estimated_flow_veh_per_hr");
    }

    if (DisplayName == TEXT("Average Recorded Speed"))
    {
        return TEXT("avg_speed_mph");
    }

    if (DisplayName == TEXT("Average Stopped Time per Vehicle Entry"))
    {
        return TEXT("avg_wait_per_vehicle_s");
    }

    return TEXT("");
}

// Changes a Python metric name into text the user can understand.
FString UTelemetryPanelWidget::GetMetricDisplayName(const FString& MetricId) const
{
    if (MetricId == TEXT("comparison_delta"))
    {
        return TEXT("Run-to-Run Change");
    }
    if (MetricId == TEXT("fdot_geh_score"))
    {
        return TEXT("FDOT Traffic Comparison");
    }

    if (MetricId == TEXT("estimated_flow_veh_per_hr"))
    {
        return TEXT("Estimated Hourly Traffic Flow");
    }

    if (MetricId == TEXT("avg_speed_mph"))
    {
        return TEXT("Average Recorded Speed");
    }

    if (MetricId == TEXT("avg_wait_per_vehicle_s"))
    {
        return TEXT("Average Stopped Time per Vehicle Entry");
    }

    return TEXT("RoadMap Bottleneck Index");
}

// Explains each measurement without assuming prior traffic-engineering knowledge.
// Gives a short explanation for the selected heatmap metric.
FString UTelemetryPanelWidget::GetMetricHelpText(const FString& MetricId) const
{
    if (MetricId == TEXT("estimated_flow_veh_per_hr"))
    {
        return TEXT("This counts entries into each road direction and converts them to vehicles per hour. Green means lower flow and red means higher flow. Red means busiest, not automatically congested. This color range is fitted to the run, and short runs can give unstable hourly estimates.");
    }
    if (MetricId == TEXT("avg_speed_mph"))
    {
        return TEXT("This averages recorded vehicle speeds, including vehicles stopped at signals or in queues. Red means lower speed and green means higher speed. Low speed may come from congestion, a signal, a turn, a short intersection segment, or a low speed limit.");
    }
    if (MetricId == TEXT("avg_wait_per_vehicle_s"))
    {
        return TEXT("This averages the time vehicles were moving below about 1.1 mph each time they entered a road direction. Red means more stopped time per entry, not automatically a failed road. Signal stops can be normal. The color range is fitted to this run.");
    }
    if (MetricId == TEXT("fdot_geh_score"))
    {
        return TEXT("This compares RoadMap hourly flow with FDOT-based hourly reference estimates. Green is close, yellow needs review, and red is a large difference. The reference assumes a 50/50 directional split. Runs of 15 minutes or more give a steadier result.");
    }
    if (MetricId == TEXT("comparison_delta"))
    {
        if (SelectedComparisonMetric == TEXT("estimated_flow_veh_per_hr"))
        {
            return TEXT("Blue roads had higher hourly flow in the second run and orange roads had lower flow. This shows a change in activity, not whether the change is good or bad.");
        }
        if (SelectedComparisonMetric == TEXT("avg_speed_mph"))
        {
            return TEXT("Blue roads had higher recorded speed in the second run and orange roads had lower speed. Higher speed is not automatically better when demand, speed limits, or road settings changed.");
        }
        return TEXT("Green roads improved in the second run, red roads became worse, and light roads changed very little.");
    }
    return TEXT("This 0-100 RoadMap screening index combines average stopped time per vehicle entry with slow and stopped movement. Green is lower concern and red marks stronger bottleneck candidates. It is a project index, not an official engineering grade.");
}

// Shows a normal or error message at the bottom of the panel.
void UTelemetryPanelWidget::SetStatus(const FString& Message, bool bIsError)
{
    using namespace TelemetryPalette;
    if (!StatusText)
    {
        return;
    }

    StatusText->SetText(FText::FromString(Message));
    StatusText->SetColorAndOpacity(FSlateColor(bIsError ? ErrorColor : Success));
}

// Changes the road-group choice into the short name used by Python.
FString UTelemetryPanelWidget::GetFocusId(const FString& DisplayName) const
{
    if (DisplayName == TEXT("Focused 25%")) return TEXT("worst_25");
    if (DisplayName == TEXT("Focused 10%")) return TEXT("worst_10");
    if (DisplayName == TEXT("Focused 5%")) return TEXT("worst_5");
    return TEXT("all");
}

// Changes a road-group ID into the text shown in the panel.
FString UTelemetryPanelWidget::GetFocusDisplayName(const FString& FocusId) const
{
    if (FocusId == TEXT("worst_25")) return TEXT("Focused 25%");
    if (FocusId == TEXT("worst_10")) return TEXT("Focused 10%");
    if (FocusId == TEXT("worst_5")) return TEXT("Focused 5%");
    return TEXT("All Roads");
}

// Builds a small image so the legend changes color smoothly.
// Heatmap viewer drawing and mouse tools

// Builds the smooth color strip shown beside the heatmap.
void UTelemetryPanelWidget::UpdateHeatmapLegendGradient(const TArray<FString>& ColorsTopToBottom)
{
    using namespace TelemetryPalette;
    if (!HeatmapLegendGradient || ColorsTopToBottom.Num() < 2)
    {
        return;
    }

    constexpr int32 TextureWidth = 1;
    constexpr int32 TextureHeight = 256;
    LoadedLegendTexture = UTexture2D::CreateTransient(TextureWidth, TextureHeight, PF_B8G8R8A8);
    if (!LoadedLegendTexture || !LoadedLegendTexture->GetPlatformData())
    {
        return;
    }

    FTexture2DMipMap& Mip = LoadedLegendTexture->GetPlatformData()->Mips[0];
    FColor* Pixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
    for (int32 Y = 0; Y < TextureHeight; ++Y)
    {
        const float PalettePosition =
            (static_cast<float>(Y) / static_cast<float>(TextureHeight - 1)) *
            static_cast<float>(ColorsTopToBottom.Num() - 1);
        const int32 FirstIndex = FMath::Min(FMath::FloorToInt(PalettePosition), ColorsTopToBottom.Num() - 2);
        const float Blend = PalettePosition - static_cast<float>(FirstIndex);

        const FLinearColor First = FLinearColor::FromSRGBColor(FColor::FromHex(ColorsTopToBottom[FirstIndex]));
        const FLinearColor Second = FLinearColor::FromSRGBColor(FColor::FromHex(ColorsTopToBottom[FirstIndex + 1]));
        Pixels[Y] = FMath::Lerp(First, Second, Blend).ToFColorSRGB();
    }
    Mip.BulkData.Unlock();

    LoadedLegendTexture->NeverStream = true;
    LoadedLegendTexture->Filter = TF_Bilinear;
    LoadedLegendTexture->SRGB = true;
    LoadedLegendTexture->UpdateResource();
    HeatmapLegendGradient->SetBrushFromTexture(LoadedLegendTexture, true);
}

// Applies the current zoom and pan values to the map image.
void UTelemetryPanelWidget::ApplyHeatmapViewTransform()
{
    if (HeatmapImage)
    {
        HeatmapImage->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
        HeatmapImage->SetRenderScale(FVector2D(HeatmapZoom, HeatmapZoom));
        HeatmapImage->SetRenderTranslation(HeatmapPan);
    }

    if (HeatmapZoomText)
    {
        HeatmapZoomText->SetText(FText::FromString(FString::Printf(
            TEXT("%d%%"),
            FMath::RoundToInt(HeatmapZoom * 100.0f)
        )));
    }

    UpdateHeatmapRoadMarker();
    UpdateHeatmapExtremeMarker();

    // The cached image position can be one frame behind its render transform.
    // Check it again briefly so both markers finish on the correct road.
    HeatmapMarkerLayoutFramesRemaining = 3;
}

// Changes map zoom and keeps the point under the mouse in the same place.
void UTelemetryPanelWidget::SetHeatmapZoom(
    float NewZoom,
    const FVector2D* CursorScreenPosition
)
{
    using namespace TelemetryPalette;

    const float ClampedZoom = FMath::Clamp(
        NewZoom,
        MinimumHeatmapZoom,
        MaximumHeatmapZoom
    );
    if (FMath::IsNearlyEqual(ClampedZoom, HeatmapZoom))
    {
        return;
    }

    if (CursorScreenPosition && HeatmapViewport)
    {
        const FGeometry ViewportGeometry = HeatmapViewport->GetCachedGeometry();
        const FVector2D CursorLocal = ViewportGeometry.AbsoluteToLocal(*CursorScreenPosition);
        const FVector2D CursorFromCenter = CursorLocal - ViewportGeometry.GetLocalSize() * 0.5f;
        const float ZoomRatio = ClampedZoom / HeatmapZoom;

        // Adjust the translation so the point below the cursor stays in place.
        HeatmapPan = CursorFromCenter - (CursorFromCenter - HeatmapPan) * ZoomRatio;
    }

    HeatmapZoom = ClampedZoom;
    HeatmapPan = ClampHeatmapPan(HeatmapPan);
    ApplyHeatmapViewTransform();
}

// Returns the map to its starting size and center position.
void UTelemetryPanelWidget::ResetHeatmapView()
{
    using namespace TelemetryPalette;

    bIsHeatmapPanning = false;
    bHeatmapPointerPressed = false;
    bHeatmapDragMoved = false;
    HeatmapZoom = MinimumHeatmapZoom;
    HeatmapPan = FVector2D::ZeroVector;
    ClearHeatmapRoadInteraction();
    ApplyHeatmapViewTransform();
}

// Limits map movement so the user cannot drag it completely off screen.
FVector2D UTelemetryPanelWidget::ClampHeatmapPan(const FVector2D& RequestedPan) const
{
    using namespace TelemetryPalette;

    if (!HeatmapViewport || HeatmapZoom <= MinimumHeatmapZoom)
    {
        return FVector2D::ZeroVector;
    }

    const FVector2D ViewportSize = HeatmapViewport->GetCachedGeometry().GetLocalSize();
    const FVector2D MaximumPan = ViewportSize * 0.5f * (HeatmapZoom - MinimumHeatmapZoom);
    return FVector2D(
        FMath::Clamp(RequestedPan.X, -MaximumPan.X, MaximumPan.X),
        FMath::Clamp(RequestedPan.Y, -MaximumPan.Y, MaximumPan.Y)
    );
}

// Checks whether a mouse position is inside the part of the viewer used by the map.
bool UTelemetryPanelWidget::IsPointerOverHeatmap(const FVector2D& ScreenPosition) const
{
    return HeatmapViewer &&
        HeatmapViewer->IsVisible() &&
        HeatmapViewport &&
        HeatmapViewport->GetCachedGeometry().IsUnderLocation(ScreenPosition);
}

// Finds the closest road line to the mouse so it can be highlighted.
bool UTelemetryPanelWidget::FindNearestHeatmapRoad(
    const FVector2D& ScreenPosition,
    int32& OutRoadIndex,
    FVector2D& OutClosestNormalizedPoint
) const
{
    OutRoadIndex = INDEX_NONE;
    OutClosestNormalizedPoint = FVector2D::ZeroVector;
    if (!HeatmapImage || CurrentHeatmapDisplayInfo.Roads.IsEmpty())
    {
        return false;
    }

    const FGeometry ImageGeometry = HeatmapImage->GetCachedGeometry();
    const FVector2D ImageSize = ImageGeometry.GetLocalSize();
    if (ImageSize.X <= 0.0f || ImageSize.Y <= 0.0f)
    {
        return false;
    }

    const FVector4 Rect = CurrentHeatmapDisplayInfo.MapRect;
    float BestDistanceSquared = FMath::Square(16.0f);

    for (int32 RoadIndex = 0; RoadIndex < CurrentHeatmapDisplayInfo.Roads.Num(); ++RoadIndex)
    {
        const FTelemetryHeatmapRoad& Road = CurrentHeatmapDisplayInfo.Roads[RoadIndex];
        for (int32 PointIndex = 1; PointIndex < Road.Points.Num(); ++PointIndex)
        {
            const FVector2D FirstNormalized = Road.Points[PointIndex - 1];
            const FVector2D SecondNormalized = Road.Points[PointIndex];
            const FVector2D FirstLocal(
                (Rect.X + FirstNormalized.X * Rect.Z) * ImageSize.X,
                (Rect.Y + (1.0f - FirstNormalized.Y) * Rect.W) * ImageSize.Y
            );
            const FVector2D SecondLocal(
                (Rect.X + SecondNormalized.X * Rect.Z) * ImageSize.X,
                (Rect.Y + (1.0f - SecondNormalized.Y) * Rect.W) * ImageSize.Y
            );
            const FVector2D FirstScreen = ImageGeometry.LocalToAbsolute(FirstLocal);
            const FVector2D SecondScreen = ImageGeometry.LocalToAbsolute(SecondLocal);
            const FVector2D Segment = SecondScreen - FirstScreen;
            const float SegmentLengthSquared = Segment.SizeSquared();
            const float Along = SegmentLengthSquared > KINDA_SMALL_NUMBER
                ? FMath::Clamp(FVector2D::DotProduct(ScreenPosition - FirstScreen, Segment) /
                    SegmentLengthSquared, 0.0f, 1.0f)
                : 0.0f;
            const FVector2D ClosestScreen = FirstScreen + Segment * Along;
            const float DistanceSquared = FVector2D::DistSquared(ScreenPosition, ClosestScreen);
            if (DistanceSquared < BestDistanceSquared)
            {
                BestDistanceSquared = DistanceSquared;
                OutRoadIndex = RoadIndex;
                OutClosestNormalizedPoint = FMath::Lerp(FirstNormalized, SecondNormalized, Along);
            }
        }
    }

    return OutRoadIndex != INDEX_NONE;
}

// Updates the road card as the user moves over the map.
void UTelemetryPanelWidget::UpdateHeatmapRoadHover(const FVector2D& ScreenPosition)
{
    LastHeatmapPointerScreenPosition = ScreenPosition;
    int32 RoadIndex = INDEX_NONE;
    FVector2D ClosestPoint = FVector2D::ZeroVector;
    if (IsPointerOverHeatmap(ScreenPosition) &&
        FindNearestHeatmapRoad(ScreenPosition, RoadIndex, ClosestPoint))
    {
        HoveredHeatmapRoadIndex = RoadIndex;
        HoveredRoadNormalizedPoint = ClosestPoint;
        if (PinnedHeatmapRoadIndex == INDEX_NONE)
        {
            ShowHeatmapRoadDetails(RoadIndex, false);
        }
    }
    else
    {
        HoveredHeatmapRoadIndex = INDEX_NONE;
        if (PinnedHeatmapRoadIndex == INDEX_NONE)
        {
            if (HeatmapRoadCard)
            {
                HeatmapRoadCard->SetVisibility(ESlateVisibility::Collapsed);
            }
        }
    }

    UpdateHeatmapRoadMarker();
}

// Formats one road value with the correct unit for the current map.
FString UTelemetryPanelWidget::FormatHeatmapRoadValue(const FTelemetryHeatmapRoad& Road) const
{
    if (Road.bIsComparison)
    {
        return FString::Printf(TEXT("%+.2f %s"), Road.RawDelta, *Road.ComparisonUnit);
    }
    if (CurrentHeatmapMetric == TEXT("avg_speed_mph"))
    {
        return FString::Printf(TEXT("%.1f mph"), Road.MetricValue);
    }
    if (CurrentHeatmapMetric == TEXT("estimated_flow_veh_per_hr"))
    {
        return FString::Printf(TEXT("%.0f vehicles/hour"), Road.MetricValue);
    }
    if (CurrentHeatmapMetric == TEXT("avg_wait_per_vehicle_s"))
    {
        return FString::Printf(TEXT("%.1f seconds"), Road.MetricValue);
    }
    if (CurrentHeatmapMetric == TEXT("fdot_geh_score"))
    {
        return FString::Printf(TEXT("%.1f GEH"), Road.MetricValue);
    }
    if (CurrentHeatmapMetric == TEXT("bottleneck_score"))
    {
        return FString::Printf(TEXT("%.1f / 100"), Road.MetricValue);
    }
    return FString::Printf(TEXT("%.2f"), Road.MetricValue);
}

// Shows the selected road's name and values in the map information card.
void UTelemetryPanelWidget::ShowHeatmapRoadDetails(int32 RoadIndex, bool bPinned)
{
    if (!CurrentHeatmapDisplayInfo.Roads.IsValidIndex(RoadIndex) ||
        !HeatmapRoadCard || !HeatmapRoadNameText || !HeatmapRoadDetailsText || !HeatmapRoadPinText)
    {
        return;
    }

    const FTelemetryHeatmapRoad& Road = CurrentHeatmapDisplayInfo.Roads[RoadIndex];
    HeatmapRoadNameText->SetText(FText::FromString(Road.RoadName));

    FString Details;
    if (!Road.RouteRef.IsEmpty() && !Road.RoadName.Equals(Road.RouteRef, ESearchCase::IgnoreCase))
    {
        Details += Road.RouteRef + TEXT("  |  ");
    }
    FString RoadType = Road.HighwayType.Replace(TEXT("_"), TEXT(" ")).ToUpper();
    Details += RoadType;
    if (Road.bIsComparison)
    {
        Details += FString::Printf(
            TEXT("\n%s  |  %s\nBaseline: %.2f %s\nComparison: %.2f %s\nChange: %+.2f %s\nEdge %d"),
            *Road.ComparisonMetric,
            *Road.ComparisonStatus.Replace(TEXT("_"), TEXT(" ")).ToUpper(),
            Road.BaselineValue,
            *Road.ComparisonUnit,
            Road.ComparisonValue,
            *Road.ComparisonUnit,
            Road.RawDelta,
            *Road.ComparisonUnit,
            Road.EdgeId
        );
    }
    else
    {
        Details += FString::Printf(
            TEXT("\n%s: %s\nEdge %d"),
            *GetMetricDisplayName(CurrentHeatmapMetric),
            *FormatHeatmapRoadValue(Road),
            Road.EdgeId
        );
    }
    HeatmapRoadDetailsText->SetText(FText::FromString(Details));
    HeatmapRoadPinText->SetText(FText::FromString(
        bPinned ? TEXT("PINNED  |  Click road again to unpin") : TEXT("Click road to pin")
    ));
    HeatmapRoadCard->SetVisibility(ESlateVisibility::HitTestInvisible);
}

// Places one marker over a point stored in the heatmap JSON.
void UTelemetryPanelWidget::PlaceHeatmapMarker(
    UWidget* MarkerWidget,
    const FVector2D& NormalizedPoint,
    const FVector2D& MarkerHalfSize
) const
{
    if (!MarkerWidget || !HeatmapMarkerLayer || !HeatmapImage)
    {
        return;
    }
    const FGeometry ImageGeometry = HeatmapImage->GetCachedGeometry();
    const FVector2D ImageSize = ImageGeometry.GetLocalSize();
    const FVector4 Rect = CurrentHeatmapDisplayInfo.MapRect;
    const FVector2D ImageLocal(
        (Rect.X + NormalizedPoint.X * Rect.Z) * ImageSize.X,
        (Rect.Y + (1.0f - NormalizedPoint.Y) * Rect.W) * ImageSize.Y
    );
    const FVector2D ScreenPosition = ImageGeometry.LocalToAbsolute(ImageLocal);
    const FVector2D MarkerLocal = HeatmapMarkerLayer->GetCachedGeometry().AbsoluteToLocal(ScreenPosition);
    if (UCanvasPanelSlot* MarkerSlot = Cast<UCanvasPanelSlot>(MarkerWidget->Slot))
    {
        MarkerSlot->SetPosition(MarkerLocal - MarkerHalfSize);
    }
    MarkerWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
}

// Places the small marker over the hovered or pinned road.
void UTelemetryPanelWidget::UpdateHeatmapRoadMarker()
{
    if (!HeatmapRoadMarker)
    {
        return;
    }

    const int32 RoadIndex = PinnedHeatmapRoadIndex != INDEX_NONE
        ? PinnedHeatmapRoadIndex
        : HoveredHeatmapRoadIndex;
    if (!CurrentHeatmapDisplayInfo.Roads.IsValidIndex(RoadIndex))
    {
        HeatmapRoadMarker->SetVisibility(ESlateVisibility::Collapsed);
        return;
    }

    const FVector2D NormalizedPoint = PinnedHeatmapRoadIndex != INDEX_NONE
        ? PinnedRoadNormalizedPoint
        : HoveredRoadNormalizedPoint;
    PlaceHeatmapMarker(HeatmapRoadMarker, NormalizedPoint, FVector2D(9.0f, 9.0f));
}

// Finds the current metric's highest value, or its lowest value for speed.
void UTelemetryPanelWidget::SelectHeatmapExtremeRoad()
{
    ExtremeHeatmapRoadIndex = INDEX_NONE;
    ExtremeRoadNormalizedPoint = FVector2D::ZeroVector;
    if (HeatmapExtremeMarker)
    {
        HeatmapExtremeMarker->SetVisibility(ESlateVisibility::Collapsed);
    }

    const bool bChooseLowest = CurrentHeatmapMetric == TEXT("avg_speed_mph");
    const bool bChooseLargestChange = CurrentHeatmapMetric == TEXT("comparison_delta");
    float BestScore = -TNumericLimits<float>::Max();

    for (int32 RoadIndex = 0; RoadIndex < CurrentHeatmapDisplayInfo.Roads.Num(); ++RoadIndex)
    {
        const FTelemetryHeatmapRoad& Road = CurrentHeatmapDisplayInfo.Roads[RoadIndex];
        if (Road.Points.Num() < 2)
        {
            continue;
        }

        const float Score = bChooseLargestChange
            ? FMath::Abs(Road.MetricValue)
            : bChooseLowest ? -Road.MetricValue : Road.MetricValue;
        if (Score <= BestScore)
        {
            continue;
        }

        // Use the middle of the longest line so the arrow does not land on a corner.
        float LongestSegment = -1.0f;
        FVector2D BestPoint = Road.Points[0];
        for (int32 PointIndex = 1; PointIndex < Road.Points.Num(); ++PointIndex)
        {
            const FVector2D First = Road.Points[PointIndex - 1];
            const FVector2D Second = Road.Points[PointIndex];
            const float SegmentLength = FVector2D::DistSquared(First, Second);
            if (SegmentLength > LongestSegment)
            {
                LongestSegment = SegmentLength;
                BestPoint = (First + Second) * 0.5f;
            }
        }

        BestScore = Score;
        ExtremeHeatmapRoadIndex = RoadIndex;
        ExtremeRoadNormalizedPoint = BestPoint;
    }

    UpdateHeatmapExtremeMarker();
}

// Keeps the automatic metric arrow attached to its road while the map moves.
void UTelemetryPanelWidget::UpdateHeatmapExtremeMarker()
{
    if (!HeatmapExtremeMarker ||
        !CurrentHeatmapDisplayInfo.Roads.IsValidIndex(ExtremeHeatmapRoadIndex))
    {
        if (HeatmapExtremeMarker)
        {
            HeatmapExtremeMarker->SetVisibility(ESlateVisibility::Collapsed);
        }
        return;
    }

    PlaceHeatmapMarker(
        HeatmapExtremeMarker,
        ExtremeRoadNormalizedPoint,
        FVector2D(9.0f, 20.0f)
    );
}

// Clears road hover and pin data when the map changes or the user clicks away.
void UTelemetryPanelWidget::ClearHeatmapRoadInteraction()
{
    HoveredHeatmapRoadIndex = INDEX_NONE;
    PinnedHeatmapRoadIndex = INDEX_NONE;
    if (HeatmapRoadMarker)
    {
        HeatmapRoadMarker->SetVisibility(ESlateVisibility::Collapsed);
    }
    if (HeatmapRoadCard)
    {
        HeatmapRoadCard->SetVisibility(ESlateVisibility::Collapsed);
    }
}

// Shows one focused workspace instead of displaying every telemetry control at once.
// Main panel tab and button state

// Opens one workspace tab while keeping the same run selected.
void UTelemetryPanelWidget::SetWorkspaceTab(int32 TabIndex)
{
    ActiveWorkspaceTab = FMath::Clamp(TabIndex, 0, 3);
    if (WorkspaceSwitcher)
    {
        WorkspaceSwitcher->SetActiveWidgetIndex(ActiveWorkspaceTab);
    }
    RefreshWorkspaceTabStyles();
}

// Uses the amber treatment only on the currently active workspace tab.
// Gives the active tab its amber style so the user knows where they are.
void UTelemetryPanelWidget::RefreshWorkspaceTabStyles()
{
    using namespace TelemetryPalette;
    if (OverviewTabButton)
    {
        OverviewTabButton->SetStyle(ActionButtonStyle(ActiveWorkspaceTab == 0));
    }
    if (HeatmapsTabButton)
    {
        HeatmapsTabButton->SetStyle(ActionButtonStyle(ActiveWorkspaceTab == 1));
    }
    if (FDOTTabButton)
    {
        FDOTTabButton->SetStyle(ActionButtonStyle(ActiveWorkspaceTab == 2));
    }
    if (CompareTabButton)
    {
        CompareTabButton->SetStyle(ActionButtonStyle(ActiveWorkspaceTab == 3));
    }
}

// Checks whether the selected heatmap was already generated for this run.
bool UTelemetryPanelWidget::DoesSelectedHeatmapExist() const
{
    if (!SavedRuns.IsValidIndex(SelectedRunIndex))
    {
        return false;
    }

    FString HeatmapPath;
    FString ErrorMessage;
    return UTelemetryPanelBridge::GetGeneratedHeatmapPath(
        SavedRuns[SelectedRunIndex].RunId,
        SelectedMetric,
        SelectedFocus,
        HeatmapPath,
        ErrorMessage
    );
}

// Presents one clear primary action and only reveals Regenerate when needed.
// Chooses whether the main heatmap button should say Generate or View.
void UTelemetryPanelWidget::RefreshHeatmapActionState()
{
    const bool bHasSelectedRun = SavedRuns.IsValidIndex(SelectedRunIndex);
    const bool bHasExistingHeatmap = bHasSelectedRun && DoesSelectedHeatmapExist();
    const bool bCanStartTask = bHasSelectedRun && !bTelemetryTaskRunning;

    if (PrimaryHeatmapButton)
    {
        PrimaryHeatmapButton->SetIsEnabled(bCanStartTask);
    }
    if (PrimaryHeatmapButtonText)
    {
        PrimaryHeatmapButtonText->SetText(FText::FromString(
            bHasExistingHeatmap ? TEXT("VIEW EXISTING") : TEXT("GENERATE & VIEW")
        ));
    }
    if (RegenerateHeatmapButton)
    {
        RegenerateHeatmapButton->SetIsEnabled(bCanStartTask);
        RegenerateHeatmapButton->SetVisibility(
            bHasExistingHeatmap ? ESlateVisibility::Visible : ESlateVisibility::Collapsed
        );
    }
    if (CompareFDOTButton)
    {
        CompareFDOTButton->SetIsEnabled(bCanStartTask);
    }
    if (ViewFDOTMapButton)
    {
        ViewFDOTMapButton->SetIsEnabled(bCanStartTask);
    }
}

// Keep task inputs fixed and prevent a second Python process from starting.
// Long Python jobs run away from Unreal's main work. This keeps the panel usable
// while a heatmap or FDOT comparison is being created.

// Disables task buttons during long work so the same job cannot start twice.
void UTelemetryPanelWidget::SetTelemetryTaskRunning(bool bIsRunning)
{
    bTelemetryTaskRunning = bIsRunning;
    RefreshHeatmapActionState();

    if (MetricComboBox)
    {
        MetricComboBox->SetIsEnabled(!bIsRunning);
    }
    if (FocusComboBox)
    {
        FocusComboBox->SetIsEnabled(!bIsRunning);
    }
    if (CompareFDOTButton)
    {
        CompareFDOTButton->SetIsEnabled(!bIsRunning && SavedRuns.IsValidIndex(SelectedRunIndex));
    }
    if (ViewFDOTMapButton)
    {
        ViewFDOTMapButton->SetIsEnabled(!bIsRunning && SavedRuns.IsValidIndex(SelectedRunIndex));
    }
    if (ComparisonRunComboBox)
    {
        ComparisonRunComboBox->SetIsEnabled(!bIsRunning);
    }
    if (ComparisonMetricComboBox)
    {
        ComparisonMetricComboBox->SetIsEnabled(!bIsRunning);
    }
    if (ComparisonFocusComboBox)
    {
        ComparisonFocusComboBox->SetIsEnabled(!bIsRunning);
    }
    if (CompareRunsButton)
    {
        CompareRunsButton->SetIsEnabled(
            !bIsRunning && SavedRuns.IsValidIndex(SelectedRunIndex) && SavedRuns.IsValidIndex(ComparisonRunIndex)
        );
    }
    if (SwapComparisonRunsButton)
    {
        SwapComparisonRunsButton->SetIsEnabled(
            !bIsRunning && SavedRuns.IsValidIndex(SelectedRunIndex) && SavedRuns.IsValidIndex(ComparisonRunIndex)
        );
    }
    if (GenerateComparisonHeatmapsButton)
    {
        GenerateComparisonHeatmapsButton->SetIsEnabled(
            !bIsRunning && SavedRuns.IsValidIndex(SelectedRunIndex) && SavedRuns.IsValidIndex(ComparisonRunIndex)
        );
    }
    if (ViewComparisonHeatmapsButton)
    {
        ViewComparisonHeatmapsButton->SetIsEnabled(!bIsRunning && !ComparisonHeatmapPaths.ChangePath.IsEmpty());
    }
    if (DeleteRunButton)
    {
        DeleteRunButton->SetIsEnabled(!bIsRunning && SavedRuns.IsValidIndex(SelectedRunIndex));
    }

    for (UTelemetryRunButton* RunRow : RunRows)
    {
        if (RunRow)
        {
            RunRow->SetIsEnabled(!bIsRunning);
        }
    }
}

// Button and combo-box events

// Changes the selected run and refreshes every tab that depends on it.
void UTelemetryPanelWidget::HandleRunSelected(int32 RunIndex)
{
    using namespace TelemetryPalette;
    if (bTelemetryTaskRunning)
    {
        return;
    }

    if (!SavedRuns.IsValidIndex(RunIndex))
    {
        SetStatus(TEXT("The selected telemetry run is no longer available."), true);
        return;
    }

    SelectedRunIndex = RunIndex;
    ResetDeleteConfirmation();
    if (DeleteRunButton)
    {
        DeleteRunButton->SetIsEnabled(true);
    }
    RefreshComparisonOptions();
    RefreshHeatmapActionState();
    if (FDOTValidationText)
    {
        FDOTValidationText->SetText(FText::FromString(
            TEXT("Ready to compare this run with 2025 FDOT design-hour estimates. Results will be summarized in plain language with engineering details below.")
        ));
        FDOTValidationText->SetColorAndOpacity(FSlateColor(TextSecondary));
    }
    RefreshRunRowStyles();
    RefreshSelectedRunDetails();
    SetStatus(FString::Printf(TEXT("Selected %s."), *SavedRuns[RunIndex].CreatedAt));
}

// Closes the telemetry panel.
void UTelemetryPanelWidget::HandleCloseClicked()
{
    RemoveFromParent();
}

// Reloads saved runs after a new simulation finishes.
void UTelemetryPanelWidget::HandleRefreshClicked()
{
    if (bTelemetryTaskRunning)
    {
        SetStatus(TEXT("Wait for the current telemetry task to finish before refreshing runs."));
        return;
    }

    RefreshRunList();
}

// Opens the run summary tab.
void UTelemetryPanelWidget::HandleOverviewTabClicked()
{
    SetWorkspaceTab(0);
    SetStatus(TEXT("Showing the selected run overview."));
}

// Opens the single-run heatmap tab.
void UTelemetryPanelWidget::HandleHeatmapsTabClicked()
{
    SetWorkspaceTab(1);
    SetStatus(TEXT("Choose a measurement and road focus to create a heatmap."));
}

// Opens the FDOT comparison tab and explains what it does.
void UTelemetryPanelWidget::HandleFDOTTabClicked()
{
    SetWorkspaceTab(2);
    SetStatus(TEXT("FDOT comparison checks simulated hourly flow against FDOT-based reference estimates."));
}

// Opens the saved-run comparison tab.
void UTelemetryPanelWidget::HandleCompareTabClicked()
{
    SetWorkspaceTab(3);
    SetStatus(TEXT("Choose a baseline run and a second run from the same map."));
}

// Saves which second run was chosen from the comparison list.
void UTelemetryPanelWidget::HandleComparisonRunChanged(
    FString SelectedItem,
    ESelectInfo::Type SelectionType
)
{
    ComparisonRunIndex = INDEX_NONE;
    for (const int32 RunIndex : ComparisonRunIndices)
    {
        if (SavedRuns.IsValidIndex(RunIndex) && SavedRuns[RunIndex].CreatedAt == SelectedItem)
        {
            ComparisonRunIndex = RunIndex;
            break;
        }
    }
    const bool bCanCompare = SavedRuns.IsValidIndex(SelectedRunIndex) &&
        SavedRuns.IsValidIndex(ComparisonRunIndex) && !bTelemetryTaskRunning;
    CompareRunsButton->SetIsEnabled(bCanCompare);
    SwapComparisonRunsButton->SetIsEnabled(bCanCompare);
    GenerateComparisonHeatmapsButton->SetIsEnabled(bCanCompare);
    ComparisonHeatmapPaths = FTelemetryHeatmapComparisonPaths();
    ViewComparisonHeatmapsButton->SetIsEnabled(false);
}

// Swaps the two run choices so the change can be viewed in reverse.
void UTelemetryPanelWidget::HandleSwapComparisonRunsClicked()
{
    if (bTelemetryTaskRunning || !SavedRuns.IsValidIndex(SelectedRunIndex) ||
        !SavedRuns.IsValidIndex(ComparisonRunIndex))
    {
        return;
    }

    const int32 OldBaselineIndex = SelectedRunIndex;
    SelectedRunIndex = ComparisonRunIndex;
    RefreshComparisonOptions(OldBaselineIndex);
    RefreshRunRowStyles();
    RefreshSelectedRunDetails();
    SetStatus(TEXT("Swapped the baseline and comparison runs."));
}

// Starts the run comparison away from the main game work.
void UTelemetryPanelWidget::HandleCompareRunsClicked()
{
    if (bTelemetryTaskRunning)
    {
        return;
    }
    if (!SavedRuns.IsValidIndex(SelectedRunIndex) || !SavedRuns.IsValidIndex(ComparisonRunIndex))
    {
        SetStatus(TEXT("Select two different saved runs before comparing."), true);
        return;
    }

    const FString BaselineRunId = SavedRuns[SelectedRunIndex].RunId;
    const FString ComparisonRunId = SavedRuns[ComparisonRunIndex].RunId;
    SetTelemetryTaskRunning(true);
    SetStatus(TEXT("Comparing saved runs in the background..."));

    TWeakObjectPtr<UTelemetryPanelWidget> WeakThis(this);
    // Run the file work in the background so Unreal can keep drawing the panel.
    Async(EAsyncExecution::ThreadPool, [WeakThis, BaselineRunId, ComparisonRunId]()
    {
        FTelemetryRunComparisonResult Result;
        FString ErrorMessage;
        const bool bSucceeded = UTelemetryPanelBridge::CompareSavedRuns(
            BaselineRunId,
            ComparisonRunId,
            Result,
            ErrorMessage
        );
        // Return to Unreal's main work before changing any widgets.
        AsyncTask(ENamedThreads::GameThread, [WeakThis, BaselineRunId, ComparisonRunId, bSucceeded, Result, ErrorMessage]()
        {
            if (WeakThis.IsValid())
            {
                WeakThis->FinishRunComparison(
                    BaselineRunId,
                    ComparisonRunId,
                    bSucceeded,
                    Result,
                    ErrorMessage
                );
            }
        });
    });
}

// Shows the finished comparison, or a clear message if it failed.
void UTelemetryPanelWidget::FinishRunComparison(
    const FString& BaselineRunId,
    const FString& ComparisonRunId,
    bool bSucceeded,
    const FTelemetryRunComparisonResult& Result,
    const FString& ErrorMessage
)
{
    using namespace TelemetryPalette;

    SetTelemetryTaskRunning(false);
    if (!bSucceeded)
    {
        ComparisonResultsBox->ClearChildren();
        UTextBlock* ErrorText = MakeText(ErrorMessage, 12, ErrorColor, FName("Medium"));
        ErrorText->SetAutoWrapText(true);
        ComparisonResultsBox->AddChildToVerticalBox(ErrorText);
        SetStatus(ErrorMessage, true);
        return;
    }

    RenderComparisonResult(Result);
    SetStatus(Result.bPreliminary
        ? TEXT("Run comparison completed with preliminary-data warnings.")
        : TEXT("Run comparison completed."));
}

// Saves the metric chosen for comparison heatmaps.
void UTelemetryPanelWidget::HandleComparisonMetricChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
    SelectedComparisonMetric = GetMetricId(SelectedItem);
    ComparisonHeatmapPaths = FTelemetryHeatmapComparisonPaths();
    ViewComparisonHeatmapsButton->SetIsEnabled(false);
}

// Saves how many roads should be highlighted in comparison heatmaps.
void UTelemetryPanelWidget::HandleComparisonFocusChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
    SelectedComparisonFocus = GetFocusId(SelectedItem);
    ComparisonHeatmapPaths = FTelemetryHeatmapComparisonPaths();
    ViewComparisonHeatmapsButton->SetIsEnabled(false);
}

// Starts creation of the baseline, second-run, and change maps.
void UTelemetryPanelWidget::HandleGenerateComparisonHeatmapsClicked()
{
    // Save the IDs now because the user could change selections before the job ends.
    if (bTelemetryTaskRunning || !SavedRuns.IsValidIndex(SelectedRunIndex) ||
        !SavedRuns.IsValidIndex(ComparisonRunIndex))
    {
        return;
    }

    const FString BaselineRunId = SavedRuns[SelectedRunIndex].RunId;
    const FString ComparisonRunId = SavedRuns[ComparisonRunIndex].RunId;
    const FString Metric = SelectedComparisonMetric;
    const FString Focus = SelectedComparisonFocus;
    ComparisonHeatmapBaselineLabel = SavedRuns[SelectedRunIndex].CreatedAt;
    ComparisonHeatmapComparisonLabel = SavedRuns[ComparisonRunIndex].CreatedAt;
    SetTelemetryTaskRunning(true);
    SetStatus(TEXT("Generating baseline, comparison, and change maps in the background..."));

    TWeakObjectPtr<UTelemetryPanelWidget> WeakThis(this);
    // Generate map files in the background because this can take a few seconds.
    Async(EAsyncExecution::ThreadPool, [WeakThis, BaselineRunId, ComparisonRunId, Metric, Focus]()
    {
        FTelemetryHeatmapComparisonPaths Paths;
        FString ErrorMessage;
        const bool bSucceeded = UTelemetryPanelBridge::GenerateComparisonHeatmaps(
            BaselineRunId, ComparisonRunId, Metric, Focus, Paths, ErrorMessage
        );
        // Return to Unreal's main work before updating buttons and status text.
        AsyncTask(ENamedThreads::GameThread, [WeakThis, BaselineRunId, ComparisonRunId, bSucceeded, Paths, ErrorMessage]()
        {
            if (WeakThis.IsValid())
            {
                WeakThis->FinishComparisonHeatmaps(
                    BaselineRunId, ComparisonRunId, bSucceeded, Paths, ErrorMessage
                );
            }
        });
    });
}

// Saves the returned map paths and enables the View button after success.
void UTelemetryPanelWidget::FinishComparisonHeatmaps(
    const FString& BaselineRunId,
    const FString& ComparisonRunId,
    bool bSucceeded,
    const FTelemetryHeatmapComparisonPaths& Paths,
    const FString& ErrorMessage
)
{
    SetTelemetryTaskRunning(false);
    if (!bSucceeded)
    {
        SetStatus(ErrorMessage, true);
        return;
    }
    ComparisonHeatmapPaths = Paths;
    ViewComparisonHeatmapsButton->SetIsEnabled(true);
    SetStatus(FString::Printf(
        TEXT("Comparison maps generated for %d shared road directions."),
        Paths.SharedRoads
    ));
}

// Opens the comparison viewer using the first of the three maps.
void UTelemetryPanelWidget::HandleViewComparisonHeatmapsClicked()
{
    if (ComparisonHeatmapPaths.ChangePath.IsEmpty())
    {
        SetStatus(TEXT("Generate the comparison maps before viewing them."), true);
        return;
    }
    ResetHeatmapView();
    ComparisonHeatmapModeBar->SetVisibility(ESlateVisibility::Visible);
    ShowComparisonHeatmapMode(2);
}

// Switches the viewer between baseline, second-run, and change maps.
void UTelemetryPanelWidget::ShowComparisonHeatmapMode(int32 ModeIndex)
{
    using namespace TelemetryPalette;

    FString Path;
    FString ModeLabel;
    FString Metric = SelectedComparisonMetric;
    if (ModeIndex == 0)
    {
        Path = ComparisonHeatmapPaths.BaselinePath;
        ModeLabel = TEXT("Baseline");
    }
    else if (ModeIndex == 1)
    {
        Path = ComparisonHeatmapPaths.ComparisonPath;
        ModeLabel = TEXT("Comparison");
    }
    else
    {
        Path = ComparisonHeatmapPaths.ChangePath;
        ModeLabel = TEXT("Change");
        Metric = TEXT("comparison_delta");
    }

    const FString Title = FString::Printf(
        TEXT("%s  |  %s vs %s  |  %s"),
        *ModeLabel,
        *ComparisonHeatmapBaselineLabel,
        *ComparisonHeatmapComparisonLabel,
        *GetMetricDisplayName(SelectedComparisonMetric)
    );
    if (OpenHeatmapPath(Path, Title, Metric))
    {
        BaselineHeatmapModeButton->SetStyle(ActionButtonStyle(ModeIndex == 0));
        ComparisonHeatmapModeButton->SetStyle(ActionButtonStyle(ModeIndex == 1));
        ChangeHeatmapModeButton->SetStyle(ActionButtonStyle(ModeIndex == 2));
        SetStatus(FString::Printf(TEXT("Showing the %s map."), *ModeLabel.ToLower()));
    }
}

// Shows the baseline map.
void UTelemetryPanelWidget::HandleBaselineHeatmapModeClicked()
{
    ShowComparisonHeatmapMode(0);
}

// Shows the second run's map.
void UTelemetryPanelWidget::HandleComparisonHeatmapModeClicked()
{
    ShowComparisonHeatmapMode(1);
}

// Shows where roads improved, worsened, or stayed close to the same.
void UTelemetryPanelWidget::HandleChangeHeatmapModeClicked()
{
    ShowComparisonHeatmapMode(2);
}

// Returns the delete button to its normal one-click state.
void UTelemetryPanelWidget::ResetDeleteConfirmation()
{
    bDeleteConfirmationPending = false;
    if (DeleteRunButtonText)
    {
        DeleteRunButtonText->SetText(FText::FromString(TEXT("DELETE RUN")));
    }
}

// Requires two clicks before starting deletion to prevent an accident.
void UTelemetryPanelWidget::HandleDeleteRunClicked()
{
    // A second click is required so a run is not removed by accident.
    if (bTelemetryTaskRunning || !SavedRuns.IsValidIndex(SelectedRunIndex))
    {
        return;
    }
    if (!bDeleteConfirmationPending)
    {
        bDeleteConfirmationPending = true;
        DeleteRunButtonText->SetText(FText::FromString(TEXT("CONFIRM DELETE")));
        SetStatus(TEXT("Click Confirm Delete to permanently remove this run and its generated files."), true);
        return;
    }

    const FString RunId = SavedRuns[SelectedRunIndex].RunId;
    SetTelemetryTaskRunning(true);
    SetStatus(TEXT("Deleting the selected run..."));
    TWeakObjectPtr<UTelemetryPanelWidget> WeakThis(this);
    // Delete the folder in the background so the panel stays responsive.
    Async(EAsyncExecution::ThreadPool, [WeakThis, RunId]()
    {
        FString ErrorMessage;
        const bool bSucceeded = UTelemetryPanelBridge::DeleteSavedRun(RunId, ErrorMessage);
        // Return to Unreal's main work before rebuilding the run list.
        AsyncTask(ENamedThreads::GameThread, [WeakThis, RunId, bSucceeded, ErrorMessage]()
        {
            if (WeakThis.IsValid())
            {
                WeakThis->FinishDeleteRun(RunId, bSucceeded, ErrorMessage);
            }
        });
    });
}

// Refreshes the list after deletion, or shows why it could not be deleted.
void UTelemetryPanelWidget::FinishDeleteRun(
    const FString& RunId,
    bool bSucceeded,
    const FString& ErrorMessage
)
{
    SetTelemetryTaskRunning(false);
    ResetDeleteConfirmation();
    if (!bSucceeded)
    {
        SetStatus(ErrorMessage, true);
        return;
    }
    RefreshRunList();
    SetStatus(TEXT("Saved run deleted."));
}

// Saves the selected heatmap metric and updates its help text.
void UTelemetryPanelWidget::HandleMetricSelectionChanged(
    FString SelectedItem,
    ESelectInfo::Type SelectionType
)
{
    const FString MetricId = GetMetricId(SelectedItem);

    if (MetricId.IsEmpty())
    {
        SelectedMetric = TEXT("bottleneck_score");
        MetricComboBox->SetSelectedOption(TEXT("RoadMap Bottleneck Index"));
        SetStatus(TEXT("The selected metric was invalid. RoadMap Bottleneck Index was restored."), true);
        return;
    }

    SelectedMetric = MetricId;
    if (MetricHelpText)
    {
        MetricHelpText->SetText(FText::FromString(GetMetricHelpText(SelectedMetric)));
    }
    RefreshHeatmapActionState();
    SetStatus(FString::Printf(TEXT("Selected metric: %s."), *SelectedItem));
}

// Saves whether the map should show all roads or only the worst group.
void UTelemetryPanelWidget::HandleFocusSelectionChanged(
    FString SelectedItem,
    ESelectInfo::Type SelectionType
)
{
    SelectedFocus = GetFocusId(SelectedItem);
    RefreshHeatmapActionState();
    SetStatus(FString::Printf(TEXT("Road focus: %s."), *GetFocusDisplayName(SelectedFocus)));
}

// Views an existing result, or generates and immediately opens a missing one.
// Opens an existing heatmap, or makes it first when it is missing.
void UTelemetryPanelWidget::HandlePrimaryHeatmapClicked()
{
    if (DoesSelectedHeatmapExist())
    {
        HandleViewHeatmapClicked();
    }
    else
    {
        HandleGenerateHeatmapClicked();
    }
}

// Starts one heatmap job without blocking the game screen.
void UTelemetryPanelWidget::HandleGenerateHeatmapClicked()
{
    if (bTelemetryTaskRunning)
    {
        return;
    }

    if (!SavedRuns.IsValidIndex(SelectedRunIndex))
    {
        SetStatus(TEXT("Select a saved run before generating a heatmap."), true);
        return;
    }

    if (SelectedMetric.IsEmpty())
    {
        SelectedMetric = TEXT("bottleneck_score");
    }

    const FString RunId = SavedRuns[SelectedRunIndex].RunId;
    const FString Metric = SelectedMetric;
    const FString Focus = SelectedFocus;
    const TWeakObjectPtr<UTelemetryPanelWidget> WeakThis(this);

    SetTelemetryTaskRunning(true);
    SetStatus(TEXT("Generating the selected heatmap in the background..."));

    // Let Python draw the map in the background instead of freezing the screen.
    Async(EAsyncExecution::ThreadPool, [WeakThis, RunId, Metric, Focus]()
    {
        FString ResultJson;
        const bool bSucceeded = UTelemetryPanelBridge::GenerateSelectedHeatmap(
            RunId,
            Metric,
            Focus,
            ResultJson
        );

        // Return to Unreal's main work before opening the finished map.
        AsyncTask(ENamedThreads::GameThread, [WeakThis, RunId, Metric, Focus, bSucceeded]()
        {
            if (WeakThis.IsValid())
            {
                WeakThis->FinishHeatmapGeneration(RunId, Metric, Focus, bSucceeded);
            }
        });
    });
}

// Updates the buttons and opens the map after a successful job.
void UTelemetryPanelWidget::FinishHeatmapGeneration(
    const FString& RunId,
    const FString& Metric,
    const FString& Focus,
    bool bSucceeded
)
{
    SetTelemetryTaskRunning(false);

    if (!bSucceeded)
    {
        SetStatus(TEXT("Heatmap generation failed. Check the telemetry pipeline output."), true);
        return;
    }

    const bool bSameSelection = SavedRuns.IsValidIndex(SelectedRunIndex) &&
        SavedRuns[SelectedRunIndex].RunId == RunId &&
        SelectedMetric == Metric &&
        SelectedFocus == Focus;
    if (!bSameSelection)
    {
        SetStatus(TEXT("Heatmap generation completed for the previously selected run."));
        return;
    }

    RefreshHeatmapActionState();
    HandleViewHeatmapClicked();
}

// Runs and displays the map-specific FDOT reference comparison for the selected run.
// Starts the FDOT check for the selected run without blocking the panel.
void UTelemetryPanelWidget::HandleCompareFDOTClicked()
{
    if (bTelemetryTaskRunning)
    {
        return;
    }

    if (!SavedRuns.IsValidIndex(SelectedRunIndex))
    {
        SetStatus(TEXT("Select a saved run before comparing with FDOT."), true);
        return;
    }

    const FString RunId = SavedRuns[SelectedRunIndex].RunId;
    const TWeakObjectPtr<UTelemetryPanelWidget> WeakThis(this);

    SetTelemetryTaskRunning(true);
    SetStatus(TEXT("Comparing with FDOT in the background..."));

    // Run the FDOT file work in the background so the panel does not freeze.
    Async(EAsyncExecution::ThreadPool, [WeakThis, RunId]()
    {
        FTelemetryFDOTValidationSummary Summary;
        FString ErrorMessage;
        const bool bSucceeded = UTelemetryPanelBridge::CompareSelectedRunWithFDOT(
            RunId,
            Summary,
            ErrorMessage
        );

        // Return to Unreal's main work before showing the FDOT results.
        AsyncTask(ENamedThreads::GameThread, [
            WeakThis,
            RunId,
            bSucceeded,
            Summary = MoveTemp(Summary),
            ErrorMessage = MoveTemp(ErrorMessage)
        ]()
        {
            if (WeakThis.IsValid())
            {
                WeakThis->FinishFDOTComparison(RunId, bSucceeded, Summary, ErrorMessage);
            }
        });
    });
}

// Shows plain results first, followed by the engineering values and road list.
void UTelemetryPanelWidget::FinishFDOTComparison(
    const FString& RunId,
    bool bSucceeded,
    const FTelemetryFDOTValidationSummary& Summary,
    const FString& ErrorMessage
)
{
    using namespace TelemetryPalette;
    SetTelemetryTaskRunning(false);

    if (!bSucceeded)
    {
        if (FDOTValidationText)
        {
            FDOTValidationText->SetText(FText::FromString(ErrorMessage));
            FDOTValidationText->SetColorAndOpacity(FSlateColor(ErrorColor));
        }
        SetStatus(ErrorMessage, true);
        return;
    }

    if (!SavedRuns.IsValidIndex(SelectedRunIndex) || SavedRuns[SelectedRunIndex].RunId != RunId)
    {
        SetStatus(TEXT("FDOT comparison completed for the previously selected run."));
        return;
    }

    FString ResultText = FString::Printf(
        TEXT("COMPARISON SUMMARY\n"
             "Close matches: %d\n"
             "Needs review: %d\n"
             "Large differences: %d\n"
             "Compared road directions: %d\n\n"
             "Overall, %.1f%% of compared road directions were close to the FDOT-based hourly reference.\n\n"
             "DATA COVERAGE\n"
             "%.1f%% (%d of %d road directions) had an FDOT reference.\n"
             "%d road directions were not compared. FDOT data primarily covers monitored and major roads, so missing neighborhood roads are expected."),
        Summary.GoodEdges,
        Summary.ReviewEdges,
        Summary.PoorEdges,
        Summary.MatchedEdges,
        Summary.GoodPercent,
        Summary.CoveragePercent,
        Summary.MatchedEdges,
        Summary.TotalRoadDirections,
        Summary.UnmatchedRoadDirections
    );

    if (!Summary.TopRoadDifferences.IsEmpty())
    {
        ResultText += TEXT("\n\nTOP ROAD DIFFERENCES");
        for (int32 DifferenceIndex = 0; DifferenceIndex < Summary.TopRoadDifferences.Num(); ++DifferenceIndex)
        {
            const FTelemetryFDOTRoadDifference& Difference = Summary.TopRoadDifferences[DifferenceIndex];
            const bool bHigher = Difference.PercentDifference >= 0.0f;
            FString PlainResult = TEXT("Close match");
            if (Difference.Result == TEXT("Poor"))
            {
                PlainResult = TEXT("Large difference");
            }
            else if (Difference.Result == TEXT("Review"))
            {
                PlainResult = TEXT("Needs review");
            }

            ResultText += FString::Printf(
                TEXT("\n\n%d. %s\n"
                     "RoadMap: %.0f veh/hr  |  FDOT-based reference: %.0f veh/hr\n"
                     "RoadMap is %.1f%% %s  |  %s\n"
                     "Technical: GEH %.2f"),
                DifferenceIndex + 1,
                *Difference.RoadName,
                Difference.SimulationFlowVehPerHour,
                Difference.FDOTFlowVehPerHour,
                FMath::Abs(Difference.PercentDifference),
                bHigher ? TEXT("higher") : TEXT("lower"),
                *PlainResult,
                Difference.GEHScore
            );
        }
    }

    ResultText += FString::Printf(
        TEXT("\n\nTECHNICAL DETAILS\n"
             "Mean GEH: %.2f\n"
             "GEH is the difference score used for this check. Lower is closer; under 5 is treated as a close match. The FDOT hourly reference uses AADT and a K factor when available, then assumes an even 50/50 directional split."),
        Summary.MeanGEH
    );

    if (Summary.bPreliminary)
    {
        ResultText += TEXT(
            "\n\nPRELIMINARY RESULT\n"
            "This simulation is shorter than the recommended 15 minutes. Use this as an early reference check, not a final conclusion."
        );
    }

    if (FDOTValidationText)
    {
        FDOTValidationText->SetText(FText::FromString(ResultText));
        FDOTValidationText->SetColorAndOpacity(FSlateColor(
            Summary.bPreliminary ? AccentHover : Success
        ));
    }

    SetStatus(TEXT("FDOT comparison completed. Results were saved with this run."));
}

// Opens the FDOT reference map generated for the selected run.
// Opens the map created by the most recent FDOT comparison.
void UTelemetryPanelWidget::HandleViewFDOTHeatmapClicked()
{
    if (!SavedRuns.IsValidIndex(SelectedRunIndex))
    {
        SetStatus(TEXT("Select a saved run before viewing its FDOT comparison map."), true);
        return;
    }

    const FString PreviousMetric = SelectedMetric;
    const FString PreviousFocus = SelectedFocus;
    SelectedMetric = TEXT("fdot_geh_score");
    SelectedFocus = TEXT("all");
    HandleViewHeatmapClicked();
    SelectedMetric = PreviousMetric;
    SelectedFocus = PreviousFocus;
}

// Finds and opens the current run's selected SVG heatmap.
void UTelemetryPanelWidget::HandleViewHeatmapClicked()
{
    using namespace TelemetryPalette;
    if (!SavedRuns.IsValidIndex(SelectedRunIndex))
    {
        SetStatus(TEXT("Select a saved run before viewing a heatmap."), true);
        return;
    }

    if (SelectedMetric.IsEmpty())
    {
        SelectedMetric = TEXT("bottleneck_score");
    }

    FString HeatmapPath;
    FString ErrorMessage;
    if (!UTelemetryPanelBridge::GetGeneratedHeatmapPath(
            SavedRuns[SelectedRunIndex].RunId,
            SelectedMetric,
            SelectedFocus,
            HeatmapPath,
            ErrorMessage
        ))
    {
        SetStatus(ErrorMessage, true);
        return;
    }

    const FString Title = FString::Printf(
        TEXT("%s Heatmap  |  %s  |  %s"),
        *GetMetricDisplayName(SelectedMetric),
        *GetFocusDisplayName(SelectedFocus),
        *SavedRuns[SelectedRunIndex].CreatedAt
    );
    if (ComparisonHeatmapModeBar)
    {
        ComparisonHeatmapModeBar->SetVisibility(ESlateVisibility::Collapsed);
    }
    ResetHeatmapView();
    if (OpenHeatmapPath(HeatmapPath, Title, SelectedMetric))
    {
        SetStatus(TEXT("Heatmap loaded."));
    }
}

// Loads a map file and its display information into the full-screen viewer.
bool UTelemetryPanelWidget::OpenHeatmapPath(
    const FString& HeatmapPath,
    const FString& Title,
    const FString& Metric
)
{
    using namespace TelemetryPalette;

    // The SVG keeps roads and labels sharp. The JSON beside it gives Unreal the
    // title, legend, summary values, and road shapes used for mouse interaction.
    const FString SvgPath = HeatmapPath;
    FTelemetryHeatmapDisplayInfo DisplayInfo;
    FString DisplayInfoError;
    if (!FPaths::FileExists(SvgPath))
    {
        SetStatus(FString::Printf(TEXT("Heatmap SVG was not found: %s"), *SvgPath), true);
        return false;
    }
    if (!UTelemetryPanelBridge::GetHeatmapDisplayInfo(HeatmapPath, DisplayInfo, DisplayInfoError))
    {
        SetStatus(DisplayInfoError, true);
        return false;
    }
    CurrentHeatmapMetric = Metric;
    CurrentHeatmapDisplayInfo = FTelemetryHeatmapDisplayInfo();
    ExtremeHeatmapRoadIndex = INDEX_NONE;
    if (HeatmapExtremeMarker)
    {
        HeatmapExtremeMarker->SetVisibility(ESlateVisibility::Collapsed);
    }
    ClearHeatmapRoadInteraction();

    // Display the sharp SVG and build the extra panels from its JSON file.
    CurrentHeatmapDisplayInfo = DisplayInfo;
        const FSlateVectorImageBrush SvgBrush(
            SvgPath,
            FVector2D(1600.0f, 1128.0f)
        );
        HeatmapImage->SetBrush(SvgBrush);

        HeatmapSummaryCard->SetVisibility(ESlateVisibility::Visible);
        HeatmapLegendCard->SetVisibility(ESlateVisibility::Visible);
        HeatmapSummaryBox->ClearChildren();
        HeatmapLegendTicks->ClearChildren();

        HeatmapSummaryBox->AddChildToVerticalBox(MakeText(
            Metric == TEXT("comparison_delta")
                ? TEXT("RUN COMPARISON")
                : Metric == TEXT("fdot_geh_score")
                ? TEXT("FDOT COMPARISON")
                : TEXT("SIMULATION SUMMARY"),
            13,
            TextPrimary,
            FName("Bold"),
            60
        ));
        for (const FTelemetryHeatmapSummaryRow& Row : DisplayInfo.SummaryRows)
        {
            UTextBlock* Label = MakeText(Row.Label, 10, TextSecondary, FName("Medium"));
            if (UVerticalBoxSlot* LabelSlot = HeatmapSummaryBox->AddChildToVerticalBox(Label))
            {
                LabelSlot->SetPadding(FMargin(0.0f, 16.0f, 0.0f, 2.0f));
            }

            UTextBlock* Value = MakeText(Row.Value, 13, TextPrimary, FName("Bold"));
            Value->SetAutoWrapText(true);
            HeatmapSummaryBox->AddChildToVerticalBox(Value);
        }

        // Put a plain explanation below the numbers so the colors are not
        // mistaken for an official pass or fail result.
        UTextBlock* MeaningLabel = MakeText(TEXT("HOW TO READ THIS MAP"), 10, Accent, FName("Bold"));
        if (UVerticalBoxSlot* MeaningLabelSlot = HeatmapSummaryBox->AddChildToVerticalBox(MeaningLabel))
        {
            MeaningLabelSlot->SetPadding(FMargin(0.0f, 18.0f, 0.0f, 5.0f));
        }
        UTextBlock* MeaningText = MakeText(GetMetricHelpText(Metric), 10, TextSecondary, FName("Medium"));
        MeaningText->SetAutoWrapText(true);
        MeaningText->SetLineHeightPercentage(1.15f);
        HeatmapSummaryBox->AddChildToVerticalBox(MeaningText);

        HeatmapLegendLabel->SetText(FText::FromString(DisplayInfo.LegendLabel));
        UpdateHeatmapLegendGradient(DisplayInfo.LegendColorsTopToBottom);

        for (int32 TickIndex = 0; TickIndex < DisplayInfo.LegendTicksTopToBottom.Num(); ++TickIndex)
        {
            if (TickIndex > 0)
            {
                USpacer* TickSpacer = WidgetTree->ConstructWidget<USpacer>();
                if (UVerticalBoxSlot* SpacerSlot = HeatmapLegendTicks->AddChildToVerticalBox(TickSpacer))
                {
                    SpacerSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
                }
            }

            UTextBlock* Tick = MakeText(
                DisplayInfo.LegendTicksTopToBottom[TickIndex],
                11,
                TextPrimary,
                FName("Medium")
            );
            if (UVerticalBoxSlot* TickSlot = HeatmapLegendTicks->AddChildToVerticalBox(Tick))
            {
                TickSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
            }
        }

        if (HeatmapInteractionHint)
        {
            HeatmapInteractionHint->SetText(FText::FromString(
                DisplayInfo.Roads.IsEmpty()
                    ? TEXT("SCROLL TO ZOOM  |  DRAG TO PAN  |  REGENERATE FOR ROAD DETAILS")
                    : TEXT("HOVER ROAD  |  CLICK TO PIN  |  SCROLL TO ZOOM  |  DRAG TO PAN")
            ));
        }

    HeatmapTitleText->SetText(FText::FromString(Title));
    HeatmapViewer->SetVisibility(ESlateVisibility::Visible);
    SelectHeatmapExtremeRoad();
    // The image size becomes final shortly after the viewer appears.
    // Updating for three frames places the arrow correctly without doing layout work forever.
    HeatmapMarkerLayoutFramesRemaining = 3;
    return true;
}

// Hides the map viewer and returns to the telemetry tabs.
void UTelemetryPanelWidget::HandleCloseHeatmapClicked()
{
    HeatmapMarkerLayoutFramesRemaining = 0;
    ResetHeatmapView();
    HeatmapViewer->SetVisibility(ESlateVisibility::Collapsed);
}

// Moves the map one zoom step closer.
void UTelemetryPanelWidget::HandleZoomInClicked()
{
    using namespace TelemetryPalette;

    SetHeatmapZoom(HeatmapZoom + HeatmapZoomStep);
}

// Moves the map one zoom step farther away.
void UTelemetryPanelWidget::HandleZoomOutClicked()
{
    using namespace TelemetryPalette;

    SetHeatmapZoom(HeatmapZoom - HeatmapZoomStep);
}

// Returns the map to its original view.
void UTelemetryPanelWidget::HandleResetHeatmapViewClicked()
{
    ResetHeatmapView();
}

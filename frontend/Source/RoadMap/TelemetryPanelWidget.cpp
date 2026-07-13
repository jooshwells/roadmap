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
#include "Misc/Paths.h"
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

using namespace TelemetryPalette;

void UTelemetryRunButton::InitializeRow(int32 InRunIndex)
{
    RunIndex = InRunIndex;
    OnClicked.AddUniqueDynamic(this, &UTelemetryRunButton::HandleClicked);
}

void UTelemetryRunButton::HandleClicked()
{
    OnRunSelected.ExecuteIfBound(RunIndex);
}

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

UButton* UTelemetryPanelWidget::MakeActionButton(const FString& Label, bool bPrimary)
{
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

UTelemetryRunButton* UTelemetryPanelWidget::MakeRunRow(
    int32 RunIndex,
    const FTelemetryRunInfo& RunInfo
)
{
    UTelemetryRunButton* Row = WidgetTree->ConstructWidget<UTelemetryRunButton>();
    Row->InitializeRow(RunIndex);
    Row->OnRunSelected.BindUObject(this, &UTelemetryPanelWidget::HandleRunSelected);
    Row->SetStyle(RunRowStyle(false));

    UVerticalBox* Labels = WidgetTree->ConstructWidget<UVerticalBox>();
    Labels->AddChildToVerticalBox(MakeText(RunInfo.CreatedAt, 14, TextPrimary, FName("Medium")));

    const FString Summary = FString::Printf(
        TEXT("%d vehicles   |   %.1f mph average"),
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

void UTelemetryPanelWidget::BuildWidgetTree()
{
    UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
    WidgetTree->RootWidget = Canvas;

    UBorder* ScreenShade = WidgetTree->ConstructWidget<UBorder>();
    ScreenShade->SetBrush(FSlateColorBrush(Hex(TEXT("05070B"), 0.78f)));
    if (UCanvasPanelSlot* ShadeSlot = Canvas->AddChildToCanvas(ScreenShade))
    {
        ShadeSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
        ShadeSlot->SetOffsets(FMargin(0.0f));
    }

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

    UHorizontalBox* Body = WidgetTree->ConstructWidget<UHorizontalBox>();
    if (UVerticalBoxSlot* BodySlot = PanelColumn->AddChildToVerticalBox(Body))
    {
        BodySlot->SetPadding(FMargin(0.0f, 26.0f, 0.0f, 0.0f));
        BodySlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    }

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

    UBorder* DetailsCard = WidgetTree->ConstructWidget<UBorder>();
    DetailsCard->SetBrush(RoundedBrush(CardFill, 10.0f, Outline, 1.0f));
    DetailsCard->SetPadding(FMargin(22.0f));
    if (UHorizontalBoxSlot* DetailsSlot = Body->AddChildToHorizontalBox(DetailsCard))
    {
        DetailsSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    }

    UVerticalBox* DetailsColumn = WidgetTree->ConstructWidget<UVerticalBox>();
    DetailsCard->SetContent(DetailsColumn);

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

    MetricComboBox = WidgetTree->ConstructWidget<UComboBoxString>();
    MetricComboBox->AddOption(TEXT("Bottleneck Score"));
    MetricComboBox->AddOption(TEXT("Estimated Traffic Flow"));
    MetricComboBox->AddOption(TEXT("Average Speed"));
    MetricComboBox->AddOption(TEXT("Total Wait Added"));
    MetricComboBox->OnSelectionChanged.AddDynamic(
        this,
        &UTelemetryPanelWidget::HandleMetricSelectionChanged
    );
    MetricComboBox->SetSelectedOption(TEXT("Bottleneck Score"));
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

    FocusComboBox = WidgetTree->ConstructWidget<UComboBoxString>();
    FocusComboBox->AddOption(TEXT("All Roads"));
    FocusComboBox->AddOption(TEXT("Worst 25%"));
    FocusComboBox->AddOption(TEXT("Worst 10%"));
    FocusComboBox->AddOption(TEXT("Worst 5%"));
    FocusComboBox->OnSelectionChanged.AddDynamic(
        this,
        &UTelemetryPanelWidget::HandleFocusSelectionChanged
    );
    FocusComboBox->SetSelectedOption(TEXT("All Roads"));
    HeatmapsPanel->AddChildToVerticalBox(FocusComboBox);

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

    // FDOT validation uses plain language first and keeps GEH as a technical detail.
    UVerticalBox* FDOTPanel = WidgetTree->ConstructWidget<UVerticalBox>();
    WorkspaceSwitcher->AddChild(FDOTPanel);

    UTextBlock* FDOTLabel = MakeText(TEXT("FDOT VALIDATION"), 11, TextSecondary, FName("Medium"), 180);
    FDOTPanel->AddChildToVerticalBox(FDOTLabel);

    UTextBlock* FDOTHelp = MakeText(
        TEXT("Compare RoadMap traffic volumes with 2025 Florida Department of Transportation estimates. This helps show where the simulation is close to observed traffic and where it may need adjustment."),
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

    // Long validation results stay inside the tab instead of extending beyond the panel.
    UScrollBox* FDOTResultsScrollBox = WidgetTree->ConstructWidget<UScrollBox>();
    FDOTResultsScrollBox->AddChild(FDOTValidationText);
    if (UVerticalBoxSlot* FDOTTextSlot = FDOTPanel->AddChildToVerticalBox(FDOTResultsScrollBox))
    {
        FDOTTextSlot->SetPadding(FMargin(0.0f, 18.0f, 0.0f, 0.0f));
        FDOTTextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    }

    SetWorkspaceTab(0);

    StatusText = MakeText(TEXT("Select a saved run to begin."), 12, TextSecondary);
    StatusText->SetAutoWrapText(true);
    if (UVerticalBoxSlot* StatusSlot = PanelColumn->AddChildToVerticalBox(StatusText))
    {
        StatusSlot->SetPadding(FMargin(2.0f, 18.0f, 0.0f, 0.0f));
    }

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

    UBorder* ViewerHeaderFrame = WidgetTree->ConstructWidget<UBorder>();
    ViewerHeaderFrame->SetBrush(RoundedBrush(Hex(TEXT("101620")), 8.0f, Outline, 1.0f));
    ViewerHeaderFrame->SetPadding(FMargin(18.0f, 12.0f));
    if (UVerticalBoxSlot* HeaderFrameSlot = ViewerColumn->AddChildToVerticalBox(ViewerHeaderFrame))
    {
        HeaderFrameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
    }

    UHorizontalBox* ViewerHeader = WidgetTree->ConstructWidget<UHorizontalBox>();
    ViewerHeaderFrame->SetContent(ViewerHeader);
    HeatmapTitleText = MakeText(TEXT("Telemetry Heatmap"), 22, TextPrimary, FName("Bold"));
    if (UHorizontalBoxSlot* TitleSlot = ViewerHeader->AddChildToHorizontalBox(HeatmapTitleText))
    {
        TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        TitleSlot->SetVerticalAlignment(VAlign_Center);
    }

    UButton* CloseViewerButton = MakeActionButton(TEXT("Close Viewer"), false);
    CloseViewerButton->OnClicked.AddDynamic(this, &UTelemetryPanelWidget::HandleCloseHeatmapClicked);
    ViewerHeader->AddChildToHorizontalBox(CloseViewerButton);
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

    UBorder* ImageFrame = WidgetTree->ConstructWidget<UBorder>();
    ImageFrame->SetBrush(RoundedBrush(Hex(TEXT("05080D")), 8.0f));
    ImageFrame->SetPadding(FMargin(10.0f));

    // Limit the displayed PNG so Unreal does not spread it across the full viewport.
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

    UScaleBox* HeatmapScaleBox = WidgetTree->ConstructWidget<UScaleBox>();
    HeatmapScaleBox->SetStretch(EStretch::ScaleToFit);
    HeatmapScaleBox->SetStretchDirection(EStretchDirection::Both);
    ImageFrame->SetContent(HeatmapScaleBox);

    HeatmapImage = WidgetTree->ConstructWidget<UImage>();
    HeatmapScaleBox->SetContent(HeatmapImage);

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

void UTelemetryPanelWidget::RefreshRunList()
{
    if (!RunListBox)
    {
        return;
    }

    RunListBox->ClearChildren();
    RunRows.Empty();
    SavedRuns.Empty();
    SelectedRunIndex = INDEX_NONE;
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

    RunDetailsText->SetText(FText::FromString(TEXT("Select a saved run to view its telemetry summary.")));
    SetStatus(FString::Printf(
        TEXT("Loaded %d saved run%s for %s."),
        SavedRuns.Num(),
        SavedRuns.Num() == 1 ? TEXT("") : TEXT("s"),
        *ActiveMapName
    ));
}

void UTelemetryPanelWidget::RefreshSelectedRunDetails()
{
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

    const FString DetailsString = FString::Printf(
        TEXT("Vehicles\n%d\n\nDuration\n%.1f seconds\n\nRoad edges used\n%d\n\nAverage speed\n%.1f mph\n\nTotal wait added\n%.1f seconds\n\nMaximum wait\n%.1f seconds\n\nWorst bottleneck\n%s\nScore: %.2f"),
        Details.TotalVehicles,
        Details.SimulationDurationSeconds,
        Details.EdgesUsed,
        Details.AverageSpeedMph,
        Details.TotalWaitAddedSeconds,
        Details.MaximumWaitSeconds,
        *BottleneckRoad,
        Details.WorstBottleneckScore
    );

    RunDetailsText->SetText(FText::FromString(DetailsString));
    RunDetailsText->SetColorAndOpacity(FSlateColor(TextPrimary));
}

void UTelemetryPanelWidget::RefreshRunRowStyles()
{
    for (int32 RowIndex = 0; RowIndex < RunRows.Num(); ++RowIndex)
    {
        if (RunRows[RowIndex])
        {
            RunRows[RowIndex]->SetStyle(RunRowStyle(RowIndex == SelectedRunIndex));
        }
    }
}

FString UTelemetryPanelWidget::GetMetricId(const FString& DisplayName) const
{
    if (DisplayName == TEXT("Bottleneck Score"))
    {
        return TEXT("bottleneck_score");
    }

    if (DisplayName == TEXT("Estimated Traffic Flow"))
    {
        return TEXT("estimated_flow_veh_per_hr");
    }

    if (DisplayName == TEXT("Average Speed"))
    {
        return TEXT("avg_speed_mph");
    }

    if (DisplayName == TEXT("Total Wait Added"))
    {
        return TEXT("total_wait_added_s");
    }

    return TEXT("");
}

FString UTelemetryPanelWidget::GetMetricDisplayName(const FString& MetricId) const
{
    if (MetricId == TEXT("fdot_geh_score"))
    {
        return TEXT("FDOT Traffic Comparison");
    }

    if (MetricId == TEXT("estimated_flow_veh_per_hr"))
    {
        return TEXT("Estimated Traffic Flow");
    }

    if (MetricId == TEXT("avg_speed_mph"))
    {
        return TEXT("Average Speed");
    }

    if (MetricId == TEXT("total_wait_added_s"))
    {
        return TEXT("Total Wait Added");
    }

    return TEXT("Bottleneck Score");
}

// Explains each measurement without assuming prior traffic-engineering knowledge.
FString UTelemetryPanelWidget::GetMetricHelpText(const FString& MetricId) const
{
    if (MetricId == TEXT("estimated_flow_veh_per_hr"))
    {
        return TEXT("Estimated vehicles passing each road direction per hour. Higher flow means the road carried more traffic, but does not automatically mean it was congested.");
    }
    if (MetricId == TEXT("avg_speed_mph"))
    {
        return TEXT("Average recorded vehicle speed. Red highlights slower traffic and green highlights faster traffic; interpret it alongside the road's expected speed.");
    }
    if (MetricId == TEXT("total_wait_added_s"))
    {
        return TEXT("Additional waiting time accumulated by vehicles on each road direction. Red roads added the most delay during this run.");
    }
    return TEXT("A combined congestion indicator based on slow traffic and added waiting. Green is lower concern and red identifies the strongest bottleneck candidates.");
}

void UTelemetryPanelWidget::SetStatus(const FString& Message, bool bIsError)
{
    if (!StatusText)
    {
        return;
    }

    StatusText->SetText(FText::FromString(Message));
    StatusText->SetColorAndOpacity(FSlateColor(bIsError ? ErrorColor : Success));
}

FString UTelemetryPanelWidget::GetFocusId(const FString& DisplayName) const
{
    if (DisplayName == TEXT("Worst 25%")) return TEXT("worst_25");
    if (DisplayName == TEXT("Worst 10%")) return TEXT("worst_10");
    if (DisplayName == TEXT("Worst 5%")) return TEXT("worst_5");
    return TEXT("all");
}

FString UTelemetryPanelWidget::GetFocusDisplayName(const FString& FocusId) const
{
    if (FocusId == TEXT("worst_25")) return TEXT("Worst 25%");
    if (FocusId == TEXT("worst_10")) return TEXT("Worst 10%");
    if (FocusId == TEXT("worst_5")) return TEXT("Worst 5%");
    return TEXT("All Roads");
}

// Builds a small bilinear texture so the native legend changes color smoothly.
void UTelemetryPanelWidget::UpdateHeatmapLegendGradient(const TArray<FString>& ColorsTopToBottom)
{
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

// Shows one focused workspace instead of displaying every telemetry control at once.
void UTelemetryPanelWidget::SetWorkspaceTab(int32 TabIndex)
{
    ActiveWorkspaceTab = FMath::Clamp(TabIndex, 0, 2);
    if (WorkspaceSwitcher)
    {
        WorkspaceSwitcher->SetActiveWidgetIndex(ActiveWorkspaceTab);
    }
    RefreshWorkspaceTabStyles();
}

// Uses the amber treatment only on the currently active workspace tab.
void UTelemetryPanelWidget::RefreshWorkspaceTabStyles()
{
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
}

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

    for (UTelemetryRunButton* RunRow : RunRows)
    {
        if (RunRow)
        {
            RunRow->SetIsEnabled(!bIsRunning);
        }
    }
}

void UTelemetryPanelWidget::HandleRunSelected(int32 RunIndex)
{
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

void UTelemetryPanelWidget::HandleCloseClicked()
{
    RemoveFromParent();
}

void UTelemetryPanelWidget::HandleRefreshClicked()
{
    if (bTelemetryTaskRunning)
    {
        SetStatus(TEXT("Wait for the current telemetry task to finish before refreshing runs."));
        return;
    }

    RefreshRunList();
}

void UTelemetryPanelWidget::HandleOverviewTabClicked()
{
    SetWorkspaceTab(0);
    SetStatus(TEXT("Showing the selected run overview."));
}

void UTelemetryPanelWidget::HandleHeatmapsTabClicked()
{
    SetWorkspaceTab(1);
    SetStatus(TEXT("Choose a measurement and road focus to create a heatmap."));
}

void UTelemetryPanelWidget::HandleFDOTTabClicked()
{
    SetWorkspaceTab(2);
    SetStatus(TEXT("FDOT validation compares simulated traffic with real-world reference estimates."));
}

void UTelemetryPanelWidget::HandleMetricSelectionChanged(
    FString SelectedItem,
    ESelectInfo::Type SelectionType
)
{
    const FString MetricId = GetMetricId(SelectedItem);

    if (MetricId.IsEmpty())
    {
        SelectedMetric = TEXT("bottleneck_score");
        MetricComboBox->SetSelectedOption(TEXT("Bottleneck Score"));
        SetStatus(TEXT("The selected metric was invalid. Bottleneck Score was restored."), true);
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

    Async(EAsyncExecution::ThreadPool, [WeakThis, RunId, Metric, Focus]()
    {
        FString ResultJson;
        const bool bSucceeded = UTelemetryPanelBridge::GenerateSelectedHeatmap(
            RunId,
            Metric,
            Focus,
            ResultJson
        );

        AsyncTask(ENamedThreads::GameThread, [WeakThis, RunId, Metric, Focus, bSucceeded]()
        {
            if (WeakThis.IsValid())
            {
                WeakThis->FinishHeatmapGeneration(RunId, Metric, Focus, bSucceeded);
            }
        });
    });
}

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

// Runs and displays the map-specific FDOT validation for the selected run.
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

    Async(EAsyncExecution::ThreadPool, [WeakThis, RunId]()
    {
        FTelemetryFDOTValidationSummary Summary;
        FString ErrorMessage;
        const bool bSucceeded = UTelemetryPanelBridge::CompareSelectedRunWithFDOT(
            RunId,
            Summary,
            ErrorMessage
        );

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

void UTelemetryPanelWidget::FinishFDOTComparison(
    const FString& RunId,
    bool bSucceeded,
    const FTelemetryFDOTValidationSummary& Summary,
    const FString& ErrorMessage
)
{
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
             "Overall, %.1f%% of compared road directions closely matched the FDOT reference.\n\n"
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
                     "RoadMap: %.0f veh/hr  |  FDOT: %.0f veh/hr\n"
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
             "GEH is a standard traffic-model comparison measure. Lower is better; a score under 5 generally indicates a close match."),
        Summary.MeanGEH
    );

    if (Summary.bPreliminary)
    {
        ResultText += TEXT(
            "\n\nPRELIMINARY RESULT\n"
            "This simulation is shorter than the recommended 15 minutes. Use this result as an early indication, not a final validation."
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

// Opens the FDOT comparison map generated with the selected run's validation.
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

// Prefer the vector map but keep the PNG as a reliable fallback.
void UTelemetryPanelWidget::HandleViewHeatmapClicked()
{
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

    const FString SvgPath = FPaths::ChangeExtension(HeatmapPath, TEXT("svg"));
    FTelemetryHeatmapDisplayInfo DisplayInfo;
    FString DisplayInfoError;
    const bool bUseHybridViewer = FPaths::FileExists(SvgPath) &&
        UTelemetryPanelBridge::GetHeatmapDisplayInfo(HeatmapPath, DisplayInfo, DisplayInfoError);

    // Prefer the crisp vector map when its native Unreal display information is available.
    if (bUseHybridViewer)
    {
        LoadedHeatmapTexture = nullptr;
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
            SelectedMetric == TEXT("fdot_geh_score")
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
    }
    else
    {
        LoadedHeatmapTexture = UTelemetryPanelBridge::LoadHeatmapTexture(HeatmapPath, ErrorMessage);
        if (!LoadedHeatmapTexture)
        {
            SetStatus(ErrorMessage, true);
            return;
        }

        HeatmapImage->SetBrushFromTexture(LoadedHeatmapTexture, true);
        HeatmapSummaryCard->SetVisibility(ESlateVisibility::Collapsed);
        HeatmapLegendCard->SetVisibility(ESlateVisibility::Collapsed);
    }

    HeatmapTitleText->SetText(FText::FromString(FString::Printf(
        TEXT("%s Heatmap  |  %s  |  %s"),
        *GetMetricDisplayName(SelectedMetric),
        *GetFocusDisplayName(SelectedFocus),
        *SavedRuns[SelectedRunIndex].CreatedAt
    )));
    HeatmapViewer->SetVisibility(ESlateVisibility::Visible);
    SetStatus(TEXT("Heatmap loaded."));
}

void UTelemetryPanelWidget::HandleCloseHeatmapClicked()
{
    HeatmapViewer->SetVisibility(ESlateVisibility::Collapsed);
}

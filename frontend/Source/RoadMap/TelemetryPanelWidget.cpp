#include "TelemetryPanelWidget.h"

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
#include "Engine/Texture2D.h"
#include "Misc/Paths.h"
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

// Stores the row index and connects the normal button click event.
void UTelemetryRunButton::InitializeRow(int32 InRunIndex)
{
    RunIndex = InRunIndex;
    OnClicked.AddUniqueDynamic(this, &UTelemetryRunButton::HandleClicked);
}

// Passes this row's index back to the telemetry panel.
void UTelemetryRunButton::HandleClicked()
{
    OnRunSelected.ExecuteIfBound(RunIndex);
}

// Builds the widget tree the first time this widget is initialized.
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

// Creates consistently styled text for the telemetry interface.
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

// Creates a primary or secondary RoadMap action button.
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

// Creates one selectable row for a saved simulation run.
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

// Builds every visible panel and heatmap-viewer widget.
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
    DetailsColumn->AddChildToVerticalBox(MakeText(TEXT("RUN DETAILS"), 11, TextSecondary, FName("Medium"), 180));

    RunDetailsText = MakeText(TEXT("Select a saved run to view its telemetry summary."), 14, TextSecondary);
    RunDetailsText->SetAutoWrapText(true);
    RunDetailsText->SetLineHeightPercentage(1.35f);

    // Keep long summaries inside their own scrollable area so the controls stay visible.
    UScrollBox* DetailsScrollBox = WidgetTree->ConstructWidget<UScrollBox>();
    DetailsScrollBox->AddChild(RunDetailsText);
    if (UVerticalBoxSlot* DetailsTextSlot = DetailsColumn->AddChildToVerticalBox(DetailsScrollBox))
    {
        DetailsTextSlot->SetPadding(FMargin(0.0f, 16.0f, 0.0f, 0.0f));
        DetailsTextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    }

    UTextBlock* MetricLabel = MakeText(TEXT("HEATMAP METRIC"), 11, TextSecondary, FName("Medium"), 180);
    if (UVerticalBoxSlot* MetricLabelSlot = DetailsColumn->AddChildToVerticalBox(MetricLabel))
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
    DetailsColumn->AddChildToVerticalBox(MetricComboBox);

    UHorizontalBox* Actions = WidgetTree->ConstructWidget<UHorizontalBox>();
    if (UVerticalBoxSlot* ActionsSlot = DetailsColumn->AddChildToVerticalBox(Actions))
    {
        ActionsSlot->SetPadding(FMargin(0.0f, 16.0f, 0.0f, 0.0f));
    }

    UButton* GenerateButton = MakeActionButton(TEXT("Generate Heatmap"), true);
    GenerateButton->OnClicked.AddDynamic(this, &UTelemetryPanelWidget::HandleGenerateHeatmapClicked);
    if (UHorizontalBoxSlot* GenerateSlot = Actions->AddChildToHorizontalBox(GenerateButton))
    {
        GenerateSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        GenerateSlot->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 0.0f));
    }

    UButton* ViewButton = MakeActionButton(TEXT("View Heatmap"), false);
    ViewButton->OnClicked.AddDynamic(this, &UTelemetryPanelWidget::HandleViewHeatmapClicked);
    if (UHorizontalBoxSlot* ViewSlot = Actions->AddChildToHorizontalBox(ViewButton))
    {
        ViewSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    }

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

// Loads the saved runs directly through the C++ telemetry bridge.
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

    FString ErrorMessage;
    if (!UTelemetryPanelBridge::GetSavedRuns(SavedRuns, ErrorMessage))
    {
        RunListBox->AddChildToVerticalBox(MakeText(ErrorMessage, 12, ErrorColor, FName("Medium")));
        SetStatus(ErrorMessage, true);
        return;
    }

    if (SavedRuns.IsEmpty())
    {
        UTextBlock* EmptyText = MakeText(TEXT("No saved simulation runs were found."), 13, TextSecondary);
        EmptyText->SetAutoWrapText(true);
        RunListBox->AddChildToVerticalBox(EmptyText);
        SetStatus(TEXT("No saved telemetry runs are available."));
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
    SetStatus(FString::Printf(TEXT("Loaded %d saved run%s."), SavedRuns.Num(), SavedRuns.Num() == 1 ? TEXT("") : TEXT("s")));
}

// Loads and formats details for the currently selected run.
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

// Updates each run row so the selected row has the amber highlight.
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

// Converts a readable dropdown option into the Python metric ID.
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

// Converts the active metric ID into a readable heatmap title.
FString UTelemetryPanelWidget::GetMetricDisplayName(const FString& MetricId) const
{
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

// Displays a normal or error status message at the bottom of the panel.
void UTelemetryPanelWidget::SetStatus(const FString& Message, bool bIsError)
{
    if (!StatusText)
    {
        return;
    }

    StatusText->SetText(FText::FromString(Message));
    StatusText->SetColorAndOpacity(FSlateColor(bIsError ? ErrorColor : Success));
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

// Selects a saved run when its list row is clicked.
void UTelemetryPanelWidget::HandleRunSelected(int32 RunIndex)
{
    if (!SavedRuns.IsValidIndex(RunIndex))
    {
        SetStatus(TEXT("The selected telemetry run is no longer available."), true);
        return;
    }

    SelectedRunIndex = RunIndex;
    RefreshRunRowStyles();
    RefreshSelectedRunDetails();
    SetStatus(FString::Printf(TEXT("Selected %s."), *SavedRuns[RunIndex].CreatedAt));
}

// Closes the complete telemetry panel.
void UTelemetryPanelWidget::HandleCloseClicked()
{
    RemoveFromParent();
}

// Reloads the saved telemetry run folders.
void UTelemetryPanelWidget::HandleRefreshClicked()
{
    RefreshRunList();
}

// Keeps the readable metric option and internal metric ID synchronized.
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
    SetStatus(FString::Printf(TEXT("Selected metric: %s."), *SelectedItem));
}

// Launches Python to generate only the selected metric heatmap.
void UTelemetryPanelWidget::HandleGenerateHeatmapClicked()
{
    if (!SavedRuns.IsValidIndex(SelectedRunIndex))
    {
        SetStatus(TEXT("Select a saved run before generating a heatmap."), true);
        return;
    }

    if (SelectedMetric.IsEmpty())
    {
        SelectedMetric = TEXT("bottleneck_score");
    }

    SetStatus(TEXT("Generating the selected heatmap. This may take several seconds..."));

    FString ResultJson;
    if (!UTelemetryPanelBridge::GenerateSelectedHeatmap(
            SavedRuns[SelectedRunIndex].RunId,
            SelectedMetric,
            ResultJson
        ))
    {
        SetStatus(TEXT("Heatmap generation failed. Check the telemetry pipeline output."), true);
        return;
    }

    SetStatus(FString::Printf(
        TEXT("%s heatmap generated successfully."),
        *GetMetricDisplayName(SelectedMetric)
    ));
}

// Loads an existing heatmap PNG and opens the large viewer overlay.
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

        HeatmapSummaryBox->AddChildToVerticalBox(
            MakeText(TEXT("SIMULATION SUMMARY"), 13, TextPrimary, FName("Bold"), 60)
        );
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
        TEXT("%s Heatmap  |  %s"),
        *GetMetricDisplayName(SelectedMetric),
        *SavedRuns[SelectedRunIndex].CreatedAt
    )));
    HeatmapViewer->SetVisibility(ESlateVisibility::Visible);
    SetStatus(TEXT("Heatmap loaded."));
}

// Closes the large heatmap viewer without closing the telemetry panel.
void UTelemetryPanelWidget::HandleCloseHeatmapClicked()
{
    HeatmapViewer->SetVisibility(ESlateVisibility::Collapsed);
}

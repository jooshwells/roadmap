#include "VehicleStatsWidget.h"
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
#include "Components/ProgressBar.h"
#include "Components/Button.h"
#include "Styling/CoreStyle.h"
#include "RoadPanelStyle.h"

namespace
{
    // Live-state colours: what the car is doing right now.
    const FLinearColor AccelGreen(0.45f, 0.90f, 0.45f);
    const FLinearColor BrakeRed(1.00f, 0.45f, 0.45f);
    const FLinearColor StoppedAmber(1.00f, 0.75f, 0.35f);
    const FLinearColor CoastGrey(0.75f, 0.75f, 0.75f);
    const FLinearColor SpeedBarBlue(0.30f, 0.65f, 1.00f);
}

void UVehicleStatsWidget::InitWithStats(ASimulationManager* InSimManager, const FVehicleIDMStats& InStats)
{
    SimManager = InSimManager;
    Stats = InStats;
    bStale = false;
    PollAccumulator = 0.0f;

    if (BodyBox) BodyBox->SetRenderOpacity(1.0f);
    RefreshLiveFields();
    RefreshProfileFields();
}

TSharedRef<SWidget> UVehicleStatsWidget::RebuildWidget()
{
    // Build the tree once, before the underlying Slate widget is created.
    if (WidgetTree && !WidgetTree->RootWidget)
    {
        UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
        WidgetTree->RootWidget = Canvas;

        // Dark movable panel in the top-right corner: the road editor opens at
        // the right edge's vertical centre, so both can be open at once.
        // Visible border so clicks over the panel don't fall through to the map.
        UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
        Panel->SetBrushColor(FLinearColor(0.015f, 0.015f, 0.02f, 0.95f));
        Panel->SetPadding(FMargin(0.0f));
        Panel->SetVisibility(ESlateVisibility::Visible);

        PanelSlot = Canvas->AddChildToCanvas(Panel);
        PanelSlot->SetAnchors(FAnchors(1.0f, 0.0f, 1.0f, 0.0f));
        PanelSlot->SetAlignment(FVector2D(1.0f, 0.0f));
        PanelSlot->SetPosition(FVector2D(-40.0f, 120.0f));
        PanelSlot->SetAutoSize(true);

        // Fixed width so the panel doesn't resize as live numbers change length.
        USizeBox* Width = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("PanelWidth"));
        Width->SetWidthOverride(330.0f);
        Panel->SetContent(Width);

        UVerticalBox* Outer = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Outer"));
        Width->AddChild(Outer);

        // Title bar: drag handle for moving the window, plus the close button.
        TitleBar = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("TitleBar"));
        TitleBar->SetBrushColor(FLinearColor(0.06f, 0.06f, 0.09f, 1.0f));
        TitleBar->SetPadding(FMargin(18.0f, 10.0f, 10.0f, 10.0f));

        UHorizontalBox* TitleRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("TitleRow"));
        TitleBar->SetContent(TitleRow);

        HeaderText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Header"));
        HeaderText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 16));
        HeaderText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
        UHorizontalBoxSlot* HeaderSlot = TitleRow->AddChildToHorizontalBox(HeaderText);
        HeaderSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        HeaderSlot->SetVerticalAlignment(VAlign_Center);

        CloseButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Close"));
        CloseButton->SetBackgroundColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.10f));
        UTextBlock* CloseText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        CloseText->SetText(NSLOCTEXT("VehicleStats", "Close", "X"));
        CloseText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 11));
        CloseText->SetColorAndOpacity(FSlateColor(RoadPanelStyle::ControlText));
        CloseButton->AddChild(CloseText);
        UHorizontalBoxSlot* CloseSlot = TitleRow->AddChildToHorizontalBox(CloseButton);
        CloseSlot->SetVerticalAlignment(VAlign_Center);

        UVerticalBoxSlot* TitleSlot = Outer->AddChildToVerticalBox(TitleBar);
        TitleSlot->SetHorizontalAlignment(HAlign_Fill);

        BodyBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Body"));
        UVerticalBoxSlot* BodySlot = Outer->AddChildToVerticalBox(BodyBox);
        BodySlot->SetPadding(FMargin(18.0f, 12.0f, 18.0f, 18.0f));

        // Hero row: big current speed on the left, what the car is doing
        // (Accelerating / Braking / Cruising / Stopped) on the right.
        UHorizontalBox* HeroRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("HeroRow"));

        SpeedBigText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SpeedBig"));
        SpeedBigText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 26));
        SpeedBigText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
        UHorizontalBoxSlot* SpeedBigSlot = HeroRow->AddChildToHorizontalBox(SpeedBigText);
        SpeedBigSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        SpeedBigSlot->SetVerticalAlignment(VAlign_Bottom);

        StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Status"));
        StatusText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 12));
        UHorizontalBoxSlot* StatusSlot = HeroRow->AddChildToHorizontalBox(StatusText);
        StatusSlot->SetVerticalAlignment(VAlign_Bottom);
        StatusSlot->SetPadding(FMargin(8.0f, 0.0f, 0.0f, 4.0f));

        BodyBox->AddChildToVerticalBox(HeroRow);

        SpeedSubText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SpeedSub"));
        SpeedSubText->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 10));
        SpeedSubText->SetColorAndOpacity(FSlateColor(RoadPanelStyle::StatusLabel));
        UVerticalBoxSlot* SpeedSubSlot = BodyBox->AddChildToVerticalBox(SpeedSubText);
        SpeedSubSlot->SetPadding(FMargin(0.0f, 2.0f, 0.0f, 6.0f));

        // How close the car is to the speed it wants to hold.
        SpeedBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("SpeedBar"));
        SpeedBar->SetFillColorAndOpacity(SpeedBarBlue);
        USizeBox* SpeedBarHeight = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
        SpeedBarHeight->SetHeightOverride(8.0f);
        SpeedBarHeight->AddChild(SpeedBar);
        UVerticalBoxSlot* SpeedBarSlot = BodyBox->AddChildToVerticalBox(SpeedBarHeight);
        SpeedBarSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));
        SpeedBarSlot->SetHorizontalAlignment(HAlign_Fill);

        AccelValue = AddValueRow(BodyBox, NSLOCTEXT("VehicleStats", "Accel", "Acceleration"));
        LaneValue = AddValueRow(BodyBox, NSLOCTEXT("VehicleStats", "Lane", "Lane"));
        LimitValue = AddValueRow(BodyBox, NSLOCTEXT("VehicleStats", "Limit", "Road speed limit"));
        WaitValue = AddValueRow(BodyBox, NSLOCTEXT("VehicleStats", "Wait", "Time stopped"));
        RouteValue = AddValueRow(BodyBox, NSLOCTEXT("VehicleStats", "Route", "Route progress"));

        RouteBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("RouteBar"));
        RouteBar->SetFillColorAndOpacity(RoadPanelStyle::ControlText);
        USizeBox* RouteBarHeight = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
        RouteBarHeight->SetHeightOverride(5.0f);
        RouteBarHeight->AddChild(RouteBar);
        UVerticalBoxSlot* RouteBarSlot = BodyBox->AddChildToVerticalBox(RouteBarHeight);
        RouteBarSlot->SetPadding(FMargin(0.0f, 2.0f, 0.0f, 0.0f));
        RouteBarSlot->SetHorizontalAlignment(HAlign_Fill);

        // The driver's fixed IDM/MOBIL parameters -- who this driver is,
        // as opposed to what they're doing right now.
        AddSectionHeader(BodyBox, NSLOCTEXT("VehicleStats", "Profile", "Driver profile (IDM)"));
        DesiredSpeedValue = AddValueRow(BodyBox, NSLOCTEXT("VehicleStats", "Desired", "Desired speed"));
        MaxAccelValue = AddValueRow(BodyBox, NSLOCTEXT("VehicleStats", "MaxAccel", "Max acceleration"));
        BrakeValue = AddValueRow(BodyBox, NSLOCTEXT("VehicleStats", "Brake", "Comfortable braking"));
        MinGapValue = AddValueRow(BodyBox, NSLOCTEXT("VehicleStats", "MinGap", "Min gap"));
        HeadwayValue = AddValueRow(BodyBox, NSLOCTEXT("VehicleStats", "Headway", "Time headway"));
        AccelExpValue = AddValueRow(BodyBox, NSLOCTEXT("VehicleStats", "AccelExp", "Accel exponent"));
        PolitenessValue = AddValueRow(BodyBox, NSLOCTEXT("VehicleStats", "Politeness", "Politeness"));
    }

    return Super::RebuildWidget();
}

void UVehicleStatsWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (CloseButton) CloseButton->OnClicked.AddUniqueDynamic(this, &UVehicleStatsWidget::HandleCloseClicked);

    if (TitleBar)
    {
        TitleBar->OnMouseButtonDownEvent.BindUFunction(this, FName("HandleTitleBarMouseDown"));
        TitleBar->OnMouseMoveEvent.BindUFunction(this, FName("HandleTitleBarMouseMove"));
        TitleBar->OnMouseButtonUpEvent.BindUFunction(this, FName("HandleTitleBarMouseUp"));
    }

    RefreshLiveFields();
    RefreshProfileFields();
}

void UVehicleStatsWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    if (bStale) return;

    PollAccumulator += InDeltaTime;
    if (PollAccumulator < PollInterval) return;
    PollAccumulator = 0.0f;

    FVehicleIDMStats Fresh;
    if (SimManager.IsValid() && SimManager->GetVehicleStatsByID(Stats.VehicleID, Fresh))
    {
        Stats = Fresh;
        RefreshLiveFields();
    }
    else
    {
        EnterStaleState();
    }
}

void UVehicleStatsWidget::RefreshLiveFields()
{
    // Called both from InitWithStats (which can run before the tree is built)
    // and from NativeConstruct (after it is) -- whichever comes last sees the
    // controls and wins.
    if (!HeaderText || !SpeedBigText) return;

    HeaderText->SetText(FText::FromString(FString::Printf(TEXT("Vehicle %d"), Stats.VehicleID)));

    SpeedBigText->SetText(FText::FromString(FString::Printf(TEXT("%.0f mph"), Stats.CurrentSpeed * MpsToMph)));
    SpeedSubText->SetText(FText::FromString(FString::Printf(TEXT("%.1f m/s   -   wants %.0f mph"),
        Stats.CurrentSpeed, Stats.DesiredSpeed * MpsToMph)));
    SpeedBar->SetPercent(Stats.DesiredSpeed > KINDA_SMALL_NUMBER
        ? FMath::Clamp(Stats.CurrentSpeed / Stats.DesiredSpeed, 0.0f, 1.0f) : 0.0f);

    // Colour the status + acceleration by what the car is doing.
    const float Accel = Stats.CurrentAcceleration;
    FLinearColor StateColor = CoastGrey;
    FText StateWord = NSLOCTEXT("VehicleStats", "Cruising", "Cruising");
    if (Stats.CurrentSpeed < 0.3f && Accel <= 0.05f)
    {
        StateColor = StoppedAmber;
        StateWord = NSLOCTEXT("VehicleStats", "StoppedState", "Stopped");
    }
    else if (Accel > 0.15f)
    {
        StateColor = AccelGreen;
        StateWord = NSLOCTEXT("VehicleStats", "Accelerating", "Accelerating");
    }
    else if (Accel < -0.15f)
    {
        StateColor = BrakeRed;
        StateWord = NSLOCTEXT("VehicleStats", "Braking", "Braking");
    }
    StatusText->SetText(StateWord);
    StatusText->SetColorAndOpacity(FSlateColor(StateColor));
    AccelValue->SetText(FText::FromString(FString::Printf(TEXT("%+.2f m/s\u00B2"), Accel)));
    AccelValue->SetColorAndOpacity(FSlateColor(StateColor));

    LaneValue->SetText(FText::AsNumber(Stats.Lane + 1)); // 0-based in the sim

    LimitValue->SetText(Stats.RoadSpeedLimit > KINDA_SMALL_NUMBER
        ? FText::FromString(FString::Printf(TEXT("%.0f mph"), Stats.RoadSpeedLimit * MpsToMph))
        : NSLOCTEXT("VehicleStats", "NoLimit", "-"));

    WaitValue->SetText(FText::FromString(FString::Printf(TEXT("%.1f s"), Stats.WaitTime)));
    WaitValue->SetColorAndOpacity(FSlateColor(Stats.WaitTime > 3.0f ? StoppedAmber : RoadPanelStyle::ControlText));

    // Route progress: index counts nodes reached out of RouteLength total,
    // i.e. RouteLength - 1 edges to travel.
    if (Stats.RouteLength > 1)
    {
        const float Fraction = FMath::Clamp(
            static_cast<float>(Stats.RouteIndex) / static_cast<float>(Stats.RouteLength - 1), 0.0f, 1.0f);
        RouteValue->SetText(FText::FromString(FString::Printf(TEXT("%.0f%%"), Fraction * 100.0f)));
        RouteBar->SetPercent(Fraction);
    }
    else
    {
        RouteValue->SetText(NSLOCTEXT("VehicleStats", "NoRoute", "-"));
        RouteBar->SetPercent(0.0f);
    }
}

void UVehicleStatsWidget::RefreshProfileFields()
{
    if (!DesiredSpeedValue) return;

    DesiredSpeedValue->SetText(FText::FromString(FString::Printf(TEXT("%.0f mph  (%.1f m/s)"),
        Stats.DesiredSpeed * MpsToMph, Stats.DesiredSpeed)));
    MaxAccelValue->SetText(FText::FromString(FString::Printf(TEXT("%.2f m/s\u00B2"), Stats.MaxAcceleration)));
    BrakeValue->SetText(FText::FromString(FString::Printf(TEXT("%.2f m/s\u00B2"), Stats.SafeBrakePower)));
    MinGapValue->SetText(FText::FromString(FString::Printf(TEXT("%.1f m"), Stats.MinGap)));
    HeadwayValue->SetText(FText::FromString(FString::Printf(TEXT("%.1f s"), Stats.SafeTimeHeadway)));
    AccelExpValue->SetText(FText::FromString(FString::Printf(TEXT("%.1f"), Stats.AccelerationExponent)));
    PolitenessValue->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), Stats.Politeness)));
}

void UVehicleStatsWidget::EnterStaleState()
{
    bStale = true;

    if (HeaderText)
    {
        HeaderText->SetText(FText::FromString(FString::Printf(TEXT("Vehicle %d - left the map"), Stats.VehicleID)));
    }
    if (StatusText)
    {
        StatusText->SetText(NSLOCTEXT("VehicleStats", "Gone", "Despawned"));
        StatusText->SetColorAndOpacity(FSlateColor(RoadPanelStyle::StatusLabel));
    }
    // Last-known values stay visible but clearly read as history.
    if (BodyBox) BodyBox->SetRenderOpacity(0.45f);
}

UTextBlock* UVehicleStatsWidget::AddValueRow(UVerticalBox* Parent, const FText& Label)
{
    UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

    UTextBlock* LabelText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
    LabelText->SetText(Label);
    LabelText->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 11));
    LabelText->SetColorAndOpacity(FSlateColor(RoadPanelStyle::RowLabel));

    UHorizontalBoxSlot* LabelSlot = Row->AddChildToHorizontalBox(LabelText);
    LabelSlot->SetPadding(FMargin(0.0f, 0.0f, 14.0f, 0.0f));
    LabelSlot->SetVerticalAlignment(VAlign_Center);
    LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

    UTextBlock* ValueText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
    ValueText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 11));
    ValueText->SetColorAndOpacity(FSlateColor(RoadPanelStyle::ControlText));
    ValueText->SetJustification(ETextJustify::Right);

    UHorizontalBoxSlot* ValueSlot = Row->AddChildToHorizontalBox(ValueText);
    ValueSlot->SetVerticalAlignment(VAlign_Center);

    UVerticalBoxSlot* RowSlot = Parent->AddChildToVerticalBox(Row);
    RowSlot->SetPadding(FMargin(0.0f, 3.0f));

    return ValueText;
}

void UVehicleStatsWidget::AddSectionHeader(UVerticalBox* Parent, const FText& Label)
{
    UTextBlock* Header = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
    Header->SetText(Label);
    Header->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 10));
    Header->SetColorAndOpacity(FSlateColor(RoadPanelStyle::StatusLabel));

    UVerticalBoxSlot* HeaderSlot = Parent->AddChildToVerticalBox(Header);
    HeaderSlot->SetPadding(FMargin(0.0f, 14.0f, 0.0f, 2.0f));
}

void UVehicleStatsWidget::HandleCloseClicked()
{
    RemoveFromParent();
}

FEventReply UVehicleStatsWidget::HandleTitleBarMouseDown(FGeometry /*MyGeometry*/, const FPointerEvent& MouseEvent)
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

FEventReply UVehicleStatsWidget::HandleTitleBarMouseMove(FGeometry /*MyGeometry*/, const FPointerEvent& MouseEvent)
{
    if (!bDraggingWindow || !PanelSlot) return UWidgetBlueprintLibrary::Unhandled();

    // Screen-space delta -> canvas units (viewport scale covers DPI scaling).
    const float Scale = FMath::Max(UWidgetLayoutLibrary::GetViewportScale(this), KINDA_SMALL_NUMBER);
    PanelSlot->SetPosition(DragStartPanelPos + (MouseEvent.GetScreenSpacePosition() - DragStartScreenPos) / Scale);
    return UWidgetBlueprintLibrary::Handled();
}

FEventReply UVehicleStatsWidget::HandleTitleBarMouseUp(FGeometry /*MyGeometry*/, const FPointerEvent& MouseEvent)
{
    if (!bDraggingWindow || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
    {
        return UWidgetBlueprintLibrary::Unhandled();
    }

    bDraggingWindow = false;
    FEventReply Reply = UWidgetBlueprintLibrary::Handled();
    return UWidgetBlueprintLibrary::ReleaseMouseCapture(Reply);
}

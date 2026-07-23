#include "IntersectionInspectorWidget.h"
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
#include "Components/Button.h"
#include "Styling/CoreStyle.h"
#include "RoadPanelStyle.h"

namespace
{
    const FLinearColor LiveGreen(0.45f, 0.90f, 0.45f);
    const FLinearColor WarnOrange(1.00f, 0.65f, 0.25f);
}

void UIntersectionInspectorWidget::InitWithInfo(ASimulationManager* InSimManager, const FIntersectionNodeInfo& InInfo)
{
    // Retargeting at another intersection cancels a close queued this frame.
    bPendingClose = false;

    SimManager = InSimManager;
    Info = InInfo;

    RefreshStaticFields();
}

TSharedRef<SWidget> UIntersectionInspectorWidget::RebuildWidget()
{
    // Build the tree once, before the underlying Slate widget is created.
    if (WidgetTree && !WidgetTree->RootWidget)
    {
        UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
        WidgetTree->RootWidget = Canvas;

        // Dark movable panel on the left edge, clear of the vehicle stats
        // panel (top-right) and the road editor (right-center) so all three
        // can be open at once. Visible border so clicks don't fall through.
        UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
        Panel->SetBrushColor(FLinearColor(0.015f, 0.015f, 0.02f, 0.95f));
        Panel->SetPadding(FMargin(0.0f));
        Panel->SetVisibility(ESlateVisibility::Visible);

        PanelSlot = Canvas->AddChildToCanvas(Panel);
        PanelSlot->SetAnchors(FAnchors(0.0f, 0.0f, 0.0f, 0.0f));
        PanelSlot->SetAlignment(FVector2D(0.0f, 0.0f));
        PanelSlot->SetPosition(FVector2D(40.0f, 120.0f));
        PanelSlot->SetAutoSize(true);

        // Fixed width so live numbers changing length don't resize the panel.
        USizeBox* Width = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("PanelWidth"));
        Width->SetWidthOverride(360.0f);
        Panel->SetContent(Width);

        UVerticalBox* Outer = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Outer"));
        Width->AddChild(Outer);

        // Title bar: drag handle plus the close button.
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
        CloseText->SetText(NSLOCTEXT("IntersectionInspector", "Close", "X"));
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

        ControlValue = AddValueRow(BodyBox, NSLOCTEXT("IntersectionInspector", "Control", "Traffic control"));
        GeometryValue = AddValueRow(BodyBox, NSLOCTEXT("IntersectionInspector", "Geometry", "Roads in / out"));
        MaxLanesValue = AddValueRow(BodyBox, NSLOCTEXT("IntersectionInspector", "MaxLanes", "Widest road"));
        SetbackValue = AddValueRow(BodyBox, NSLOCTEXT("IntersectionInspector", "Setback", "Junction box radius"));

        AddSectionHeader(BodyBox, NSLOCTEXT("IntersectionInspector", "Approaches", "Feeding roads"));
        ApproachBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Approaches"));
        BodyBox->AddChildToVerticalBox(ApproachBox);
    }

    return Super::RebuildWidget();
}

void UIntersectionInspectorWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (CloseButton) CloseButton->OnClicked.AddUniqueDynamic(this, &UIntersectionInspectorWidget::HandleCloseClicked);

    if (TitleBar)
    {
        TitleBar->OnMouseButtonDownEvent.BindUFunction(this, FName("HandleTitleBarMouseDown"));
        TitleBar->OnMouseMoveEvent.BindUFunction(this, FName("HandleTitleBarMouseMove"));
        TitleBar->OnMouseButtonUpEvent.BindUFunction(this, FName("HandleTitleBarMouseUp"));
    }

    RefreshStaticFields();
}

void UIntersectionInspectorWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    // World-space overlay every frame: setback circle + stop-line bars.
    // (The panel text is topology-only, so nothing needs re-polling; road
    // edits close the panel.)
    if (SimManager.IsValid())
    {
        SimManager->DrawIntersectionDebug(Info.NodeId);
    }

    // Deferred from the X button: a widget must not tear its own Slate subtree
    // down while that subtree is still dispatching the click.
    if (bPendingClose)
    {
        bPendingClose = false;
        RemoveFromParent();
    }
}

void UIntersectionInspectorWidget::RefreshStaticFields()
{
    // Called both from InitWithInfo (which can run before the tree is built)
    // and from NativeConstruct (after it is) -- whichever comes last wins.
    if (!HeaderText || !ControlValue || !ApproachBox) return;

    HeaderText->SetText(FText::FromString(FString::Printf(TEXT("Intersection %lld"), Info.NodeId)));

    ControlValue->SetText(FText::FromString(Info.ControlType));
    ControlValue->SetColorAndOpacity(FSlateColor(
        Info.ControlType == TEXT("Uncontrolled") ? RoadPanelStyle::ControlText : LiveGreen));

    GeometryValue->SetText(FText::FromString(FString::Printf(TEXT("%d in / %d out%s"),
        Info.IncomingCount, Info.OutgoingCount,
        Info.bIsIntersection ? TEXT("") : TEXT("  (not a junction)"))));
    // A controlled node that is not a geometric intersection gets a setback
    // of zero: its stop line lands on the node itself, which is exactly the
    // "sign in a weird place / car stops mid-junction" symptom.
    if (!Info.bIsIntersection && Info.ControlType != TEXT("Uncontrolled"))
    {
        GeometryValue->SetColorAndOpacity(FSlateColor(WarnOrange));
    }
    else
    {
        GeometryValue->SetColorAndOpacity(FSlateColor(RoadPanelStyle::ControlText));
    }

    MaxLanesValue->SetText(FText::FromString(FString::Printf(TEXT("%d lanes"), Info.MaxLanesAtNode)));
    SetbackValue->SetText(FText::FromString(FString::Printf(TEXT("%.1f m"), Info.SetbackMeters)));

    // The per-road block: one line per feeding road -- just its number, lane
    // count, and speed limit. Anything deeper (turn maps, stop lines) lives in
    // the road editor panel and the world overlay.
    //
    // Rows are REUSED, never cleared and rebuilt. This panel is retargeted on
    // every intersection click, and tearing the subtree down each time churns
    // Slate's widget list -- the fault commit 0bfc072 fixed in the road panels'
    // RebuildTurnLaneCombos, which shows up as an access violation in a later
    // window prepass (e.g. when the Road Tools panel is opened). Rows are only
    // ever added, never removed: surplus ones are collapsed, so retargeting
    // between intersections is zero add/remove operations.
    const int32 WantedRows = Info.Approaches.Num();
    while (ApproachRows.Num() < WantedRows)
    {
        UTextBlock* Row = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        Row->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 11));
        Row->SetColorAndOpacity(FSlateColor(RoadPanelStyle::ControlText));
        if (UVerticalBoxSlot* RowSlot = ApproachBox->AddChildToVerticalBox(Row))
        {
            RowSlot->SetPadding(FMargin(0.0f, 5.0f, 0.0f, 0.0f));
        }
        ApproachRows.Add(Row);
    }

    for (int32 RowIdx = 0; RowIdx < ApproachRows.Num(); RowIdx++)
    {
        UTextBlock* Row = ApproachRows[RowIdx];
        if (!Row) continue;

        if (RowIdx < WantedRows)
        {
            const FIntersectionApproachInfo& A = Info.Approaches[RowIdx];
            Row->SetText(FText::FromString(FString::Printf(TEXT("Road %lld   -   %d lane%s @ %.0f mph"),
                A.RoadId, A.Lanes, A.Lanes == 1 ? TEXT("") : TEXT("s"),
                A.SpeedLimitMps * MpsToMph)));
            Row->SetVisibility(ESlateVisibility::Visible);
        }
        else
        {
            Row->SetVisibility(ESlateVisibility::Collapsed);
        }
    }

    // Built once on the first junction that has no approaches, then reused.
    if (WantedRows == 0 && !NoApproachesText)
    {
        NoApproachesText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        NoApproachesText->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 10));
        NoApproachesText->SetColorAndOpacity(FSlateColor(RoadPanelStyle::RowLabel));
        NoApproachesText->SetText(NSLOCTEXT("IntersectionInspector", "NoApproaches", "No incoming roads."));
        ApproachBox->AddChildToVerticalBox(NoApproachesText);
    }
    if (NoApproachesText)
    {
        NoApproachesText->SetVisibility(WantedRows == 0 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    }
}

UTextBlock* UIntersectionInspectorWidget::AddValueRow(UVerticalBox* Parent, const FText& Label)
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

void UIntersectionInspectorWidget::AddSectionHeader(UVerticalBox* Parent, const FText& Label)
{
    UTextBlock* Header = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
    Header->SetText(Label);
    Header->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 10));
    Header->SetColorAndOpacity(FSlateColor(RoadPanelStyle::StatusLabel));

    UVerticalBoxSlot* HeaderSlot = Parent->AddChildToVerticalBox(Header);
    HeaderSlot->SetPadding(FMargin(0.0f, 14.0f, 0.0f, 2.0f));
}

void UIntersectionInspectorWidget::HandleCloseClicked()
{
    OnClosed.Broadcast();
    bPendingClose = true; // closed on the next tick, not mid-click
}

FEventReply UIntersectionInspectorWidget::HandleTitleBarMouseDown(FGeometry /*MyGeometry*/, const FPointerEvent& MouseEvent)
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

FEventReply UIntersectionInspectorWidget::HandleTitleBarMouseMove(FGeometry /*MyGeometry*/, const FPointerEvent& MouseEvent)
{
    if (!bDraggingWindow || !PanelSlot) return UWidgetBlueprintLibrary::Unhandled();

    // Screen-space delta -> canvas units (viewport scale covers DPI scaling).
    const float Scale = FMath::Max(UWidgetLayoutLibrary::GetViewportScale(this), KINDA_SMALL_NUMBER);
    PanelSlot->SetPosition(DragStartPanelPos + (MouseEvent.GetScreenSpacePosition() - DragStartScreenPos) / Scale);
    return UWidgetBlueprintLibrary::Handled();
}

FEventReply UIntersectionInspectorWidget::HandleTitleBarMouseUp(FGeometry /*MyGeometry*/, const FPointerEvent& MouseEvent)
{
    if (!bDraggingWindow || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
    {
        return UWidgetBlueprintLibrary::Unhandled();
    }

    bDraggingWindow = false;
    FEventReply Reply = UWidgetBlueprintLibrary::Handled();
    return UWidgetBlueprintLibrary::ReleaseMouseCapture(Reply);
}

#include "SimControlBarWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "Components/WidgetSwitcherSlot.h"
#include "Kismet/GameplayStatics.h"
#include "MainMenuWidget.h" // URoadmapRowButton
#include "MenuPalette.h"
#include "SimulationManager.h"
#include "Styling/CoreStyle.h"

namespace
{
	constexpr float SpeedPresets[] = { 0.5f, 1.0f, 2.0f, 4.0f };
	// \u00D7 is the multiplication sign for the "2x"-style labels.
	const TCHAR* SpeedLabels[] = { TEXT("0.5\u00D7"), TEXT("1\u00D7"), TEXT("2\u00D7"), TEXT("4\u00D7") };

	// The menu's action buttons, repadded down to transport-control size.
	FButtonStyle TransportButtonStyle(bool bPrimary)
	{
		FButtonStyle Style = MenuPalette::ActionButtonStyle(bPrimary);
		Style.SetNormalPadding(FMargin(14.0f, 8.0f))
			.SetPressedPadding(FMargin(14.0f, 9.0f, 14.0f, 7.0f));
		return Style;
	}

	// Speed pills: quiet text until hovered; the active preset gets the same
	// soft amber fill + outline as a selected menu list row.
	FButtonStyle SpeedPillStyle(bool bSelected)
	{
		using namespace MenuPalette;
		FButtonStyle Style;
		if (bSelected)
		{
			Style.SetNormal(RoundedBrush(AccentFillSoft, 8.0f, Accent, 1.0f))
				.SetHovered(RoundedBrush(AccentFillSoft, 8.0f, AccentHover, 1.0f))
				.SetPressed(RoundedBrush(AccentFillSoft, 8.0f, AccentPressed, 1.0f))
				.SetNormalForeground(AccentHover)
				.SetHoveredForeground(AccentHover)
				.SetPressedForeground(AccentPressed);
		}
		else
		{
			Style.SetNormal(RoundedBrush(FLinearColor::Transparent, 8.0f))
				.SetHovered(RoundedBrush(CardFillHover, 8.0f, OutlineHover, 1.0f))
				.SetPressed(RoundedBrush(AccentFillSoft, 8.0f, Accent, 1.0f))
				.SetNormalForeground(TextSecondary)
				.SetHoveredForeground(TextPrimary)
				.SetPressedForeground(AccentHover);
		}
		Style.SetDisabled(Style.Normal);
		Style.SetDisabledForeground(TextDisabled);
		Style.SetNormalPadding(FMargin(10.0f, 6.0f)).SetPressedPadding(FMargin(10.0f, 7.0f, 10.0f, 5.0f));
		return Style;
	}
}

TSharedRef<SWidget> USimControlBarWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		using namespace MenuPalette;

		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
		WidgetTree->RootWidget = Canvas;

		auto MakeLabel = [&](const FString& Text, const FName& Typeface, int32 Size, const FLinearColor& Color)
		{
			UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			Block->SetFont(FCoreStyle::GetDefaultFontStyle(Typeface, Size));
			Block->SetText(FText::FromString(Text));
			Block->SetColorAndOpacity(FSlateColor(Color));
			return Block;
		};

		// Icons are built from Borders / glyphs sized here so the play and stop
		// buttons come out the same width and the bar never shifts on toggle.
		auto WrapIcon = [&](UWidget* Icon)
		{
			USizeBox* Sizer = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			Sizer->SetWidthOverride(22.0f);
			Sizer->SetHeightOverride(18.0f);
			Sizer->AddChild(Icon);
			if (USizeBoxSlot* IconSlot = Cast<USizeBoxSlot>(Icon->Slot))
			{
				IconSlot->SetHorizontalAlignment(HAlign_Center);
				IconSlot->SetVerticalAlignment(VAlign_Center);
			}
			return Sizer;
		};

		// Near-opaque asphalt card so the bar stays readable over the map.
		FLinearColor BarFill = Background;
		BarFill.A = 0.92f;
		UBorder* Bar = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Bar"));
		Bar->SetBrush(RoundedBrush(BarFill, 14.0f, Outline, 1.0f));
		Bar->SetPadding(FMargin(10.0f, 8.0f));

		UCanvasPanelSlot* BarSlot = Canvas->AddChildToCanvas(Bar);
		BarSlot->SetAnchors(FAnchors(0.5f, 0.0f, 0.5f, 0.0f));
		BarSlot->SetAlignment(FVector2D(0.5f, 0.0f));
		BarSlot->SetPosition(FVector2D(0.0f, 18.0f));
		BarSlot->SetAutoSize(true);

		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Row"));
		Bar->SetContent(Row);

		auto AddCell = [&](UWidget* Widget, float LeftPad)
		{
			UHorizontalBoxSlot* CellSlot = Row->AddChildToHorizontalBox(Widget);
			CellSlot->SetPadding(FMargin(LeftPad, 0.0f, 0.0f, 0.0f));
			CellSlot->SetVerticalAlignment(VAlign_Center);
		};

		auto AddSeparator = [&]()
		{
			UBorder* Line = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
			Line->SetBrush(RoundedBrush(Outline, 1.0f));
			USizeBox* Sizer = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			Sizer->SetWidthOverride(1.0f);
			Sizer->SetHeightOverride(20.0f);
			Sizer->AddChild(Line);
			AddCell(Sizer, 12.0f);
		};

		// --- Play / pause (primary amber) -----------------------------------
		PlayPauseButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("PlayPause"));
		PlayPauseButton->SetStyle(TransportButtonStyle(true));

		PlayPauseIcon = WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass(), TEXT("PlayPauseIcon"));
		// \u25B6 is the black right-pointing "play" triangle.
		PlayPauseIcon->AddChild(MakeLabel(TEXT("\u25B6"), FName("Bold"), 14, TextOnAccent));

		// Pause icon: two rounded bars, echoing the menu's dashed-line motif.
		UHorizontalBox* PauseBars = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		for (int32 BarIndex = 0; BarIndex < 2; ++BarIndex)
		{
			UBorder* PauseBar = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
			PauseBar->SetBrush(RoundedBrush(TextOnAccent, 1.5f));
			USizeBox* BarSizer = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			BarSizer->SetWidthOverride(4.0f);
			BarSizer->SetHeightOverride(14.0f);
			BarSizer->AddChild(PauseBar);
			UHorizontalBoxSlot* PauseSlot = PauseBars->AddChildToHorizontalBox(BarSizer);
			PauseSlot->SetPadding(FMargin(BarIndex == 0 ? 0.0f : 4.0f, 0.0f, 0.0f, 0.0f));
		}
		PlayPauseIcon->AddChild(PauseBars);

		for (int32 ChildIndex = 0; ChildIndex < PlayPauseIcon->GetChildrenCount(); ++ChildIndex)
		{
			if (UWidgetSwitcherSlot* IconSlot = Cast<UWidgetSwitcherSlot>(PlayPauseIcon->GetChildAt(ChildIndex)->Slot))
			{
				IconSlot->SetHorizontalAlignment(HAlign_Center);
				IconSlot->SetVerticalAlignment(VAlign_Center);
			}
		}

		PlayPauseButton->AddChild(WrapIcon(PlayPauseIcon));
		if (UButtonSlot* ContentSlot = Cast<UButtonSlot>(PlayPauseButton->GetChildAt(0)->Slot))
		{
			ContentSlot->SetPadding(FMargin(0.0f));
		}
		AddCell(PlayPauseButton, 0.0f);

		// --- Stop (secondary card, square icon) -----------------------------
		StopButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Stop"));
		StopButton->SetStyle(TransportButtonStyle(false));

		StopIcon = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("StopIcon"));
		StopIcon->SetBrush(RoundedBrush(TextPrimary, 2.0f));
		USizeBox* StopSizer = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		StopSizer->SetWidthOverride(12.0f);
		StopSizer->SetHeightOverride(12.0f);
		StopSizer->AddChild(StopIcon);

		StopButton->AddChild(WrapIcon(StopSizer));
		if (UButtonSlot* ContentSlot = Cast<UButtonSlot>(StopButton->GetChildAt(0)->Slot))
		{
			ContentSlot->SetPadding(FMargin(0.0f));
		}
		AddCell(StopButton, 8.0f);

		AddSeparator();

		// --- Speed pills ------------------------------------------------------
		for (int32 PresetIndex = 0; PresetIndex < UE_ARRAY_COUNT(SpeedPresets); ++PresetIndex)
		{
			URoadmapRowButton* Pill = WidgetTree->ConstructWidget<URoadmapRowButton>(URoadmapRowButton::StaticClass());
			Pill->InitRow(PresetIndex);
			Pill->OnRowSelected.BindUObject(this, &USimControlBarWidget::HandleSpeedSelected);
			Pill->SetStyle(SpeedPillStyle(PresetIndex == LastSpeedIndex));

			UTextBlock* Label = MakeLabel(SpeedLabels[PresetIndex], FName("Bold"), 11, TextSecondary);
			Label->SetColorAndOpacity(FSlateColor::UseForeground());
			Pill->AddChild(Label);
			if (UButtonSlot* LabelSlot = Cast<UButtonSlot>(Label->Slot))
			{
				LabelSlot->SetPadding(FMargin(0.0f));
				LabelSlot->SetHorizontalAlignment(HAlign_Center);
				LabelSlot->SetVerticalAlignment(VAlign_Center);
			}

			AddCell(Pill, PresetIndex == 0 ? 12.0f : 4.0f);
			SpeedButtons.Add(Pill);
		}

		AddSeparator();

		// --- Status readout ---------------------------------------------------
		StatusText = MakeLabel(TEXT("STOPPED"), FName("Bold"), 10, TextSecondary);
		FSlateFontInfo StatusFont = FCoreStyle::GetDefaultFontStyle("Bold", 10);
		StatusFont.LetterSpacing = 200;
		StatusText->SetFont(StatusFont);

		// Min width so the bar doesn't resize as the state word changes.
		USizeBox* StatusSizer = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		StatusSizer->SetMinDesiredWidth(78.0f);
		StatusSizer->AddChild(StatusText);
		if (USizeBoxSlot* StatusSlot = Cast<USizeBoxSlot>(StatusText->Slot))
		{
			StatusSlot->SetVerticalAlignment(VAlign_Center);
		}
		AddCell(StatusSizer, 12.0f);

		AddSeparator();

		// --- Back to main menu ------------------------------------------------
		MenuButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Menu"));
		MenuButton->SetStyle(TransportButtonStyle(false));

		UTextBlock* MenuLabel = MakeLabel(TEXT("MENU"), FName("Bold"), 10, TextPrimary);
		FSlateFontInfo MenuFont = FCoreStyle::GetDefaultFontStyle("Bold", 10);
		MenuFont.LetterSpacing = 200;
		MenuLabel->SetFont(MenuFont);

		MenuButton->AddChild(MenuLabel);
		if (UButtonSlot* ContentSlot = Cast<UButtonSlot>(MenuLabel->Slot))
		{
			ContentSlot->SetPadding(FMargin(0.0f));
			ContentSlot->SetHorizontalAlignment(HAlign_Center);
			ContentSlot->SetVerticalAlignment(VAlign_Center);
		}
		AddCell(MenuButton, 12.0f);
	}

	return Super::RebuildWidget();
}

void USimControlBarWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (PlayPauseButton) PlayPauseButton->OnClicked.AddUniqueDynamic(this, &USimControlBarWidget::HandlePlayPauseClicked);
	if (StopButton) StopButton->OnClicked.AddUniqueDynamic(this, &USimControlBarWidget::HandleStopClicked);
	if (MenuButton) MenuButton->OnClicked.AddUniqueDynamic(this, &USimControlBarWidget::HandleMenuClicked);

	ASimulationManager* Sim = ResolveSimManager();
	const bool bRunning = Sim && Sim->bSimulationRunning;
	RefreshVisuals(bRunning, bRunning && Sim->bSimulationPaused,
		Sim ? SpeedIndexForMultiplier(Sim->SimSpeedMultiplier) : LastSpeedIndex);
}

void USimControlBarWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// The sim can also be started/stopped elsewhere (Blueprint UI, editor
	// debugging), so mirror the manager's state instead of assuming our
	// buttons are the only writers.
	ASimulationManager* Sim = ResolveSimManager();
	const bool bRunning = Sim && Sim->bSimulationRunning;
	const bool bPaused = bRunning && Sim->bSimulationPaused;
	const int32 SpeedIndex = Sim ? SpeedIndexForMultiplier(Sim->SimSpeedMultiplier) : LastSpeedIndex;

	if (bRunning != bLastRunning || bPaused != bLastPaused || SpeedIndex != LastSpeedIndex)
	{
		RefreshVisuals(bRunning, bPaused, SpeedIndex);
	}
}

void USimControlBarWidget::HandlePlayPauseClicked()
{
	ASimulationManager* Sim = ResolveSimManager();
	if (!Sim) return;

	if (!Sim->bSimulationRunning)
	{
		Sim->StartSimulation();
	}
	else
	{
		Sim->SetSimulationPaused(!Sim->bSimulationPaused);
	}
	RefreshVisuals(Sim->bSimulationRunning, Sim->bSimulationRunning && Sim->bSimulationPaused, LastSpeedIndex);
}

void USimControlBarWidget::HandleStopClicked()
{
	ASimulationManager* Sim = ResolveSimManager();
	if (Sim && Sim->bSimulationRunning)
	{
		Sim->StopSimulation();
		RefreshVisuals(false, false, LastSpeedIndex);
	}
}

void USimControlBarWidget::HandleMenuClicked()
{
	// Same full reset as the stop button (telemetry pipeline included) so a
	// run in progress is flushed before the level is torn down.
	ASimulationManager* Sim = ResolveSimManager();
	if (Sim && Sim->bSimulationRunning)
	{
		Sim->StopSimulation();
	}
	UGameplayStatics::OpenLevel(this, MainMenuLevelName);
}

void USimControlBarWidget::HandleSpeedSelected(int32 PresetIndex)
{
	if (PresetIndex < 0 || PresetIndex >= UE_ARRAY_COUNT(SpeedPresets)) return;

	if (ASimulationManager* Sim = ResolveSimManager())
	{
		Sim->SetSimulationSpeed(SpeedPresets[PresetIndex]);
		RefreshVisuals(bLastRunning, bLastPaused, PresetIndex);
	}
}

ASimulationManager* USimControlBarWidget::ResolveSimManager()
{
	if (!IsValid(CachedSimManager))
	{
		CachedSimManager = Cast<ASimulationManager>(
			UGameplayStatics::GetActorOfClass(GetWorld(), ASimulationManager::StaticClass()));
	}
	return CachedSimManager;
}

void USimControlBarWidget::RefreshVisuals(bool bRunning, bool bPaused, int32 SpeedIndex)
{
	bLastRunning = bRunning;
	bLastPaused = bPaused;
	LastSpeedIndex = SpeedIndex;

	if (PlayPauseIcon)
	{
		PlayPauseIcon->SetActiveWidgetIndex(bRunning && !bPaused ? 1 : 0);
	}

	// Stop only means something while the sim is running; the button style dims
	// itself when disabled, but the hand-built icon needs dimming explicitly.
	if (StopButton)
	{
		StopButton->SetIsEnabled(bRunning);
	}
	if (StopIcon)
	{
		StopIcon->SetBrushColor(bRunning ? MenuPalette::TextPrimary : MenuPalette::TextDisabled);
	}

	for (int32 PresetIndex = 0; PresetIndex < SpeedButtons.Num(); ++PresetIndex)
	{
		if (SpeedButtons[PresetIndex])
		{
			SpeedButtons[PresetIndex]->SetStyle(SpeedPillStyle(PresetIndex == SpeedIndex));
		}
	}

	if (StatusText)
	{
		if (!bRunning)
		{
			StatusText->SetText(NSLOCTEXT("SimControlBar", "Stopped", "STOPPED"));
			StatusText->SetColorAndOpacity(FSlateColor(MenuPalette::TextSecondary));
		}
		else if (bPaused)
		{
			StatusText->SetText(NSLOCTEXT("SimControlBar", "Paused", "PAUSED"));
			StatusText->SetColorAndOpacity(FSlateColor(MenuPalette::TextPrimary));
		}
		else
		{
			StatusText->SetText(NSLOCTEXT("SimControlBar", "Running", "RUNNING"));
			StatusText->SetColorAndOpacity(FSlateColor(MenuPalette::Accent));
		}
	}
}

int32 USimControlBarWidget::SpeedIndexForMultiplier(float Multiplier) const
{
	int32 BestIndex = 0;
	for (int32 PresetIndex = 1; PresetIndex < UE_ARRAY_COUNT(SpeedPresets); ++PresetIndex)
	{
		if (FMath::Abs(SpeedPresets[PresetIndex] - Multiplier) < FMath::Abs(SpeedPresets[BestIndex] - Multiplier))
		{
			BestIndex = PresetIndex;
		}
	}
	return BestIndex;
}

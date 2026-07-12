#include "MainMenuWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/Border.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableText.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Components/WidgetSwitcherSlot.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "MenuPalette.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"

// Palette + shared button styles live in MenuPalette.h so in-game HUD widgets
// (e.g. the sim control bar) can match the menu; only the menu-specific
// formatting helpers stay here.
namespace MenuPalette
{
	FString FormatSize(int64 Bytes)
	{
		if (Bytes >= 1024 * 1024)
		{
			return FString::Printf(TEXT("%.1f MB"), Bytes / (1024.0 * 1024.0));
		}
		return FString::Printf(TEXT("%lld KB"), FMath::Max<int64>(1, Bytes / 1024));
	}

	FString FormatDate(const FDateTime& Time)
	{
		static const TCHAR* Months[] = { TEXT("Jan"), TEXT("Feb"), TEXT("Mar"), TEXT("Apr"), TEXT("May"), TEXT("Jun"),
			TEXT("Jul"), TEXT("Aug"), TEXT("Sep"), TEXT("Oct"), TEXT("Nov"), TEXT("Dec") };
		return FString::Printf(TEXT("%s %d, %d %02d:%02d"), Months[Time.GetMonth() - 1], Time.GetDay(),
			Time.GetYear(), Time.GetHour(), Time.GetMinute());
	}
}

using namespace MenuPalette;

// ---------------------------------------------------------------------------
// URoadmapRowButton
// ---------------------------------------------------------------------------
void URoadmapRowButton::InitRow(int32 InIndex)
{
	RowIndex = InIndex;
	OnClicked.AddUniqueDynamic(this, &URoadmapRowButton::HandleClicked);
}

void URoadmapRowButton::HandleClicked()
{
	OnRowSelected.ExecuteIfBound(RowIndex);
}

// ---------------------------------------------------------------------------
// UMainMenuWidget -- construction
// ---------------------------------------------------------------------------
bool UMainMenuWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}

	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildTree();
	}
	SetIsFocusable(true);
	return true;
}

UTextBlock* UMainMenuWidget::MakeText(const FString& Text, int32 Size, const FLinearColor& Color,
	const FName& Typeface, int32 LetterSpacing)
{
	UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>();
	FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(Typeface, Size);
	Font.LetterSpacing = LetterSpacing;
	Block->SetFont(Font);
	Block->SetText(FText::FromString(Text));
	Block->SetColorAndOpacity(FSlateColor(Color));
	return Block;
}

UButton* UMainMenuWidget::MakeActionButton(const FString& Label, bool bPrimary)
{
	UButton* Button = WidgetTree->ConstructWidget<UButton>();
	Button->SetStyle(ActionButtonStyle(bPrimary));

	UTextBlock* Text = MakeText(Label.ToUpper(), 14, TextPrimary, FName("Bold"), 150);
	Text->SetColorAndOpacity(FSlateColor::UseForeground());
	Button->AddChild(Text);
	if (UButtonSlot* TextSlot = Cast<UButtonSlot>(Text->Slot))
	{
		TextSlot->SetPadding(FMargin(0.0f));
		TextSlot->SetHorizontalAlignment(HAlign_Center);
		TextSlot->SetVerticalAlignment(VAlign_Center);
	}
	return Button;
}

UWidget* UMainMenuWidget::WrapMinWidth(UWidget* Inner, float MinWidth)
{
	USizeBox* Sizer = WidgetTree->ConstructWidget<USizeBox>();
	Sizer->SetMinDesiredWidth(MinWidth);
	Sizer->SetContent(Inner);
	return Sizer;
}

URoadmapRowButton* UMainMenuWidget::MakeListRow(int32 Index, const FString& Title, const FString& Subtitle)
{
	URoadmapRowButton* Row = WidgetTree->ConstructWidget<URoadmapRowButton>();
	Row->InitRow(Index);
	Row->SetStyle(ListRowStyle(false));

	UHorizontalBox* Content = WidgetTree->ConstructWidget<UHorizontalBox>();

	UVerticalBox* Labels = WidgetTree->ConstructWidget<UVerticalBox>();
	UTextBlock* TitleText = MakeText(Title, 15, TextPrimary, FName("Medium"));
	TitleText->SetColorAndOpacity(FSlateColor::UseForeground());
	Labels->AddChildToVerticalBox(TitleText);
	UTextBlock* SubText = MakeText(Subtitle, 11, TextSecondary);
	if (UVerticalBoxSlot* SubSlot = Cast<UVerticalBoxSlot>(SubText->Slot ? SubText->Slot : Labels->AddChildToVerticalBox(SubText)))
	{
		SubSlot->SetPadding(FMargin(0.0f, 3.0f, 0.0f, 0.0f));
	}

	if (UHorizontalBoxSlot* LabelsSlot = Content->AddChildToHorizontalBox(Labels))
	{
		LabelsSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		LabelsSlot->SetVerticalAlignment(VAlign_Center);
	}

	// A single right-pointing angle quote as a subtle chevron.
	UTextBlock* Chevron = MakeText(TEXT("\u203A"), 20, TextSecondary, FName("Light"));
	Chevron->SetColorAndOpacity(FSlateColor::UseForeground());
	if (UHorizontalBoxSlot* ChevronSlot = Content->AddChildToHorizontalBox(Chevron))
	{
		ChevronSlot->SetVerticalAlignment(VAlign_Center);
		ChevronSlot->SetPadding(FMargin(10.0f, 0.0f, 2.0f, 0.0f));
	}

	Row->AddChild(Content);
	if (UButtonSlot* ContentSlot = Cast<UButtonSlot>(Content->Slot))
	{
		ContentSlot->SetPadding(FMargin(0.0f));
		ContentSlot->SetHorizontalAlignment(HAlign_Fill);
		ContentSlot->SetVerticalAlignment(VAlign_Center);
	}
	return Row;
}

void UMainMenuWidget::BuildTree()
{
	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
	WidgetTree->RootWidget = Canvas;

	// Full-screen asphalt backdrop.
	UBorder* Backdrop = WidgetTree->ConstructWidget<UBorder>();
	Backdrop->SetBrush(FSlateColorBrush(Background));
	Backdrop->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (UCanvasPanelSlot* BackdropSlot = Canvas->AddChildToCanvas(Backdrop))
	{
		BackdropSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
		BackdropSlot->SetOffsets(FMargin(0.0f));
	}

	// Very faint two-layer glows so the flat backdrop reads as lit space.
	auto AddGlow = [&](const FLinearColor& Color, const FAnchors& Anchors, const FVector2D& Position, float Diameter)
	{
		for (int32 Layer = 0; Layer < 2; ++Layer)
		{
			const float LayerDiameter = Layer == 0 ? Diameter : Diameter * 0.6f;
			FLinearColor LayerColor = Color;
			LayerColor.A = Layer == 0 ? Color.A : Color.A * 1.5f;

			UBorder* Glow = WidgetTree->ConstructWidget<UBorder>();
			Glow->SetBrush(RoundedBrush(LayerColor, LayerDiameter * 0.5f));
			Glow->SetVisibility(ESlateVisibility::HitTestInvisible);
			if (UCanvasPanelSlot* GlowSlot = Canvas->AddChildToCanvas(Glow))
			{
				GlowSlot->SetAnchors(Anchors);
				GlowSlot->SetAlignment(FVector2D(0.5f, 0.5f));
				GlowSlot->SetPosition(Position);
				GlowSlot->SetSize(FVector2D(LayerDiameter, LayerDiameter));
			}
		}
	};
	AddGlow(GlowWarm, FAnchors(1.0f, 0.0f), FVector2D(-160.0f, 140.0f), 1050.0f);
	AddGlow(GlowCool, FAnchors(0.0f, 1.0f), FVector2D(180.0f, -100.0f), 1150.0f);

	// --- Left-anchored content column --------------------------------------
	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>();

	// Logo: ROAD in white, MAP in amber, over a dashed center-line motif.
	UHorizontalBox* TitleRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	TitleRow->AddChildToHorizontalBox(MakeText(TEXT("ROAD"), 56, TextPrimary, FName("Black"), 100));
	TitleRow->AddChildToHorizontalBox(MakeText(TEXT("MAP"), 56, Accent, FName("Black"), 100));
	Column->AddChildToVerticalBox(TitleRow);

	UHorizontalBox* DashRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	for (int32 Dash = 0; Dash < 5; ++Dash)
	{
		UBorder* DashMark = WidgetTree->ConstructWidget<UBorder>();
		DashMark->SetBrush(RoundedBrush(Accent, 2.0f));
		DashMark->SetVisibility(ESlateVisibility::HitTestInvisible);
		USizeBox* DashSizer = WidgetTree->ConstructWidget<USizeBox>();
		DashSizer->SetWidthOverride(30.0f);
		DashSizer->SetHeightOverride(4.0f);
		DashSizer->SetContent(DashMark);
		if (UHorizontalBoxSlot* DashSlot = DashRow->AddChildToHorizontalBox(DashSizer))
		{
			DashSlot->SetPadding(FMargin(0.0f, 0.0f, 12.0f, 0.0f));
			DashSlot->SetVerticalAlignment(VAlign_Center);
		}
	}
	if (UVerticalBoxSlot* DashRowSlot = Column->AddChildToVerticalBox(DashRow))
	{
		DashRowSlot->SetPadding(FMargin(4.0f, 14.0f, 0.0f, 0.0f));
	}

	UTextBlock* Subtitle = MakeText(TEXT("TRAFFIC SIMULATION SANDBOX"), 12, TextSecondary, FName("Light"), 400);
	if (UVerticalBoxSlot* SubtitleSlot = Column->AddChildToVerticalBox(Subtitle))
	{
		SubtitleSlot->SetPadding(FMargin(4.0f, 14.0f, 0.0f, 0.0f));
	}

	PageSwitcher = WidgetTree->ConstructWidget<UWidgetSwitcher>();
	PageSwitcher->AddChild(BuildRootPage());
	PageSwitcher->AddChild(BuildNewPage());
	PageSwitcher->AddChild(BuildLoadPage());
	for (int32 PageIndex = 0; PageIndex < PageSwitcher->GetChildrenCount(); ++PageIndex)
	{
		if (UWidgetSwitcherSlot* PageSlot = Cast<UWidgetSwitcherSlot>(PageSwitcher->GetChildAt(PageIndex)->Slot))
		{
			PageSlot->SetHorizontalAlignment(HAlign_Left);
			PageSlot->SetVerticalAlignment(VAlign_Top);
		}
	}
	if (UVerticalBoxSlot* SwitcherSlot = Column->AddChildToVerticalBox(PageSwitcher))
	{
		SwitcherSlot->SetPadding(FMargin(0.0f, 44.0f, 0.0f, 0.0f));
	}

	if (UCanvasPanelSlot* ColumnSlot = Canvas->AddChildToCanvas(Column))
	{
		ColumnSlot->SetAnchors(FAnchors(0.0f, 0.5f));
		ColumnSlot->SetAlignment(FVector2D(0.0f, 0.5f));
		ColumnSlot->SetPosition(FVector2D(120.0f, 0.0f));
		ColumnSlot->SetAutoSize(true);
	}

	// Footer.
	UTextBlock* Footer = MakeText(TEXT("UNREAL ENGINE 5.7"), 10, TextFaint, FName("Regular"), 300);
	Footer->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (UCanvasPanelSlot* FooterSlot = Canvas->AddChildToCanvas(Footer))
	{
		FooterSlot->SetAnchors(FAnchors(1.0f, 1.0f));
		FooterSlot->SetAlignment(FVector2D(1.0f, 1.0f));
		FooterSlot->SetPosition(FVector2D(-28.0f, -22.0f));
		FooterSlot->SetAutoSize(true);
	}
}

UWidget* UMainMenuWidget::BuildRootPage()
{
	UVerticalBox* Page = WidgetTree->ConstructWidget<UVerticalBox>();

	auto AddRootButton = [&](UButton* Button)
	{
		if (UVerticalBoxSlot* ButtonSlot = Page->AddChildToVerticalBox(Button))
		{
			ButtonSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 12.0f));
		}
	};

	UButton* NewButton = MakeActionButton(TEXT("Start New Road Map"), true);
	NewButton->OnClicked.AddDynamic(this, &UMainMenuWidget::OnNewRoadmapClicked);
	AddRootButton(NewButton);

	UButton* LoadButton = MakeActionButton(TEXT("Load Road Map"), false);
	LoadButton->OnClicked.AddDynamic(this, &UMainMenuWidget::OnLoadRoadmapClicked);
	AddRootButton(LoadButton);

	UButton* QuitButton = MakeActionButton(TEXT("Quit"), false);
	QuitButton->OnClicked.AddDynamic(this, &UMainMenuWidget::OnQuitClicked);
	AddRootButton(QuitButton);

	USizeBox* Sizer = WidgetTree->ConstructWidget<USizeBox>();
	Sizer->SetWidthOverride(360.0f);
	Sizer->SetContent(Page);
	return Sizer;
}

UWidget* UMainMenuWidget::BuildNewPage()
{
	UVerticalBox* Page = WidgetTree->ConstructWidget<UVerticalBox>();

	Page->AddChildToVerticalBox(MakeText(TEXT("START NEW ROAD MAP"), 11, Accent, FName("Medium"), 300));
	UTextBlock* Heading = MakeText(TEXT("Choose a starting template"), 22, TextPrimary, FName("Bold"));
	if (UVerticalBoxSlot* HeadingSlot = Page->AddChildToVerticalBox(Heading))
	{
		HeadingSlot->SetPadding(FMargin(0.0f, 6.0f, 0.0f, 0.0f));
	}
	UTextBlock* Blurb = MakeText(
		TEXT("Your road map starts as a private copy of the template \u2014 the original is never modified."),
		12, TextSecondary);
	Blurb->SetAutoWrapText(true);
	if (UVerticalBoxSlot* BlurbSlot = Page->AddChildToVerticalBox(Blurb))
	{
		BlurbSlot->SetPadding(FMargin(0.0f, 6.0f, 0.0f, 0.0f));
	}

	UScrollBox* TemplateScroll = WidgetTree->ConstructWidget<UScrollBox>();
	TemplateScroll->SetScrollbarThickness(FVector2D(4.0f, 4.0f));
	TemplateScroll->SetAnimateWheelScrolling(true);
	TemplateListBox = WidgetTree->ConstructWidget<UVerticalBox>();
	TemplateScroll->AddChild(TemplateListBox);

	USizeBox* ScrollSizer = WidgetTree->ConstructWidget<USizeBox>();
	ScrollSizer->SetMaxDesiredHeight(252.0f);
	ScrollSizer->SetContent(TemplateScroll);
	if (UVerticalBoxSlot* ScrollSlot = Page->AddChildToVerticalBox(ScrollSizer))
	{
		ScrollSlot->SetPadding(FMargin(0.0f, 16.0f, 0.0f, 0.0f));
	}

	if (UVerticalBoxSlot* NameLabelSlot = Page->AddChildToVerticalBox(
		MakeText(TEXT("ROAD MAP NAME"), 11, TextSecondary, FName("Medium"), 250)))
	{
		NameLabelSlot->SetPadding(FMargin(0.0f, 18.0f, 0.0f, 0.0f));
	}

	NameField = WidgetTree->ConstructWidget<UEditableText>();
	FEditableTextStyle NameStyle;
	NameStyle.SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 15));
	NameStyle.SetColorAndOpacity(FSlateColor(TextPrimary));
	NameStyle.SetCaretImage(FSlateColorBrush(Accent));
	NameStyle.SetBackgroundImageSelected(FSlateColorBrush(FLinearColor(Accent.R, Accent.G, Accent.B, 0.35f)));
	NameField->SetWidgetStyle(NameStyle);
	NameField->SetHintText(FText::FromString(TEXT("Name your road map...")));
	NameField->OnTextCommitted.AddDynamic(this, &UMainMenuWidget::OnNameCommitted);

	UBorder* NameBorder = WidgetTree->ConstructWidget<UBorder>();
	NameBorder->SetBrush(RoundedBrush(InputFill, 8.0f, Outline, 1.0f));
	NameBorder->SetPadding(FMargin(14.0f, 11.0f));
	NameBorder->SetContent(NameField);
	if (UVerticalBoxSlot* NameSlot = Page->AddChildToVerticalBox(NameBorder))
	{
		NameSlot->SetPadding(FMargin(0.0f, 6.0f, 0.0f, 0.0f));
	}

	NewPageErrorText = MakeText(TEXT(""), 12, ErrorColor);
	NewPageErrorText->SetAutoWrapText(true);
	NewPageErrorText->SetVisibility(ESlateVisibility::Collapsed);
	if (UVerticalBoxSlot* ErrorSlot = Page->AddChildToVerticalBox(NewPageErrorText))
	{
		ErrorSlot->SetPadding(FMargin(0.0f, 10.0f, 0.0f, 0.0f));
	}

	UHorizontalBox* Actions = WidgetTree->ConstructWidget<UHorizontalBox>();
	UButton* BackButton = MakeActionButton(TEXT("Back"), false);
	BackButton->OnClicked.AddDynamic(this, &UMainMenuWidget::OnBackClicked);
	Actions->AddChildToHorizontalBox(WrapMinWidth(BackButton, 130.0f));

	CreateButton = MakeActionButton(TEXT("Create & Start"), true);
	CreateButton->OnClicked.AddDynamic(this, &UMainMenuWidget::OnCreateAndStartClicked);
	if (UHorizontalBoxSlot* CreateSlot = Actions->AddChildToHorizontalBox(CreateButton))
	{
		CreateSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		CreateSlot->SetPadding(FMargin(12.0f, 0.0f, 0.0f, 0.0f));
	}
	if (UVerticalBoxSlot* ActionsSlot = Page->AddChildToVerticalBox(Actions))
	{
		ActionsSlot->SetPadding(FMargin(0.0f, 20.0f, 0.0f, 0.0f));
	}

	USizeBox* Sizer = WidgetTree->ConstructWidget<USizeBox>();
	Sizer->SetWidthOverride(560.0f);
	Sizer->SetContent(Page);
	return Sizer;
}

UWidget* UMainMenuWidget::BuildLoadPage()
{
	UVerticalBox* Page = WidgetTree->ConstructWidget<UVerticalBox>();

	Page->AddChildToVerticalBox(MakeText(TEXT("LOAD ROAD MAP"), 11, Accent, FName("Medium"), 300));
	UTextBlock* Heading = MakeText(TEXT("Continue where you left off"), 22, TextPrimary, FName("Bold"));
	if (UVerticalBoxSlot* HeadingSlot = Page->AddChildToVerticalBox(Heading))
	{
		HeadingSlot->SetPadding(FMargin(0.0f, 6.0f, 0.0f, 0.0f));
	}

	SaveScrollBox = WidgetTree->ConstructWidget<UScrollBox>();
	SaveScrollBox->SetScrollbarThickness(FVector2D(4.0f, 4.0f));
	SaveScrollBox->SetAnimateWheelScrolling(true);
	SaveListBox = WidgetTree->ConstructWidget<UVerticalBox>();
	SaveScrollBox->AddChild(SaveListBox);

	USizeBox* ScrollSizer = WidgetTree->ConstructWidget<USizeBox>();
	ScrollSizer->SetMaxDesiredHeight(320.0f);
	ScrollSizer->SetContent(SaveScrollBox);
	if (UVerticalBoxSlot* ScrollSlot = Page->AddChildToVerticalBox(ScrollSizer))
	{
		ScrollSlot->SetPadding(FMargin(0.0f, 16.0f, 0.0f, 0.0f));
	}

	NoSavesText = MakeText(
		TEXT("No saved road maps yet.\nStart a new one from a template and it will show up here."),
		13, TextSecondary);
	NoSavesText->SetVisibility(ESlateVisibility::Collapsed);
	if (UVerticalBoxSlot* NoSavesSlot = Page->AddChildToVerticalBox(NoSavesText))
	{
		NoSavesSlot->SetPadding(FMargin(0.0f, 16.0f, 0.0f, 0.0f));
	}

	LoadPageErrorText = MakeText(TEXT(""), 12, ErrorColor);
	LoadPageErrorText->SetAutoWrapText(true);
	LoadPageErrorText->SetVisibility(ESlateVisibility::Collapsed);
	if (UVerticalBoxSlot* ErrorSlot = Page->AddChildToVerticalBox(LoadPageErrorText))
	{
		ErrorSlot->SetPadding(FMargin(0.0f, 10.0f, 0.0f, 0.0f));
	}

	UHorizontalBox* Actions = WidgetTree->ConstructWidget<UHorizontalBox>();
	UButton* BackButton = MakeActionButton(TEXT("Back"), false);
	BackButton->OnClicked.AddDynamic(this, &UMainMenuWidget::OnBackClicked);
	Actions->AddChildToHorizontalBox(WrapMinWidth(BackButton, 130.0f));

	OpenButton = MakeActionButton(TEXT("Open Road Map"), true);
	OpenButton->OnClicked.AddDynamic(this, &UMainMenuWidget::OnOpenSaveClicked);
	if (UHorizontalBoxSlot* OpenSlot = Actions->AddChildToHorizontalBox(OpenButton))
	{
		OpenSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		OpenSlot->SetPadding(FMargin(12.0f, 0.0f, 0.0f, 0.0f));
	}
	if (UVerticalBoxSlot* ActionsSlot = Page->AddChildToVerticalBox(Actions))
	{
		ActionsSlot->SetPadding(FMargin(0.0f, 20.0f, 0.0f, 0.0f));
	}

	USizeBox* Sizer = WidgetTree->ConstructWidget<USizeBox>();
	Sizer->SetWidthOverride(560.0f);
	Sizer->SetContent(Page);
	return Sizer;
}

// ---------------------------------------------------------------------------
// UMainMenuWidget -- flow
// ---------------------------------------------------------------------------
void UMainMenuWidget::ShowPage(EMenuPage Page)
{
	ClearError();
	if (PageSwitcher)
	{
		PageSwitcher->SetActiveWidgetIndex(static_cast<int32>(Page));
	}
}

URoadmapGameInstance* UMainMenuWidget::GetRoadmapGameInstance() const
{
	return GetWorld() ? GetWorld()->GetGameInstance<URoadmapGameInstance>() : nullptr;
}

void UMainMenuWidget::OnNewRoadmapClicked()
{
	ShowPage(EMenuPage::NewRoadmap);
	RefreshTemplateList();
}

void UMainMenuWidget::OnLoadRoadmapClicked()
{
	ShowPage(EMenuPage::LoadRoadmap);
	RefreshSaveList();
}

void UMainMenuWidget::OnBackClicked()
{
	ShowPage(EMenuPage::Root);
}

void UMainMenuWidget::OnQuitClicked()
{
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}

void UMainMenuWidget::RefreshTemplateList()
{
	TemplateListBox->ClearChildren();
	TemplateRows.Reset();
	SelectedTemplateIndex = INDEX_NONE;

	URoadmapGameInstance* GameInstance = GetRoadmapGameInstance();
	if (!GameInstance)
	{
		ShowError(TEXT("RoadmapGameInstance is not active \u2014 check GameInstanceClass in DefaultEngine.ini."));
		CreateButton->SetIsEnabled(false);
		return;
	}

	Templates = GameInstance->GetAvailableTemplates();
	if (Templates.Num() == 0)
	{
		ShowError(FString::Printf(TEXT("No templates found in %s"), *URoadmapGameInstance::GetTemplatesDir()));
	}
	CreateButton->SetIsEnabled(Templates.Num() > 0);

	for (int32 Index = 0; Index < Templates.Num(); ++Index)
	{
		const FRoadmapEntry& Template = Templates[Index];
		URoadmapRowButton* Row = MakeListRow(Index, Template.DisplayName,
			FString::Printf(TEXT("%s of road data"), *FormatSize(Template.SizeBytes)));
		Row->OnRowSelected.BindUObject(this, &UMainMenuWidget::OnTemplateRowSelected);
		if (UVerticalBoxSlot* RowSlot = TemplateListBox->AddChildToVerticalBox(Row))
		{
			RowSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 8.0f));
		}
		TemplateRows.Add(Row);
	}

	if (Templates.Num() > 0)
	{
		OnTemplateRowSelected(0);
	}
}

void UMainMenuWidget::OnTemplateRowSelected(int32 Index)
{
	if (!Templates.IsValidIndex(Index))
	{
		return;
	}
	SelectedTemplateIndex = Index;
	ApplyRowSelection(TemplateRows, Index);
	ClearError();

	if (URoadmapGameInstance* GameInstance = GetRoadmapGameInstance())
	{
		NameField->SetText(FText::FromString(GameInstance->MakeUniqueRoadmapName(Templates[Index].DisplayName)));
	}
}

void UMainMenuWidget::RefreshSaveList()
{
	SaveListBox->ClearChildren();
	SaveRows.Reset();
	SelectedSaveIndex = INDEX_NONE;

	URoadmapGameInstance* GameInstance = GetRoadmapGameInstance();
	if (!GameInstance)
	{
		ShowError(TEXT("RoadmapGameInstance is not active \u2014 check GameInstanceClass in DefaultEngine.ini."));
		OpenButton->SetIsEnabled(false);
		return;
	}

	Saves = GameInstance->GetSavedRoadmaps();
	const bool bHasSaves = Saves.Num() > 0;
	OpenButton->SetIsEnabled(bHasSaves);
	NoSavesText->SetVisibility(bHasSaves ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	SaveScrollBox->SetVisibility(bHasSaves ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);

	for (int32 Index = 0; Index < Saves.Num(); ++Index)
	{
		const FRoadmapEntry& Save = Saves[Index];
		URoadmapRowButton* Row = MakeListRow(Index, Save.DisplayName,
			FString::Printf(TEXT("Edited %s  \u00B7  %s"), *FormatDate(Save.LastModified), *FormatSize(Save.SizeBytes)));
		Row->OnRowSelected.BindUObject(this, &UMainMenuWidget::OnSaveRowSelected);
		if (UVerticalBoxSlot* RowSlot = SaveListBox->AddChildToVerticalBox(Row))
		{
			RowSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 8.0f));
		}
		SaveRows.Add(Row);
	}

	if (bHasSaves)
	{
		OnSaveRowSelected(0); // most recently edited save
	}
}

void UMainMenuWidget::OnSaveRowSelected(int32 Index)
{
	if (!Saves.IsValidIndex(Index))
	{
		return;
	}
	SelectedSaveIndex = Index;
	ApplyRowSelection(SaveRows, Index);
	ClearError();
}

void UMainMenuWidget::ApplyRowSelection(const TArray<TObjectPtr<URoadmapRowButton>>& Rows, int32 SelectedIndex)
{
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		Rows[Index]->SetStyle(ListRowStyle(Index == SelectedIndex));
	}
}

void UMainMenuWidget::OnNameCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter)
	{
		OnCreateAndStartClicked();
	}
}

void UMainMenuWidget::OnCreateAndStartClicked()
{
	URoadmapGameInstance* GameInstance = GetRoadmapGameInstance();
	if (!GameInstance || !Templates.IsValidIndex(SelectedTemplateIndex))
	{
		ShowError(TEXT("Pick a template first."));
		return;
	}

	FString ErrorMessage;
	if (!GameInstance->CreateRoadmapFromTemplate(Templates[SelectedTemplateIndex], NameField->GetText().ToString(), ErrorMessage))
	{
		ShowError(ErrorMessage);
		return;
	}
	StartSimulation();
}

void UMainMenuWidget::OnOpenSaveClicked()
{
	URoadmapGameInstance* GameInstance = GetRoadmapGameInstance();
	if (!GameInstance || !Saves.IsValidIndex(SelectedSaveIndex))
	{
		ShowError(TEXT("Pick a road map first."));
		return;
	}

	FString ErrorMessage;
	if (!GameInstance->LoadRoadmap(Saves[SelectedSaveIndex], ErrorMessage))
	{
		ShowError(ErrorMessage);
		return;
	}
	StartSimulation();
}

void UMainMenuWidget::StartSimulation()
{
	UGameplayStatics::OpenLevel(this, SimulationLevelName);
}

void UMainMenuWidget::ShowError(const FString& Message)
{
	UTextBlock* Target = PageSwitcher && PageSwitcher->GetActiveWidgetIndex() == static_cast<int32>(EMenuPage::LoadRoadmap)
		? LoadPageErrorText.Get() : NewPageErrorText.Get();
	if (Target)
	{
		Target->SetText(FText::FromString(Message));
		Target->SetVisibility(ESlateVisibility::Visible);
	}
}

void UMainMenuWidget::ClearError()
{
	for (UTextBlock* ErrorText : { NewPageErrorText.Get(), LoadPageErrorText.Get() })
	{
		if (ErrorText)
		{
			ErrorText->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

FReply UMainMenuWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape && PageSwitcher &&
		PageSwitcher->GetActiveWidgetIndex() != static_cast<int32>(EMenuPage::Root))
	{
		ShowPage(EMenuPage::Root);
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

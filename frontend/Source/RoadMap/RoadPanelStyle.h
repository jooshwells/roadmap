#pragma once

// Shared control styling for the road toolbar and road editor panels. Both sit
// on a near-black UBorder, so every input gets a dark fill and light text --
// engine defaults leave combo/menu text black-on-dark. Colours are picked for
// >= 4.5:1 (WCAG AA); since these FLinearColors are already linear, a neutral
// grey's relative luminance equals its component value.

#include "CoreMinimal.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Styling/CoreStyle.h"

namespace RoadPanelStyle
{
    inline const FLinearColor ControlBackground(0.05f, 0.05f, 0.065f); // field / combo fill
    inline const FLinearColor ControlHighlight(0.14f, 0.14f, 0.18f);  // hovered / selected row
    inline const FLinearColor ControlText(0.92f, 0.92f, 0.92f);       // text on either of the above
    inline const FLinearColor RowLabel(0.85f, 0.85f, 0.85f);          // labels on the panel
    inline const FLinearColor StatusLabel(0.7f, 0.7f, 0.7f);          // dimmer hint text

    // Dark fill + light text for a number-entry box, across every text state
    // (normal, focused, read-only). The default background brushes are tinted
    // down rather than replaced, keeping their rounded shape.
    inline void StyleNumberField(UEditableTextBox* Box)
    {
        if (!Box) return;

        FEditableTextBoxStyle Style = Box->GetWidgetStyle();
        Style.BackgroundColor = FSlateColor(ControlBackground);
        Style.ForegroundColor = FSlateColor(ControlText);
        Style.FocusedForegroundColor = FSlateColor(ControlText);
        Style.ReadOnlyForegroundColor = FSlateColor(ControlText);
        Box->SetWidgetStyle(Style);
    }

    // Dark fill and light text for the closed combo button and its dropdown
    // rows.
    //
    // Do NOT reintroduce OnGenerateWidgetEvent here to colour the entries: the
    // widget a generate-handler returns is never parented into the widget tree,
    // and UWidgetTree only keeps a reference to unparented widgets in the
    // editor (UWidgetTree::AllWidgets is WITH_EDITORONLY_DATA). In a packaged
    // build those UTextBlocks are unreferenced, get garbage collected, and the
    // surviving STextBlock is left with a dangling &UTextBlock::StrikeBrush --
    // which the next Slate prepass dereferences and crashes on.
    //
    // The engine's default entry widget inherits FSlateColor::UseForeground(),
    // so the colours below reach both the closed button (ForegroundColor) and
    // the dropdown rows (ItemStyle.TextColor) without any custom widget at all.
    //
    // ForegroundColor / Font are deprecated for *runtime* writes because the
    // combo only reads them while building its SWidget -- which is exactly when
    // we set them, so call this right after ConstructWidget and before the
    // combo is parented. (UComboBoxString::InitForegroundColor / InitFont do
    // the same assignment but are protected.)
    inline void StyleTurnLaneCombo(UComboBoxString* Combo, int32 FontSize = 10)
    {
        if (!Combo) return;

PRAGMA_DISABLE_DEPRECATION_WARNINGS
        Combo->ForegroundColor = FSlateColor(ControlText);
        Combo->Font = FCoreStyle::GetDefaultFontStyle("Regular", FontSize);
PRAGMA_ENABLE_DEPRECATION_WARNINGS

        FComboBoxStyle BoxStyle = Combo->GetWidgetStyle();
        FButtonStyle& Button = BoxStyle.ComboButtonStyle.ButtonStyle;
        Button.Normal.TintColor = FSlateColor(ControlBackground);
        Button.Hovered.TintColor = FSlateColor(ControlHighlight);
        Button.Pressed.TintColor = FSlateColor(ControlHighlight);
        Button.NormalForeground = FSlateColor(ControlText);
        Button.HoveredForeground = FSlateColor(ControlText);
        Button.PressedForeground = FSlateColor(ControlText);
        Combo->SetWidgetStyle(BoxStyle);

        const FSlateColorBrush NormalRow(ControlBackground);
        const FSlateColorBrush HighlightRow(ControlHighlight);
        FTableRowStyle ItemStyle = Combo->GetItemStyle();
        ItemStyle.EvenRowBackgroundBrush = NormalRow;
        ItemStyle.OddRowBackgroundBrush = NormalRow;
        ItemStyle.EvenRowBackgroundHoveredBrush = HighlightRow;
        ItemStyle.OddRowBackgroundHoveredBrush = HighlightRow;
        ItemStyle.ActiveBrush = HighlightRow;
        ItemStyle.ActiveHoveredBrush = HighlightRow;
        ItemStyle.InactiveBrush = HighlightRow;
        ItemStyle.InactiveHoveredBrush = HighlightRow;
        ItemStyle.TextColor = FSlateColor(ControlText);
        ItemStyle.SelectedTextColor = FSlateColor(ControlText);
        Combo->SetItemStyle(ItemStyle);
    }
}

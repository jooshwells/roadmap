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

    // Dark fill for the closed combo button and its dropdown rows. The per-entry
    // text colour is owned by the panel's OnGenerateWidgetEvent handler (the
    // engine's closed-content ForegroundColor is construction-only), so here we
    // set the backgrounds, the down-arrow foreground, and the row text colour as
    // a fallback.
    inline void StyleTurnLaneCombo(UComboBoxString* Combo)
    {
        if (!Combo) return;

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

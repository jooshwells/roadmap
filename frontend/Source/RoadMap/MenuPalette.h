#pragma once

// Shared UI palette + button styles established by the main menu: dark asphalt
// backdrop with a road-marking amber accent. In-game HUD widgets (e.g. the sim
// control bar) include this so they read as the same product as the menu.

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"

namespace MenuPalette
{
	inline FLinearColor Hex(const TCHAR* Code, float Alpha = 1.0f)
	{
		FLinearColor Color = FLinearColor::FromSRGBColor(FColor::FromHex(Code));
		Color.A = Alpha;
		return Color;
	}

	inline const FLinearColor Background     = Hex(TEXT("0B0F16"));
	inline const FLinearColor GlowWarm       = Hex(TEXT("F5B93E"), 0.028f);
	inline const FLinearColor GlowCool       = Hex(TEXT("3E7BF5"), 0.04f);
	inline const FLinearColor CardFill       = Hex(TEXT("FFFFFF"), 0.03f);
	inline const FLinearColor CardFillHover  = Hex(TEXT("FFFFFF"), 0.06f);
	inline const FLinearColor Outline        = Hex(TEXT("273140"));
	inline const FLinearColor OutlineHover   = Hex(TEXT("3A4658"));
	inline const FLinearColor Accent         = Hex(TEXT("F5B93E"));
	inline const FLinearColor AccentHover    = Hex(TEXT("FFCE5C"));
	inline const FLinearColor AccentPressed  = Hex(TEXT("D99F27"));
	inline const FLinearColor AccentFillSoft = Hex(TEXT("F5B93E"), 0.10f);
	inline const FLinearColor InputFill      = Hex(TEXT("0D1219"));
	inline const FLinearColor TextPrimary    = Hex(TEXT("EEF2F7"));
	inline const FLinearColor TextSecondary  = Hex(TEXT("8A94A6"));
	inline const FLinearColor TextFaint      = Hex(TEXT("4A5364"));
	inline const FLinearColor TextOnAccent   = Hex(TEXT("17130A"));
	inline const FLinearColor TextDisabled   = Hex(TEXT("566072"));
	inline const FLinearColor ErrorColor     = Hex(TEXT("FF7A66"));
	inline const FLinearColor DangerHover    = Hex(TEXT("FF8F7D"));
	inline const FLinearColor DangerPressed  = Hex(TEXT("E05A46"));
	inline const FLinearColor DangerFillSoft = Hex(TEXT("FF7A66"), 0.10f);

	inline FSlateBrush RoundedBrush(const FLinearColor& Fill, float Radius,
		const FLinearColor& OutlineColor = FLinearColor::Transparent, float OutlineWidth = 0.0f)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.TintColor = Fill;
		Brush.OutlineSettings = FSlateBrushOutlineSettings(FVector4(Radius, Radius, Radius, Radius), OutlineColor, OutlineWidth);
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		return Brush;
	}

	// Big menu buttons + footer action buttons. Primary = amber fill with dark
	// text; secondary = translucent card that lights up amber on hover.
	inline FButtonStyle ActionButtonStyle(bool bPrimary)
	{
		FButtonStyle Style;
		if (bPrimary)
		{
			Style.SetNormal(RoundedBrush(Accent, 10.0f))
				.SetHovered(RoundedBrush(AccentHover, 10.0f))
				.SetPressed(RoundedBrush(AccentPressed, 10.0f))
				.SetDisabled(RoundedBrush(CardFill, 10.0f, Outline, 1.0f))
				.SetNormalForeground(TextOnAccent)
				.SetHoveredForeground(TextOnAccent)
				.SetPressedForeground(TextOnAccent)
				.SetDisabledForeground(TextDisabled);
		}
		else
		{
			Style.SetNormal(RoundedBrush(CardFill, 10.0f, Outline, 1.0f))
				.SetHovered(RoundedBrush(AccentFillSoft, 10.0f, Accent, 1.0f))
				.SetPressed(RoundedBrush(AccentFillSoft, 10.0f, AccentPressed, 1.0f))
				.SetDisabled(RoundedBrush(CardFill, 10.0f, Outline, 1.0f))
				.SetNormalForeground(TextPrimary)
				.SetHoveredForeground(AccentHover)
				.SetPressedForeground(AccentPressed)
				.SetDisabledForeground(TextDisabled);
		}
		Style.SetNormalPadding(FMargin(24.0f, 14.0f))
			.SetPressedPadding(FMargin(24.0f, 15.0f, 24.0f, 13.0f));
		return Style;
	}

	// Destructive footer action (e.g. deleting a save). Quiet card that turns
	// red on hover; bArmed = solid red fill for the confirm step.
	inline FButtonStyle DangerButtonStyle(bool bArmed)
	{
		FButtonStyle Style;
		if (bArmed)
		{
			Style.SetNormal(RoundedBrush(ErrorColor, 10.0f))
				.SetHovered(RoundedBrush(DangerHover, 10.0f))
				.SetPressed(RoundedBrush(DangerPressed, 10.0f))
				.SetDisabled(RoundedBrush(CardFill, 10.0f, Outline, 1.0f))
				.SetNormalForeground(TextOnAccent)
				.SetHoveredForeground(TextOnAccent)
				.SetPressedForeground(TextOnAccent)
				.SetDisabledForeground(TextDisabled);
		}
		else
		{
			Style.SetNormal(RoundedBrush(CardFill, 10.0f, Outline, 1.0f))
				.SetHovered(RoundedBrush(DangerFillSoft, 10.0f, ErrorColor, 1.0f))
				.SetPressed(RoundedBrush(DangerFillSoft, 10.0f, DangerPressed, 1.0f))
				.SetDisabled(RoundedBrush(CardFill, 10.0f, Outline, 1.0f))
				.SetNormalForeground(TextPrimary)
				.SetHoveredForeground(DangerHover)
				.SetPressedForeground(DangerPressed)
				.SetDisabledForeground(TextDisabled);
		}
		Style.SetNormalPadding(FMargin(24.0f, 14.0f))
			.SetPressedPadding(FMargin(24.0f, 15.0f, 24.0f, 13.0f));
		return Style;
	}

	// Template / save list rows.
	inline FButtonStyle ListRowStyle(bool bSelected)
	{
		FButtonStyle Style;
		if (bSelected)
		{
			Style.SetNormal(RoundedBrush(AccentFillSoft, 8.0f, Accent, 1.5f))
				.SetHovered(RoundedBrush(AccentFillSoft, 8.0f, AccentHover, 1.5f))
				.SetPressed(RoundedBrush(AccentFillSoft, 8.0f, AccentPressed, 1.5f))
				.SetNormalForeground(AccentHover)
				.SetHoveredForeground(AccentHover)
				.SetPressedForeground(AccentPressed);
		}
		else
		{
			Style.SetNormal(RoundedBrush(CardFill, 8.0f, Outline, 1.0f))
				.SetHovered(RoundedBrush(CardFillHover, 8.0f, OutlineHover, 1.0f))
				.SetPressed(RoundedBrush(CardFillHover, 8.0f, Accent, 1.0f))
				.SetNormalForeground(TextPrimary)
				.SetHoveredForeground(TextPrimary)
				.SetPressedForeground(AccentHover);
		}
		Style.SetDisabled(Style.Normal);
		Style.SetDisabledForeground(TextDisabled);
		Style.SetNormalPadding(FMargin(16.0f, 12.0f)).SetPressedPadding(FMargin(16.0f, 12.0f));
		return Style;
	}
}

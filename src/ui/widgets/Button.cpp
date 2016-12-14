#pragma region Copyright (c) 2014-2016 OpenRCT2 Developers
/*****************************************************************************
 * OpenRCT2, an open source clone of Roller Coaster Tycoon 2.
 *
 * OpenRCT2 is the work of many authors, a full list can be found in contributors.md
 * For more information, visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * A full copy of the GNU General Public License can be found in licence.txt
 *****************************************************************************/
#pragma endregion

#include "../../drawing/IDrawingContext.h"
#include "Button.h"

using namespace OpenRCT2::Ui;

void Button::Draw(IDrawingContext * dc)
{
    bool isHighlighted = (_buttonFlags & BUTTON_FLAGS::HIGHLIGHTED) != 0;
    if (!IsDisabled() && isHighlighted)
    {
        // widget_button_draw(dpi, w, widgetIndex);
        return;
    }

    // Get the colour
    // uint8 colour = w->colours[widget->colour];
    uint8 colour = 0;

    // Check if the button is pressed down
    bool isPressed = (_buttonFlags & BUTTON_FLAGS::PRESSED) != 0;
    if (isPressed)
    {
        if (Image == (uint32)-2)
        {
            // Draw border with no fill
            // gfx_fill_rect_inset(dpi, l, t, r, b, colour, INSET_RECT_FLAG_BORDER_INSET | INSET_RECT_FLAG_FILL_NONE);
            return;
        }

        // Draw the border with fill
        // gfx_fill_rect_inset(dpi, l, t, r, b, colour, INSET_RECT_FLAG_BORDER_INSET);
    }

    // Draw image
    if (IsDisabled())
    {
        // Draw greyed out (light border bottom right shadow)
        colour = ColourMapA[NOT_TRANSLUCENT(colour)].lighter;
        dc->DrawSpriteSolid(Image, 1, 1, colour);

        // Draw greyed out (dark)
        colour = ColourMapA[NOT_TRANSLUCENT(colour)].mid_light;
        dc->DrawSpriteSolid(Image, 0, 0, colour);
    }
    else
    {
        Image = 5178;
        uint32 sprite = Image;
        if (sprite & 0x40000000)
        {
            sprite &= ~0x40000000;
        }
        else
        {
            sprite |= colour << 19;
        }
        dc->DrawSprite(sprite, 0, 0, 0);
    }
}

void Button::MouseDown(const MouseEventArgs * e)
{
    _buttonFlags |= BUTTON_FLAGS::PRESSED;
}

void Button::MouseUp(const MouseEventArgs * e)
{
    _buttonFlags &= ~BUTTON_FLAGS::PRESSED;
}

void Button::MouseEnter(const MouseEventArgs * e)
{
    _buttonFlags |= BUTTON_FLAGS::HIGHLIGHTED;
}

void Button::MouseLeave(const MouseEventArgs * e)
{
    _buttonFlags &= ~BUTTON_FLAGS::HIGHLIGHTED;
}

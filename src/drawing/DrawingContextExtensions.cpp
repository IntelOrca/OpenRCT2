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

#include "../drawing/DrawingContext.h"

extern "C"
{
    #include "../interface/colour.h"
}

void IDrawingContext::FillRect3D(sint32 left, sint32 top, sint32 right, sint32 bottom, uint32 colour, uint32 flags)
{
    if (colour & (COLOUR_FLAG_TRANSLUCENT | COLOUR_FLAG_8))
    {
        translucent_window_palette palette;
        if (colour & COLOUR_FLAG_8)
        {
            // TODO: This can't be added up
            // palette = NOT_TRANSLUCENT(colour);
            assert(false);
            return;
        }
        else
        {
            palette = TranslucentWindowPalettes[BASE_COLOUR(colour)];
        }

        if (flags & INSET_RECT_FLAG_BORDER_NONE)
        {
            FilterRect(palette.base, left, top, right, bottom);
        }
        else if (flags & INSET_RECT_FLAG_BORDER_INSET)
        {
            // Draw outline of box
            FilterRect(palette.highlight, left, top, left, bottom);
            FilterRect(palette.highlight, left, top, right, top);
            FilterRect(palette.shadow, right, top, right, bottom);
            FilterRect(palette.shadow, left, bottom, right, bottom);

            if (!(flags & INSET_RECT_FLAG_FILL_NONE))
            {
                FilterRect(palette.base, left + 1, top + 1, right - 1, bottom - 1);
            }
        }
        else
        {
            // Draw outline of box
            FilterRect(palette.shadow, left, top, left, bottom);
            FilterRect(palette.shadow, left, top, right, top);
            FilterRect(palette.highlight, right, top, right, bottom);
            FilterRect(palette.highlight, left, bottom, right, bottom);

            if (!(flags & INSET_RECT_FLAG_FILL_NONE))
            {
                FilterRect(palette.base, left + 1, top + 1, right - 1, bottom - 1);
            }
        }
    }
    else
    {
        uint8 shadow, fill, hilight;
        if (flags & INSET_RECT_FLAG_FILL_MID_LIGHT)
        {
            shadow = ColourMapA[colour].dark;
            fill = ColourMapA[colour].mid_light;
            hilight = ColourMapA[colour].lighter;
        }
        else
        {
            shadow = ColourMapA[colour].mid_dark;
            fill = ColourMapA[colour].light;
            hilight = ColourMapA[colour].lighter;
        }

        if (flags & INSET_RECT_FLAG_BORDER_NONE)
        {
            FillRect(fill, left, top, right, bottom);
        }
        else if (flags & INSET_RECT_FLAG_BORDER_INSET)
        {
            // Draw outline of box
            FillRect(shadow, left, top, left, bottom);
            FillRect(shadow, left + 1, top, right, top);
            FillRect(hilight, right, top + 1, right, bottom - 1);
            FillRect(hilight, left + 1, bottom, right, bottom);

            if (!(flags & INSET_RECT_FLAG_FILL_NONE))
            {
                if (!(flags & INSET_RECT_FLAG_FILL_DONT_LIGHTEN))
                {
                    if (flags & INSET_RECT_FLAG_FILL_GREY)
                    {
                        fill = ColourMapA[COLOUR_BLACK].light;
                    }
                    else
                    {
                        fill = ColourMapA[colour].lighter;
                    }
                }
                FillRect(fill, left + 1, top + 1, right - 1, bottom - 1);
            }
        }
        else
        {
            // Draw outline of box
            FillRect(hilight, left, top, left, bottom - 1);
            FillRect(hilight, left + 1, top, right - 1, top);
            FillRect(shadow, right, top, right, bottom - 1);
            FillRect(shadow, left, bottom, right, bottom);

            if (!(flags & INSET_RECT_FLAG_FILL_NONE))
            {
                if (flags & INSET_RECT_FLAG_FILL_GREY)
                {
                    fill = ColourMapA[COLOUR_BLACK].light;
                }
                FillRect(fill, left + 1, top + 1, right - 1, bottom - 1);
            }
        }
    }
}

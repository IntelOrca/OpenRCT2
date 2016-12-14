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

#pragma once

#include "../common.h"
#include "Primitives.h"

interface IDrawingContext;

namespace OpenRCT2 { namespace Ui
{
    struct MouseEventArgs;

    enum class VISIBILITY : uint8
    {
        VISIBLE,        // The widget is visible.
        HIDDEN,         // Space is reserved for the widget, but can not be seen or interacted with.
        COLLAPSED,      // No space is reserved for the widget in a widget container.
    };

    namespace WIDGET_FLAGS
    {
        constexpr uint8 AUTOSIZE    = 1 << 0;
        constexpr uint8 ENABLED     = 1 << 1;
    }

    struct Thickness
    {
        sint32 Top, Left, Right, Bottom;
    };

    class Widget
    {
    public:
        union
        {
            struct { sint32 X, Y, Width, Height; };
            struct { xy32 Location; size32 Size; };
            rect32 Bounds;
        };
        Thickness   Margin;
        uint8       Flags;
        VISIBILITY  Visibility;

        rct_string_id DefaultTooltip;

        Widget();
        virtual ~Widget() = default;

        virtual rct_string_id GetTooltip(sint32 x, sint32 y);

        virtual void Update() { };
        virtual void Draw(IDrawingContext * dc) { };

        // Interaction
        virtual void MouseDown(const MouseEventArgs * e) { };
        virtual void MouseMove(const MouseEventArgs * e) { };
        virtual void MouseUp(const MouseEventArgs * e) { };
        virtual void MouseWheel(const MouseEventArgs * e) { };
        virtual void MouseEnter(const MouseEventArgs * e) { };
        virtual void MouseLeave(const MouseEventArgs * e) { };

        // Helpers
        bool IsEnabled() { return (Flags & WIDGET_FLAGS::ENABLED) != 0; }
        bool IsDisabled() { return !IsEnabled(); }
        bool IsVisible() { return Visibility == VISIBILITY::VISIBLE; }
    };
} }

#pragma region Copyright (c) 2014-2017 OpenRCT2 Developers
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

#include <algorithm>
#include "../audio/audio.h"
#include "../config/Config.h"
#include "../Context.h"
#include "../core/Guard.hpp"
#include "../core/Math.hpp"
#include "../core/Util.hpp"
#include "../drawing/Drawing.h"
#include "../Editor.h"
#include "../Game.h"
#include "../Input.h"
#include "../interface/Cursors.h"
#include "../localisation/Localisation.h"
#include "../localisation/StringIds.h"
#include "../OpenRCT2.h"
#include "../platform/platform.h"
#include "../scenario/Scenario.h"
#include "../sprites.h"
#include "../world/Map.h"
#include "../world/Sprite.h"
#include "Viewport.h"
#include "Widget.h"
#include "Window.h"
#include "Window_internal.h"

#define RCT2_FIRST_WINDOW       (g_window_list)
#define RCT2_LAST_WINDOW        (gWindowNextSlot - 1)
#define RCT2_NEW_WINDOW         (gWindowNextSlot)

// The amount of pixels to scroll per wheel click
#define WINDOW_SCROLL_PIXELS    17

rct_window g_window_list[WINDOW_LIMIT_MAX + WINDOW_LIMIT_RESERVED];
rct_window * gWindowFirst;
rct_window * gWindowNextSlot = nullptr;
rct_window * gWindowAudioExclusive;

uint16 TextInputDescriptionArgs[4];
widget_identifier gCurrentTextBox = { { 255, 0 }, 0 };
char gTextBoxInput[TEXT_INPUT_SIZE] = { 0 };
sint32 gMaxTextBoxInputLength = 0;
sint32 gTextBoxFrameNo = 0;
bool gUsingWidgetTextBox = false;
TextInputSession * gTextInput;

uint16 gWindowUpdateTicks;
uint16 gWindowMapFlashingFlags;

colour_t gCurrentWindowColours[4];

// converted from uint16 values at 0x009A41EC - 0x009A4230
// these are percentage coordinates of the viewport to centre to, if a window is obscuring a location, the next is tried
static constexpr const float window_scroll_locations[][2] = {
    {0.5f,      0.5f},
    {0.75f,     0.5f},
    {0.25f,     0.5f},
    {0.5f,      0.75f},
    {0.5f,      0.25f},
    {0.75f,     0.75f},
    {0.75f,     0.25f},
    {0.25f,     0.75f},
    {0.25f,     0.25f},
    {0.125f,    0.5f},
    {0.875f,    0.5f},
    {0.5f,      0.125f},
    {0.5f,      0.875f},
    {0.875f,    0.125f},
    {0.875f,    0.875f},
    {0.125f,    0.875f},
    {0.125f,    0.125f},
};

static sint32 _previousAbsoluteWheel = 0;

static bool window_fits_between_others(sint32 x, sint32 y, sint32 width, sint32 height);
static void window_all_wheel_input();
static sint32 window_draw_split(rct_drawpixelinfo *dpi, rct_window *w, sint32 left, sint32 top, sint32 right, sint32 bottom);
static void window_draw_single(rct_drawpixelinfo *dpi, rct_window *w, sint32 left, sint32 top, sint32 right, sint32 bottom);

static sint32 window_get_widget_index(rct_window *w, rct_widget *widget)
{
    sint32 i = 0;
    for (rct_widget *widget2 = w->widgets; widget2->type != WWT_LAST; widget2++, i++)
        if (widget == widget2)
            return i;
    return -1;
}

static sint32 window_get_scroll_index(rct_window *w, sint32 targetWidgetIndex)
{
    if (w->widgets[targetWidgetIndex].type != WWT_SCROLL)
        return -1;

    sint32 scrollIndex = 0;
    rct_widgetindex widgetIndex = 0;
    for (rct_widget *widget = w->widgets; widget->type != WWT_LAST; widget++, widgetIndex++) {
        if (widgetIndex == targetWidgetIndex)
            break;
        if (widget->type == WWT_SCROLL)
            scrollIndex++;
    }

    return scrollIndex;
}

static rct_widget *window_get_scroll_widget(rct_window *w, sint32 scrollIndex)
{
    for (rct_widget *widget = w->widgets; widget->type != WWT_LAST; widget++) {
        if (widget->type != WWT_SCROLL)
            continue;

        if (scrollIndex == 0)
            return widget;
        scrollIndex--;
    }

    return nullptr;
}

/**
 *
 *  rct2: 0x006ED7B0
 */
void window_dispatch_update_all()
{
    // gTooltipNotShownTicks++;
    for (rct_window *w = RCT2_LAST_WINDOW; w >= g_window_list; w--)
        window_event_update_call(w);
}

void window_update_all_viewports()
{
    for (rct_window *w = g_window_list; w < RCT2_NEW_WINDOW; w++)
        if (w->viewport != nullptr && window_is_visible(w))
            viewport_update_position(w);
}

/**
 *
 *  rct2: 0x006E77A1
 */
void window_update_all()
{
    // gfx_draw_all_dirty_blocks();
    // window_update_all_viewports();
    // gfx_draw_all_dirty_blocks();

    // 1000 tick update
    gWindowUpdateTicks += gTicksSinceLastUpdate;
    if (gWindowUpdateTicks >= 1000) {
        gWindowUpdateTicks = 0;
        for (rct_window* w = RCT2_LAST_WINDOW; w >= g_window_list; w--)
            window_event_unknown_07_call(w);
    }

    // Border flash invalidation
    for (rct_window* w = RCT2_LAST_WINDOW; w >= g_window_list; w--) {
        if (w->flags & WF_WHITE_BORDER_MASK) {
            w->flags -= WF_WHITE_BORDER_ONE;
            if (!(w->flags & WF_WHITE_BORDER_MASK))
                window_invalidate(w);
        }
    }

    window_all_wheel_input();
}

/**
 *
 *  rct2: 0x006E78E3
 */
static void window_scroll_wheel_input(rct_window *w, sint32 scrollIndex, sint32 wheel)
{
    rct_scroll *scroll = &w->scrolls[scrollIndex];
    rct_widget *widget = window_get_scroll_widget(w, scrollIndex);
    rct_widgetindex widgetIndex = window_get_widget_index(w, widget);

    if (scroll->flags & VSCROLLBAR_VISIBLE) {
        sint32 size = widget->bottom - widget->top - 1;
        if (scroll->flags & HSCROLLBAR_VISIBLE)
            size -= 11;
        size = std::max(0, scroll->v_bottom - size);
        scroll->v_top = std::min(std::max(0, scroll->v_top + wheel), size);
    } else {
        sint32 size = widget->right - widget->left - 1;
        if (scroll->flags & VSCROLLBAR_VISIBLE)
            size -= 11;
        size = std::max(0, scroll->h_right - size);
        scroll->h_left = std::min(std::max(0, scroll->h_left + wheel), size);
    }

    widget_scroll_update_thumbs(w, widgetIndex);
    widget_invalidate(w, widgetIndex);
}

/**
 *
 *  rct2: 0x006E793B
 */
static sint32 window_wheel_input(rct_window *w, sint32 wheel)
{
    sint32 i = 0;
    for (rct_widget *widget = w->widgets; widget->type != WWT_LAST; widget++) {
        if (widget->type != WWT_SCROLL)
            continue;

        // Originally always checked first scroll view, bug maybe?
        rct_scroll *scroll = &w->scrolls[i];
        if (scroll->flags & (HSCROLLBAR_VISIBLE | VSCROLLBAR_VISIBLE)) {
            window_scroll_wheel_input(w, i, wheel);
            return 1;
        }
        i++;
    }

    return 0;
}

/**
 *
 *  rct2: 0x006E79FB
 */
static void window_viewport_wheel_input(rct_window *w, sint32 wheel)
{
    if (gScreenFlags & (SCREEN_FLAGS_TRACK_MANAGER | SCREEN_FLAGS_TITLE_DEMO))
        return;

    if (wheel < 0)
        window_zoom_in(w, true);
    else if (wheel > 0)
        window_zoom_out(w, true);
}

static bool window_other_wheel_input(rct_window* w, rct_widgetindex widgetIndex, sint32 wheel)
{
    // HACK: Until we have a new window system that allows us to add new events like mouse wheel easily,
    //       this selective approach will have to do.

    // Allow mouse wheel scrolling to increment or decrement the land tool size for various windows
    auto widgetType = w->widgets[widgetIndex].type;

    // Lower widgetIndex once or twice we got a type that matches, to allow scrolling on the increase/decrease buttons too
    sint32 attempts = 0;
    while (widgetType != WWT_IMGBTN && widgetType != WWT_STEPPER && widgetIndex > 0)
    {
        switch (widgetType)
        {
            case WWT_TRNBTN: // + and - for preview widget
            case WWT_BUTTON: // + and - for stepper widget
            {
                if (attempts > 0)
                {
                    // Verify that the previous button was of the same type
                    auto previousType = w->widgets[widgetIndex + 1].type;
                    if (previousType != widgetType)
                    {
                        return false;
                    }
                }
                break;
            }
            default:
                // The widget type is not an increment or decrement button
                return false;
        }

        attempts++;
        if (attempts > 2)
        {
            // We're 2 buttons up, and no preview or stepper widget was found
            return false;
        }

        widgetIndex--;
        widgetType = w->widgets[widgetIndex].type;
    }

    rct_widgetindex buttonWidgetIndex;
    uint16 expectedType;
    uint32 expectedContent[2];
    switch (widgetType)
    {
        case WWT_IMGBTN:
            buttonWidgetIndex = wheel < 0 ? widgetIndex + 2 : widgetIndex + 1;
            expectedType = WWT_TRNBTN;
            expectedContent[0] = IMAGE_TYPE_REMAP | SPR_LAND_TOOL_DECREASE;
            expectedContent[1] = IMAGE_TYPE_REMAP | SPR_LAND_TOOL_INCREASE;
            break;
        case WWT_STEPPER:
            buttonWidgetIndex = wheel < 0 ? widgetIndex + 1 : widgetIndex + 2;
            expectedType = WWT_BUTTON;
            expectedContent[0] = STR_NUMERIC_UP;
            expectedContent[1] = STR_NUMERIC_DOWN;
            break;
        default: return false;
    }

    if (widget_is_disabled(w, buttonWidgetIndex))
    {
        return false;
    }

    auto button1Type = w->widgets[widgetIndex + 1].type;
    auto button1Image = w->widgets[widgetIndex + 1].image;
    auto button2Type = w->widgets[widgetIndex + 2].type;
    auto button2Image = w->widgets[widgetIndex + 2].image;
    if (button1Type != expectedType || button2Type != expectedType || button1Image != expectedContent[0]
        || button2Image != expectedContent[1])
    {
        return false;
    }

    window_event_mouse_down_call(w, buttonWidgetIndex);
    return true;
}

/**
 *
 *  rct2: 0x006E7868
 */
static void window_all_wheel_input()
{
    // Get wheel value
    CursorState * cursorState = (CursorState *)context_get_cursor_state();
    sint32 absolute_wheel = cursorState->wheel;
    sint32 relative_wheel = absolute_wheel - _previousAbsoluteWheel;
    sint32 pixel_scroll = relative_wheel * WINDOW_SCROLL_PIXELS;
    _previousAbsoluteWheel = absolute_wheel;

    if (relative_wheel == 0)
        return;

    // Check window cursor is over
    if (!(input_test_flag(INPUT_FLAG_5))) {
        rct_window *w = window_find_from_point(cursorState->x, cursorState->y);
        if (w != nullptr) {
            // Check if main window
            if (w->classification == WC_MAIN_WINDOW || w->classification == WC_VIEWPORT) {
                window_viewport_wheel_input(w, relative_wheel);
                return;
            }

            // Check scroll view, cursor is over
            rct_widgetindex widgetIndex = window_find_widget_from_point(w, cursorState->x, cursorState->y);
            if (widgetIndex != -1) {
                rct_widget *widget = &w->widgets[widgetIndex];
                if (widget->type == WWT_SCROLL) {
                    sint32 scrollIndex = window_get_scroll_index(w, widgetIndex);
                    rct_scroll *scroll =  &w->scrolls[scrollIndex];
                    if (scroll->flags & (HSCROLLBAR_VISIBLE | VSCROLLBAR_VISIBLE)) {
                        window_scroll_wheel_input(w, window_get_scroll_index(w, widgetIndex), pixel_scroll);
                        return;
                    }
                } else {
                    if (window_other_wheel_input(w, widgetIndex, pixel_scroll)) {
                        return;
                    }
                }

                // Check other scroll views on window
                if (window_wheel_input(w, pixel_scroll))
                    return;
            }
        }
    }
}

static void window_close_surplus(sint32 cap, sint8 avoid_classification)
{
    sint32 count, i, diff;
    //find the amount of windows that are currently open
    count = WINDOW_LIMIT_MAX;
    for (i = 0; i < WINDOW_LIMIT_MAX; i++) {
        if (&g_window_list[i] == RCT2_NEW_WINDOW) {
            count = i;
            break;
        }
    }
    //difference between amount open and cap = amount to close
    diff = count - WINDOW_LIMIT_RESERVED - cap;
    for (i = 0; i < diff; i++) {
        rct_window *w = nullptr;
        //iterates through the list until it finds the newest window, or a window that can be closed
        for (w = g_window_list; w < RCT2_NEW_WINDOW; w++) {
            if (!(w->flags & (WF_STICK_TO_BACK | WF_STICK_TO_FRONT | WF_NO_AUTO_CLOSE)))
                break;
        }
        //skip window if window matches specified rct_windowclass (as user may be modifying via options)
        if (avoid_classification != -1 && w != nullptr && w->classification == avoid_classification) {
            continue;
        }
        window_close(w);
    }
}

/*
 * Changes the maximum amount of windows allowed
 */
void window_set_window_limit(sint32 value)
{
    sint32 prev = gConfigGeneral.window_limit;
    sint32 val = Math::Clamp(WINDOW_LIMIT_MIN, value, WINDOW_LIMIT_MAX);
    gConfigGeneral.window_limit = val;
    config_save_default();
    // Checks if value decreases and then closes surplus
    // windows if one sets a limit lower than the number of windows open
    if (val < prev) {
        window_close_surplus(val, WC_OPTIONS);
    }
}

/**
 * Closes the specified window.
 *  rct2: 0x006ECD4C
 *
 * @param window The window to close (esi).
 */
void window_close(rct_window* window)
{
    sint32 num_windows;

    if (window == nullptr)
        return;

    // Make a copy of the window class and number in case
    // the window order is changed by the close event.
    rct_windowclass cls = window->classification;
    rct_windownumber number = window->number;

    window_event_close_call(window);

    window = window_find_by_number(cls, number);
    if (window == nullptr)
        return;

    // Remove viewport
    if (window->viewport != nullptr) {
        window->viewport->width = 0;
        window->viewport = nullptr;
    }

    // Invalidate the window (area)
    window_invalidate(window);

    // Remove window from list and reshift all windows
    RCT2_NEW_WINDOW--;
    num_windows = (sint32)(RCT2_NEW_WINDOW - window);
    if (num_windows > 0)
        memmove(window, window + 1, num_windows * sizeof(rct_window));

    viewport_update_pointers();
}

/**
 * Closes all windows with the specified window class.
 *  rct2: 0x006ECCF4
 * @param cls (cl) with bit 15 set
 */
void window_close_by_class(rct_windowclass cls)
{
    for (rct_window *w = g_window_list; w < RCT2_NEW_WINDOW;) {
        if (w->classification == cls) {
            window_close(w);
            w = g_window_list;
        } else {
            w++;
        }
    }
}

/**
 * Closes all windows with specified window class and number.
 *  rct2: 0x006ECCF4
 * @param cls (cl) without bit 15 set
 * @param number (dx)
 */
void window_close_by_number(rct_windowclass cls, rct_windownumber number)
{
    for (rct_window* w = g_window_list; w < RCT2_NEW_WINDOW;) {
        if (w->classification == cls && w->number == number) {
            window_close(w);
            w = g_window_list;
        } else {
            w++;
        }
    }
}

/**
 * Finds the first window with the specified window class.
 *  rct2: 0x006EA8A0
 * @param cls (cl) with bit 15 set
 * @returns the window or NULL if no window was found.
 */
rct_window *window_find_by_class(rct_windowclass cls)
{
    for (rct_window *w = g_window_list; w < RCT2_NEW_WINDOW; w++)
        if (w->classification == cls)
            return w;

    return nullptr;
}

/**
 * Finds the first window with the specified window class and number.
 *  rct2: 0x006EA8A0
 * @param cls (cl) without bit 15 set
 * @param number (dx)
 * @returns the window or NULL if no window was found.
 */
rct_window *window_find_by_number(rct_windowclass cls, rct_windownumber number)
{
    rct_window *w;

    for (w = g_window_list; w < RCT2_NEW_WINDOW; w++)
        if (w->classification == cls && w->number == number)
            return w;

    return nullptr;
}

/**
 * Closes the top-most window
 *
 *  rct2: 0x006E403C
 */
void window_close_top()
{
    rct_window* w;

    window_close_by_class(WC_DROPDOWN);

    if (gScreenFlags & SCREEN_FLAGS_SCENARIO_EDITOR)
        if (gS6Info.editor_step != EDITOR_STEP_LANDSCAPE_EDITOR)
            return;

    for (w = RCT2_NEW_WINDOW - 1; w >= g_window_list; w--) {
        if (!(w->flags & (WF_STICK_TO_BACK | WF_STICK_TO_FRONT))) {
            window_close(w);
            return;
        }
    }
}

/**
 * Closes all open windows
 *
 *  rct2: 0x006EE927
 */
void window_close_all()
{
    if (gWindowNextSlot == nullptr)
    {
        return;
    }

    window_close_by_class(WC_DROPDOWN);

    for (rct_window * w = RCT2_LAST_WINDOW; w >= g_window_list; w--)
    {
        if (!(w->flags & (WF_STICK_TO_BACK | WF_STICK_TO_FRONT)))
        {
            window_close(w);
        }
    }
}

void window_close_all_except_class(rct_windowclass cls)
{
    rct_window* w;

    window_close_by_class(WC_DROPDOWN);

    for (w = g_window_list; w <= RCT2_LAST_WINDOW; w++){
        if (w->classification != cls && !(w->flags & (WF_STICK_TO_BACK | WF_STICK_TO_FRONT))) {
            window_close(w);
            w = g_window_list;
        }
    }
}

/**
 * Closes all windows, save for those having any of the passed flags.
 */
void window_close_all_except_flags(uint16 flags)
{
    for (rct_window * w = RCT2_LAST_WINDOW; w >= g_window_list; w--)
    {
        if (!(w->flags & flags))
        {
            window_close(w);
        }
    }
}

/**
 *
 *  rct2: 0x006EA845
 */
rct_window *window_find_from_point(sint32 x, sint32 y)
{
    rct_window *w;
    sint32 widget_index;

    for (w = RCT2_LAST_WINDOW; w >= g_window_list; w--) {
        if (x < w->x || x >= w->x + w->width || y < w->y || y >= w->y + w->height)
            continue;

        if (w->flags & WF_NO_BACKGROUND) {
            widget_index = window_find_widget_from_point(w, x, y);
            if (widget_index == -1)
                continue;
        }

        return w;
    }

    return nullptr;
}

/**
 *
 *  rct2: 0x006EA594
 * x (ax)
 * y (bx)
 * returns widget_index (edx)
 * EDI NEEDS TO BE SET TO w->widgets[widget_index] AFTER
 */
rct_widgetindex window_find_widget_from_point(rct_window *w, sint32 x, sint32 y)
{
    // Invalidate the window
    window_event_invalidate_call(w);

    // Find the widget at point x, y
    rct_widgetindex widget_index = -1;
    for (sint32 i = 0;; i++) {
        rct_widget *widget = &w->widgets[i];
        if (widget->type == WWT_LAST) {
            break;
        } else if (widget->type != WWT_EMPTY) {
            if (x >= w->x + widget->left && x <= w->x + widget->right &&
                y >= w->y + widget->top && y <= w->y + widget->bottom
            ) {
                widget_index = i;
            }
        }
    }

    // Return next widget if a dropdown
    if (widget_index != -1)
        if (w->widgets[widget_index].type == WWT_DROPDOWN)
            widget_index++;

    // Return the widget index
    return widget_index;
}

/**
 * Invalidates the specified window.
 *  rct2: 0x006EB13A
 *
 * @param window The window to invalidate (esi).
 */
void window_invalidate(rct_window *window)
{
    if (window != nullptr)
        gfx_set_dirty_blocks(window->x, window->y, window->x + window->width, window->y + window->height);
}

/**
 * Invalidates all windows with the specified window class.
 *  rct2: 0x006EC3AC
 * @param cls (al) with bit 14 set
 */
void window_invalidate_by_class(rct_windowclass cls)
{
    rct_window* w;

    for (w = g_window_list; w < RCT2_NEW_WINDOW; w++)
        if (w->classification == cls)
            window_invalidate(w);
}

/**
 * Invalidates all windows with the specified window class and number.
 *  rct2: 0x006EC3AC
 * @param (ah) widget index
 * @param cls (al) without bit 14 set
 * @param number (bx)
 */
void window_invalidate_by_number(rct_windowclass cls, rct_windownumber number)
{
    rct_window* w;

    for (w = g_window_list; w < RCT2_NEW_WINDOW; w++)
        if (w->classification == cls && w->number == number)
            window_invalidate(w);
}

/**
 * Invalidates all windows.
 */
void window_invalidate_all()
{
    rct_window* w;

    for (w = g_window_list; w < RCT2_NEW_WINDOW; w++)
        window_invalidate(w);
}

/**
 * Invalidates the specified widget of a window.
 *  rct2: 0x006EC402
 */
void widget_invalidate(rct_window *w, rct_widgetindex widgetIndex)
{
    rct_widget* widget;

    assert(w != nullptr);
#ifdef DEBUG
    for (sint32 i = 0; i <= widgetIndex; i++) {
        assert(w->widgets[i].type != WWT_LAST);
    }
#endif

    widget = &w->widgets[widgetIndex];
    if (widget->left == -2)
        return;

    gfx_set_dirty_blocks(w->x + widget->left, w->y + widget->top, w->x + widget->right + 1, w->y + widget->bottom + 1);
}

/**
 * Invalidates the specified widget of all windows that match the specified window class.
 */
void widget_invalidate_by_class(rct_windowclass cls, rct_widgetindex widgetIndex)
{
    rct_window* w;

    for (w = g_window_list; w < RCT2_NEW_WINDOW; w++)
        if (w->classification == cls)
            widget_invalidate(w, widgetIndex);
}

/**
 * Invalidates the specified widget of all windows that match the specified window class and number.
 *  rct2: 0x006EC3AC
 * @param (ah) widget index
 * @param cls (al) with bit 15 set
 * @param number (bx)
 */
void widget_invalidate_by_number(rct_windowclass cls, rct_windownumber number, rct_widgetindex widgetIndex)
{
    rct_window* w;

    for (w = g_window_list; w < RCT2_NEW_WINDOW; w++)
        if (w->classification == cls && w->number == number)
            widget_invalidate(w, widgetIndex);
}

/**
 *
 *  rct2: 0x006EAE4E
 *
 * @param w The window (esi).
 */
void window_update_scroll_widgets(rct_window *w)
{
    sint32 scrollIndex, width, height, scrollPositionChanged;
    rct_widgetindex widgetIndex;
    rct_scroll *scroll;
    rct_widget *widget;

    widgetIndex = 0;
    scrollIndex = 0;
    assert(w != nullptr);
    for (widget = w->widgets; widget->type != WWT_LAST; widget++, widgetIndex++) {
        if (widget->type != WWT_SCROLL)
            continue;

        scroll = &w->scrolls[scrollIndex];
        width = 0;
        height = 0;
        window_get_scroll_size(w, scrollIndex, &width, &height);
        if (height == 0){
            scroll->v_top = 0;
        }
        else if (width == 0){
            scroll->h_left = 0;
        }
        width++;
        height++;

        scrollPositionChanged = 0;
        if ((widget->content & SCROLL_HORIZONTAL) && width != scroll->h_right) {
            scrollPositionChanged = 1;
            scroll->h_right = width;
        }

        if ((widget->content & SCROLL_VERTICAL) && height != scroll->v_bottom) {
            scrollPositionChanged = 1;
            scroll->v_bottom = height;
        }

        if (scrollPositionChanged) {
            widget_scroll_update_thumbs(w, widgetIndex);
            window_invalidate(w);
        }
        scrollIndex++;
    }
}

sint32 window_get_scroll_data_index(rct_window *w, rct_widgetindex widget_index)
{
    sint32 i, result;

    result = 0;
    assert(w != nullptr);
    for (i = 0; i < widget_index; i++) {
        if (w->widgets[i].type == WWT_SCROLL)
            result++;
    }
    return result;
}

/**
 *
 *  rct2: 0x006ECDA4
 */
rct_window *window_bring_to_front(rct_window *w)
{
    if (w->flags & (WF_STICK_TO_BACK | WF_STICK_TO_FRONT))
        return w;

    rct_window *v;
    for (v = RCT2_LAST_WINDOW; v >= g_window_list; v--)
        if (!(v->flags & WF_STICK_TO_FRONT))
            break;

    if (v >= g_window_list && w != v) {
        do {
            rct_window t = *w;
            *w = *(w + 1);
            *(w + 1) = t;
            w++;
        } while (w != v);

        window_invalidate(w);
    }

    if (w->x + w->width < 20) {
        sint32 i = 20 - w->x;
        w->x += i;
        if (w->viewport != nullptr)
            w->viewport->x += i;
        window_invalidate(w);
    }

    return w;
}

rct_window *window_bring_to_front_by_class_with_flags(rct_windowclass cls, uint16 flags)
{
    rct_window* w;

    w = window_find_by_class(cls);
    if (w != nullptr) {
        w->flags |= flags;
        window_invalidate(w);
        w = window_bring_to_front(w);
    }

    return w;
}

rct_window *window_bring_to_front_by_class(rct_windowclass cls)
{
    return window_bring_to_front_by_class_with_flags(cls, WF_WHITE_BORDER_MASK);
}

/**
 *
 *  rct2: 0x006ED78A
 * cls (cl)
 * number (dx)
 */
rct_window *window_bring_to_front_by_number(rct_windowclass cls, rct_windownumber number)
{
    rct_window* w;

    w = window_find_by_number(cls, number);
    if (w != nullptr) {
        w->flags |= WF_WHITE_BORDER_MASK;
        window_invalidate(w);
        w = window_bring_to_front(w);
    }

    return w;
}

/**
 *
 *  rct2: 0x006EE65A
 */
void window_push_others_right(rct_window* window)
{
    for (rct_window* w = g_window_list; w < gWindowNextSlot; w++) {
        if (w == window)
            continue;
        if (w->flags & (WF_STICK_TO_BACK | WF_STICK_TO_FRONT))
            continue;
        if (w->x >= window->x + window->width)
            continue;
        if (w->x + w->width <= window->x)
            continue;
        if (w->y >= window->y + window->height)
            continue;
        if (w->y + w->height <= window->y)
            continue;

        window_invalidate(w);
        if (window->x + window->width + 13 >= context_get_width())
            continue;
        uint16 push_amount = window->x + window->width - w->x + 3;
        w->x += push_amount;
        window_invalidate(w);
        if (w->viewport != nullptr)
            w->viewport->x += push_amount;
    }
}

/**
 *
 *  rct2: 0x006EE6EA
 */
void window_push_others_below(rct_window *w1)
{
    sint32 push_amount;
    rct_window* w2;

    // Enumerate through all other windows
    for (w2 = g_window_list; w2 < RCT2_NEW_WINDOW; w2++) {
        if (w1 == w2)
            continue;

        // ?
        if (w2->flags & (WF_STICK_TO_BACK | WF_STICK_TO_FRONT))
            continue;

        // Check if w2 intersects with w1
        if (w2->x > (w1->x + w1->width) || w2->x + w2->width < w1->x)
            continue;
        if (w2->y > (w1->y + w1->height) || w2->y + w2->height < w1->y)
            continue;

        // Check if there is room to push it down
        if (w1->y + w1->height + 80 >= context_get_height())
            continue;

        // Invalidate the window's current area
        window_invalidate(w2);

        push_amount = w1->y + w1->height - w2->y + 3;
        w2->y += push_amount;

        // Invalidate the window's new area
        window_invalidate(w2);

        // Update viewport position if necessary
        if (w2->viewport != nullptr)
            w2->viewport->y += push_amount;
    }
}

/**
 *
 *  rct2: 0x006EE2E4
 */
rct_window *window_get_main()
{
    rct_window* w = nullptr;

    for (w = g_window_list; w < RCT2_NEW_WINDOW; w++)
        if (w->classification == WC_MAIN_WINDOW)
            return w;

    openrct2_assert(w != nullptr, "Failed to get main window");
    return nullptr;
}

/**
 * Based on
 *  rct2: 0x696ee9, 0x66842F, 0x006AF3B3
 */
void window_scroll_to_viewport(rct_window *w)
{
    sint32 x, y, z;
    rct_window *mainWindow;
    assert(w != nullptr);
    // In original checked to make sure x and y were not -1 as well.
    if (w->viewport == nullptr || w->viewport_focus_coordinates.y == -1)
        return;

    if (w->viewport_focus_sprite.type & VIEWPORT_FOCUS_TYPE_SPRITE) {
        rct_sprite *sprite = get_sprite(w->viewport_focus_sprite.sprite_id);
        x = sprite->unknown.x;
        y = sprite->unknown.y;
        z = sprite->unknown.z;
    } else {
        x = w->viewport_focus_coordinates.x;
        y = w->viewport_focus_coordinates.y & VIEWPORT_FOCUS_Y_MASK;
        z = w->viewport_focus_coordinates.z;
    }

    mainWindow = window_get_main();
    if (mainWindow != nullptr)
        window_scroll_to_location(mainWindow, x, y, z);
}

void window_set_location(rct_window *w, sint32 x, sint32 y, sint32 z)
{
    window_scroll_to_location(w, x, y, z);
    w->flags &= ~WF_SCROLLING_TO_LOCATION;
}

/**
*
*  rct2: 0x006E7C9C
* @param w (esi)
* @param x (eax)
* @param y (ecx)
* @param z (edx)
*/
void window_scroll_to_location(rct_window *w, sint32 x, sint32 y, sint32 z)
{
    LocationXYZ16 location_3d = {
        /* .x = */ (sint16)x,
        /* .y = */ (sint16)y,
        /* .z = */ (sint16)z
    };

    assert(w != nullptr);

    window_unfollow_sprite(w);

    if (w->viewport) {
        sint16 height = tile_element_height(x, y);
        if (z < height - 16) {
            if (!(w->viewport->flags & 1 << 0)) {
                w->viewport->flags |= 1 << 0;
                window_invalidate(w);
            }
        } else {
            if (w->viewport->flags & 1 << 0) {
                w->viewport->flags &= ~(1 << 0);
                window_invalidate(w);
            }
        }

        LocationXY16 map_coordinate = coordinate_3d_to_2d(&location_3d, get_current_rotation());

        sint32 i = 0;
        if (!(gScreenFlags & SCREEN_FLAGS_TITLE_DEMO)) {
            sint32 found = 0;
            while (!found) {
                sint16 x2 = w->viewport->x + (sint16)(w->viewport->width * window_scroll_locations[i][0]);
                sint16 y2 = w->viewport->y + (sint16)(w->viewport->height * window_scroll_locations[i][1]);
                rct_window* w2 = w;
                while (true) {
                    w2++;
                    if (w2 >= gWindowNextSlot) {
                        found = 1;
                        break;
                    }
                    sint16 x1 = w2->x - 10;
                    sint16 y1 = w2->y - 10;
                    if (x2 >= x1 && x2 <= w2->width + x1 + 20) {
                        if (y2 >= y1 && y2 <= w2->height + y1 + 20) {
                            // window is covering this area, try the next one
                            i++;
                            found = 0;
                            break;
                        }
                    }
                }
                if (i >= (sint32)Util::CountOf(window_scroll_locations)) {
                    i = 0;
                    found = 1;
                }
            }
        }
        // rct2: 0x006E7C76
        if (w->viewport_target_sprite == SPRITE_INDEX_NULL) {
            if (!(w->flags & WF_NO_SCROLLING)) {
                w->saved_view_x = map_coordinate.x - (sint16)(w->viewport->view_width * window_scroll_locations[i][0]);
                w->saved_view_y = map_coordinate.y - (sint16)(w->viewport->view_height * window_scroll_locations[i][1]);
                w->flags |= WF_SCROLLING_TO_LOCATION;
            }
        }
    }
}

/**
 *
 *  rct2: 0x00688956
 */
static void call_event_viewport_rotate_on_all_windows()
{
    rct_window *w;

    for (w = RCT2_NEW_WINDOW - 1; w >= g_window_list; w--)
        window_event_viewport_rotate_call(w);
}

/**
 *
 *  rct2: 0x0068881A
 * direction can be used to alter the camera rotation:
 *      1: clockwise
 *      -1: anti-clockwise
 */
void window_rotate_camera(rct_window *w, sint32 direction)
{
    rct_viewport *viewport = w->viewport;
    if (viewport == nullptr)
        return;

    sint16 x = (viewport->width >> 1) + viewport->x;
    sint16 y = (viewport->height >> 1) + viewport->y;
    sint16 z;

    //has something to do with checking if middle of the viewport is obstructed
    rct_viewport *other;
    screen_get_map_xy(x, y, &x, &y, &other);

    // other != viewport probably triggers on viewports in ride or guest window?
    // x is LOCATION_NULL if middle of viewport is obstructed by another window?
    if (x == LOCATION_NULL || other != viewport) {
        x = (viewport->view_width >> 1) + viewport->view_x;
        y = (viewport->view_height >> 1) + viewport->view_y;

        viewport_adjust_for_map_height(&x, &y, &z);
    }
    else {
        z = tile_element_height(x, y);
    }

    gCurrentRotation = (get_current_rotation() + direction) & 3;

    sint32 new_x, new_y;
    centre_2d_coordinates(x, y, z, &new_x, &new_y, viewport);

    w->saved_view_x = new_x;
    w->saved_view_y = new_y;
    viewport->view_x = new_x;
    viewport->view_y = new_y;

    window_invalidate(w);

    call_event_viewport_rotate_on_all_windows();
    reset_all_sprite_quadrant_placements();
}

void window_viewport_get_map_coords_by_cursor(rct_window *w, sint16 *map_x, sint16 *map_y, sint16 *offset_x, sint16 *offset_y)
{
    // Get mouse position to offset against.
    sint32 mouse_x, mouse_y;
    context_get_cursor_position_scaled(&mouse_x, &mouse_y);

    // Compute map coordinate by mouse position.
    get_map_coordinates_from_pos(mouse_x, mouse_y, VIEWPORT_INTERACTION_MASK_NONE, map_x, map_y, nullptr, nullptr, nullptr);

    // Get viewport coordinates centring around the tile.
    sint32 base_height = tile_element_height(*map_x, *map_y);
    sint32 dest_x, dest_y;
    centre_2d_coordinates(*map_x, *map_y, base_height, &dest_x, &dest_y, w->viewport);

    // Rebase mouse position onto centre of window, and compensate for zoom level.
    sint32 rebased_x = ((w->width >> 1) - mouse_x) * (1 << w->viewport->zoom),
        rebased_y = ((w->height >> 1) - mouse_y) * (1 << w->viewport->zoom);

    // Compute cursor offset relative to tile.
    *offset_x = (w->saved_view_x - (dest_x + rebased_x)) * (1 << w->viewport->zoom);
    *offset_y = (w->saved_view_y - (dest_y + rebased_y)) * (1 << w->viewport->zoom);
}

void window_viewport_centre_tile_around_cursor(rct_window *w, sint16 map_x, sint16 map_y, sint16 offset_x, sint16 offset_y)
{
    // Get viewport coordinates centring around the tile.
    sint32 dest_x, dest_y;
    sint32 base_height = tile_element_height(map_x, map_y);
    centre_2d_coordinates(map_x, map_y, base_height, &dest_x, &dest_y, w->viewport);

    // Get mouse position to offset against.
    sint32 mouse_x, mouse_y;
    context_get_cursor_position_scaled(&mouse_x, &mouse_y);

    // Rebase mouse position onto centre of window, and compensate for zoom level.
    sint32 rebased_x = ((w->width >> 1) - mouse_x) * (1 << w->viewport->zoom),
        rebased_y = ((w->height >> 1) - mouse_y) * (1 << w->viewport->zoom);

    // Apply offset to the viewport.
    w->saved_view_x = dest_x + rebased_x + (offset_x / (1 << w->viewport->zoom));
    w->saved_view_y = dest_y + rebased_y + (offset_y / (1 << w->viewport->zoom));
}

void window_zoom_set(rct_window *w, sint32 zoomLevel, bool atCursor)
{
    rct_viewport* v = w->viewport;

    zoomLevel = Math::Clamp(0, zoomLevel, MAX_ZOOM_LEVEL);
    if (v->zoom == zoomLevel)
        return;

    // Zooming to cursor? Remember where we're pointing at the moment.
    sint16 saved_map_x = 0;
    sint16 saved_map_y = 0;
    sint16 offset_x = 0;
    sint16 offset_y = 0;
    if (gConfigGeneral.zoom_to_cursor && atCursor) {
        window_viewport_get_map_coords_by_cursor(w, &saved_map_x, &saved_map_y, &offset_x, &offset_y);
    }

    // Zoom in
    while (v->zoom > zoomLevel) {
        v->zoom--;
        w->saved_view_x += v->view_width / 4;
        w->saved_view_y += v->view_height / 4;
        v->view_width  /= 2;
        v->view_height /= 2;
    }

    // Zoom out
    while (v->zoom < zoomLevel) {
        v->zoom++;
        w->saved_view_x -= v->view_width / 2;
        w->saved_view_y -= v->view_height / 2;
        v->view_width  *= 2;
        v->view_height *= 2;
    }

    // Zooming to cursor? Centre around the tile we were hovering over just now.
    if (gConfigGeneral.zoom_to_cursor && atCursor) {
        window_viewport_centre_tile_around_cursor(w, saved_map_x, saved_map_y, offset_x, offset_y);
    }

    // HACK: Prevents the redraw from failing when there is
    // a window on top of the viewport.
    window_bring_to_front(w);
    window_invalidate(w);
}

/**
 *
 *  rct2: 0x006887A6
 */
void window_zoom_in(rct_window *w, bool atCursor)
{
    window_zoom_set(w, w->viewport->zoom - 1, atCursor);
}

/**
 *
 *  rct2: 0x006887E0
 */
void window_zoom_out(rct_window *w, bool atCursor)
{
    window_zoom_set(w, w->viewport->zoom + 1, atCursor);
}

void main_window_zoom(bool zoomIn, bool atCursor) {
    if (gScreenFlags & SCREEN_FLAGS_TITLE_DEMO)
        return;
    if (!(gScreenFlags & SCREEN_FLAGS_SCENARIO_EDITOR) || gS6Info.editor_step == EDITOR_STEP_LANDSCAPE_EDITOR) {
        if (!(gScreenFlags & SCREEN_FLAGS_TRACK_MANAGER)) {
            rct_window *mainWindow = window_get_main();
            if (mainWindow != nullptr)
                window_zoom_set(mainWindow, mainWindow->viewport->zoom + (zoomIn ? -1 : 1), atCursor);
        }
    }
}

/**
 * Draws a window that is in the specified region.
 *  rct2: 0x006E756C
 * left (ax)
 * top (bx)
 * right (dx)
 * bottom (bp)
 */
void window_draw(rct_drawpixelinfo *dpi, rct_window *w, sint32 left, sint32 top, sint32 right, sint32 bottom)
{
    if (!window_is_visible(w)) return;

    // Split window into only the regions that require drawing
    if (window_draw_split(dpi, w, left, top, right, bottom))
        return;

    // Clamp region
    left = std::max<sint32>(left, w->x);
    top = std::max<sint32>(top, w->y);
    right = std::min<sint32>(right, w->x + w->width);
    bottom = std::min<sint32>(bottom, w->y + w->height);
    if (left >= right) return;
    if (top >= bottom) return;

    // Draw the window in this region
    for (rct_window *v = w; v < RCT2_NEW_WINDOW; v++) {
        // Don't draw overlapping opaque windows, they won't have changed
        if ((w == v || (v->flags & WF_TRANSPARENT)) && window_is_visible(v)) {
            window_draw_single(dpi, v, left, top, right, bottom);
        }
    }
}

/**
 * Splits a drawing of a window into regions that can be seen and are not hidden
 * by other opaque overlapping windows.
 */
static sint32 window_draw_split(rct_drawpixelinfo *dpi, rct_window *w, sint32 left, sint32 top, sint32 right, sint32 bottom)
{
    rct_window* topwindow;

    // Divide the draws up for only the visible regions of the window recursively
    for (topwindow = w + 1; topwindow < RCT2_NEW_WINDOW; topwindow++) {
        // Check if this window overlaps w
        if (topwindow->x >= right || topwindow->y >= bottom)
            continue;
        if (topwindow->x + topwindow->width <= left || topwindow->y + topwindow->height <= top)
            continue;
        if (topwindow->flags & WF_TRANSPARENT)
            continue;

        // A window overlaps w, split up the draw into two regions where the window starts to overlap
        if (topwindow->x > left) {
            // Split draw at topwindow.left
            window_draw(dpi, w, left, top, topwindow->x, bottom);
            window_draw(dpi, w, topwindow->x, top, right, bottom);
        } else if (topwindow->x + topwindow->width < right) {
            // Split draw at topwindow.right
            window_draw(dpi, w, left, top, topwindow->x + topwindow->width, bottom);
            window_draw(dpi, w, topwindow->x + topwindow->width, top, right, bottom);
        } else if (topwindow->y > top) {
            // Split draw at topwindow.top
            window_draw(dpi, w, left, top, right, topwindow->y);
            window_draw(dpi, w, left, topwindow->y, right, bottom);
        } else if (topwindow->y + topwindow->height < bottom) {
            // Split draw at topwindow.bottom
            window_draw(dpi, w, left, top, right, topwindow->y + topwindow->height);
            window_draw(dpi, w, left, topwindow->y + topwindow->height, right, bottom);
        }

        // Drawing for this region should be done now, exit
        return 1;
    }

    // No windows overlap
    return 0;
}

static void window_draw_single(rct_drawpixelinfo *dpi, rct_window *w, sint32 left, sint32 top, sint32 right, sint32 bottom)
{
    // Copy dpi so we can crop it
    rct_drawpixelinfo copy = *dpi;
    dpi = &copy;

    // Clamp left to 0
    sint32 overflow = left - dpi->x;
    if (overflow > 0) {
        dpi->x += overflow;
        dpi->width -= overflow;
        if (dpi->width <= 0)
            return;
        dpi->pitch += overflow;
        dpi->bits += overflow;
    }

    // Clamp width to right
    overflow = dpi->x + dpi->width - right;
    if (overflow > 0) {
        dpi->width -= overflow;
        if (dpi->width <= 0)
            return;
        dpi->pitch += overflow;
    }

    // Clamp top to 0
    overflow = top - dpi->y;
    if (overflow > 0) {
        dpi->y += overflow;
        dpi->height -= overflow;
        if (dpi->height <= 0)
            return;
        dpi->bits += (dpi->width + dpi->pitch) * overflow;
    }

    // Clamp height to bottom
    overflow = dpi->y + dpi->height - bottom;
    if (overflow > 0) {
        dpi->height -= overflow;
        if (dpi->height <= 0)
            return;
    }

    // Invalidate modifies the window colours so first get the correct
    // colour before setting the global variables for the string painting
    window_event_invalidate_call(w);

    // Text colouring
    gCurrentWindowColours[0] = NOT_TRANSLUCENT(w->colours[0]);
    gCurrentWindowColours[1] = NOT_TRANSLUCENT(w->colours[1]);
    gCurrentWindowColours[2] = NOT_TRANSLUCENT(w->colours[2]);
    gCurrentWindowColours[3] = NOT_TRANSLUCENT(w->colours[3]);

    window_event_paint_call(w, dpi);
}

/**
 *
 *  rct2: 0x00685BE1
 *
 * @param dpi (edi)
 * @param w (esi)
 */
void window_draw_viewport(rct_drawpixelinfo *dpi, rct_window *w)
{
    viewport_render(dpi, w->viewport, dpi->x, dpi->y, dpi->x + dpi->width, dpi->y + dpi->height);
}

void window_set_position(rct_window *w, sint32 x, sint32 y)
{
    window_move_position(w, x - w->x, y - w->y);
}

void window_move_position(rct_window *w, sint32 dx, sint32 dy)
{
    if (dx == 0 && dy == 0)
        return;

    // Invalidate old region
    window_invalidate(w);

    // Translate window and viewport
    w->x += dx;
    w->y += dy;
    if (w->viewport != nullptr) {
        w->viewport->x += dx;
        w->viewport->y += dy;
    }

    // Invalidate new region
    window_invalidate(w);
}

void window_resize(rct_window *w, sint32 dw, sint32 dh)
{
    sint32 i;
    if (dw == 0 && dh == 0)
        return;

    // Invalidate old region
    window_invalidate(w);

    // Clamp new size to minimum and maximum
    w->width = Math::Clamp<sint16>(w->min_width, w->width + dw, w->max_width);
    w->height = Math::Clamp<sint16>(w->min_height, w->height + dh, w->max_height);

    window_event_resize_call(w);
    window_event_invalidate_call(w);

    // Update scroll widgets
    for (i = 0; i < 3; i++) {
        w->scrolls[i].h_right = WINDOW_SCROLL_UNDEFINED;
        w->scrolls[i].v_bottom = WINDOW_SCROLL_UNDEFINED;
    }
    window_update_scroll_widgets(w);

    // Invalidate new region
    window_invalidate(w);
}

void window_set_resize(rct_window *w, sint32 minWidth, sint32 minHeight, sint32 maxWidth, sint32 maxHeight)
{
    w->min_width = minWidth;
    w->min_height = minHeight;
    w->max_width = maxWidth;
    w->max_height = maxHeight;

    // Clamp width and height to minimum and maximum
    sint32 width = Math::Clamp<sint32>(minWidth, w->width, maxWidth);
    sint32 height = Math::Clamp<sint32>(minHeight, w->height, maxHeight);

    // Resize window if size has changed
    if (w->width != width || w->height != height) {
        window_invalidate(w);
        w->width = width;
        w->height = height;
        window_invalidate(w);
    }
}

/**
 *
 *  rct2: 0x006EE212
 *
 * @param tool (al)
 * @param widgetIndex (dx)
 * @param w (esi)
 */
bool tool_set(rct_window *w, rct_widgetindex widgetIndex, TOOL_IDX tool)
{
    if (input_test_flag(INPUT_FLAG_TOOL_ACTIVE)) {
        if (
            w->classification == gCurrentToolWidget.window_classification &&
            w->number == gCurrentToolWidget.window_number &&
            widgetIndex == gCurrentToolWidget.widget_index
        ) {
            tool_cancel();
            return true;
        } else {
            tool_cancel();
        }
    }

    input_set_flag(INPUT_FLAG_TOOL_ACTIVE, true);
    input_set_flag(INPUT_FLAG_6, false);
    gCurrentToolId = tool;
    gCurrentToolWidget.window_classification = w->classification;
    gCurrentToolWidget.window_number = w->number;
    gCurrentToolWidget.widget_index = widgetIndex;
    return false;
}

/**
 *
 *  rct2: 0x006EE281
 */
void tool_cancel()
{
    if (input_test_flag(INPUT_FLAG_TOOL_ACTIVE)) {
        input_set_flag(INPUT_FLAG_TOOL_ACTIVE, false);

        map_invalidate_selection_rect();
        map_invalidate_map_selection_tiles();

        // Reset map selection
        gMapSelectFlags = 0;

        if (gCurrentToolWidget.widget_index != -1) {
            // Invalidate tool widget
            widget_invalidate_by_number(
                gCurrentToolWidget.window_classification,
                gCurrentToolWidget.window_number,
                gCurrentToolWidget.widget_index
            );

            // Abort tool event
            rct_window *w = window_find_by_number(
                gCurrentToolWidget.window_classification,
                gCurrentToolWidget.window_number
            );
            if (w != nullptr)
                window_event_tool_abort_call(w, gCurrentToolWidget.widget_index);
        }
    }
}

void window_event_close_call(rct_window *w)
{
    if (w->event_handlers->close != nullptr)
        w->event_handlers->close(w);
}

void window_event_mouse_up_call(rct_window *w, rct_widgetindex widgetIndex)
{
    if (w->event_handlers->mouse_up != nullptr)
        w->event_handlers->mouse_up(w, widgetIndex);
}

void window_event_resize_call(rct_window *w)
{
    if (w->event_handlers->resize != nullptr)
        w->event_handlers->resize(w);
}

void window_event_mouse_down_call(rct_window *w, rct_widgetindex widgetIndex)
{
    if (w->event_handlers->mouse_down != nullptr)
        w->event_handlers->mouse_down(w, widgetIndex, &w->widgets[widgetIndex]);
}

void window_event_dropdown_call(rct_window *w, rct_widgetindex widgetIndex, sint32 dropdownIndex)
{
    if (w->event_handlers->dropdown != nullptr)
        w->event_handlers->dropdown(w, widgetIndex, dropdownIndex);
}

void window_event_unknown_05_call(rct_window *w)
{
    if (w->event_handlers->unknown_05 != nullptr)
        w->event_handlers->unknown_05(w);
}

void window_event_update_call(rct_window *w)
{
    if (w->event_handlers->update != nullptr)
        w->event_handlers->update(w);
}

void window_event_unknown_07_call(rct_window *w)
{
    if (w->event_handlers->unknown_07 != nullptr)
        w->event_handlers->unknown_07(w);
}

void window_event_unknown_08_call(rct_window *w)
{
    if (w->event_handlers->unknown_08 != nullptr)
        w->event_handlers->unknown_08(w);
}

void window_event_tool_update_call(rct_window *w, rct_widgetindex widgetIndex, sint32 x, sint32 y)
{
    if (w->event_handlers->tool_update != nullptr)
        w->event_handlers->tool_update(w, widgetIndex, x, y);
}

void window_event_tool_down_call(rct_window *w, rct_widgetindex widgetIndex, sint32 x, sint32 y)
{
    if (w->event_handlers->tool_down != nullptr)
        w->event_handlers->tool_down(w, widgetIndex, x, y);
}

void window_event_tool_drag_call(rct_window *w, rct_widgetindex widgetIndex, sint32 x, sint32 y)
{
    if (w->event_handlers->tool_drag != nullptr)
        w->event_handlers->tool_drag(w, widgetIndex, x, y);
}

void window_event_tool_up_call(rct_window *w, rct_widgetindex widgetIndex, sint32 x, sint32 y)
{
    if (w->event_handlers->tool_up != nullptr)
        w->event_handlers->tool_up(w, widgetIndex, x, y);
}

void window_event_tool_abort_call(rct_window *w, rct_widgetindex widgetIndex)
{
    if (w->event_handlers->tool_abort != nullptr)
        w->event_handlers->tool_abort(w, widgetIndex);
}

void window_event_unknown_0E_call(rct_window *w)
{
    if (w->event_handlers->unknown_0E != nullptr)
        w->event_handlers->unknown_0E(w);
}

void window_get_scroll_size(rct_window *w, sint32 scrollIndex, sint32 *width, sint32 *height)
{
    if (w->event_handlers->get_scroll_size != nullptr) {
        w->event_handlers->get_scroll_size(w, scrollIndex, width, height);
    }
}

void window_event_scroll_mousedown_call(rct_window *w, sint32 scrollIndex, sint32 x, sint32 y)
{
    if (w->event_handlers->scroll_mousedown != nullptr)
        w->event_handlers->scroll_mousedown(w, scrollIndex, x, y);
}

void window_event_scroll_mousedrag_call(rct_window *w, sint32 scrollIndex, sint32 x, sint32 y)
{
    if (w->event_handlers->scroll_mousedrag != nullptr)
        w->event_handlers->scroll_mousedrag(w, scrollIndex, x, y);
}

void window_event_scroll_mouseover_call(rct_window *w, sint32 scrollIndex, sint32 x, sint32 y)
{
    if (w->event_handlers->scroll_mouseover != nullptr)
        w->event_handlers->scroll_mouseover(w, scrollIndex, x, y);
}

void window_event_textinput_call(rct_window *w, rct_widgetindex widgetIndex, char *text)
{
    if (w->event_handlers->text_input != nullptr)
        w->event_handlers->text_input(w, widgetIndex, text);
}

void window_event_viewport_rotate_call(rct_window *w)
{
    if (w->event_handlers->viewport_rotate != nullptr)
        w->event_handlers->viewport_rotate(w);
}

void window_event_unknown_15_call(rct_window *w, sint32 scrollIndex, sint32 scrollAreaType)
{
    if (w->event_handlers->unknown_15 != nullptr)
        w->event_handlers->unknown_15(w, scrollIndex, scrollAreaType);
}

rct_string_id window_event_tooltip_call(rct_window *w, rct_widgetindex widgetIndex)
{
    rct_string_id result = 0;
    if (w->event_handlers->tooltip != nullptr)
        w->event_handlers->tooltip(w, widgetIndex, &result);
    return result;
}

sint32 window_event_cursor_call(rct_window *w, rct_widgetindex widgetIndex, sint32 x, sint32 y)
{
    sint32 cursorId = CURSOR_ARROW;
    if (w->event_handlers->cursor != nullptr)
        w->event_handlers->cursor(w, widgetIndex, x, y, &cursorId);
    return cursorId;
}

void window_event_moved_call(rct_window *w, sint32 x, sint32 y)
{
    if (w->event_handlers->moved != nullptr)
        w->event_handlers->moved(w, x, y);
}

void window_event_invalidate_call(rct_window *w)
{
    if (w->event_handlers->invalidate != nullptr)
        w->event_handlers->invalidate(w);
}

void window_event_paint_call(rct_window *w, rct_drawpixelinfo *dpi)
{
    if (w->event_handlers->paint != nullptr)
        w->event_handlers->paint(w, dpi);
}

void window_event_scroll_paint_call(rct_window *w, rct_drawpixelinfo *dpi, sint32 scrollIndex)
{
    if (w->event_handlers->scroll_paint != nullptr)
        w->event_handlers->scroll_paint(w, dpi, scrollIndex);
}

/**
 * Bubbles an item one position up in the window list.  This is done by swapping
 * the two locations.
 *  rct2: New function not from rct2
 */
void window_bubble_list_item(rct_window* w, sint32 item_position){
    char swap = w->list_item_positions[item_position];
    w->list_item_positions[item_position] = w->list_item_positions[item_position + 1];
    w->list_item_positions[item_position + 1] = swap;
}

/**
 *
 *  rct2: 0x006ED710
 * Called after a window resize to move windows if they
 * are going to be out of sight.
 */
void window_relocate_windows(sint32 width, sint32 height){
    sint32 new_location = 8;
    for (rct_window* w = g_window_list; w < RCT2_NEW_WINDOW; w++){

        // Work out if the window requires moving
        if (w->x + 10 < width){
            if (w->flags&(WF_STICK_TO_BACK | WF_STICK_TO_FRONT)){
                if (w->y -22 < height)continue;
            }
            if (w->y + 10 < height)continue;
        }

        // Calculate the new locations
        sint32 x = w->x;
        sint32 y = w->y;
        w->x = new_location;
        w->y = new_location + TOP_TOOLBAR_HEIGHT + 1;

        // Move the next new location so windows are not directly on top
        new_location += 8;

        // Adjust the viewport if required.
        if (w->viewport != nullptr){
            w->viewport->x -= x - w->x;
            w->viewport->y -= y - w->y;
        }
    }
}

/**
* rct2: 0x0066B905
*/
void window_resize_gui(sint32 width, sint32 height)
{
    if (gScreenFlags & (SCREEN_FLAGS_SCENARIO_EDITOR | SCREEN_FLAGS_TRACK_DESIGNER | SCREEN_FLAGS_TRACK_MANAGER)){
        window_resize_gui_scenario_editor(width, height);
        return;
    }
    rct_window *mainWind = window_get_main();
    if (mainWind != nullptr) {
        rct_viewport* viewport = mainWind->viewport;
        mainWind->width = width;
        mainWind->height = height;
        viewport->width = width;
        viewport->height = height;
        viewport->view_width = width << viewport->zoom;
        viewport->view_height = height << viewport->zoom;
        if (mainWind->widgets != nullptr && mainWind->widgets[WC_MAIN_WINDOW__0].type == WWT_VIEWPORT){
            mainWind->widgets[WC_MAIN_WINDOW__0].right = width;
            mainWind->widgets[WC_MAIN_WINDOW__0].bottom = height;
        }
    }

    rct_window *topWind = window_find_by_class(WC_TOP_TOOLBAR);
    if (topWind != nullptr) {
        topWind->width = std::max(640, width);
    }

    rct_window *bottomWind = window_find_by_class(WC_BOTTOM_TOOLBAR);
    if (bottomWind != nullptr) {
        bottomWind->y = height - 32;
        bottomWind->width = std::max(640, width);
    }

    rct_window *titleWind = window_find_by_class(WC_TITLE_MENU);
    if (titleWind != nullptr) {
        titleWind->x = (width - titleWind->width) / 2;
        titleWind->y = height - 142;
    }

    rct_window *exitWind = window_find_by_class(WC_TITLE_EXIT);
    if (exitWind != nullptr) {
        exitWind->x = width - 40;
        exitWind->y = height - 64;
    }

    rct_window *optionsWind = window_find_by_class(WC_TITLE_OPTIONS);
    if (optionsWind != nullptr) {
        optionsWind->x = width - 80;
    }

    gfx_invalidate_screen();
}

/**
* rct2: 0x0066F0DD
*/
void window_resize_gui_scenario_editor(sint32 width, sint32 height)
{
    rct_window* mainWind = window_get_main();
    if (mainWind != nullptr) {
        rct_viewport* viewport = mainWind->viewport;
        mainWind->width = width;
        mainWind->height = height;
        viewport->width = width;
        viewport->height = height;
        viewport->view_width = width << viewport->zoom;
        viewport->view_height = height << viewport->zoom;
        if (mainWind->widgets != nullptr && mainWind->widgets[WC_MAIN_WINDOW__0].type == WWT_VIEWPORT){
            mainWind->widgets[WC_MAIN_WINDOW__0].right = width;
            mainWind->widgets[WC_MAIN_WINDOW__0].bottom = height;
        }
    }

    rct_window *topWind = window_find_by_class(WC_TOP_TOOLBAR);
    if (topWind != nullptr) {
        topWind->width = std::max(640, width);
    }

    rct_window *bottomWind = window_find_by_class(WC_BOTTOM_TOOLBAR);
    if (bottomWind != nullptr) {
        bottomWind->y = height - 32;
        bottomWind->width = std::max(640, width);
    }

}

/* Based on rct2: 0x6987ED and another version from window_park */
void window_align_tabs(rct_window *w, rct_widgetindex start_tab_id, rct_widgetindex end_tab_id)
{
    sint32 i, x = w->widgets[start_tab_id].left;
    sint32 tab_width = w->widgets[start_tab_id].right - w->widgets[start_tab_id].left;

    for (i = start_tab_id; i <= end_tab_id; i++) {
        if (!(w->disabled_widgets & (1LL << i))) {
            w->widgets[i].left = x;
            w->widgets[i].right = x + tab_width;
            x += tab_width + 1;
        }
    }
}

/**
 *
 *  rct2: 0x006CBCC3
 */
void window_close_construction_windows()
{
    window_close_by_class(WC_RIDE_CONSTRUCTION);
    window_close_by_class(WC_FOOTPATH);
    window_close_by_class(WC_TRACK_DESIGN_LIST);
    window_close_by_class(WC_TRACK_DESIGN_PLACE);
}

/**
 * Update zoom based volume attenuation for ride music and clear music list.
 *  rct2: 0x006BC348
 */
void window_update_viewport_ride_music()
{
    rct_viewport *viewport;
    rct_window *w;

    gRideMusicParamsListEnd = &gRideMusicParamsList[0];
    g_music_tracking_viewport = nullptr;

    for (w = RCT2_LAST_WINDOW; w >= g_window_list; w--) {
        viewport = w->viewport;
        if (viewport == nullptr || !(viewport->flags & VIEWPORT_FLAG_SOUND_ON))
            continue;

        g_music_tracking_viewport = viewport;
        gWindowAudioExclusive = w;

        switch (viewport->zoom) {
        case 0:
            gVolumeAdjustZoom = 0;
            break;
        case 1:
            gVolumeAdjustZoom = 30;
            break;
        default:
            gVolumeAdjustZoom = 60;
            break;
        }
        break;
    }
}

static void window_snap_left(rct_window *w, sint32 proximity)
{
    sint32 right, rightMost, wLeftProximity, wRightProximity, wBottom;
    rct_window *mainWindow, *w2;

    mainWindow = window_get_main();

    wBottom = w->y + w->height;
    wLeftProximity = w->x - (proximity * 2);
    wRightProximity = w->x + (proximity * 2);
    rightMost = INT32_MIN;
    for (w2 = g_window_list; w2 < RCT2_NEW_WINDOW; w2++) {
        if (w2 == w || w2 == mainWindow)
            continue;

        right = w2->x + w2->width;

        if (wBottom < w2->y || w->y > w2->y + w2->height)
            continue;

        if (right < wLeftProximity || right > wRightProximity)
            continue;

        rightMost = std::max(rightMost, right);
    }

    if (0 >= wLeftProximity && 0 <= wRightProximity)
        rightMost = std::max(rightMost, 0);

    if (rightMost != INT32_MIN)
        w->x = rightMost;
}

static void window_snap_top(rct_window *w, sint32 proximity)
{
    sint32 bottom, bottomMost, wTopProximity, wBottomProximity, wRight;
    rct_window *mainWindow, *w2;

    mainWindow = window_get_main();

    wRight = w->x + w->width;
    wTopProximity = w->y - (proximity * 2);
    wBottomProximity = w->y + (proximity * 2);
    bottomMost = INT32_MIN;
    for (w2 = g_window_list; w2 < RCT2_NEW_WINDOW; w2++) {
        if (w2 == w || w2 == mainWindow)
            continue;

        bottom = w2->y + w2->height;

        if (wRight < w2->x || w->x > w2->x + w2->width)
            continue;

        if (bottom < wTopProximity || bottom > wBottomProximity)
            continue;

        bottomMost = std::max(bottomMost, bottom);
    }

    if (0 >= wTopProximity && 0 <= wBottomProximity)
        bottomMost = std::max(bottomMost, 0);

    if (bottomMost != INT32_MIN)
        w->y = bottomMost;
}

static void window_snap_right(rct_window *w, sint32 proximity)
{
    sint32 leftMost, wLeftProximity, wRightProximity, wRight, wBottom, screenWidth;
    rct_window *mainWindow, *w2;

    mainWindow = window_get_main();

    wRight = w->x + w->width;
    wBottom = w->y + w->height;
    wLeftProximity = wRight - (proximity * 2);
    wRightProximity = wRight + (proximity * 2);
    leftMost = INT32_MAX;
    for (w2 = g_window_list; w2 < RCT2_NEW_WINDOW; w2++) {
        if (w2 == w || w2 == mainWindow)
            continue;

        if (wBottom < w2->y || w->y > w2->y + w2->height)
            continue;

        if (w2->x < wLeftProximity || w2->x > wRightProximity)
            continue;

        leftMost = std::min<sint32>(leftMost, w2->x);
    }

    screenWidth = context_get_width();
    if (screenWidth >= wLeftProximity && screenWidth <= wRightProximity)
        leftMost = std::min(leftMost, screenWidth);

    if (leftMost != INT32_MAX)
        w->x = leftMost - w->width;
}

static void window_snap_bottom(rct_window *w, sint32 proximity)
{
    sint32 topMost, wTopProximity, wBottomProximity, wRight, wBottom, screenHeight;
    rct_window *mainWindow, *w2;

    mainWindow = window_get_main();

    wRight = w->x + w->width;
    wBottom = w->y + w->height;
    wTopProximity = wBottom - (proximity * 2);
    wBottomProximity = wBottom + (proximity * 2);
    topMost = INT32_MAX;
    for (w2 = g_window_list; w2 < RCT2_NEW_WINDOW; w2++) {
        if (w2 == w || w2 == mainWindow)
            continue;

        if (wRight < w2->x || w->x > w2->x + w2->width)
            continue;

        if (w2->y < wTopProximity || w2->y > wBottomProximity)
            continue;

        topMost = std::min<sint32>(topMost, w2->y);
    }

    screenHeight = context_get_height();
    if (screenHeight >= wTopProximity && screenHeight <= wBottomProximity)
        topMost = std::min(topMost, screenHeight);

    if (topMost != INT32_MAX)
        w->y = topMost - w->height;
}

void window_move_and_snap(rct_window *w, sint32 newWindowX, sint32 newWindowY, sint32 snapProximity)
{
    sint32 originalX = w->x;
    sint32 originalY = w->y;
    sint32 minY = (gScreenFlags & SCREEN_FLAGS_TITLE_DEMO) ? 1 : TOP_TOOLBAR_HEIGHT + 2;

    newWindowY = Math::Clamp(minY, newWindowY, context_get_height() - 34);

    if (snapProximity > 0) {
        w->x = newWindowX;
        w->y = newWindowY;

        window_snap_right(w, snapProximity);
        window_snap_bottom(w, snapProximity);
        window_snap_left(w, snapProximity);
        window_snap_top(w, snapProximity);

        if (w->x == originalX && w->y == originalY)
            return;

        newWindowX = w->x;
        newWindowY = w->y;
        w->x = originalX;
        w->y = originalY;
    }

    window_set_position(w, newWindowX, newWindowY);
}

sint32 window_can_resize(rct_window *w)
{
    return (w->flags & WF_RESIZABLE) && (w->min_width != w->max_width || w->min_height != w->max_height);
}

/**
 *
 *  rct2: 0x006EE3C3
 */
void textinput_cancel()
{
    window_close_by_class(WC_TEXTINPUT);
}

void window_start_textbox(rct_window *call_w, rct_widgetindex call_widget, rct_string_id existing_text, char * existing_args, sint32 maxLength)
{
    if (gUsingWidgetTextBox)
        window_cancel_textbox();

    gUsingWidgetTextBox = true;
    gCurrentTextBox.window.classification = call_w->classification;
    gCurrentTextBox.window.number = call_w->number;
    gCurrentTextBox.widget_index = call_widget;
    gTextBoxFrameNo = 0;

    gMaxTextBoxInputLength = maxLength;

    window_close_by_class(WC_TEXTINPUT);

    // Clear the text input buffer
    memset(gTextBoxInput, 0, maxLength);

    // Enter in the text input buffer any existing
    // text.
    if (existing_text != STR_NONE)
        format_string(gTextBoxInput, TEXT_INPUT_SIZE, existing_text, &existing_args);

    // In order to prevent strings that exceed the maxLength
    // from crashing the game.
    gTextBoxInput[maxLength - 1] = '\0';

    gTextInput = context_start_text_input(gTextBoxInput, maxLength);
}

void window_cancel_textbox()
{
    if (gUsingWidgetTextBox) {
        rct_window *w = window_find_by_number(
            gCurrentTextBox.window.classification,
            gCurrentTextBox.window.number
            );
        window_event_textinput_call(w, gCurrentTextBox.widget_index, nullptr);
        gCurrentTextBox.window.classification = WC_NULL;
        gCurrentTextBox.window.number = 0;
        context_stop_text_input();
        gUsingWidgetTextBox = false;
        widget_invalidate(w, gCurrentTextBox.widget_index);
        gCurrentTextBox.widget_index = WWT_LAST;
    }
}

void window_update_textbox_caret()
{
    gTextBoxFrameNo++;
    if (gTextBoxFrameNo > 30)
        gTextBoxFrameNo = 0;
}

void window_update_textbox()
{
    if (gUsingWidgetTextBox) {
        gTextBoxFrameNo = 0;
        rct_window *w = window_find_by_number(
            gCurrentTextBox.window.classification,
            gCurrentTextBox.window.number
            );
        widget_invalidate(w, gCurrentTextBox.widget_index);
        window_event_textinput_call(w, gCurrentTextBox.widget_index, gTextBoxInput);
    }
}

bool window_is_visible(rct_window* w)
{
    // w->visibility is used to prevent repeat calculations within an iteration by caching the result
    if (w == nullptr)
        return false;

    if (w->visibility == VC_VISIBLE) return true;
    if (w->visibility == VC_COVERED) return false;

    // only consider viewports, consider the main window always visible
    if (w->viewport == nullptr || w->classification == WC_MAIN_WINDOW)
    {
        // default to previous behaviour
        w->visibility = VC_VISIBLE;
        return true;
    }

    // start from the window above the current
    for (rct_window *w_other = (w+1); w_other < RCT2_NEW_WINDOW; w_other++)
    {
        // if covered by a higher window, no rendering needed
        if (w_other->x <= w->x
            && w_other->y <= w->y
            && w_other->x + w_other->width >= w->x + w->width
            && w_other->y + w_other->height >= w->y + w->height)
        {
            w->visibility = VC_COVERED;
            w->viewport->visibility = VC_COVERED;
            return false;
        }
    }

    // default to previous behaviour
    w->visibility = VC_VISIBLE;
    w->viewport->visibility = VC_VISIBLE;
    return true;
}

/**
 *
 *  rct2: 0x006E7499
 * left (ax)
 * top (bx)
 * right (dx)
 * bottom (bp)
 */
void window_draw_all(rct_drawpixelinfo *dpi, sint16 left, sint16 top, sint16 right, sint16 bottom)
{
    rct_drawpixelinfo windowDPI;
    windowDPI.bits = dpi->bits + left + ((dpi->width + dpi->pitch) * top);
    windowDPI.x = left;
    windowDPI.y = top;
    windowDPI.width = right - left;
    windowDPI.height = bottom - top;
    windowDPI.pitch = dpi->width + dpi->pitch + left - right;
    windowDPI.zoom_level = 0;

    for (rct_window *w = g_window_list; w < gWindowNextSlot; w++) {
        if (w->flags & WF_TRANSPARENT) continue;
        if (right <= w->x || bottom <= w->y) continue;
        if (left >= w->x + w->width || top >= w->y + w->height) continue;

        window_draw(&windowDPI, w, left, top, right, bottom);
    }
}

rct_viewport * window_get_previous_viewport(rct_viewport * current)
{
    bool foundPrevious = (current == nullptr);
    rct_window * window = gWindowNextSlot;

    if (window == nullptr)
    {
        return nullptr;
    }

    while (true)
    {
        window--;
        if (window < g_window_list)
        {
            break;
        }

        if (window->viewport == nullptr)
        {
            continue;
        }

        if (foundPrevious)
        {
            return window->viewport;
        }

        if (window->viewport == current)
        {
            foundPrevious = true;
        }
    }

    return nullptr;
}

void window_reset_visibilities()
{
    // reset window visibility status to unknown
    for (rct_window * w = g_window_list; w < gWindowNextSlot; w++)
    {
        w->visibility = VC_UNKNOWN;
        if (w->viewport != nullptr)
        {
            w->viewport->visibility = VC_UNKNOWN;
        }
    }
}

void window_init_all()
{
    window_close_all();
    gWindowNextSlot = g_window_list;
}

void window_follow_sprite(rct_window * w, size_t spriteIndex)
{
    if (spriteIndex < MAX_SPRITES || spriteIndex == SPRITE_INDEX_NULL)
    {
        w->viewport_smart_follow_sprite = (uint16)spriteIndex;
    }
}

void window_unfollow_sprite(rct_window * w)
{
    w->viewport_smart_follow_sprite = SPRITE_INDEX_NULL;
    w->viewport_target_sprite = SPRITE_INDEX_NULL;
}

rct_viewport * window_get_viewport(rct_window * w)
{
    if (w == nullptr)
    {
        return nullptr;
    }

    return w->viewport;
}


rct_window * window_get_listening()
{
    for (rct_window * w = RCT2_LAST_WINDOW; w >= g_window_list; w--)
    {
        if (w->viewport == nullptr)
        {
            continue;
        }

        if (w->viewport->flags & VIEWPORT_FLAG_SOUND_ON)
        {
            return w;
        }
    }

    return nullptr;
}

rct_windowclass window_get_classification(rct_window * window)
{
    return window->classification;
}

/**
*
*  rct2: 0x006EAF26
*/
void widget_scroll_update_thumbs(rct_window *w, rct_widgetindex widget_index)
{
    rct_widget *widget = &w->widgets[widget_index];
    rct_scroll* scroll = &w->scrolls[window_get_scroll_data_index(w, widget_index)];

    if (scroll->flags & HSCROLLBAR_VISIBLE) {
        sint32 view_size = widget->right - widget->left - 21;
        if (scroll->flags & VSCROLLBAR_VISIBLE)
            view_size -= 11;
        sint32 x = scroll->h_left * view_size;
        if (scroll->h_right != 0)
            x /= scroll->h_right;
        scroll->h_thumb_left = x + 11;

        x = widget->right - widget->left - 2;
        if (scroll->flags & VSCROLLBAR_VISIBLE)
            x -= 11;
        x += scroll->h_left;
        if (scroll->h_right != 0)
            x = (x * view_size) / scroll->h_right;
        x += 11;
        view_size += 10;
        scroll->h_thumb_right = std::min(x, view_size);

        if (scroll->h_thumb_right - scroll->h_thumb_left < 20) {
            double barPosition = (scroll->h_thumb_right * 1.0) / view_size;

            scroll->h_thumb_left = (uint16)std::lround(scroll->h_thumb_left - (20 * barPosition));
            scroll->h_thumb_right = (uint16)std::lround(scroll->h_thumb_right + (20 * (1 - barPosition)));
        }
    }

    if (scroll->flags & VSCROLLBAR_VISIBLE) {
        sint32 view_size = widget->bottom - widget->top - 21;
        if (scroll->flags & HSCROLLBAR_VISIBLE)
            view_size -= 11;
        sint32 y = scroll->v_top * view_size;
        if (scroll->v_bottom != 0)
            y /= scroll->v_bottom;
        scroll->v_thumb_top = y + 11;

        y = widget->bottom - widget->top - 2;
        if (scroll->flags & HSCROLLBAR_VISIBLE)
            y -= 11;
        y += scroll->v_top;
        if (scroll->v_bottom != 0)
            y = (y * view_size) / scroll->v_bottom;
        y += 11;
        view_size += 10;
        scroll->v_thumb_bottom = std::min(y, view_size);

        if (scroll->v_thumb_bottom - scroll->v_thumb_top < 20) {
            double barPosition = (scroll->v_thumb_bottom * 1.0) / view_size;

            scroll->v_thumb_top = (uint16)std::lround(scroll->v_thumb_top - (20 * barPosition));
            scroll->v_thumb_bottom = (uint16)std::lround(scroll->v_thumb_bottom + (20 * (1 - barPosition)));
        }
    }
}

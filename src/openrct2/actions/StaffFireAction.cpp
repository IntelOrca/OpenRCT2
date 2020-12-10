/*****************************************************************************
 * Copyright (c) 2014-2020 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "StaffFireAction.h"

#include "../interface/Window.h"
#include "../peep/Peep.h"

void StaffFireAction::Serialise(DataSerialiser& stream)
{
    GameAction::Serialise(stream);
    stream << DS_TAG(_spriteId);
}

GameActions::Result::Ptr StaffFireAction::Query() const
{
    if (_spriteId >= MAX_SPRITES)
    {
        log_error("Invalid spriteId. spriteId = %u", _spriteId);
        return MakeResult(GameActions::Status::InvalidParameters, STR_NONE);
    }

    auto staff = TryGetEntity<Staff>(_spriteId);
    if (staff == nullptr)
    {
        log_error("Invalid spriteId. spriteId = %u", _spriteId);
        return MakeResult(GameActions::Status::InvalidParameters, STR_NONE);
    }

    return MakeResult();
}

GameActions::Result::Ptr StaffFireAction::Execute() const
{
    auto staff = TryGetEntity<Staff>(_spriteId);
    if (staff == nullptr)
    {
        log_error("Invalid spriteId. spriteId = %u", _spriteId);
        return MakeResult(GameActions::Status::InvalidParameters, STR_NONE);
    }
    window_close_by_class(WC_FIRE_PROMPT);
    peep_sprite_remove(staff);
    return MakeResult();
}
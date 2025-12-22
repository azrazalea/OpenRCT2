/*****************************************************************************
 * Copyright (c) 2014-2025 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

namespace OpenRCT2::AIAgent
{
    bool IsFollowEnabled();
    void SetFollowEnabled(bool enabled, bool persistConfig);
    void ToggleFollow(bool persistConfig);
} // namespace OpenRCT2::AIAgent

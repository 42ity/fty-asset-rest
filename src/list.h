/*  ====================================================================================================================
    Copyright (C) 2014 - 2020 Eaton

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
    ====================================================================================================================
*/

#pragma once
#include <fty/rest/runner.h>

namespace fty::asset {

class List: public rest::Runner
{
public:
    INIT_REST("asset/list");

public:
    unsigned run() override;

private:
    // clang-format off
    Permissions m_permissions = {
        { rest::User::Profile::Admin,     rest::Access::Read },
        { rest::User::Profile::Dashboard, rest::Access::Read }
    };
    // clang-format on
};

}

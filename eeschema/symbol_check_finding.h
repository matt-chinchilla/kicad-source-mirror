/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#ifndef SYMBOL_CHECK_FINDING_H
#define SYMBOL_CHECK_FINDING_H

#ifdef __EMSCRIPTEN__

#include <string>
#include <vector>
#include <wx/string.h>
#include <kiid.h>
#include <math/vector2d.h>

class LIB_SYMBOL;
class UNITS_PROVIDER;

/**
 * One finding of the symbol checker as a record, for the WebAssembly editor's host (engine
 * run 14): what CheckLibSymbol and CheckDuplicatePins say in a message, by code. The messages
 * are unchanged; a finding is filled beside each one.
 */
struct SYMBOL_CHECK_FINDING
{
    /// empty_reference, reference_prefix_digit, duplicate_pin, power_units, power_pins,
    /// power_pin_type, power_pin_hidden, hidden_power_pin, off_grid_pin, circle_zero_radius,
    /// rectangle_zero_size.
    std::string code;

    /// KiCad's "Info:" lines (a hidden power pin); every other finding is a warning.
    bool info = false;

    /// The pin numbers the finding names (two for a duplicate: the later pin's, then the earlier's).
    std::vector<wxString> pins;

    /// The unit and body style of the item the finding is about (0 = every unit or body style).
    int unit = 0;
    int bodyStyle = 0;

    /// Where, in internal units in the symbol's own frame (Y down, as stored); none for a
    /// finding about the whole symbol.
    bool     hasPosition = false;
    VECTOR2I position;

    /// The items the finding is about.
    std::vector<KIID> items;
};

/// CheckLibSymbol, its findings also appended to \a aFindings (when not null).
void CheckLibSymbol( LIB_SYMBOL* aSymbol, std::vector<wxString>& aMessages, int aGridForPins,
                     UNITS_PROVIDER* aUnitsProvider, std::vector<SYMBOL_CHECK_FINDING>* aFindings );

/// CheckDuplicatePins, its findings also appended to \a aFindings (when not null).
void CheckDuplicatePins( LIB_SYMBOL* aSymbol, std::vector<wxString>& aMessages,
                         UNITS_PROVIDER* aUnitsProvider,
                         std::vector<SYMBOL_CHECK_FINDING>* aFindings );

#endif // __EMSCRIPTEN__

#endif // SYMBOL_CHECK_FINDING_H

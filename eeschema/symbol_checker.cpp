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

#include <vector>
#include <eda_draw_frame.h>
#include <lib_symbol.h>
#include <sch_shape.h>
#include <macros.h>

#ifdef __EMSCRIPTEN__
#include <symbol_check_finding.h>

// Where the running check also files its findings, as records for the WebAssembly editor's
// host (null: messages only, as in every plain build).
static std::vector<SYMBOL_CHECK_FINDING>* s_ccFindings = nullptr;

static void ccSymbolFinding( const char* aCode )
{
    if( !s_ccFindings )
        return;

    SYMBOL_CHECK_FINDING finding;
    finding.code = aCode;
    s_ccFindings->push_back( std::move( finding ) );
}

static void ccPinFinding( const char* aCode, bool aInfo, const SCH_PIN* aPin )
{
    if( !s_ccFindings )
        return;

    SYMBOL_CHECK_FINDING finding;
    finding.code = aCode;
    finding.info = aInfo;
    finding.pins.push_back( aPin->GetNumber() );
    finding.unit = aPin->GetUnit();
    finding.bodyStyle = aPin->GetBodyStyle();
    finding.hasPosition = true;
    finding.position = aPin->GetPosition();
    finding.items.push_back( aPin->m_Uuid );
    s_ccFindings->push_back( std::move( finding ) );
}

static void ccShapeFinding( const char* aCode, const SCH_SHAPE* aShape )
{
    if( !s_ccFindings )
        return;

    SYMBOL_CHECK_FINDING finding;
    finding.code = aCode;
    finding.unit = aShape->GetUnit();
    finding.bodyStyle = aShape->GetBodyStyle();
    finding.hasPosition = true;
    finding.position = aShape->GetPosition();
    finding.items.push_back( aShape->m_Uuid );
    s_ccFindings->push_back( std::move( finding ) );
}
#endif

// helper function to sort pins by pin num
static bool sort_by_pin_number( const SCH_PIN* ref, const SCH_PIN* tst );


static void CheckLibSymbolGraphics( LIB_SYMBOL* aSymbol, std::vector<wxString>& aMessages,
                                    UNITS_PROVIDER* aUnitsProvider );


void CheckDuplicatePins( LIB_SYMBOL* aSymbol, std::vector<wxString>& aMessages, UNITS_PROVIDER* aUnitsProvider )
{
    wxString              msg;
    std::vector<SCH_PIN*> pinList = aSymbol->GetGraphicalPins( 0, 0 );

    std::vector<LIB_SYMBOL::LOGICAL_PIN> logicalPins;
    logicalPins.reserve( pinList.size() );

    for( SCH_PIN* pin : pinList )
    {
        bool valid = false;
        std::vector<wxString> numbers = pin->GetStackedPinNumbers( &valid );

        if( !valid || numbers.empty() )
        {
            logicalPins.push_back( { pin, pin->GetNumber() } );
            continue;
        }

        for( const wxString& number : numbers )
            logicalPins.push_back( { pin, number } );
    }

    sort( logicalPins.begin(), logicalPins.end(),
            []( const LIB_SYMBOL::LOGICAL_PIN& lhs, const LIB_SYMBOL::LOGICAL_PIN& rhs )
            {
                int result = lhs.number.Cmp( rhs.number );

                if( result == 0 )
                    result = lhs.pin->GetBodyStyle() - rhs.pin->GetBodyStyle();

                if( result == 0 )
                    result = lhs.pin->GetUnit() - rhs.pin->GetUnit();

                if( result == 0 && lhs.pin != rhs.pin )
                    return lhs.pin < rhs.pin;

                return result < 0;
            } );

    for( unsigned ii = 1; ii < logicalPins.size(); ii++ )
    {
        LIB_SYMBOL::LOGICAL_PIN& prev = logicalPins[ii - 1];
        LIB_SYMBOL::LOGICAL_PIN& next = logicalPins[ii];

        if( prev.number != next.number )
            continue;

        if( prev.pin == next.pin )
            continue;

        // Pins are not duplicated only if they are in different body styles
        // (but GetBodyStyle() == 0 means common to all body styles)
        if( prev.pin->GetBodyStyle() != 0 && next.pin->GetBodyStyle() != 0 )
        {
            if( prev.pin->GetBodyStyle() != next.pin->GetBodyStyle() )
                continue;
        }

        wxString pinName;
        wxString nextName;

        if( !prev.pin->GetName().IsEmpty() )
            pinName = " '" + prev.pin->GetName() + "'";

        if( !next.pin->GetName().IsEmpty() )
            nextName = " '" + next.pin->GetName() + "'";

        auto formatNumberForMessage = []( const SCH_PIN* pin, const wxString& logicalNumber )
        {
            wxString shown = pin->GetNumber();

            if( shown == logicalNumber )
                return logicalNumber;

            return wxString::Format( wxT( "%s (%s)" ), logicalNumber, shown );
        };

        wxString prevNumber = formatNumberForMessage( prev.pin, prev.number );
        wxString nextNumber = formatNumberForMessage( next.pin, next.number );

        if( aSymbol->IsMultiBodyStyle() && next.pin->GetBodyStyle() )
        {
            if( prev.pin->GetUnit() == 0 || next.pin->GetUnit() == 0 )
            {
                msg.Printf( _( "<b>Duplicate pin %s</b> %s at location <b>(%s, %s)</b>"
                               " conflicts with pin %s%s at location <b>(%s, %s)</b>"
                               " in %s body style." ),
                            nextNumber,
                            nextName,
                            aUnitsProvider->MessageTextFromValue( next.pin->GetPosition().x ),
                            aUnitsProvider->MessageTextFromValue( -next.pin->GetPosition().y ),
                            prevNumber,
                            prev.pin->GetName(),
                            aUnitsProvider->MessageTextFromValue( prev.pin->GetPosition().x ),
                            aUnitsProvider->MessageTextFromValue( -prev.pin->GetPosition().y ),
                            aSymbol->GetBodyStyleDescription( prev.pin->GetBodyStyle(), true ).Lower() );
            }
            else
            {
                msg.Printf( _( "<b>Duplicate pin %s</b> %s at location <b>(%s, %s)</b>"
                               " conflicts with pin %s%s at location <b>(%s, %s)</b>"
                               " in units %s and %s of %s body style." ),
                            nextNumber,
                            nextName,
                            aUnitsProvider->MessageTextFromValue( next.pin->GetPosition().x ),
                            aUnitsProvider->MessageTextFromValue( -next.pin->GetPosition().y ),
                            prevNumber,
                            pinName,
                            aUnitsProvider->MessageTextFromValue( prev.pin->GetPosition().x ),
                            aUnitsProvider->MessageTextFromValue( -prev.pin->GetPosition().y ),
                            aSymbol->GetUnitDisplayName( next.pin->GetUnit(), false ),
                            aSymbol->GetUnitDisplayName( prev.pin->GetUnit(), false ),
                            aSymbol->GetBodyStyleDescription( prev.pin->GetBodyStyle(), true ).Lower() );
            }
        }
        else
        {
            if( prev.pin->GetUnit() == 0 || next.pin->GetUnit() == 0 )
            {
                msg.Printf( _( "<b>Duplicate pin %s</b> %s at location <b>(%s, %s)</b>"
                               " conflicts with pin %s%s at location <b>(%s, %s)</b>." ),
                            nextNumber,
                            nextName,
                            aUnitsProvider->MessageTextFromValue( next.pin->GetPosition().x ),
                            aUnitsProvider->MessageTextFromValue( -next.pin->GetPosition().y ),
                            prevNumber,
                            pinName,
                            aUnitsProvider->MessageTextFromValue( prev.pin->GetPosition().x ),
                            aUnitsProvider->MessageTextFromValue( -prev.pin->GetPosition().y ) );
            }
            else
            {
                msg.Printf( _( "<b>Duplicate pin %s</b> %s at location <b>(%s, %s)</b>"
                               " conflicts with pin %s%s at location <b>(%s, %s)</b>"
                               " in units %s and %s." ),
                            nextNumber,
                            nextName,
                            aUnitsProvider->MessageTextFromValue( next.pin->GetPosition().x ),
                            aUnitsProvider->MessageTextFromValue( -next.pin->GetPosition().y ),
                            prevNumber,
                            pinName,
                            aUnitsProvider->MessageTextFromValue( prev.pin->GetPosition().x ),
                            aUnitsProvider->MessageTextFromValue( -prev.pin->GetPosition().y ),
                            aSymbol->GetUnitDisplayName( next.pin->GetUnit(), false ),
                            aSymbol->GetUnitDisplayName( prev.pin->GetUnit(), false ) );
            }
        }

        msg += wxT( "<br><br>" );
        aMessages.push_back( msg );

#ifdef __EMSCRIPTEN__
        if( s_ccFindings )
        {
            SYMBOL_CHECK_FINDING finding;
            finding.code = "duplicate_pin";
            finding.pins.push_back( next.number );
            finding.unit = next.pin->GetUnit();
            finding.bodyStyle = next.pin->GetBodyStyle();
            finding.hasPosition = true;
            finding.position = next.pin->GetPosition();
            finding.items.push_back( next.pin->m_Uuid );
            finding.items.push_back( prev.pin->m_Uuid );
            s_ccFindings->push_back( std::move( finding ) );
        }
#endif
    }
}


/**
 * Check a library symbol to find incorrect settings.
 *
 *  - Pins not on a valid grid
 *  - Pins duplicated
 *  - Conflict with pins at same location
 *  - Incorrect Power Symbols
 *  - illegal reference prefix (cannot ends by a digit or a '?')
 *
 * @param aSymbol is the library symbol to check.
 * @param aMessages is a room to store error messages.
 * @param aGridForPins (in IU) is the grid to test pin positions ( >= 25 mils )
 * should be 25, 50 or 100 mils (converted to IUs).
 * @param aUnitsProvider a frame to format coordinates in messages.
 */
void CheckLibSymbol( LIB_SYMBOL* aSymbol, std::vector<wxString>& aMessages,
                     int aGridForPins, UNITS_PROVIDER* aUnitsProvider )
{
    if( !aSymbol )
        return;

    wxString msg;

    // Test reference prefix validity:
    // if the symbol is saved in a library, the prefix should not ends by a digit or a '?'
    // but it is acceptable if the symbol is saved to a schematic.
    wxString reference_base = aSymbol->GetReferenceField().GetText();

    if( reference_base.IsEmpty() )
    {
        aMessages.push_back( _( "<b>Warning: reference is empty</b><br><br>" ) );

#ifdef __EMSCRIPTEN__
        ccSymbolFinding( "empty_reference" );
#endif
    }
    else
    {
        wxString illegal_end( wxT( "0123456789?" ) );
        wxUniChar last_char = reference_base.Last();

        if( illegal_end.Find( last_char ) != wxNOT_FOUND )
        {
            msg.Printf( _( "<b>Warning: reference prefix</b><br>prefix ending by '%s' can create"
                           " issues if saved in a symbol library" ),
                        illegal_end );
            msg += wxT( "<br><br>" );
            aMessages.push_back( msg );

#ifdef __EMSCRIPTEN__
            ccSymbolFinding( "reference_prefix_digit" );
#endif
        }
    }

    CheckDuplicatePins( aSymbol, aMessages, aUnitsProvider );

    std::vector<SCH_PIN*> pinList = aSymbol->GetGraphicalPins( 0, 0 );
    sort( pinList.begin(), pinList.end(), sort_by_pin_number );

    // The minimal grid size allowed to place a pin is 25 mils
    // the best grid size is 50 mils, but 25 mils is still usable
    // this is because all aSymbols are using a 50 mils grid to place pins, and therefore
    // the wires must be on the 50 mils grid
    // So raise an error if a pin is not on a 25 (or bigger :50 or 100) mils grid
    const int min_grid_size = schIUScale.MilsToIU( 25 );
    const int clamped_grid_size = ( aGridForPins < min_grid_size ) ? min_grid_size : aGridForPins;

    // Test for a valid power aSymbol.
    // A valid power aSymbol has only one unit, no alternate body styles and one pin.
    // And this pin should be PT_POWER_IN (invisible to be automatically connected)
    // or PT_POWER_OUT for a power flag
    if( aSymbol->IsPower() )
    {
        if( aSymbol->GetUnitCount() != 1 )
        {
            msg.Printf( _( "<b>A Power Symbol should have only one unit</b><br><br>" ) );
            aMessages.push_back( msg );

#ifdef __EMSCRIPTEN__
            ccSymbolFinding( "power_units" );
#endif
        }

        if( pinList.size() != 1 )
        {
            msg.Printf( _( "<b>A Power Symbol should have only one pin</b><br><br>" ) );
            aMessages.push_back( msg );

#ifdef __EMSCRIPTEN__
            ccSymbolFinding( "power_pins" );
#endif
        }

#ifdef __EMSCRIPTEN__
        // A power symbol with no pin at all is the finding above; the lines below read the
        // first pin, so they are skipped for it (plain builds read pinList[0] unguarded).
        SCH_PIN* pin = pinList.empty() ? nullptr : pinList[0];

        if( pin )
#else
        SCH_PIN* pin = pinList[0];
#endif
        {
            if( pin->GetType() != ELECTRICAL_PINTYPE::PT_POWER_IN
                    && pin->GetType() != ELECTRICAL_PINTYPE::PT_POWER_OUT )
            {
                msg.Printf( _( "<b>Suspicious Power Symbol</b><br>"
                               "Only an input or output power pin has meaning<br><br>" ) );
                aMessages.push_back( msg );

#ifdef __EMSCRIPTEN__
                ccPinFinding( "power_pin_type", false, pin );
#endif
            }

            if( pin->GetType() == ELECTRICAL_PINTYPE::PT_POWER_IN && !pin->IsVisible() )
            {
                msg.Printf( _( "<b>Suspicious Power Symbol</b><br>"
                               "Invisible input power pins are no longer required<br><br>" ) );
                aMessages.push_back( msg );

#ifdef __EMSCRIPTEN__
                ccPinFinding( "power_pin_hidden", false, pin );
#endif
            }
        }
    }


    for( SCH_PIN* pin : pinList )
    {
        wxString pinName = pin->GetName();

        if( pinName.IsEmpty() || pinName == "~" )
            pinName = "";
        else
            pinName = "'" + pinName + "'";

        if( !aSymbol->IsGlobalPower()
                && pin->GetType() == ELECTRICAL_PINTYPE::PT_POWER_IN
                && !pin->IsVisible() )
        {
            // hidden power pin
            if( aSymbol->IsMultiBodyStyle() && pin->GetBodyStyle() )
            {
                if( aSymbol->GetUnitCount() <= 1 )
                {
                    msg.Printf( _( "Info: <b>Hidden power pin %s</b> %s at location <b>(%s, %s)</b>"
                                   " in %s body style." ),
                                pin->GetNumber(),
                                pinName,
                                aUnitsProvider->MessageTextFromValue( pin->GetPosition().x ),
                                aUnitsProvider->MessageTextFromValue( -pin->GetPosition().y ),
                                aSymbol->GetBodyStyleDescription( pin->GetBodyStyle(), true ).Lower() );
                }
                else
                {
                    msg.Printf( _( "Info: <b>Hidden power pin %s</b> %s at location <b>(%s, %s)</b>"
                                   " in unit %c of %s body style." ),
                                pin->GetNumber(),
                                pinName,
                                aUnitsProvider->MessageTextFromValue( pin->GetPosition().x ),
                                aUnitsProvider->MessageTextFromValue( -pin->GetPosition().y ),
                                'A' + pin->GetUnit() - 1,
                                aSymbol->GetBodyStyleDescription( pin->GetBodyStyle(), true ).Lower() );
                }
            }
            else
            {
                if( aSymbol->GetUnitCount() <= 1 )
                {
                    msg.Printf( _( "Info: <b>Hidden power pin %s</b> %s at location <b>"
                                   "(%s, %s)</b>." ),
                                pin->GetNumber(),
                                pinName,
                                aUnitsProvider->MessageTextFromValue( pin->GetPosition().x ),
                                aUnitsProvider->MessageTextFromValue( -pin->GetPosition().y ) );
                }
                else
                {
                    msg.Printf( _( "Info: <b>Hidden power pin %s</b> %s at location <b>(%s, %s)</b>"
                                   " in unit %c." ),
                                pin->GetNumber(),
                                pinName,
                                aUnitsProvider->MessageTextFromValue( pin->GetPosition().x ),
                                aUnitsProvider->MessageTextFromValue( -pin->GetPosition().y ),
                                'A' + pin->GetUnit() - 1 );
                }
            }

            msg += wxT( "<br>" );
            msg += _( "(Hidden power pins will drive their pin names on to any connected nets.)" );
            msg += wxT( "<br><br>" );
            aMessages.push_back( msg );

#ifdef __EMSCRIPTEN__
            ccPinFinding( "hidden_power_pin", true, pin );
#endif
        }

        if( ( (pin->GetPosition().x % clamped_grid_size) != 0 )
                || ( (pin->GetPosition().y % clamped_grid_size) != 0 ) )
        {
            // pin is off grid
            msg.Empty();

            if( aSymbol->IsMultiBodyStyle() && pin->GetBodyStyle() )
            {
                if( aSymbol->GetUnitCount() <= 1 )
                {
                    msg.Printf( _( "<b>Off grid pin %s</b> %s at location <b>(%s, %s)</b>"
                                   " of %s body style." ),
                                pin->GetNumber(),
                                pinName,
                                aUnitsProvider->MessageTextFromValue( pin->GetPosition().x ),
                                aUnitsProvider->MessageTextFromValue( -pin->GetPosition().y ),
                                aSymbol->GetBodyStyleDescription( pin->GetBodyStyle(), true ).Lower() );
                }
                else
                {
                    msg.Printf( _( "<b>Off grid pin %s</b> %s at location <b>(%s, %s)</b>"
                                   " in unit %c of %s body style." ),
                                pin->GetNumber(),
                                pinName,
                                aUnitsProvider->MessageTextFromValue( pin->GetPosition().x ),
                                aUnitsProvider->MessageTextFromValue( -pin->GetPosition().y ),
                                'A' + pin->GetUnit() - 1,
                                aSymbol->GetBodyStyleDescription( pin->GetBodyStyle(), true ).Lower() );
                 }
            }
            else
            {
                if( aSymbol->GetUnitCount() <= 1 )
                {
                    msg.Printf( _( "<b>Off grid pin %s</b> %s at location <b>(%s, %s)</b>." ),
                                pin->GetNumber(),
                                pinName,
                                aUnitsProvider->MessageTextFromValue( pin->GetPosition().x ),
                                aUnitsProvider->MessageTextFromValue( -pin->GetPosition().y ) );
                }
                else
                {
                    msg.Printf( _( "<b>Off grid pin %s</b> %s at location <b>(%s, %s)</b>"
                                   " in unit %c." ),
                                pin->GetNumber(),
                                pinName,
                                aUnitsProvider->MessageTextFromValue( pin->GetPosition().x ),
                                aUnitsProvider->MessageTextFromValue( -pin->GetPosition().y ),
                                'A' + pin->GetUnit() - 1 );
                }
            }

            msg += wxT( "<br><br>" );
            aMessages.push_back( msg );

#ifdef __EMSCRIPTEN__
            ccPinFinding( "off_grid_pin", false, pin );
#endif
        }
    }

    CheckLibSymbolGraphics( aSymbol, aMessages,  aUnitsProvider );
}


void CheckLibSymbolGraphics( LIB_SYMBOL* aSymbol, std::vector<wxString>& aMessages,
                             UNITS_PROVIDER* aUnitsProvider )
{
    if( !aSymbol )
        return;

    wxString msg;

    for( const SCH_ITEM& item : aSymbol->GetDrawItems() )
    {
        if( item.Type() != SCH_SHAPE_T )
            continue;

        const SCH_SHAPE* shape = static_cast<const SCH_SHAPE*>( &item );

        switch( shape->GetShape() )
        {
        case SHAPE_T::ARC:
            break;

        case SHAPE_T::CIRCLE:
            if( shape->GetRadius() <= 0 )
            {
                msg.Printf( _( "<b>Graphic circle has radius = 0</b> at location "
                             "<b>(%s, %s)</b>." ),
                            aUnitsProvider->MessageTextFromValue(shape->GetPosition().x ),
                            aUnitsProvider->MessageTextFromValue( -shape->GetPosition().y ) );
                msg += wxT( "<br>" );
                aMessages.push_back( msg );

#ifdef __EMSCRIPTEN__
                ccShapeFinding( "circle_zero_radius", shape );
#endif
            }
            break;

        case SHAPE_T::RECTANGLE:
            if( shape->GetPosition() == shape->GetEnd() )
            {
                msg.Printf( _( "<b>Graphic rectangle has size 0</b> at location <b>(%s, %s)</b>." ),
                            aUnitsProvider->MessageTextFromValue(shape->GetPosition().x ),
                            aUnitsProvider->MessageTextFromValue( -shape->GetPosition().y ) );
                msg += wxT( "<br>" );
                aMessages.push_back( msg );

#ifdef __EMSCRIPTEN__
                ccShapeFinding( "rectangle_zero_size", shape );
#endif
            }
            break;

        case SHAPE_T::POLY:
            break;

        case SHAPE_T::BEZIER:
            break;

        default:
            UNIMPLEMENTED_FOR( shape->SHAPE_T_asString() );
        }
    }
}


bool sort_by_pin_number( const SCH_PIN* ref, const SCH_PIN* tst )
{
    // Use number as primary key
    int test = ref->GetNumber().Cmp( tst->GetNumber() );

    // Use DeMorgan variant as secondary key
    if( test == 0 )
        test = ref->GetBodyStyle() - tst->GetBodyStyle();

    // Use unit as tertiary key
    if( test == 0 )
        test = ref->GetUnit() - tst->GetUnit();

    return test < 0;
}


#ifdef __EMSCRIPTEN__
void CheckLibSymbol( LIB_SYMBOL* aSymbol, std::vector<wxString>& aMessages, int aGridForPins,
                     UNITS_PROVIDER* aUnitsProvider, std::vector<SYMBOL_CHECK_FINDING>* aFindings )
{
    std::vector<SYMBOL_CHECK_FINDING>* before = s_ccFindings;
    s_ccFindings = aFindings;

    try
    {
        CheckLibSymbol( aSymbol, aMessages, aGridForPins, aUnitsProvider );
    }
    catch( ... )
    {
        s_ccFindings = before;
        throw;
    }

    s_ccFindings = before;
}


void CheckDuplicatePins( LIB_SYMBOL* aSymbol, std::vector<wxString>& aMessages,
                         UNITS_PROVIDER* aUnitsProvider,
                         std::vector<SYMBOL_CHECK_FINDING>* aFindings )
{
    std::vector<SYMBOL_CHECK_FINDING>* before = s_ccFindings;
    s_ccFindings = aFindings;

    try
    {
        CheckDuplicatePins( aSymbol, aMessages, aUnitsProvider );
    }
    catch( ... )
    {
        s_ccFindings = before;
        throw;
    }

    s_ccFindings = before;
}
#endif

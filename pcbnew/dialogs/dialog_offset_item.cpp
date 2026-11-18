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
 *
 * Modified by Circuit Center Inc on 2026-11-18: under __EMSCRIPTEN__ ShowModal asks the host
 * (host_prompt.h, ask offset, modal) and writes its answer through the dialog's own controls and
 * TransferDataFromWindow; a plain build is unchanged.
 */

#include "dialog_offset_item.h"

#ifdef __EMSCRIPTEN__
#include <cmath>
#include <limits>
#include <optional>
#endif


DIALOG_OFFSET_ITEM::DIALOG_OFFSET_ITEM( PCB_BASE_FRAME& aParent, VECTOR2I& aOffset ) :
        DIALOG_OFFSET_ITEM_BASE( &aParent ),
        m_originalOffset( aOffset ),
        m_updatedOffset( aOffset ),
        m_xOffset( &aParent, m_xLabel, m_xEntry, m_xUnit ),
        m_yOffset( &aParent, m_yLabel, m_yEntry, m_yUnit ),
        m_stateX( 0.0 ),
        m_stateY( 0.0 ),
        m_stateRadius( 0.0 ),
        m_stateTheta( ANGLE_0 )
{
    m_xOffset.SetCoordType( ORIGIN_TRANSFORMS::REL_X_COORD );
    m_yOffset.SetCoordType( ORIGIN_TRANSFORMS::REL_Y_COORD );

    SetInitialFocus( m_xEntry );

    SetupStandardButtons();

    finishDialogSettings();
}


void DIALOG_OFFSET_ITEM::OnTextFocusLost( wxFocusEvent& event )
{
    wxTextCtrl* obj = static_cast<wxTextCtrl*>( event.GetEventObject() );

    if( obj->GetValue().IsEmpty() )
        obj->SetValue( "0" );

    event.Skip();
}


static void ToPolar( double x, double y, double& r, EDA_ANGLE& q )
{
    // convert to polar coordinates
    r = hypot( x, y );

    q = ( r != 0 ) ? EDA_ANGLE( VECTOR2D( x, y ) ) : ANGLE_0;
}


void DIALOG_OFFSET_ITEM::OnClear( wxCommandEvent& event )
{
    const wxObject* const obj = event.GetEventObject();
    VECTOR2I              offset = m_originalOffset;
    double                r;
    EDA_ANGLE             q;
    ToPolar( offset.x, offset.y, r, q );

    if( obj == m_clearX )
    {
        m_stateX = offset.x;
        m_xOffset.SetDoubleValue( r );
        m_stateRadius = m_xOffset.GetDoubleValue();

        if( m_polarCoords->IsChecked() )
            m_xOffset.SetDoubleValue( m_stateRadius );
        else
            m_xOffset.SetValue( m_stateX );
    }
    else if( obj == m_clearY )
    {
        m_stateY = offset.y;
        m_yOffset.SetAngleValue( q );
        m_stateTheta = m_yOffset.GetAngleValue();

        if( m_polarCoords->IsChecked() )
            m_yOffset.SetAngleValue( m_stateTheta );
        else
            m_yOffset.SetValue( m_stateY );
    }
}

void DIALOG_OFFSET_ITEM::OnPolarChanged( wxCommandEvent& event )
{
    bool   newPolar = m_polarCoords->IsChecked();
    double xOffset = m_xOffset.GetDoubleValue();
    double yOffset = m_yOffset.GetDoubleValue();
    updateDialogControls( newPolar );

    if( newPolar )
    {
        if( xOffset != m_stateX || yOffset != m_stateY )
        {
            m_stateX = xOffset;
            m_stateY = yOffset;
            ToPolar( m_stateX, m_stateY, m_stateRadius, m_stateTheta );

            m_xOffset.SetDoubleValue( m_stateRadius );
            m_stateRadius = m_xOffset.GetDoubleValue();
            m_yOffset.SetAngleValue( m_stateTheta );
            m_stateTheta = m_yOffset.GetAngleValue();
        }
        else
        {
            m_xOffset.SetDoubleValue( m_stateRadius );
            m_yOffset.SetAngleValue( m_stateTheta );
        }
    }
    else
    {
        if( xOffset != m_stateRadius || yOffset != m_stateTheta.AsDegrees() )
        {
            m_stateRadius = xOffset;
            m_stateTheta = EDA_ANGLE( yOffset, DEGREES_T );
            m_stateX = m_stateRadius * m_stateTheta.Cos();
            m_stateY = m_stateRadius * m_stateTheta.Sin();

            m_xOffset.SetDoubleValue( m_stateX );
            m_stateX = m_xOffset.GetDoubleValue();
            m_yOffset.SetDoubleValue( m_stateY );
            m_stateY = m_yOffset.GetDoubleValue();
        }
        else
        {
            m_xOffset.SetDoubleValue( m_stateX );
            m_yOffset.SetDoubleValue( m_stateY );
        }
    }
}

void DIALOG_OFFSET_ITEM::updateDialogControls( bool aPolar )
{
    if( aPolar )
    {
        m_xOffset.SetLabel( _( "Distance:" ) ); // Polar radius
        m_yOffset.SetLabel( _( "Angle:" ) );    // Polar theta or angle
        m_yOffset.SetUnits( EDA_UNITS::DEGREES );
        m_clearX->SetToolTip( _( "Reset to the current distance from the reference position." ) );
        m_clearY->SetToolTip( _( "Reset to the current angle from the reference position." ) );
    }
    else
    {
        m_xOffset.SetLabel( _( "Offset X:" ) );
        m_yOffset.SetLabel( _( "Offset Y:" ) );
        m_yOffset.SetUnits( GetUserUnits() );
        m_clearX->SetToolTip( _( "Reset to the current X offset from the reference position." ) );
        m_clearY->SetToolTip( _( "Reset to the current Y offset from the reference position." ) );
    }
}

bool DIALOG_OFFSET_ITEM::TransferDataToWindow()
{
    m_xOffset.ChangeValue( m_originalOffset.x );
    m_yOffset.ChangeValue( m_originalOffset.y );

    if( m_polarCoords->GetValue() )
    {
        wxCommandEvent dummy;
        OnPolarChanged( dummy );
    }

    return true;
}

bool DIALOG_OFFSET_ITEM::TransferDataFromWindow()
{
    if( m_polarCoords->GetValue() )
    {
        m_stateRadius = m_xOffset.GetDoubleValue();
        m_stateTheta = m_yOffset.GetAngleValue();

        m_updatedOffset.x = KiROUND( m_stateRadius * m_stateTheta.Cos() );
        m_updatedOffset.y = KiROUND( m_stateRadius * m_stateTheta.Sin() );
    }
    else
    {
        m_updatedOffset.x = m_xOffset.GetIntValue();
        m_updatedOffset.y = m_yOffset.GetIntValue();
    }

    return true;
}


#ifdef __EMSCRIPTEN__

// The host prompt (host_prompt.h, ask "offset", modal), after the Interactive Offset tool's two
// clicks. The record is the measured vector { x, y } in millimeters; the answer { x, y } (either
// or both, closed) is written into the dialog's own controls as a Cartesian vector (the card
// converts a polar entry) and its own TransferDataFromWindow gives the tool the offset.

int DIALOG_OFFSET_ITEM::ShowModal()
{
    LoadControlState();
    TransferDataToWindow();

    nlohmann::json record;
    record["x"] = pcbIUScale.IUTomm( m_originalOffset.x );
    record["y"] = pcbIUScale.IUTomm( m_originalOffset.y );

    class RECEIVER : public CC_PROMPT_RECEIVER
    {
    public:
        explicit RECEIVER( DIALOG_OFFSET_ITEM* aDialog ) : m_dialog( aDialog ) {}

        bool CcApply( const nlohmann::json& aAnswer, std::string& aField ) override
        {
            if( !aAnswer.is_object() )
                return false;

            std::optional<double> x, y;

            for( auto it = aAnswer.begin(); it != aAnswer.end(); ++it )
            {
                const std::string& key = it.key();

                if( key != "x" && key != "y" )
                {
                    aField = key;
                    return false;
                }

                double iu = 0.0;

                if( it->is_number() && std::isfinite( it->get<double>() ) )
                    iu = std::round( pcbIUScale.mmToIU( it->get<double>() ) );

                if( !it->is_number() || !std::isfinite( it->get<double>() )
                        || std::fabs( iu ) > static_cast<double>( std::numeric_limits<int>::max() ) )
                {
                    aField = key;
                    return false;
                }

                ( key == "x" ? x : y ) = iu;
            }

            // A polar entry is first turned back to the Cartesian fields (the Polar box
            // cleared), then the answer is written over them.
            if( m_dialog->m_polarCoords->GetValue() )
            {
                m_dialog->m_polarCoords->SetValue( false );
                wxCommandEvent dummy;
                m_dialog->OnPolarChanged( dummy );
            }

            if( x )
                m_dialog->m_xOffset.SetDoubleValue( *x );

            if( y )
                m_dialog->m_yOffset.SetDoubleValue( *y );

            if( !m_dialog->Validate() || !m_dialog->TransferDataFromWindow() )
                return false;

            m_dialog->SaveControlState();
            return true;
        }

    private:
        DIALOG_OFFSET_ITEM* m_dialog;
    };

    RECEIVER receiver( this );

    switch( CcHostPromptModal( "offset", record, receiver ) )
    {
    case 1:  SetReturnCode( wxID_OK );     return wxID_OK;
    case 0:  SetReturnCode( wxID_CANCEL ); return wxID_CANCEL;
    default: return DIALOG_OFFSET_ITEM_BASE::ShowModal();
    }
}

#endif // __EMSCRIPTEN__

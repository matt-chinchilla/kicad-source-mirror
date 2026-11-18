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
 * (host_prompt.h, ask findFootprint, modal) and writes its answer into the dialog's own search
 * field; a plain build is unchanged.
 */

#include "dialog_get_footprint_by_name.h"

#include <wx/arrstr.h>


DIALOG_GET_FOOTPRINT_BY_NAME::DIALOG_GET_FOOTPRINT_BY_NAME( PCB_BASE_FRAME* aParent,
                                                            wxArrayString&  aFpList ) :
        DIALOG_GET_FOOTPRINT_BY_NAME_BASE( aParent )
{
    m_choiceFpList->Append( aFpList );

    m_multipleHint->SetFont( KIUI::GetInfoFont( this ).Italic() );

    // Hide help string until someone implements successive placement (#2227)
    m_multipleHint->Show( false );

    SetInitialFocus( m_SearchTextCtrl );

    SetupStandardButtons();

    // Dialog should not shrink beyond it's minimal size.
    GetSizer()->SetSizeHints( this );
}


wxString DIALOG_GET_FOOTPRINT_BY_NAME::GetValue() const
{
    return m_SearchTextCtrl->GetValue();
}


static wxString GetFootprintName( const wxString& aOptionCtrlString )
{
    return aOptionCtrlString.BeforeFirst( ' ' );
}


void DIALOG_GET_FOOTPRINT_BY_NAME::OnSelectFootprint( wxCommandEvent& aEvent )
{
    const int selection = m_choiceFpList->GetSelection();
    if( selection >= 0 )
    {
        m_SearchTextCtrl->SetValue( GetFootprintName( m_choiceFpList->GetString( selection ) ) );
    }
}


void DIALOG_GET_FOOTPRINT_BY_NAME::OnSearchInputChanged( wxCommandEvent& event )
{
    // Find the matching footprint in the list if there is one
    const wxString entry = m_SearchTextCtrl->GetValue().Trim( true ).Trim( false );

    for( unsigned ii = 0; ii < m_choiceFpList->GetCount(); ++ii )
    {
        if( GetFootprintName( m_choiceFpList->GetString( ii ) ).IsSameAs( entry, false ) )
        {
            m_choiceFpList->SetSelection( ii );
            return;
        }
    }
    m_choiceFpList->SetSelection( -1 );
}


#ifdef __EMSCRIPTEN__

// The host prompt (host_prompt.h, ask "findFootprint", modal), Get and Move Footprint. The record
// { references } is the list the dialog offers, the reference of each entry in its order; the
// answer { reference } is written into the dialog's own search field (its list selection
// follows, as typing does) and the caller finds the footprint and moves it, as on OK. A
// reference the list does not hold is refused, so the card stays open.

int DIALOG_GET_FOOTPRINT_BY_NAME::ShowModal()
{
    LoadControlState();
    TransferDataToWindow();

    nlohmann::json references = nlohmann::json::array();

    for( unsigned ii = 0; ii < m_choiceFpList->GetCount(); ++ii )
        references.push_back( GetFootprintName( m_choiceFpList->GetString( ii ) )
                                      .ToStdString( wxConvUTF8 ) );

    nlohmann::json record;
    record["references"] = references;

    class RECEIVER : public CC_PROMPT_RECEIVER
    {
    public:
        explicit RECEIVER( DIALOG_GET_FOOTPRINT_BY_NAME* aDialog ) : m_dialog( aDialog ) {}

        bool CcApply( const nlohmann::json& aAnswer, std::string& aField ) override
        {
            if( !aAnswer.is_object() )
                return false;

            for( auto it = aAnswer.begin(); it != aAnswer.end(); ++it )
            {
                if( it.key() != "reference" )
                {
                    aField = it.key();
                    return false;
                }
            }

            aField = "reference";

            if( !aAnswer.contains( "reference" ) || !aAnswer["reference"].is_string() )
                return false;

            wxString reference = wxString::FromUTF8( aAnswer["reference"].get<std::string>() );
            reference.Trim( true ).Trim( false );

            if( reference.IsEmpty() )
                return false;

            bool held = false;

            for( unsigned ii = 0; ii < m_dialog->m_choiceFpList->GetCount() && !held; ++ii )
            {
                held = GetFootprintName( m_dialog->m_choiceFpList->GetString( ii ) )
                               .IsSameAs( reference, false );
            }

            if( !held )
                return false;

            m_dialog->m_SearchTextCtrl->SetValue( reference );
            aField.clear();
            return m_dialog->Validate() && m_dialog->TransferDataFromWindow();
        }

    private:
        DIALOG_GET_FOOTPRINT_BY_NAME* m_dialog;
    };

    RECEIVER receiver( this );

    switch( CcHostPromptModal( "findFootprint", record, receiver ) )
    {
    case 1:  SetReturnCode( wxID_OK );     return wxID_OK;
    case 0:  SetReturnCode( wxID_CANCEL ); return wxID_CANCEL;
    default: return DIALOG_GET_FOOTPRINT_BY_NAME_BASE::ShowModal();
    }
}

#endif // __EMSCRIPTEN__

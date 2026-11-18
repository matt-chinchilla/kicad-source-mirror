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
 * Modified by Circuit Center Inc on 2026-11-18: WX_TEXT_ENTRY_DIALOG asks the in-browser editor's
 * host to answer it (host_prompt.h, ask entry) before KiCad's own dialog shows (WebAssembly build
 * only).
 */

#include <dialogs/dialog_text_entry.h>
#include <string_utils.h>

#ifdef __EMSCRIPTEN__
#include <host_prompt.h>
#endif


WX_TEXT_ENTRY_DIALOG::WX_TEXT_ENTRY_DIALOG( wxWindow* aParent, const wxString& aFieldLabel,
                                            const wxString& aCaption, const wxString& aDefaultValue,
                                            bool aExtraWidth ) :
        WX_TEXT_ENTRY_DIALOG_BASE( aParent, wxID_ANY, aCaption, wxDefaultPosition, wxDefaultSize )
{
    if( aFieldLabel.IsEmpty() )
        m_label->Hide();
    else
        m_label->SetLabel( aFieldLabel );

    m_textCtrl->SetValue( aDefaultValue );
    m_textCtrl->SetMinSize( FromDIP( aExtraWidth ? wxSize( 700, -1 ) : wxSize( 300, -1 ) ) );

    // DIALOG_SHIM needs a title- and label-specific hash_key so we don't save/restore state between
    // usage cases.
    m_hash_key = TO_UTF8( aCaption + aFieldLabel );

    // The text value is always supplied by the caller through aDefaultValue and is specific to the
    // item being edited.  Persisting and restoring it would clobber that value with whatever was last
    // entered, so opt the control out of DIALOG_SHIM state save/restore.
    OptOut( m_textCtrl );

    SetupStandardButtons();

    SetInitialFocus( m_textCtrl );

    this->Layout();
    m_mainSizer->Fit( this );
}


void WX_TEXT_ENTRY_DIALOG::SetTextValidator( const wxTextValidator& validator )
{
    m_textCtrl->SetValidator( validator );
}


wxString WX_TEXT_ENTRY_DIALOG::GetValue() const
{
    return m_textCtrl->GetValue();
}




#ifdef __EMSCRIPTEN__
int WX_TEXT_ENTRY_DIALOG::ShowModal()
{
    TransferDataToWindow();

    nlohmann::json field;
    field["key"] = "value";
    field["label"] = m_label->IsShown() ? m_label->GetLabel().ToStdString( wxConvUTF8 )
                                        : std::string();
    field["kind"] = "text";
    field["value"] = GetValue().ToStdString( wxConvUTF8 );
    field["tip"] = m_textCtrl->GetToolTipText().ToStdString( wxConvUTF8 );

    nlohmann::json record;
    record["title"] = GetTitle().ToStdString( wxConvUTF8 );
    record["fields"] = nlohmann::json::array( { field } );

    class RECEIVER : public CC_PROMPT_RECEIVER
    {
    public:
        explicit RECEIVER( WX_TEXT_ENTRY_DIALOG* aDialog ) : m_dialog( aDialog ) {}

        bool CcApply( const nlohmann::json& aAnswer, std::string& aField ) override
        {
            aField = "value";

            if( !aAnswer.is_object() || !aAnswer.contains( "values" )
                || !aAnswer["values"].is_object() )
            {
                aField.clear();
                return false;
            }

            const nlohmann::json& values = aAnswer["values"];

            if( values.contains( "value" ) )
            {
                if( !values["value"].is_string() )
                    return false;

                m_dialog->m_textCtrl->SetValue(
                        wxString::FromUTF8( values["value"].get<std::string>() ) );
            }

            // The caller's validator, asked for its sentence rather than its message box.
            if( wxTextValidator* validator =
                        dynamic_cast<wxTextValidator*>( m_dialog->m_textCtrl->GetValidator() ) )
            {
                const wxString error = validator->IsValid( m_dialog->m_textCtrl->GetValue() );

                if( !error.IsEmpty() )
                {
                    CcPromptCaptureMessage( error.ToStdString( wxConvUTF8 ) );
                    return false;
                }
            }

            return m_dialog->TransferDataFromWindow();
        }

    private:
        WX_TEXT_ENTRY_DIALOG* m_dialog;
    };

    RECEIVER receiver( this );

    switch( CcHostPromptModal( "entry", record, receiver ) )
    {
    case 1:  SetReturnCode( wxID_OK );     return wxID_OK;
    case 0:  SetReturnCode( wxID_CANCEL ); return wxID_CANCEL;
    default: return WX_TEXT_ENTRY_DIALOG_BASE::ShowModal();
    }
}
#endif // __EMSCRIPTEN__

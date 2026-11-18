/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2020 Roberto Fernandez Bautista <roberto.fer.bau@gmail.com>
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
 * Modified by Circuit Center Inc on 2026-11-18: WX_UNIT_ENTRY_DIALOG and WX_PT_ENTRY_DIALOG ask
 * the in-browser editor's host to answer them (host_prompt.h, ask entry) before KiCad's own
 * dialog shows (WebAssembly build only).
 */

#include <eda_draw_frame.h>
#include <dialogs/dialog_unit_entry.h>
#include <string_utils.h>

#ifdef __EMSCRIPTEN__
#include <cmath>
#include <host_prompt.h>
#endif


WX_UNIT_ENTRY_DIALOG::WX_UNIT_ENTRY_DIALOG( EDA_DRAW_FRAME* aParent, const wxString& aCaption,
                                            const wxString& aLabel, long long int aDefaultValue ) :
        WX_UNIT_ENTRY_DIALOG_BASE( ( wxWindow* ) aParent, wxID_ANY, aCaption ),
        m_unit_binder( aParent, m_label, m_textCtrl, m_unit_label, true )
{
    m_label->SetLabel( aLabel );
    m_unit_binder.SetValue( aDefaultValue );

    // DIALOG_SHIM needs a title- and label-specific hash_key so we don't save/restore state between
    // usage cases.
    m_hash_key = TO_UTF8( aCaption + aLabel );

    SetInitialFocus( m_textCtrl );
    SetupStandardButtons();

	Layout();
	bSizerMain->Fit( this );
}


int WX_UNIT_ENTRY_DIALOG::GetValue()
{
    return m_unit_binder.GetIntValue();
}


WX_PT_ENTRY_DIALOG::WX_PT_ENTRY_DIALOG( EDA_DRAW_FRAME* aParent, const wxString& aCaption,
                                        const wxString& aLabelX, const wxString& aLabelY,
                                        const VECTOR2I& aDefaultValue, bool aShowResetButt ) :
        WX_PT_ENTRY_DIALOG_BASE( ( wxWindow* ) aParent, wxID_ANY, aCaption ),
        m_unit_binder_x( aParent, m_labelX, m_textCtrlX, m_unitsX, true ),
        m_unit_binder_y( aParent, m_labelY, m_textCtrlY, m_unitsY, true )
{
    m_ButtonReset->Show( aShowResetButt );

    m_labelX->SetLabel( aLabelX );
    m_labelY->SetLabel( aLabelY );

    m_unit_binder_x.SetCoordType( ORIGIN_TRANSFORMS::ABS_X_COORD );
    m_unit_binder_y.SetCoordType( ORIGIN_TRANSFORMS::ABS_Y_COORD );

    OptOut( m_textCtrlX );
    OptOut( m_textCtrlY );

    m_unit_binder_x.SetValue( aDefaultValue.x );
    m_unit_binder_y.SetValue( aDefaultValue.y );

    // DIALOG_SHIM needs a title- and label-specific hash_key so we don't save/restore state between
    // usage cases.
    m_hash_key = TO_UTF8( aCaption + aLabelX + aLabelY );

    SetInitialFocus( m_textCtrlX );
    SetupStandardButtons();

	finishDialogSettings();
}


VECTOR2I WX_PT_ENTRY_DIALOG::GetValue()
{
    return VECTOR2I( m_unit_binder_x.GetIntValue(), m_unit_binder_y.GetIntValue() );
}

void WX_PT_ENTRY_DIALOG::ResetValues( wxCommandEvent& event )
{
    m_unit_binder_x.SetValue( 0 );
    m_unit_binder_y.SetValue( 0 );
}


#ifdef __EMSCRIPTEN__
namespace
{
// The frame's internal units per millimeter (the dialog's parent is the frame that owns it).
const EDA_IU_SCALE* ccPromptScale( wxWindow* aParent )
{
    UNITS_PROVIDER* units = dynamic_cast<UNITS_PROVIDER*>( aParent );
    return units ? &units->GetIuScale() : nullptr;
}


// A millimeter answer in internal units; false for anything but a finite number.
bool ccPromptLength( const nlohmann::json& aValue, const EDA_IU_SCALE& aScale, long long& aIU )
{
    if( !aValue.is_number() )
        return false;

    const double mm = aValue.get<double>();

    if( !std::isfinite( mm ) )
        return false;

    const double iu = std::round( mm * aScale.IU_PER_MM );

    if( std::fabs( iu ) > 2147483647.0 )
        return false;

    aIU = static_cast<long long>( iu );
    return true;
}
} // namespace


int WX_UNIT_ENTRY_DIALOG::ShowModal()
{
    const EDA_IU_SCALE* scale = ccPromptScale( GetParent() );

    if( !scale )
        return WX_UNIT_ENTRY_DIALOG_BASE::ShowModal();

    TransferDataToWindow();

    nlohmann::json field;
    field["key"] = "value";
    field["label"] = m_label->GetLabel().ToStdString( wxConvUTF8 );
    field["kind"] = "length";
    field["value"] = scale->IUTomm( GetValue() );
    field["tip"] = m_textCtrl->GetToolTipText().ToStdString( wxConvUTF8 );

    nlohmann::json record;
    record["title"] = GetTitle().ToStdString( wxConvUTF8 );
    record["fields"] = nlohmann::json::array( { field } );

    class RECEIVER : public CC_PROMPT_RECEIVER
    {
    public:
        RECEIVER( WX_UNIT_ENTRY_DIALOG* aDialog, const EDA_IU_SCALE& aScale ) :
                m_dialog( aDialog ), m_scale( aScale )
        {
        }

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
            long long             iu = 0;

            if( values.contains( "value" ) )
            {
                if( !ccPromptLength( values["value"], m_scale, iu ) )
                    return false;

                m_dialog->m_unit_binder.SetValue( iu );
            }

            return m_dialog->Validate() && m_dialog->TransferDataFromWindow();
        }

    private:
        WX_UNIT_ENTRY_DIALOG* m_dialog;
        const EDA_IU_SCALE&   m_scale;
    };

    RECEIVER receiver( this, *scale );

    switch( CcHostPromptModal( "entry", record, receiver ) )
    {
    case 1:  SetReturnCode( wxID_OK );     return wxID_OK;
    case 0:  SetReturnCode( wxID_CANCEL ); return wxID_CANCEL;
    default: return WX_UNIT_ENTRY_DIALOG_BASE::ShowModal();
    }
}


int WX_PT_ENTRY_DIALOG::ShowModal()
{
    const EDA_IU_SCALE* scale = ccPromptScale( GetParent() );

    if( !scale )
        return WX_PT_ENTRY_DIALOG_BASE::ShowModal();

    TransferDataToWindow();

    // The point in the frame's own coordinates (GetValue undoes the user origin the fields show).
    const VECTOR2I pt = GetValue();

    nlohmann::json field;
    field["key"] = "point";
    field["label"] = ( m_labelX->GetLabel() + wxS( ", " ) + m_labelY->GetLabel() )
                             .ToStdString( wxConvUTF8 );
    field["kind"] = "point";
    field["value"] = { { "x", scale->IUTomm( pt.x ) }, { "y", scale->IUTomm( pt.y ) } };
    field["tip"] = m_textCtrlX->GetToolTipText().ToStdString( wxConvUTF8 );

    nlohmann::json record;
    record["title"] = GetTitle().ToStdString( wxConvUTF8 );
    record["fields"] = nlohmann::json::array( { field } );

    class RECEIVER : public CC_PROMPT_RECEIVER
    {
    public:
        RECEIVER( WX_PT_ENTRY_DIALOG* aDialog, const EDA_IU_SCALE& aScale ) :
                m_dialog( aDialog ), m_scale( aScale )
        {
        }

        bool CcApply( const nlohmann::json& aAnswer, std::string& aField ) override
        {
            aField = "point";

            if( !aAnswer.is_object() || !aAnswer.contains( "values" )
                || !aAnswer["values"].is_object() )
            {
                aField.clear();
                return false;
            }

            const nlohmann::json& values = aAnswer["values"];

            if( values.contains( "point" ) )
            {
                const nlohmann::json& point = values["point"];
                long long             x = 0;
                long long             y = 0;

                if( !point.is_object() || !point.contains( "x" ) || !point.contains( "y" )
                    || !ccPromptLength( point["x"], m_scale, x )
                    || !ccPromptLength( point["y"], m_scale, y ) )
                {
                    return false;
                }

                m_dialog->m_unit_binder_x.SetValue( x );
                m_dialog->m_unit_binder_y.SetValue( y );
            }

            return m_dialog->Validate() && m_dialog->TransferDataFromWindow();
        }

    private:
        WX_PT_ENTRY_DIALOG* m_dialog;
        const EDA_IU_SCALE& m_scale;
    };

    RECEIVER receiver( this, *scale );

    switch( CcHostPromptModal( "entry", record, receiver ) )
    {
    case 1:  SetReturnCode( wxID_OK );     return wxID_OK;
    case 0:  SetReturnCode( wxID_CANCEL ); return wxID_CANCEL;
    default: return WX_PT_ENTRY_DIALOG_BASE::ShowModal();
    }
}
#endif // __EMSCRIPTEN__

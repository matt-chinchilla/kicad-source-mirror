/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2007 Jean-Pierre Charras, jp.charras at wanadoo.fr
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
 * Modified by Circuit Center Inc on 2026-11-18: KIDIALOG::ShowModal and the message boxes of
 * confirm.cpp ask the in-browser editor's host first through host_prompt.h (ask message,
 * CcHostMessage), KiCad's own dialog only when the host does not take it (WebAssembly build
 * only).
 */

#include <kidialog.h>
#include <unordered_map>

#ifdef __EMSCRIPTEN__
#include <set>
#include <host_prompt.h>
#include <wx/msgdlg.h>
#include <wx/utils.h>
#endif


// Set of dialogs that have been chosen not to be shown again
static std::unordered_map<unsigned long, int> g_doNotShowAgainDlgs;


KIDIALOG::KIDIALOG( wxWindow* aParent, const wxString& aMessage, const wxString& aCaption, long aStyle ) :
        KIDIALOG_BASE( aParent, aMessage, aCaption, aStyle | wxCENTRE | wxSTAY_ON_TOP ),
        m_hash( 0 ),
        m_cancelMeansCancel( true )
{
}


KIDIALOG::KIDIALOG( wxWindow* aParent, const wxString& aMessage, KD_TYPE aType, const wxString& aCaption ) :
        KIDIALOG_BASE( aParent, aMessage, getCaption( aType, aCaption ), getStyle( aType ) ),
        m_hash( 0 ),
        m_cancelMeansCancel( true )
{
}


void KIDIALOG::ClearDoNotShowAgainDialogs()
{
    g_doNotShowAgainDlgs = {};
}


void KIDIALOG::DoNotShowCheckbox( wxString aUniqueId, int line )
{
    ShowCheckBox( _( "Do not show again" ), false );

    m_hash = std::hash<wxString>{}( aUniqueId ) + line;
}


bool KIDIALOG::DoNotShowAgain() const
{
    return g_doNotShowAgainDlgs.count( m_hash ) > 0;
}


bool KIDIALOG::Show( bool aShow )
{
    // We should check the do-not-show-again setting only when the dialog is displayed
    if( aShow )
    {
        // Check if this dialog should be shown to the user
        auto it = g_doNotShowAgainDlgs.find( m_hash );

        if( it != g_doNotShowAgainDlgs.end() )
            return it->second;
    }

    int ret = KIDIALOG_BASE::Show( aShow );

    // Has the user asked not to show the dialog again?
    // Note that we don't save a Cancel value unless the Cancel button is being used for some
    // other function (which is actually more common than it being used for Cancel).
    if( IsCheckBoxChecked() && (!m_cancelMeansCancel || ret != wxID_CANCEL ) )
        g_doNotShowAgainDlgs[m_hash] = ret;

    return ret;
}


#ifdef __EMSCRIPTEN__
/**
 * The message ask (canvas addendum 4.2): a message box asks the in-browser editor's host to
 * show it in a card of its own, as KiCad would show @a aDialog. The record is
 * { title, text, extra, icon, buttons: [{ id, label }], dontAsk } with icon info, warning, error
 * or question and button ids ok, cancel, yes, no in KiCad's order; the answer is
 * { button, dontAsk }. A message raised while a prompt answer is applied is captured as that
 * answer's refusal (host_prompt.h), never asked.
 *
 * @param aOfferDontAsk the dialog offers KiCad's "do not show again" check box.
 * @param aDontAsk      set to the host's dontAsk when it answers (may be null).
 * @return the pressed button's wx id (wxID_OK, wxID_CANCEL, wxID_YES, wxID_NO), the dialog's
 *         dismissal id when the host canceled or the message was captured, or -1 when the host
 *         did not take it (show KiCad's dialog).
 */
int CcHostMessage( const wxMessageDialogBase& aDialog, bool aOfferDontAsk, bool* aDontAsk )
{
    const long style = aDialog.GetMessageDialogStyle();

    std::vector<std::pair<std::string, wxString>> buttons;

    if( style & wxYES_NO )
    {
        buttons.emplace_back( "yes", aDialog.GetYesLabel() );
        buttons.emplace_back( "no", aDialog.GetNoLabel() );
    }
    else
    {
        buttons.emplace_back( "ok", aDialog.GetOKLabel() );
    }

    if( style & wxCANCEL )
        buttons.emplace_back( "cancel", aDialog.GetCancelLabel() );

    // What a dismissal (Escape, the host's cancel) answers: Cancel, else No, else OK.
    int dismissed = wxID_OK;

    if( style & wxCANCEL )
        dismissed = wxID_CANCEL;
    else if( style & wxYES_NO )
        dismissed = wxID_NO;

    if( CcPromptCapturing() )
    {
        wxString text = aDialog.GetMessage();

        if( !aDialog.GetExtendedMessage().IsEmpty() )
            text << wxS( " " ) << aDialog.GetExtendedMessage();

        CcPromptCaptureMessage( text.ToStdString( wxConvUTF8 ) );
        return dismissed;
    }

    const char* icon = "info";

    switch( aDialog.GetEffectiveIcon() )
    {
    case wxICON_ERROR:    icon = "error";    break;
    case wxICON_WARNING:  icon = "warning";  break;
    case wxICON_QUESTION: icon = "question"; break;
    default:              icon = "info";     break;
    }

    nlohmann::json record;
    record["title"] = aDialog.GetTitle().ToStdString( wxConvUTF8 );
    record["text"] = aDialog.GetMessage().ToStdString( wxConvUTF8 );
    record["extra"] = aDialog.GetExtendedMessage().ToStdString( wxConvUTF8 );
    record["icon"] = icon;
    record["buttons"] = nlohmann::json::array();
    record["dontAsk"] = aOfferDontAsk;

    std::set<std::string> ids;

    for( const auto& [id, label] : buttons )
    {
        record["buttons"].push_back(
                { { "id", id }, { "label", wxStripMenuCodes( label ).ToStdString( wxConvUTF8 ) } } );
        ids.insert( id );
    }

    class RECEIVER : public CC_PROMPT_RECEIVER
    {
    public:
        RECEIVER( const std::set<std::string>& aIds, bool aOfferDontAsk ) :
                m_ids( aIds ), m_offerDontAsk( aOfferDontAsk )
        {}

        bool CcApply( const nlohmann::json& aAnswer, std::string& aField ) override
        {
            if( !aAnswer.is_object() || !aAnswer.contains( "button" )
                || !aAnswer["button"].is_string()
                || !m_ids.count( aAnswer["button"].get<std::string>() ) )
            {
                aField = "button";
                return false;
            }

            if( aAnswer.contains( "dontAsk" ) )
            {
                if( !aAnswer["dontAsk"].is_boolean() )
                {
                    aField = "dontAsk";
                    return false;
                }

                m_dontAsk = m_offerDontAsk && aAnswer["dontAsk"].get<bool>();
            }

            m_button = aAnswer["button"].get<std::string>();
            return true;
        }

        std::set<std::string> m_ids;
        bool                  m_offerDontAsk;
        std::string           m_button;
        bool                  m_dontAsk = false;
    };

    RECEIVER receiver( ids, aOfferDontAsk );

    switch( CcHostPromptModal( "message", record, receiver ) )
    {
    case 1:
        if( aDontAsk )
            *aDontAsk = receiver.m_dontAsk;

        if( receiver.m_button == "yes" )
            return wxID_YES;
        else if( receiver.m_button == "no" )
            return wxID_NO;
        else if( receiver.m_button == "cancel" )
            return wxID_CANCEL;

        return wxID_OK;

    case 0:
        return dismissed;

    default:
        return -1;
    }
}
#endif // __EMSCRIPTEN__


int KIDIALOG::ShowModal()
{
    // Check if this dialog should be shown to the user
    auto it = g_doNotShowAgainDlgs.find( m_hash );

    if( it != g_doNotShowAgainDlgs.end() )
        return it->second;

#ifdef __EMSCRIPTEN__
    // The host's card first; its "do not show again" answer stands in for the check box.
    bool      dontAsk = false;
    const int hosted = CcHostMessage( *this, m_hash != 0 && !GetCheckBoxText().IsEmpty(),
                                      &dontAsk );

    if( hosted != -1 )
    {
        if( dontAsk )
            ShowCheckBox( GetCheckBoxText(), true );

        SetReturnCode( hosted );
    }

    int ret = hosted != -1 ? hosted : KIDIALOG_BASE::ShowModal();
#else
    int ret = KIDIALOG_BASE::ShowModal();
#endif

    // Has the user asked not to show the dialog again?
    // Note that we don't save a Cancel value unless the Cancel button is being used for some
    // other function (which is actually more common than it being used for Cancel).
    if( IsCheckBoxChecked() && (!m_cancelMeansCancel || ret != wxID_CANCEL ) )
        g_doNotShowAgainDlgs[m_hash] = ret;

    return ret;
}


wxString KIDIALOG::getCaption( KD_TYPE aType, const wxString& aCaption )
{
    if( !aCaption.IsEmpty() )
        return aCaption;

    switch( aType )
    {
    case KD_NONE:       /* fall through */
    case KD_INFO:       return _( "Message" );
    case KD_QUESTION:   return _( "Question" );
    case KD_WARNING:    return _( "Warning" );
    case KD_ERROR:      return _( "Error" );
    }

    return wxEmptyString;
}


long KIDIALOG::getStyle( KD_TYPE aType )
{
    long style = wxOK | wxCENTRE | wxSTAY_ON_TOP;

    switch( aType )
    {
    case KD_NONE:       break;
    case KD_INFO:       style |= wxICON_INFORMATION; break;
    case KD_QUESTION:   style |= wxICON_QUESTION;    break;
    case KD_WARNING:    style |= wxICON_WARNING;     break;
    case KD_ERROR:      style |= wxICON_ERROR;       break;
    }

    return style;
}


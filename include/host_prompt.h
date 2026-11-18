// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef HOST_PROMPT_H
#define HOST_PROMPT_H

#ifdef __EMSCRIPTEN__

/*
 * Host prompts (engine run 15, K-PROMPT): a KiCad dialog asks the WebAssembly editor's host
 * to answer it in a card of its own before the dialog is ever shown.
 *
 * A hooked dialog overrides ShowModal (Show for a modeless one, ShowQuasiModal for a quasi
 * modal one) under __EMSCRIPTEN__: it runs its own TransferDataToWindow, builds the record from
 * what that would show, and asks through CcHostPromptModal or CcHostPromptModeless. The host
 * listener is window.kicadHostPrompt.onAsk( text ), text being the JSON
 * { token, ask, modal, record } (a string, as every host ask since run 12). A truthy answer
 * takes the prompt; no listener, a falsy answer or a throwing listener leaves it to KiCad's own
 * dialog (CcHostPromptModal returns -1, CcHostPromptModeless returns 0).
 *
 * The host answers with kicadPromptAnswer( token, json ), which calls CcHostPromptAnswer here:
 * the receiver writes the answer into the dialog's own controls and runs its own
 * TransferDataFromWindow (a modeless one also its OK handler). A refusal keeps the prompt
 * parked and is returned to the host with KiCad's sentence: a message box KiCad raises while
 * an answer is applied is captured (CcPromptCapturing, CcPromptCaptureMessage), never shown.
 * An accepted modal answer resumes the parked ShowModal, which returns wxID_OK.
 *
 * The wait is the port's own modal wait (wxWasmBeginWait, wxWasmYieldUntil, the same
 * mechanism as wxDialog::ShowModal on the port), so a prompt behaves as a dialog for every
 * caller. The C declarations of wx/wasm/private/yieldwait.h are repeated here so no wx private
 * header is included from KiCad.
 *
 * Units in every record and answer: millimeters, degrees, layers by canonical name.
 */

#include <map>
#include <vector>
#include <string>
#include <emscripten.h>
#include <json_common.h>

extern "C" int  wxWasmBeginWait( const char* kind );
extern "C" int  wxWasmYieldUntil( int token );
extern "C" void wxWasmResolveWait( int token, int result );

// wx/wasm/private/dispatch.h: the dispatch interlock a long-lived "modal" suspension zeroes for
// its whole span (wxDialog::ShowModal, the DOM popup menu), so the top-level tick keeps
// dispatching while the opener's chain is parked.
extern int wxWasmDispatchDepth;
void       wxWasmDispatchRestore( int saved, const char* site );


/**
 * Park the calling chain on the port's wait @a aWait as wxDialog::ShowModal does (the dispatch
 * interlock zeroed for the whole span) and return the result it is resolved with.
 */
inline int CcHostWait( int aWait, const char* aSite )
{
    const int savedDispatchDepth = wxWasmDispatchDepth;
    wxWasmDispatchDepth = 0;
    const int result = wxWasmYieldUntil( aWait );
    wxWasmDispatchRestore( savedDispatchDepth, aSite );
    return result;
}


/**
 * What a hooked dialog gives the prompt state so an answer, a pick or a cancel reaches its own
 * code while the prompt is parked.
 */
class CC_PROMPT_RECEIVER
{
public:
    virtual ~CC_PROMPT_RECEIVER() = default;

    /**
     * Write @a aAnswer into the dialog's own controls and run its own TransferDataFromWindow
     * (a modeless dialog: then its own OK handler). Return true when KiCad took it; false
     * keeps the prompt parked, with @a aField the record key KiCad refused ("" when none).
     */
    virtual bool CcApply( const nlohmann::json& aAnswer, std::string& aField ) = 0;

    /**
     * Run the dialog's own picker of @a aKind ("item" or "point"). Return false when this ask
     * has no such picker (the default).
     */
    virtual bool CcPick( const std::string& aKind ) { return false; }

    /** The host canceled a modeless prompt: run the dialog's own Cancel. */
    virtual void CcCancel() {}
};


/** One parked prompt. */
struct CC_PARKED_PROMPT
{
    std::string         ask;
    bool                modal = true;
    int                 waitToken = 0;          ///< the port's wait, modal prompts only
    CC_PROMPT_RECEIVER* receiver = nullptr;
};


/** The parked prompt state: every prompt the host holds, by token. */
inline std::map<int, CC_PARKED_PROMPT>& CcParkedPrompts()
{
    static std::map<int, CC_PARKED_PROMPT> s_parked;
    return s_parked;
}


inline int CcNextPromptToken()
{
    static int s_next = 0;
    return ++s_next;
}


/** True while a modal prompt is parked (the engine is inside a dialog's ShowModal). */
inline bool CcHostPromptModalParked()
{
    for( const auto& [token, parked] : CcParkedPrompts() )
    {
        if( parked.modal )
            return true;
    }

    return false;
}


struct CC_PROMPT_CAPTURE
{
    bool        active = false;
    std::string message;
};


inline CC_PROMPT_CAPTURE& CcPromptCapture()
{
    static CC_PROMPT_CAPTURE s_capture;
    return s_capture;
}


/** True while an answer is applied: a message box is captured, never shown or asked. */
inline bool CcPromptCapturing()
{
    return CcPromptCapture().active;
}


/** Keep KiCad's sentence as the refusal's message (the first one raised wins). */
inline void CcPromptCaptureMessage( const std::string& aText )
{
    CC_PROMPT_CAPTURE& capture = CcPromptCapture();

    if( capture.active && capture.message.empty() )
        capture.message = aText;
}


/** Call a window.kicadHostPrompt listener with JSON text; true when it answered truthy. */
inline bool CcCallHostPrompt( const char* aListener, const nlohmann::json& aPayload )
{
    const std::string text =
            aPayload.dump( -1, ' ', false, nlohmann::json::error_handler_t::replace );

    int taken = EM_ASM_INT( {
        var name = UTF8ToString( $0 );

        if( !window.kicadHostPrompt || typeof window.kicadHostPrompt[name] !== 'function' )
            return 0;

        // A throwing listener must never unwind the wasm frame that called it.
        try { return window.kicadHostPrompt[name]( UTF8ToString( $1 ) ) ? 1 : 0; }
        catch( e ) { console.error( '[cc prompt] ' + name + ' listener threw', e ); return 0; }
    }, aListener, text.c_str() );

    return taken != 0;
}


/**
 * Ask the host to answer a modal dialog, then park until it answers or cancels.
 *
 * @return 1 answered (the receiver took the answer: return wxID_OK), 0 canceled (return
 *         wxID_CANCEL), -1 not taken (show KiCad's own dialog).
 */
inline int CcHostPromptModal( const char* aAsk, const nlohmann::json& aRecord,
                              CC_PROMPT_RECEIVER& aReceiver )
{
    const int token = CcNextPromptToken();

    nlohmann::json ask;
    ask["token"] = token;
    ask["ask"] = aAsk;
    ask["modal"] = true;
    ask["record"] = aRecord;

    // Begin the wait BEFORE asking: an answer racing ahead of the suspend pre-resolves it.
    const int wait = wxWasmBeginWait( "prompt" );

    CC_PARKED_PROMPT& parked = CcParkedPrompts()[token];
    parked.ask = aAsk;
    parked.modal = true;
    parked.waitToken = wait;
    parked.receiver = &aReceiver;

    if( !CcCallHostPrompt( "onAsk", ask ) )
    {
        CcParkedPrompts().erase( token );
        wxWasmResolveWait( wait, -1 );
        wxWasmYieldUntil( wait );   // pre-resolved: returns at once, never suspends
        return -1;
    }

    const int result = CcHostWait( wait, "CcHostPromptModal" );
    CcParkedPrompts().erase( token );
    return result == 1 ? 1 : 0;
}


/**
 * The modal ask for a dialog with nothing to validate: the answer is handed back whole in
 * @a aAnswer and the caller writes it into its controls itself (a refusal cannot stay parked
 * on this road; a dialog whose TransferDataFromWindow can refuse uses the receiver form).
 */
inline int CcHostPromptModal( const char* aAsk, const nlohmann::json& aRecord,
                              nlohmann::json& aAnswer )
{
    class TAKE_ANSWER : public CC_PROMPT_RECEIVER
    {
    public:
        explicit TAKE_ANSWER( nlohmann::json& aOut ) : m_out( aOut ) {}

        bool CcApply( const nlohmann::json& aAnswer, std::string& aField ) override
        {
            m_out = aAnswer;
            return true;
        }

    private:
        nlohmann::json& m_out;
    };

    TAKE_ANSWER receiver( aAnswer );
    return CcHostPromptModal( aAsk, aRecord, receiver );
}


/**
 * Ask the host to answer a modeless dialog (Create Array, Position Relative). Returns at once.
 *
 * @return the prompt's token (the dialog stays alive and its receiver answers until it calls
 *         CcHostPromptEnd), or 0 when not taken (show KiCad's own dialog).
 */
inline int CcHostPromptModeless( const char* aAsk, const nlohmann::json& aRecord,
                                 CC_PROMPT_RECEIVER* aReceiver )
{
    if( !aReceiver )
        return 0;

    const int token = CcNextPromptToken();

    nlohmann::json ask;
    ask["token"] = token;
    ask["ask"] = aAsk;
    ask["modal"] = false;
    ask["record"] = aRecord;

    CC_PARKED_PROMPT& parked = CcParkedPrompts()[token];
    parked.ask = aAsk;
    parked.modal = false;
    parked.receiver = aReceiver;

    if( !CcCallHostPrompt( "onAsk", ask ) )
    {
        CcParkedPrompts().erase( token );
        return 0;
    }

    return token;
}


/** A modeless prompt's new state (a pick's result): window.kicadHostPrompt.onUpdate. */
inline void CcHostPromptUpdate( int aToken, const nlohmann::json& aRecord )
{
    if( !CcParkedPrompts().count( aToken ) )
        return;

    nlohmann::json update;
    update["token"] = aToken;
    update["record"] = aRecord;
    CcCallHostPrompt( "onUpdate", update );
}


/**
 * The engine ended a prompt itself (its tool canceled, its modeless dialog destroyed):
 * window.kicadHostPrompt.onEnd with @a aReason "canceled" or "gone". A parked modal prompt
 * resumes as canceled.
 */
inline void CcHostPromptEnd( int aToken, const char* aReason )
{
    auto it = CcParkedPrompts().find( aToken );

    if( it == CcParkedPrompts().end() )
        return;

    const CC_PARKED_PROMPT parked = it->second;
    CcParkedPrompts().erase( it );

    nlohmann::json end;
    end["token"] = aToken;
    end["reason"] = aReason;
    CcCallHostPrompt( "onEnd", end );

    if( parked.modal )
        wxWasmResolveWait( parked.waitToken, 0 );
}


/** A receiver going away ends every prompt it answers ("gone"). */
inline void CcHostPromptForget( CC_PROMPT_RECEIVER* aReceiver )
{
    std::vector<int> tokens;

    for( const auto& [token, parked] : CcParkedPrompts() )
    {
        if( parked.receiver == aReceiver )
            tokens.push_back( token );
    }

    for( int token : tokens )
        CcHostPromptEnd( token, "gone" );
}


/**
 * kicadPromptAnswer: apply the host's answer to the parked prompt @a aToken.
 *
 * @return {"accepted":true}, {"accepted":false,"field":"<key or empty>","message":"<KiCad's
 *         sentence>"} (the prompt stays parked), or {"stale":true}.
 */
inline nlohmann::json CcHostPromptAnswer( int aToken, const nlohmann::json& aAnswer )
{
    auto it = CcParkedPrompts().find( aToken );

    if( it == CcParkedPrompts().end() || !it->second.receiver )
        return nlohmann::json{ { "stale", true } };

    CC_PROMPT_RECEIVER* receiver = it->second.receiver;

    CC_PROMPT_CAPTURE& capture = CcPromptCapture();
    const CC_PROMPT_CAPTURE saved = capture;
    capture.active = true;
    capture.message.clear();

    std::string field;
    bool        accepted = false;

    try
    {
        accepted = receiver->CcApply( aAnswer, field );
    }
    catch( ... )
    {
        accepted = false;
    }

    const std::string message = capture.message;
    capture = saved;

    if( !accepted )
    {
        return nlohmann::json{ { "accepted", false }, { "field", field },
                               { "message", message } };
    }

    // The receiver may have ended the prompt itself (a modeless OK that closes its dialog).
    it = CcParkedPrompts().find( aToken );

    if( it != CcParkedPrompts().end() && it->second.modal )
    {
        const int wait = it->second.waitToken;
        CcParkedPrompts().erase( it );
        wxWasmResolveWait( wait, 1 );
    }

    return nlohmann::json{ { "accepted", true } };
}


/** kicadPromptCancel: 0 canceled, 1 stale. */
inline int CcHostPromptCancel( int aToken )
{
    auto it = CcParkedPrompts().find( aToken );

    if( it == CcParkedPrompts().end() )
        return 1;

    const CC_PARKED_PROMPT parked = it->second;
    CcParkedPrompts().erase( it );

    if( parked.modal )
        wxWasmResolveWait( parked.waitToken, 0 );
    else if( parked.receiver )
        parked.receiver->CcCancel();

    return 0;
}


/** kicadPromptPick: 0 the picker runs, 1 stale, 2 this ask has no such picker. */
inline int CcHostPromptPick( int aToken, const std::string& aKind )
{
    auto it = CcParkedPrompts().find( aToken );

    if( it == CcParkedPrompts().end() || !it->second.receiver )
        return 1;

    if( aKind != "item" && aKind != "point" )
        return 2;

    return it->second.receiver->CcPick( aKind ) ? 0 : 2;
}

#endif // __EMSCRIPTEN__

#endif // HOST_PROMPT_H

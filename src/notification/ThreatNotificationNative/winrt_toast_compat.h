// winrt_toast_compat.h — Manual COM ABI definitions for WinRT Toast Notifications
// Required because MinGW-w64 ships without <windows.ui.notifications.h> and
// <notificationactivationcallback.h>.  All GUIDs and vtable layouts are stable
// ABI contracts documented by Microsoft.
//
// References:
//   https://learn.microsoft.com/en-us/uwp/api/windows.ui.notifications
//   https://learn.microsoft.com/en-us/windows/win32/api/notificationactivationcallback/

#pragma once

#include <windows.h>
#include <inspectable.h>
#include <roapi.h>
#include <winstring.h>

// ═══════════════════════════════════════════════════════════════════════
// HSTRING helper
// ═══════════════════════════════════════════════════════════════════════
struct HStr {
    HSTRING h = nullptr;
    HStr() = default;
    explicit HStr(const wchar_t* s) { WindowsCreateString(s, (UINT32)wcslen(s), &h); }
    ~HStr() { if (h) WindowsDeleteString(h); }
    HStr(const HStr&) = delete;
    HStr& operator=(const HStr&) = delete;
    operator HSTRING() const { return h; }
};

// ═══════════════════════════════════════════════════════════════════════
// INotificationActivationCallback
// {53E31837-6600-4A81-9395-75CFFE746F94}
// ═══════════════════════════════════════════════════════════════════════
typedef struct NOTIFICATION_USER_INPUT_DATA {
    LPCWSTR Key;
    LPCWSTR Value;
} NOTIFICATION_USER_INPUT_DATA;

MIDL_INTERFACE("53E31837-6600-4A81-9395-75CFFE746F94")
INotificationActivationCallback : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE Activate(
        LPCWSTR appUserModelId,
        LPCWSTR invokedArgs,
        const NOTIFICATION_USER_INPUT_DATA* data,
        ULONG count) = 0;
};

static const GUID IID_INotificationActivationCallback =
    { 0x53E31837, 0x6600, 0x4A81, {0x93,0x95,0x75,0xCF,0xFE,0x74,0x6F,0x94} };

// ═══════════════════════════════════════════════════════════════════════
// ABI::Windows::Data::Xml::Dom — IXmlDocument, IXmlDocumentIO
// ═══════════════════════════════════════════════════════════════════════

// IXmlDocumentIO {6CD0E74E-EE65-4489-9EBF-CA43E87BA637}
MIDL_INTERFACE("6CD0E74E-EE65-4489-9EBF-CA43E87BA637")
IXmlDocumentIO : public IInspectable {
public:
    virtual HRESULT STDMETHODCALLTYPE LoadXml(HSTRING xml) = 0;
    // SaveToFileAsync omitted — not needed
};

static const GUID IID_IXmlDocumentIO =
    { 0x6CD0E74E, 0xEE65, 0x4489, {0x9E,0xBF,0xCA,0x43,0xE8,0x7B,0xA6,0x37} };

// ═══════════════════════════════════════════════════════════════════════
// ABI::Windows::UI::Notifications
// ═══════════════════════════════════════════════════════════════════════

// IToastNotificationFactory {04124B20-82C6-4229-B109-FD9ED4662B53}
MIDL_INTERFACE("04124B20-82C6-4229-B109-FD9ED4662B53")
IToastNotificationFactory : public IInspectable {
public:
    virtual HRESULT STDMETHODCALLTYPE CreateToastNotification(
        IInspectable* content, // IXmlDocument*
        IInspectable** result  // IToastNotification**
    ) = 0;
};

static const GUID IID_IToastNotificationFactory =
    { 0x04124B20, 0x82C6, 0x4229, {0xB1,0x09,0xFD,0x9E,0xD4,0x66,0x2B,0x53} };

// Forward declare event handler tokens
struct EventRegistrationToken { __int64 value; };

// IToastNotification {997E2675-059E-4E60-8B06-1760917C8B80}
MIDL_INTERFACE("997E2675-059E-4E60-8B06-1760917C8B80")
IToastNotification : public IInspectable {
public:
    virtual HRESULT STDMETHODCALLTYPE get_Content(IInspectable** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_ExpirationTime(IInspectable* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ExpirationTime(IInspectable** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_Dismissed(IUnknown* handler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_Dismissed(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_Activated(IUnknown* handler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_Activated(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_Failed(IUnknown* handler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_Failed(EventRegistrationToken token) = 0;
};

static const GUID IID_IToastNotification =
    { 0x997E2675, 0x059E, 0x4E60, {0x8B,0x06,0x17,0x60,0x91,0x7C,0x8B,0x80} };

// IToastNotifier {75927B93-03F3-41EC-91D3-6E5BAC1B38E8}
MIDL_INTERFACE("75927B93-03F3-41EC-91D3-6E5BAC1B38E8")
IToastNotifier : public IInspectable {
public:
    virtual HRESULT STDMETHODCALLTYPE Show(IInspectable* notification) = 0;
    virtual HRESULT STDMETHODCALLTYPE Hide(IInspectable* notification) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Setting(int* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_ScheduledToastNotification(IInspectable* stn) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_ScheduledToastNotification(IInspectable* stn) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetScheduledToastNotifications(IInspectable** result) = 0;
};

// IToastNotificationManagerStatics {50AC103F-D235-4598-BBEF-98FE4D1A3AD4}
MIDL_INTERFACE("50AC103F-D235-4598-BBEF-98FE4D1A3AD4")
IToastNotificationManagerStatics : public IInspectable {
public:
    virtual HRESULT STDMETHODCALLTYPE CreateToastNotifier(IToastNotifier** result) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateToastNotifierWithId(HSTRING appId, IToastNotifier** result) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTemplateContent(int type, IInspectable** result) = 0;
};

static const GUID IID_IToastNotificationManagerStatics =
    { 0x50AC103F, 0xD235, 0x4598, {0xBB,0xEF,0x98,0xFE,0x4D,0x1A,0x3A,0xD4} };

// ═══════════════════════════════════════════════════════════════════════
// ITypedEventHandler for Dismissed/Activated/Failed events
// ═══════════════════════════════════════════════════════════════════════

// Toast dismiss reason enum
enum ToastDismissalReason {
    ToastDismissalReason_UserCanceled = 0,
    ToastDismissalReason_ApplicationHidden = 1,
    ToastDismissalReason_TimedOut = 2
};

// IToastDismissedEventArgs {3F89D935-D9CB-4538-A0F0-FFE7659938F8}
MIDL_INTERFACE("3F89D935-D9CB-4538-A0F0-FFE7659938F8")
IToastDismissedEventArgs : public IInspectable {
public:
    virtual HRESULT STDMETHODCALLTYPE get_Reason(ToastDismissalReason* value) = 0;
};

static const GUID IID_IToastDismissedEventArgs =
    { 0x3F89D935, 0xD9CB, 0x4538, {0xA0,0xF0,0xFF,0xE7,0x65,0x99,0x38,0xF8} };

// ITypedEventHandler<ToastNotification*, ToastDismissedEventArgs*>
// {61C2402F-0ED0-5A18-AB69-59F4AA99A368}
static const GUID IID_ToastDismissedHandler =
    { 0x61C2402F, 0x0ED0, 0x5A18, {0xAB,0x69,0x59,0xF4,0xAA,0x99,0xA3,0x68} };

// ITypedEventHandler<ToastNotification*, IInspectable*>  (for Activated)
// {AB54DE2D-97D9-5528-B6AD-105AFE156530}
static const GUID IID_ToastActivatedHandler =
    { 0xAB54DE2D, 0x97D9, 0x5528, {0xB6,0xAD,0x10,0x5A,0xFE,0x15,0x65,0x30} };

// ITypedEventHandler<ToastNotification*, ToastFailedEventArgs*>
// {95E3E803-C969-5E3A-9753-EA2AD22A9A33}
static const GUID IID_ToastFailedHandler =
    { 0x95E3E803, 0xC969, 0x5E3A, {0x97,0x53,0xEA,0x2A,0xD2,0x2A,0x9A,0x33} };

// ═══════════════════════════════════════════════════════════════════════
// RuntimeClass name strings for RoGetActivationFactory
// ═══════════════════════════════════════════════════════════════════════
static const wchar_t* RC_ToastManager   = L"Windows.UI.Notifications.ToastNotificationManager";
static const wchar_t* RC_ToastNotif     = L"Windows.UI.Notifications.ToastNotification";
static const wchar_t* RC_XmlDocument    = L"Windows.Data.Xml.Dom.XmlDocument";

// ═══════════════════════════════════════════════════════════════════════
// CLSID for our COM Notification Activator
// {C4A78A72-7A12-4C66-9A8E-2B880A5D6B10}
// ═══════════════════════════════════════════════════════════════════════
static const GUID CLSID_RisknoxToastActivator =
    { 0xC4A78A72, 0x7A12, 0x4C66, {0x9A,0x8E,0x2B,0x88,0x0A,0x5D,0x6B,0x10} };

static const wchar_t* AUMID = L"Risknox.Agent.Notifier";

// ═══════════════════════════════════════════════════════════════════════
// Property keys for Start Menu shortcut
// ═══════════════════════════════════════════════════════════════════════
// PKEY_AppUserModel_ID: {9F4C2855-9F79-4B39-A8D0-E1D42DE1D5F3}, PID 5
static const PROPERTYKEY PKEY_AppUserModel_ID_ = {
    {0x9F4C2855, 0x9F79, 0x4B39, {0xA8,0xD0,0xE1,0xD4,0x2D,0xE1,0xD5,0xF3}}, 5
};

// PKEY_AppUserModel_ToastActivatorCLSID: {9F4C2855-9F79-4B39-A8D0-E1D42DE1D5F3}, PID 26
static const PROPERTYKEY PKEY_AppUserModel_ToastActivatorCLSID_ = {
    {0x9F4C2855, 0x9F79, 0x4B39, {0xA8,0xD0,0xE1,0xD4,0x2D,0xE1,0xD5,0xF3}}, 26
};

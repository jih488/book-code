#include "verify_signature.h"


#include <windows.h>
#include <Softpub.h>
#include <wincrypt.h>
#include <wintrust.h>

#include <cstdlib>



bool    verify_signature(const wchar_t* file_name)
{
    WINTRUST_FILE_INFO file_info;
    ::memset(&file_info, 0, sizeof(file_info));
    file_info.cbStruct = sizeof(file_info);
    file_info.pcwszFilePath = file_name;
    file_info.hFile = NULL;
    file_info.pgKnownSubject = NULL;
    /*
    WVTPolicyGUID specifies the policy to apply on the file
    WINTRUST_ACTION_GENERIC_VERIFY_V2 policy checks:
    1) The certificate used to sign the file chains up to a root
    certificate located in the trusted root certificate store. This
    implies that the identity of the publisher has been verified by
    a certification authority.
    2) In cases where user interface is displayed (which this example
    does not do), WinVerifyTrust will check for whether the
    end entity certificate is stored in the trusted publisher store,
    implying that the user trusts content from this publisher.
    3) The end entity certificate has sufficient permission to sign
    code, as indicated by the presence of a code signing EKU or no
    EKU.
    */
    GUID policy_guid = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    WINTRUST_DATA trust_data;
    ::memset(&trust_data, 0, sizeof(trust_data));
    trust_data.cbStruct = sizeof(trust_data);
    trust_data.pPolicyCallbackData = NULL;              // Use default code signing EKU.
    trust_data.pSIPClientData = NULL;               // No data to pass to SIP.
    trust_data.dwUIChoice = WTD_UI_NONE;        // Disable WVT UI.
    trust_data.fdwRevocationChecks = WTD_REVOKE_NONE;   // No revocation checking.
    trust_data.dwUnionChoice = WTD_CHOICE_FILE; // Verify an embedded signature on a file.
    trust_data.dwStateAction = 0;               // Default verification.
    trust_data.hWVTStateData = NULL;                // Not applicable for default verification of embedded signature.
    trust_data.pwszURLReference = NULL;             // Not used.
    // This is not applicable if there is no UI because it changes
    // the UI to accommodate running applications instead of
    // installing applications.
    trust_data.dwUIContext = 0;
    trust_data.pFile = &file_info;      // Set pFile.
    // WinVerifyTrust verifies signatures as specified by the GUID
    // and Wintrust_Data.
    LONG status = ::WinVerifyTrust(NULL, &policy_guid, &trust_data);

    switch (status) {
    case ERROR_SUCCESS: {
        // Signed file:
        // - Hash that represents the subject is trusted.
        //
        // - Trusted publisher without any verification errors.
        //
        // - UI was disabled in dwUIChoice. No publisher or
        // time stamp chain errors.
        //
        // - UI was enabled in dwUIChoice and the user clicked
        // "Yes" when asked to install and run the signed
        // subject.
        // wprintf_s( L"The file \"%s\" is signed and the signature was verified.\n", file_name );
        return true;
    }
    break;

    case TRUST_E_NOSIGNATURE: {
        // The file was not signed or had a signature
        // that was not valid.
        DWORD last_error = ::GetLastError();

        if (last_error == TRUST_E_NOSIGNATURE ||
            last_error == TRUST_E_SUBJECT_FORM_UNKNOWN ||
            last_error == TRUST_E_PROVIDER_UNKNOWN) {
            //wprintf_s( L"The file \"%s\" is not signed.\n", file_name );
        } else {
            // The signature was not valid or there was an error
            // opening the file.
            //wprintf_s( L"An unknown error occurred trying to verify the signature of the \"%s\" file.\n", file_name );
        }
    }
    break;

    case TRUST_E_EXPLICIT_DISTRUST: {
        // The hash that represents the subject or the publisher
        // is not allowed by the admin or user.
        //wprintf_s( L"The signature is present, but specifically disallowed.\n" );
    }
    break;

    case TRUST_E_SUBJECT_NOT_TRUSTED: {
        // The user clicked "No" when asked to install and run.
        //wprintf_s( L"The signature is present, but not trusted.\n" );
    }
    break;

    case CRYPT_E_SECURITY_SETTINGS: {
        // The hash that represents the subject or the publisher
        // was not explicitly trusted by the admin and the
        // admin policy has disabled user trust. No signature,
        // publisher or time stamp errors.
        //wprintf_s(    L"CRYPT_E_SECURITY_SETTINGS - The hash "
        //          L"representing the subject or the publisher wasn't "
        //          L"explicitly trusted by the admin and admin policy "
        //          L"has disabled user trust. No signature, publisher "
        //          L"or timestamp errors.\n" );
    }
    break;

    default: {
        // The UI was disabled in dwUIChoice or the admin policy
        // has disabled user trust. lStatus contains the
        // publisher or time stamp chain error.
        //wprintf_s( L"Error is: 0x%x.\n", status );
    }
    break;
    }

    return false;
}


namespace
{
bool    get_object_file_handle(const wchar_t* file_name, HCERTSTORE& store, HCRYPTMSG& msg)
{
    store = NULL;
    msg = NULL;
    DWORD dwEncoding = 0;
    DWORD dwContentType = 0;
    DWORD dwFormatType = 0;

    // Get message handle and store handle from the signed file.
    if (::CryptQueryObject(CERT_QUERY_OBJECT_FILE,
                           file_name,
                           CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
                           CERT_QUERY_FORMAT_FLAG_BINARY,
                           0,
                           &dwEncoding,
                           &dwContentType,
                           &dwFormatType,
                           &store,
                           &msg,
                           NULL)
        == FALSE) {
        return false;
    }

    return true;
}

PCMSG_SIGNER_INFO   get_signer_info(HCRYPTMSG msg)
{
    // Get signer information size.
    DWORD signer_info_size = 0;

    if (::CryptMsgGetParam(msg,
                           CMSG_SIGNER_INFO_PARAM,
                           0,
                           NULL,
                           &signer_info_size)
        == FALSE) {
        return NULL;
    }

    // Allocate memory for signer information.
    PCMSG_SIGNER_INFO signer_info = (PCMSG_SIGNER_INFO) ::LocalAlloc(LPTR, signer_info_size);

    if (signer_info == NULL) {
        return NULL;
    }

    // Get Signer Information.
    if (::CryptMsgGetParam(msg,
                           CMSG_SIGNER_INFO_PARAM,
                           0,
                           signer_info,
                           &signer_info_size)
        == FALSE) {
        ::LocalFree(signer_info);
        return NULL;
    }

    return signer_info;
}

PCMSG_SIGNER_INFO   get_timestamp_signer_info(PCMSG_SIGNER_INFO signer_info)
{
    PCMSG_SIGNER_INFO counter_signer_info = NULL;

    // Loop through unathenticated attributes for
    // szOID_RSA_counterSign OID.
    for (DWORD n = 0; n < signer_info->UnauthAttrs.cAttr; ++n) {
        if (::lstrcmpA(signer_info->UnauthAttrs.rgAttr[n].pszObjId, szOID_RSA_counterSign) == 0) {
            // Get size of CMSG_SIGNER_INFO structure.
            DWORD dwSize = 0;

            if (::CryptDecodeObject(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                    PKCS7_SIGNER_INFO,
                                    signer_info->UnauthAttrs.rgAttr[n].rgValue[0].pbData,
                                    signer_info->UnauthAttrs.rgAttr[n].rgValue[0].cbData,
                                    0,
                                    NULL,
                                    &dwSize) == FALSE) {
                return NULL;
            }

            // Allocate memory for CMSG_SIGNER_INFO.
            counter_signer_info = (PCMSG_SIGNER_INFO) ::LocalAlloc(LPTR, dwSize);

            if (counter_signer_info == NULL) {
                return NULL;
            }

            // Decode and get CMSG_SIGNER_INFO structure
            // for timestamp certificate.
            if (::CryptDecodeObject(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                    PKCS7_SIGNER_INFO,
                                    signer_info->UnauthAttrs.rgAttr[n].rgValue[0].pbData,
                                    signer_info->UnauthAttrs.rgAttr[n].rgValue[0].cbData,
                                    0,
                                    (PVOID)counter_signer_info,
                                    &dwSize) == FALSE) {
                ::LocalFree(counter_signer_info);
                return NULL;
            }

            break;
        }
    }

    return counter_signer_info;
}

std::wstring    get_signer_name(HCERTSTORE store, PCMSG_SIGNER_INFO signer_info)
{
    // Search for the signer certificate in the temporary
    // certificate store.
    CERT_INFO cert_info;
    cert_info.Issuer = signer_info->Issuer;
    cert_info.SerialNumber = signer_info->SerialNumber;
    PCCERT_CONTEXT cert_context
        = ::CertFindCertificateInStore(store,
                                       X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                       0,
                                       CERT_FIND_SUBJECT_CERT,
                                       &cert_info,
                                       NULL);

    if (cert_context == NULL) {
        return L"";
    }

    wchar_t* name_temp = NULL;
    DWORD data_count = 0;
    // Get Subject name size.
    data_count = ::CertGetNameString(cert_context,
                                     CERT_NAME_SIMPLE_DISPLAY_TYPE,
                                     0,
                                     NULL,
                                     NULL,
                                     0);

    if (data_count <= 0) {
        ::CertFreeCertificateContext(cert_context);
        return L"";
    }

    name_temp = new wchar_t[data_count];
    ::memset(name_temp, 0, data_count * sizeof(wchar_t));
    data_count = ::CertGetNameString(cert_context,
                                     CERT_NAME_SIMPLE_DISPLAY_TYPE,
                                     0,
                                     NULL,
                                     name_temp,
                                     data_count);
    std::wstring signer_name;

    if (data_count > 0) {
        signer_name = name_temp;
        /*
        // getting serial number
        data_count = ::CertGetNameString(cert_context,
            CERT_NAME_SIMPLE_DISPLAY_TYPE,
            0,
            NULL,
            name_temp,
            data_count);
        if (data_count > 0)
        {
            // copy Serial Number.
            DWORD serial_number_size = cert_context->pCertInfo->SerialNumber.cbData;    // 구한 결과
            BYTE* serial_number = new BYTE[serial_number_size]; // 구한 결과
            // 파일 속성에서 보는 것이랑 다르다(뒤집어 져있다).
            for (DWORD n = 0; n < serial_number_size; ++n)
            {
                serial_number[n] = cert_context->pCertInfo->SerialNumber.pbData[serial_number_size - (n + 1)];
            }
        }
        */
    }

    delete[] name_temp;
    ::CertFreeCertificateContext(cert_context);
    /*
    // timestamp 구하기
    PCMSG_SIGNER_INFO counter_signer_info = get_timestamp_signer_info(signer_info);
    if (counter_signer_info != NULL)
    {
        // Search for Timestamp certificate in the temporary
        // certificate store.
        CERT_INFO counter_cert_info;
        counter_cert_info.Issuer = counter_signer_info->Issuer;
        counter_cert_info.SerialNumber = counter_signer_info->SerialNumber;
        PCCERT_CONTEXT counter_cert_context = CertFindCertificateInStore(store,
            X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
            0,
            CERT_FIND_SUBJECT_CERT,
            (PVOID)&counter_cert_info,
            NULL);
        if (counter_cert_context != NULL)
        {
            // Loop through authenticated attributes and find
            // szOID_RSA_signingTime OID.
            for (DWORD n = 0; n < counter_signer_info->AuthAttrs.cAttr; ++n)
            {
                if (::lstrcmpA(szOID_RSA_signingTime, counter_signer_info->AuthAttrs.rgAttr[n].pszObjId) == 0)
                {
                    // Decode and get FILETIME structure.
                    FILETIME ft;
                    ::memset(&ft, 0, sizeof(ft));
                    DWORD dwData = sizeof(ft);
                    if (::CryptDecodeObject(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                        szOID_RSA_signingTime,
                        counter_signer_info->AuthAttrs.rgAttr[n].rgValue[0].pbData,
                        counter_signer_info->AuthAttrs.rgAttr[n].rgValue[0].cbData,
                        0,
                        (PVOID)&ft,
                        &dwData) == FALSE)
                    {
                        break;
                    }
                    SYSTEMTIME timestamp;   // 구한 결과
                    // Convert to local time.
                    FILETIME lft;
                    ::memset(&lft, 0, sizeof(lft));
                    ::FileTimeToLocalFileTime(&ft, &lft);
                    ::FileTimeToSystemTime(&lft, &timestamp);
                    break;
                }
            }
            ::CertFreeCertificateContext(counter_cert_context);
        }
        ::LocalFree(counter_signer_info);
    }
    */
    return signer_name;
}
};

std::wstring    get_signer(const wchar_t* file_name)
{
    HCERTSTORE store = NULL;
    HCRYPTMSG msg = NULL;

    if (get_object_file_handle(file_name, store, msg) == false) {
        return L"";
    }

    PCMSG_SIGNER_INFO signer_info = get_signer_info(msg);

    if (signer_info == NULL) {
        ::CertCloseStore(store, 0);
        ::CryptMsgClose(msg);
        return L"";
    }

    std::wstring signer_name = get_signer_name(store, signer_info);
    ::LocalFree(signer_info);
    ::CertCloseStore(store, 0);
    ::CryptMsgClose(msg);
    return signer_name;
}
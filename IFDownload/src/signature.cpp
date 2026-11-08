#include "signature.h"
#include <cstdio>
#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#include <wintrust.h>
#include <softpub.h>

bool verifyPe(const std::wstring& filePath) {
    WINTRUST_FILE_INFO fi = {};
    fi.cbStruct = sizeof(fi);
    fi.pcwszFilePath = filePath.c_str();

    GUID actionId = WINTRUST_ACTION_GENERIC_VERIFY_V2;

    WINTRUST_DATA wd = {};
    wd.cbStruct = sizeof(wd);
    wd.dwUIChoice = WTD_UI_NONE;
    wd.fdwRevocationChecks = WTD_REVOKE_NONE;
    wd.dwUnionChoice = WTD_CHOICE_FILE;
    wd.pFile = &fi;
    wd.dwStateAction = WTD_STATEACTION_VERIFY;
    wd.dwProvFlags = WTD_SAFER_FLAG;

    LONG result = WinVerifyTrust((HWND)INVALID_HANDLE_VALUE, &actionId, &wd);

    wd.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust((HWND)INVALID_HANDLE_VALUE, &actionId, &wd);

    return result == ERROR_SUCCESS;
}
#endif

bool verifyPe(const std::string& filePath) {
    FILE* fp = fopen(filePath.c_str(), "rb");
    if (!fp) return false;

    uint16_t mz = 0;
    if (fread(&mz, 2, 1, fp) != 1 || mz != 0x5A4D) {
        fclose(fp);
        return false;
    }

    if (fseek(fp, 0x3C, SEEK_SET) != 0) {
        fclose(fp);
        return false;
    }

    uint32_t peOffset = 0;
    if (fread(&peOffset, 4, 1, fp) != 1) {
        fclose(fp);
        return false;
    }

    if (fseek(fp, peOffset, SEEK_SET) != 0) {
        fclose(fp);
        return false;
    }

    uint32_t peSig = 0;
    if (fread(&peSig, 4, 1, fp) != 1 || peSig != 0x00004550) {
        fclose(fp);
        return false;
    }

    fclose(fp);
    return true;
}

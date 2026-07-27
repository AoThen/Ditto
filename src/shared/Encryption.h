// Encryption.h - Windows CryptoAPI AES-256 Implementation
// Replaces the unaudited EncryptDecrypt module with a trusted implementation
// 
// Security: Uses AES-256-CBC with SHA-256 key derivation
// No external dependencies - Windows native CryptoAPI only

#pragma once

#include <windows.h>
#include <wincrypt.h>
#include <string>

#pragma comment(lib, "advapi32.lib")

class CEncryption
{
public:
    CEncryption() : m_hProv(NULL), m_hKey(NULL), m_pLastBuffer(NULL) {}
    
    ~CEncryption()
    {
        Cleanup();
    }

    // Copy prevention
    CEncryption(const CEncryption&) = delete;
    CEncryption& operator=(const CEncryption&) = delete;

    // Encrypt data with password
    // Returns encrypted data in pOutput, caller must call FreeBuffer() when done
    BOOL Encrypt(UCHAR* pData, int nLenIn, CStringA csPassword,
                 UCHAR*& pOutput, int& nLenOut)
    {
        if (pData == NULL || nLenIn <= 0)
            return FALSE;

        if (!Initialize(csPassword))
            return FALSE;

        // Allocate buffer for encrypted data + IV prefix + padding
        // AES block size is 16 bytes, we prepend a 16-byte random IV
        DWORD dwBlockLen = 16;
        DWORD dwBufferLen = nLenIn + dwBlockLen * 2; // IV + max padding
        pOutput = (UCHAR*)LocalAlloc(LPTR, dwBufferLen);
        if (pOutput == NULL)
            return FALSE;

        // Generate random IV and prepend to output
        HCRYPTHASH hHash = NULL;
        if (!CryptCreateHash(m_hProv, CALG_SHA_256, 0, 0, &hHash))
        {
            LocalFree(pOutput);
            pOutput = NULL;
            return FALSE;
        }

        // Use random data for IV
        BYTE bIV[16];
        if (!CryptGenRandom(m_hProv, 16, bIV))
        {
            CryptDestroyHash(hHash);
            LocalFree(pOutput);
            pOutput = NULL;
            return FALSE;
        }

        // Set IV
        if (!CryptSetKeyParam(m_hKey, KP_IV, bIV, 0))
        {
            CryptDestroyHash(hHash);
            LocalFree(pOutput);
            pOutput = NULL;
            return FALSE;
        }

        CryptDestroyHash(hHash);

        // Copy IV to output buffer first
        memcpy(pOutput, bIV, 16);

        // Copy data after IV
        memcpy(pOutput + 16, pData, nLenIn);

        // Encrypt in place (after IV)
        DWORD dwDataLen = nLenIn;
        DWORD dwBufferLenRemaining = dwBufferLen - 16;
        if (!CryptEncrypt(m_hKey, NULL, TRUE, 0, pOutput + 16, &dwDataLen, dwBufferLenRemaining))
        {
            LocalFree(pOutput);
            pOutput = NULL;
            return FALSE;
        }

        nLenOut = (int)(dwDataLen + 16); // Include IV in output length
        m_pLastBuffer = pOutput;
        return TRUE;
    }

    // Decrypt data with password
    // Returns decrypted data in pOutput, caller must call FreeBuffer() when done
    BOOL Decrypt(UCHAR* pData, int nLenIn, CStringA csPassword,
                 UCHAR*& pOutput, int& nLenOut)
    {
        if (pData == NULL || nLenIn <= 16) // Minimum: IV (16 bytes)
            return FALSE;

        if (!Initialize(csPassword))
            return FALSE;

        // Extract IV from first 16 bytes
        BYTE bIV[16];
        memcpy(bIV, pData, 16);

        // Set IV for decryption
        if (!CryptSetKeyParam(m_hKey, KP_IV, bIV, 0))
            return FALSE;

        // Allocate buffer for decrypted data
        DWORD dwBufferLen = nLenIn; // Decrypted data will be smaller or equal
        pOutput = (UCHAR*)LocalAlloc(LPTR, dwBufferLen);
        if (pOutput == NULL)
            return FALSE;

        // Copy encrypted data (excluding IV) to output buffer
        DWORD dwDataLen = nLenIn - 16;
        memcpy(pOutput, pData + 16, dwDataLen);

        // Decrypt in place
        if (!CryptDecrypt(m_hKey, NULL, TRUE, 0, pOutput, &dwDataLen))
        {
            LocalFree(pOutput);
            pOutput = NULL;
            return FALSE;
        }

        nLenOut = (int)dwDataLen;
        m_pLastBuffer = pOutput;
        return TRUE;
    }

    // Free buffer allocated by Encrypt/Decrypt
    void FreeBuffer(UCHAR* pBuffer)
    {
        if (pBuffer != NULL)
        {
            // Securely zero the buffer before freeing
            SecureZeroMemory(pBuffer, LocalSize(pBuffer));
            LocalFree(pBuffer);
        }
        if (m_pLastBuffer == pBuffer)
            m_pLastBuffer = NULL;
    }

private:
    HCRYPTPROV m_hProv;
    HCRYPTKEY m_hKey;
    UCHAR* m_pLastBuffer;

    // Initialize crypto provider and derive key from password
    BOOL Initialize(CStringA csPassword)
    {
        Cleanup();

        // Acquire crypto provider (AES-capable)
        // Try enhanced provider first, fall back to default
        if (!CryptAcquireContext(&m_hProv, NULL, MS_ENH_RSA_AES_PROV, 
                                  PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        {
            if (!CryptAcquireContext(&m_hProv, NULL, NULL, 
                                      PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
            {
                return FALSE;
            }
        }

        // Create hash for key derivation
        HCRYPTHASH hHash = NULL;
        if (!CryptCreateHash(m_hProv, CALG_SHA_256, 0, 0, &hHash))
        {
            Cleanup();
            return FALSE;
        }

        // Hash the password
        if (!CryptHashData(hHash, (BYTE*)csPassword.GetString(), 
                           csPassword.GetLength(), 0))
        {
            CryptDestroyHash(hHash);
            Cleanup();
            return FALSE;
        }

        // Derive AES-256 key from hash
        if (!CryptDeriveKey(m_hProv, CALG_AES_256, hHash, CRYPT_EXPORTABLE, &m_hKey))
        {
            CryptDestroyHash(hHash);
            Cleanup();
            return FALSE;
        }

        CryptDestroyHash(hHash);
        return TRUE;
    }

    void Cleanup()
    {
        if (m_hKey != NULL)
        {
            CryptDestroyKey(m_hKey);
            m_hKey = NULL;
        }
        if (m_hProv != NULL)
        {
            CryptReleaseContext(m_hProv, 0);
            m_hProv = NULL;
        }
        // Don't free m_pLastBuffer here - caller owns it
    }
};

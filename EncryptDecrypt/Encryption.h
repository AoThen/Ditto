// Encryption.h: interface for the CEncryption class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_ENCRYPTION_H__06C80AE2_89BD_4040_A303_D3A71F44BFBD__INCLUDED_)
#define AFX_ENCRYPTION_H__06C80AE2_89BD_4040_A303_D3A71F44BFBD__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#pragma message("DEPRECATED: EncryptDecrypt 使用旧版 AES-CBC/ECB 加密。新功能请使用 CloudCrypto (AES-GCM) 替代。")
#endif // _MSC_VER > 1000

#include "iencryption.h"

#include "rijndael.h"
#include "NewRandom.h"

// The signature constants were chosen randomly
#define TD_TLSIG_1				0x139C5AFE
#define TD_TLSIG_2				0xBF3562DA

#define TD_STD_KEYENCROUNDS		100000

#pragma pack(1)

typedef struct _TD_TLHEADER // The database header
{
	BYTE	aHeaderHash[32];	// SHA-256 hash of the rest of the header

	DWORD dwSignature1; // = TD_TLSIG_1
	DWORD dwSignature2; // = TD_TLSIG_2

	BYTE aMasterSeed[16]; // Seed that gets hashed with the userkey to form the final key
	RD_UINT8 aEncryptionIV[16]; // IV used for content encryption

	BYTE aContentsHash[32]; // SHA-256 hash of the database, used for integrity check

	BYTE aMasterSeed2[32]; // Used for the dwKeyEncRounds AES transformations
	DWORD dwKeyEncRounds;
} TD_TLHEADER, *PTD_TLHEADER;

#pragma pack()


// DEPRECATED: 此模块使用旧版 AES-CBC/ECB 加密 (Rijndael)。
// 所有新功能应使用 src/CloudSync/CloudCrypto.h 中的 AES-GCM 实现。
// 旧加密数据在读取时需通过 CloudCrypto 重新加密后写入。
class CEncryption : public IEncryption
{
public:
	CEncryption();
	virtual ~CEncryption();

	void Release();
	bool Encrypt(const unsigned char* szInput, int nLenInput, const char* szPassword,
						 unsigned char*& pOutput, int& nLenOutput);
	bool Decrypt(const unsigned char* pInput, int nLenInput, const char* szPassword,
						 unsigned char*& pOutput, int& nLenOutput);
	void FreeBuffer(unsigned char*& pBuffer);

private:
	// Encrypt the master key a few times to make brute-force key-search harder
	BOOL _TransformMasterKey(BYTE *pKeySeed);

	BYTE	m_pMasterKey[32]; // Master key used to encrypt the whole database
	BYTE	m_pTransformedMasterKey[32]; // Master key encrypted several times
	DWORD	m_dwKeyEncRounds;

	CNewRandom	m_random; // Pseudo-random number generator

};

#endif // !defined(AFX_ENCRYPTION_H__06C80AE2_89BD_4040_A303_D3A71F44BFBD__INCLUDED_)

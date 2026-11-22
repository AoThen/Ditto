// DatabaseUtilites.h: interface for the CDatabaseUtilites class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_DATABASEUTILITES_H__039F53EB_228F_4640_8009_3D2B1FF435D4__INCLUDED_)
#define AFX_DATABASEUTILITES_H__039F53EB_228F_4640_8009_3D2B1FF435D4__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "DittoPopupWindow.h"
#include "sqlite/CppSQLite3.h"

#define DEFAULT_DB_NAME "Ditto.db"
#define ERROR_OPENING_DATABASE	2

CString GetDBName();
CString GetDefaultDBName();
BOOL OpenDatabase(CString csDB);
BOOL IsDatabaseOpen();

BOOL CheckDBExists(CString csDBPath);
BOOL ValidDB(CString csPath, BOOL bUpgrade=TRUE);
BOOL CreateDB(CString csPath);

BOOL CompactDatabase();
BOOL RepairDatabase();
BOOL RemoveOldEntries(bool checkIdleTime);
BOOL DeleteNonUsedClips(bool fromAppWindow);

BOOL BackupDB(CString dbPath, CString backupPath);
BOOL RestoreDB(CString backupPath);

void ReOrderStickyClips(int parentID, CppSQLite3DB &db);
BOOL MigrateDatabaseSchema();

// Background one-shot rebuild of the pinyin/pinyinAbbr columns for clips
// that predate those columns (empty values). Safe to call repeatedly.
void StartPinyinBackfillIfNeeded();

// Result of a manual rebuild request from the options page
enum PinyinRebuildStart
{
	PBRS_STARTED = 0,     // background rebuild launched
	PBRS_ALREADY_RUNNING, // a rebuild is already in progress
	PBRS_NOTHING_TO_DO,   // no clips to rebuild
	PBRS_FAILED           // could not query/start
};
PinyinRebuildStart StartPinyinBackfillForced(HWND hNotifyWnd);
BOOL StopPinyinBackfillAndWait(DWORD dwTimeoutMs);

#endif // !defined(AFX_DATABASEUTILITES_H__039F53EB_228F_4640_8009_3D2B1FF435D4__INCLUDED_)

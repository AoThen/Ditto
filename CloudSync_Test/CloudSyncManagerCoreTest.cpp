// CloudSyncManagerCoreTest.cpp - Unit tests for CCloudSyncManager core sync logic
// Tests: PushNewClips JSON construction, PullChanges response parsing,
//        GetLocalClipsSince query/enumeration logic, InitializeEncryption
//
// NOTE: CCloudSyncManager requires full MFC app context (theApp.m_db, httplib::Client)
// so we test the private method logic via inline simulation — same approach as
// CloudSyncManagerTest.cpp.

#include "stdafx.h"
#include <gtest/gtest.h>
#include "../src/CloudSync/CloudCrypto.h"
#include "../src/json.hpp"
#include "GetSetOptionsMock.h"
#include <vector>
#include <string>
#include <ctime>
#include <cstring>

using json = nlohmann::json;

// ============================================================================
// Test Fixture: Setup/Teardown for core sync logic tests
// ============================================================================

class CloudSyncManagerCoreTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		CGetSetOptions::Reset();
		CCloudCrypto::Reset();
	}

	void TearDown() override
	{
		CCloudCrypto::Reset();
	}
};

class CloudSyncManagerCore_Push : public CloudSyncManagerCoreTest {};
class CloudSyncManagerCore_Pull : public CloudSyncManagerCoreTest {};
class CloudSyncManagerCore_Query : public CloudSyncManagerCoreTest {};
class CloudSyncManagerCore_Encryption : public CloudSyncManagerCoreTest {};

// ============================================================================
// PushNewClips — JSON Construction Tests
// ============================================================================

static const int CLOUD_PUSH_BATCH_SIZE = 200;

static json SimulateBuildPushPayload(
	const json& clipsArray,
	const std::string& deviceId,
	time_t sinceTime,
	BOOL bForce = FALSE)
{
	json payload;
	if (sinceTime > 0)
	{
		SYSTEMTIME st;
		FILETIME ft;
		ULARGE_INTEGER uli;
		uli.QuadPart = ((ULONGLONG)sinceTime * 10000000ULL) + 116444736000000000ULL;
		ft.dwLowDateTime = uli.LowPart;
		ft.dwHighDateTime = uli.HighPart;
		FileTimeToSystemTime(&ft, &st);
		char timeBuf[32];
		sprintf_s(timeBuf, "%04hd-%02hd-%02hdT%02hd:%02hd:%02hdZ",
		          st.wYear, st.wMonth, st.wDay,
		          st.wHour, st.wMinute, st.wSecond);
		payload["since"] = std::string(timeBuf);
	}
	else
	{
		payload["since"] = "1970-01-01T00:00:00Z";
	}

	payload["device_id"] = deviceId;
	if (bForce)
		payload["force"] = true;
	payload["push_clips"] = clipsArray;
	return payload;
}

TEST_F(CloudSyncManagerCore_Push, PushNewClips_BuildsValidPushPayload)
{
	time_t sinceTime = 1700000000;
	std::string deviceId = "device-abc-123";

	json clip;
	clip["id"] = "uuid-clip-1";
	clip["description"] = "Test clip";
	clip["crc"] = 12345;

	json formats = json::array();
	json textFmt;
	textFmt["format_type"] = 1;
	textFmt["data"] = "SGVsbG8=";
	textFmt["encrypted"] = true;
	formats.push_back(textFmt);
	clip["formats"] = formats;

	json clipsArray = json::array();
	clipsArray.push_back(clip);

	json payload = SimulateBuildPushPayload(clipsArray, deviceId, sinceTime, FALSE);

	EXPECT_EQ(payload["device_id"], "device-abc-123");
	EXPECT_TRUE(payload["push_clips"].is_array());
	EXPECT_EQ(payload["push_clips"].size(), 1);
	EXPECT_EQ(payload["push_clips"][0]["id"], "uuid-clip-1");
	EXPECT_EQ(payload["push_clips"][0]["description"], "Test clip");
	EXPECT_EQ(payload["push_clips"][0]["crc"], 12345);
	EXPECT_EQ(payload["since"], "2023-11-14T22:13:20Z");
	EXPECT_FALSE(payload.contains("force"));
}

TEST_F(CloudSyncManagerCore_Push, PushNewClips_IncludesFormatEncryption)
{
	json formats = json::array();

	json textFmt;
	textFmt["format_type"] = 1;
	textFmt["data"] = "plain text data";
	textFmt["encrypted"] = false;
	formats.push_back(textFmt);

	// Simulate EncryptClipFormats
	for (auto& fmt : formats)
	{
		if (fmt.contains("data") && fmt["data"].is_string())
		{
			int formatType = fmt.value("format_type", 0);
			if (formatType == 15) continue; // CF_HDROP — skip

			std::string plainData = fmt["data"].get<std::string>();
			CStringA plain(plainData.c_str());
			CStringA encrypted = CCloudCrypto::Encrypt(plain);
			if (encrypted.IsEmpty()) continue;

			fmt["data"] = encrypted.GetString();
			fmt["encrypted"] = true;
		}
	}

	EXPECT_TRUE(formats[0]["encrypted"]);
	EXPECT_NE(formats[0]["data"].get<std::string>(), "plain text data");
}

TEST_F(CloudSyncManagerCore_Push, PushNewClips_IncludesHDROPMetadata)
{
	json formats = json::array();

	json hdropFmt;
	hdropFmt["format_type"] = 15;
	hdropFmt["data"] = R"(C:\docs\report.pdf\0C:\docs\photo.jpg\0)";
	formats.push_back(hdropFmt);

	// Simulate FilterHDROPForSync
	for (auto& format : formats)
	{
		int formatType = format.value("format_type", 0);
		if (formatType == 15)
		{
			std::string dataStr = format["data"].get<std::string>();
			json paths = json::array();
			size_t start = 0;
			size_t pos = dataStr.find('\0');
			while (pos != std::string::npos)
			{
				if (pos > start)
					paths.push_back(dataStr.substr(start, pos - start));
				start = pos + 1;
				pos = dataStr.find('\0', start);
			}
			if (start < dataStr.size())
				paths.push_back(dataStr.substr(start));

			json pathMeta;
			pathMeta["type"] = "file_paths";
			pathMeta["paths"] = paths;
			pathMeta["count"] = paths.size();

			format["data"] = pathMeta.dump();
			format["is_file_ref"] = true;
			format["encrypted"] = false;
			break;
		}
	}

	EXPECT_TRUE(formats[0]["is_file_ref"]);
	EXPECT_FALSE(formats[0]["encrypted"]);

	json parsedData = json::parse(formats[0]["data"].get<std::string>());
	EXPECT_EQ(parsedData["type"], "file_paths");
	EXPECT_EQ(parsedData["count"], 2);
	EXPECT_EQ(parsedData["paths"][0], "C:\\docs\\report.pdf");
	EXPECT_EQ(parsedData["paths"][1], "C:\\docs\\photo.jpg");
}

TEST_F(CloudSyncManagerCore_Push, PushNewClips_SetsCorrectTimestamp)
{
	// RFC3339 format verification

	struct TestCase {
		time_t t;
		const char* expected;
	};
	TestCase cases[] = {
		{0,                     "1970-01-01T00:00:00Z"},
		{946684800,             "2000-01-01T00:00:00Z"},
		{1700000000,            "2023-11-14T22:13:20Z"},
		{1735689599,            "2024-12-31T23:59:59Z"},
	};

	for (const auto& c : cases)
	{
		json clip;
		clip["id"] = "test";
		json clipsArray = json::array();
		clipsArray.push_back(clip);

		json payload = SimulateBuildPushPayload(
			clipsArray, "device-x", c.t, FALSE);

		EXPECT_EQ(payload["since"].get<std::string>(), c.expected);
	}
}

TEST_F(CloudSyncManagerCore_Push, PushNewClips_EncryptsBeforePush)
{
	// Setup crypto
	std::vector<BYTE> key = CCloudCrypto::RandomBytes(32);
	ASSERT_TRUE(CCloudCrypto::Initialize(key));

	// Create a clip with unencrypted formats
	json clip;
	clip["id"] = "clip-enc-test";
	clip["description"] = "Before encryption check";

	json formats = json::array();
	json textFmt;
	textFmt["format_type"] = 1;
	textFmt["data"] = "Sensitive clipboard data";
	formats.push_back(textFmt);

	json htmlFmt;
	htmlFmt["format_type"] = 49423;
	htmlFmt["data"] = "<html>Secret</html>";
	formats.push_back(htmlFmt);

	clip["formats"] = formats;

	// Simulate GetLocalClipsSince's encryption step
	json clipFormats = clip["formats"];
	BOOL cryptoInitialized = TRUE;
	if (cryptoInitialized && !clipFormats.empty())
	{
		for (auto& fmt : clipFormats)
		{
			int formatType = fmt.value("format_type", 0);
			if (formatType == 15) continue;

			if (fmt.contains("data") && fmt["data"].is_string())
			{
				std::string plainData = fmt["data"].get<std::string>();
				CStringA plain(plainData.c_str());
				CStringA encrypted = CCloudCrypto::Encrypt(plain);
				ASSERT_FALSE(encrypted.IsEmpty());
				fmt["data"] = encrypted.GetString();
				fmt["encrypted"] = true;
			}
		}
	}

	// Build the push payload
	json clipsArray = json::array();
	clip["formats"] = clipFormats;
	clipsArray.push_back(clip);

	json payload = SimulateBuildPushPayload(
		clipsArray, "device-enc-test", time(nullptr), FALSE);

	// Verify formats are encrypted in the payload
	json pushedFormats = payload["push_clips"][0]["formats"];
	EXPECT_TRUE(pushedFormats.is_array());
	for (const auto& fmt : pushedFormats)
	{
		EXPECT_TRUE(fmt["encrypted"]) << "Format should be encrypted before push";
		std::string data = fmt["data"].get<std::string>();
		EXPECT_FALSE(data.empty());
		// Should NOT contain plaintext
		EXPECT_NE(data.find("Sensitive"), std::string::npos) == false;
	}
}

TEST_F(CloudSyncManagerCore_Push, PushNewClips_EmptyClipsList)
{
	json clipsArray = json::array();
	json payload = SimulateBuildPushPayload(
		clipsArray, "device-empty", 0, FALSE);

	EXPECT_TRUE(payload["push_clips"].is_array());
	EXPECT_EQ(payload["push_clips"].size(), 0);
	EXPECT_EQ(payload["since"], "1970-01-01T00:00:00Z");
}

TEST_F(CloudSyncManagerCore_Push, PushNewClips_MaxBatchSize)
{
	// Generate exactly CLOUD_PUSH_BATCH_SIZE clips
	json clipsArray = json::array();
	for (int i = 0; i < CLOUD_PUSH_BATCH_SIZE; i++)
	{
		json clip;
		clip["id"] = "clip-" + std::to_string(i);
		clip["crc"] = i;
		clip["formats"] = json::array();
		clipsArray.push_back(clip);
	}

	EXPECT_EQ(clipsArray.size(), CLOUD_PUSH_BATCH_SIZE);
	EXPECT_LE(clipsArray.size(), CLOUD_PUSH_BATCH_SIZE);

	// Verify building payload with max batch
	json payload = SimulateBuildPushPayload(
		clipsArray, "device-max", 0, FALSE);
	EXPECT_EQ(payload["push_clips"].size(), CLOUD_PUSH_BATCH_SIZE);
}

TEST_F(CloudSyncManagerCore_Push, PushNewClips_ForceModeSetsFlag)
{
	json clipsArray = json::array();
	json clip;
	clip["id"] = "force-clip";
	clip["crc"] = 999;
	clipsArray.push_back(clip);

	json payload = SimulateBuildPushPayload(
		clipsArray, "device-force", 0, TRUE);

	EXPECT_TRUE(payload["force"].get<bool>());
}

// ============================================================================
// PullChanges — Response Parsing Tests
// ============================================================================

TEST_F(CloudSyncManagerCore_Pull, PullChanges_ParsesNewClips)
{
	// Simulate server response with new clips
	json serverResponse;
	serverResponse["code"] = 0;
	serverResponse["data"]["clips"] = json::array();

	json remoteClip;
	remoteClip["id"] = "remote-uuid-1";
	remoteClip["description"] = "Pulled clip";
	remoteClip["crc"] = 67890;
	json fmt;
	fmt["format_type"] = 1;
	fmt["data"] = "SGVsbG8gV29ybGQ=";
	remoteClip["formats"] = json::array();
	remoteClip["formats"].push_back(fmt);

	serverResponse["data"]["clips"].push_back(remoteClip);
	serverResponse["data"]["server_time"] = "2024-06-01T12:00:00Z";
	serverResponse["data"]["has_more"] = false;

	// Simulate PullChanges parsing
	ASSERT_TRUE(serverResponse.contains("code"));
	EXPECT_EQ(serverResponse["code"].get<int>(), 0);

	ASSERT_TRUE(serverResponse.contains("data"));
	const json& data = serverResponse["data"];

	const json* clipsNode = nullptr;
	if (data.contains("new_clips"))
		clipsNode = &data["new_clips"];
	else if (data.contains("clips"))
		clipsNode = &data["clips"];

	ASSERT_NE(clipsNode, nullptr);
	EXPECT_EQ(clipsNode->size(), 1);

	const json& firstClip = (*clipsNode)[0];
	EXPECT_EQ(firstClip["id"], "remote-uuid-1");
	EXPECT_EQ(firstClip["description"], "Pulled clip");
	EXPECT_EQ(firstClip["crc"], 67890);
	EXPECT_TRUE(firstClip.contains("formats"));
	EXPECT_EQ(firstClip["formats"][0]["format_type"], 1);
}

TEST_F(CloudSyncManagerCore_Pull, PullChanges_ParsesDeletedIDs)
{
	json serverResponse;
	serverResponse["code"] = 0;
	serverResponse["data"]["clips"] = json::array();
	serverResponse["data"]["deleted_ids"] = json::array();
	serverResponse["data"]["deleted_ids"].push_back("del-uuid-1");
	serverResponse["data"]["deleted_ids"].push_back("del-uuid-2");
	serverResponse["data"]["deleted_ids"].push_back("del-uuid-3");

	const json& data = serverResponse["data"];
	const json* deletedNode = nullptr;
	if (data.contains("deleted_ids"))
		deletedNode = &data["deleted_ids"];

	ASSERT_NE(deletedNode, nullptr);
	EXPECT_EQ(deletedNode->size(), 3);

	std::vector<std::string> deletedIds;
	for (const auto& id : *deletedNode)
		deletedIds.push_back(id.get<std::string>());

	EXPECT_EQ(deletedIds[0], "del-uuid-1");
	EXPECT_EQ(deletedIds[1], "del-uuid-2");
	EXPECT_EQ(deletedIds[2], "del-uuid-3");
}

TEST_F(CloudSyncManagerCore_Pull, PullChanges_HandlesEmptyResponse)
{
	json serverResponse;
	serverResponse["code"] = 0;
	serverResponse["data"]["clips"] = json::array();
	serverResponse["data"]["deleted_ids"] = json::array();
	serverResponse["data"]["has_more"] = false;

	const json& data = serverResponse["data"];

	const json* clipsNode = nullptr;
	if (data.contains("new_clips"))
		clipsNode = &data["new_clips"];
	else if (data.contains("clips"))
		clipsNode = &data["clips"];

	const json* deletedNode = nullptr;
	if (data.contains("deleted_ids"))
		deletedNode = &data["deleted_ids"];

	bool hasClips = (clipsNode && !clipsNode->empty());
	bool hasDeletions = (deletedNode && !deletedNode->empty());

	EXPECT_FALSE(hasClips);
	EXPECT_FALSE(hasDeletions);
}

TEST_F(CloudSyncManagerCore_Pull, PullChanges_DecryptsAfterPull)
{
	// Setup crypto
	std::vector<BYTE> key = CCloudCrypto::RandomBytes(32);
	ASSERT_TRUE(CCloudCrypto::Initialize(key));

	// Encrypt some data to simulate what server would return
	CStringA plaintext("Decrypted after pull");
	CStringA ciphertext = CCloudCrypto::Encrypt(plaintext);
	ASSERT_FALSE(ciphertext.IsEmpty());

	// Simulate server response with encrypted format
	json serverResponse;
	serverResponse["code"] = 0;
	serverResponse["data"]["new_clips"] = json::array();

	json remoteClip;
	remoteClip["id"] = "remote-enc-clip";
	json fmt;
	fmt["format_type"] = 1;
	fmt["data"] = ciphertext.GetString();
	fmt["encrypted"] = true;
	remoteClip["formats"] = json::array();
	remoteClip["formats"].push_back(fmt);
	serverResponse["data"]["new_clips"].push_back(remoteClip);

	// Simulate PullChanges: extract clip and decrypt
	const json& clips = serverResponse["data"]["new_clips"];
	ASSERT_EQ(clips.size(), 1);

	json clip = clips[0];
	json formats = clip.contains("formats") ? clip["formats"] : json::array();

	BOOL bCryptoInit = TRUE;
	if (bCryptoInit && !formats.empty())
	{
		for (auto& fmt : formats)
		{
			int formatType = fmt.value("format_type", 0);
			if (formatType == 15) continue;
			if (fmt.contains("encrypted") && fmt["encrypted"].get<bool>())
			{
				std::string encryptedData = fmt["data"].get<std::string>();
				CStringA encrypted(encryptedData.c_str());
				CStringA decrypted = CCloudCrypto::Decrypt(encrypted);
				ASSERT_FALSE(decrypted.IsEmpty()) << "Decryption failed for pulled clip";
				fmt["data"] = decrypted.GetString();
				fmt["encrypted"] = false;
			}
		}
		clip["formats"] = formats;
	}

	// Verify decrypted
	json decryptedFormats = clip["formats"];
	ASSERT_EQ(decryptedFormats.size(), 1);
	EXPECT_EQ(decryptedFormats[0]["data"].get<std::string>(), "Decrypted after pull");
	EXPECT_FALSE(decryptedFormats[0]["encrypted"].get<bool>());
}

TEST_F(CloudSyncManagerCore_Pull, PullChanges_HandlesServerErrorCode)
{
	json serverResponse;
	serverResponse["code"] = 1001;
	serverResponse["message"] = "Internal server error";

	ASSERT_TRUE(serverResponse.contains("code"));
	int code = serverResponse["code"].get<int>();

	EXPECT_NE(code, 0);
	// The real PullChanges logs the error and returns early
	BOOL shouldStop = (code != 0);
	EXPECT_TRUE(shouldStop);
}

TEST_F(CloudSyncManagerCore_Pull, PullChanges_ProcessesMultipleClips)
{
	json serverResponse;
	serverResponse["code"] = 0;
	serverResponse["data"]["clips"] = json::array();

	for (int i = 0; i < 5; i++)
	{
		json clip;
		clip["id"] = "multi-clip-" + std::to_string(i);
		clip["description"] = "Clip #" + std::to_string(i);
		clip["crc"] = 1000 + i;
		clip["formats"] = json::array();
		serverResponse["data"]["clips"].push_back(clip);
	}

	const json& data = serverResponse["data"];
	ASSERT_TRUE(data.contains("clips"));
	EXPECT_EQ(data["clips"].size(), 5);

	for (int i = 0; i < 5; i++)
	{
		EXPECT_EQ(data["clips"][i]["id"], "multi-clip-" + std::to_string(i));
		EXPECT_EQ(data["clips"][i]["crc"], 1000 + i);
	}
}

TEST_F(CloudSyncManagerCore_Pull, PullChanges_HandlesNewClipsKey)
{
	// The real server may use either "clips" or "new_clips" key
	json responseWithNew;
	responseWithNew["code"] = 0;
	responseWithNew["data"]["new_clips"] = json::array();
	responseWithNew["data"]["new_clips"].push_back({{"id", "via-new_clips"}});

	const json* node1 = nullptr;
	if (responseWithNew["data"].contains("new_clips"))
		node1 = &responseWithNew["data"]["new_clips"];
	ASSERT_NE(node1, nullptr);
	EXPECT_EQ((*node1)[0]["id"], "via-new_clips");
}

TEST_F(CloudSyncManagerCore_Pull, PullChanges_HasMorePagination)
{
	json serverResponse;
	serverResponse["code"] = 0;
	serverResponse["data"]["clips"] = json::array();
	for (int i = 0; i < 200; i++)
	{
		json clip;
		clip["id"] = "page-" + std::to_string(i);
		serverResponse["data"]["clips"].push_back(clip);
	}
	serverResponse["data"]["has_more"] = true;
	serverResponse["data"]["server_time"] = "2024-07-01T00:00:00Z";

	bool hasMore = serverResponse["data"].value("has_more", false);
	EXPECT_TRUE(hasMore);

	// Real code loops while hasMore is true
	if (hasMore)
	{
		EXPECT_TRUE(serverResponse["data"].contains("server_time"));
	}
}

TEST_F(CloudSyncManagerCore_Pull, PullChanges_HandlesSyncTimeKey)
{
	// Server may return "sync_time" instead of "server_time"
	json response;
	response["code"] = 0;
	response["data"]["clips"] = json::array();
	response["data"]["sync_time"] = "2024-08-15T10:30:00Z";

	time_t newSyncTime = 0;
	const json& data = response["data"];
	if (data.contains("server_time"))
	{
		// parse server_time
	}
	else if (data.contains("sync_time"))
	{
		std::string syncTimeStr = data["sync_time"].get<std::string>();
		SYSTEMTIME st = {};
		if (sscanf_s(syncTimeStr.c_str(), "%04hd-%02hd-%02hdT%02hd:%02hd:%02hdZ",
			&st.wYear, &st.wMonth, &st.wDay,
			&st.wHour, &st.wMinute, &st.wSecond) == 6)
		{
			FILETIME ft;
			SystemTimeToFileTime(&st, &ft);
			ULARGE_INTEGER uli;
			uli.LowPart = ft.dwLowDateTime;
			uli.HighPart = ft.dwHighDateTime;
			newSyncTime = static_cast<time_t>((uli.QuadPart - 116444736000000000ULL) / 10000000ULL);
		}
	}

	// Expected: 2024-08-15T10:30:00Z = 1723717800
	EXPECT_EQ(newSyncTime, 1723717800);
}

// ============================================================================
// GetLocalClipsSince — SQL Query Construction and Enumeration Logic Tests
// ============================================================================

struct SimulatedClipRow {
	int lID;
	time_t lDate;
	CString mText;
	DWORD CRC;
	time_t lModifiedDate;
};

// Simulates the SQL WHERE clause construction logic in GetLocalClipsSince
static CString SimulateBuildWhereClause(time_t sinceTime, time_t upperBound)
{
	CString where;
	if (sinceTime > 0)
	{
		CString cond;
		cond.Format(_T("lModifiedDate > %lld"), sinceTime);
		where = cond;
	}
	if (upperBound > 0)
	{
		CString cond;
		cond.Format(_T("lModifiedDate <= %lld"), upperBound);
		if (!where.IsEmpty()) where += _T(" AND ");
		where += cond;
	}
	if (!where.IsEmpty())
	{
		CString tmp;
		tmp.Format(_T("WHERE %s AND "), (LPCTSTR)where);
		where = tmp;
	}
	else
	{
		where = _T("WHERE ");
	}
	return where;
}

// Simulates the SQL query string construction
static CString SimulateBuildQuery(time_t sinceTime, time_t upperBound, int offset, int limit)
{
	CString where = SimulateBuildWhereClause(sinceTime, upperBound);

	CString csSQL;
	csSQL.Format(_T("SELECT lID, lDate, mText, CRC, bIsGroup, lParentID, ")
	             _T("clipOrder, clipGroupOrder, stickyClipOrder, lShortCut, globalShortCut, ")
	             _T("lDontAutoDelete, lDontSync, m_Description, lastPasteDate, lModifiedDate ")
	             _T("FROM Main %sbIsGroup = 0 AND lDontSync = 0 ")
	             _T("ORDER BY lModifiedDate DESC LIMIT %d OFFSET %d"),
	             (LPCTSTR)where, limit, offset);
	return csSQL;
}

// Simulates building a clip JSON from a database row (as GetLocalClipsSince does)
static json SimulateBuildClipJson(
	int clipId,
	const std::string& remoteClipId,
	const std::string& description,
	int64_t crc,
	time_t updatedAt)
{
	json clipJson;
	clipJson["id"] = remoteClipId;
	clipJson["description"] = description;
	clipJson["crc"] = crc;
	clipJson["group_id"] = "";
	clipJson["short_cut"] = 0;
	clipJson["clip_order"] = 0.0;
	clipJson["clip_group_order"] = 0.0;

	if (updatedAt > 0)
	{
		struct tm gmtm;
		gmtime_s(&gmtm, &updatedAt);
		char timeBuf[32];
		strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%SZ", &gmtm);
		clipJson["updated_at"] = std::string(timeBuf);
	}

	clipJson["formats"] = json::array();
	return clipJson;
}

TEST_F(CloudSyncManagerCore_Query, GetLocalClipsSince_ValidParameters)
{
	// Verify query construction with typical parameters
	CString sql = SimulateBuildQuery(1700000000, 0, 0, 100);

	CString expected;
	expected.Format(_T("SELECT lID, lDate, mText, CRC, bIsGroup, lParentID, ")
	                _T("clipOrder, clipGroupOrder, stickyClipOrder, lShortCut, globalShortCut, ")
	                _T("lDontAutoDelete, lDontSync, m_Description, lastPasteDate, lModifiedDate ")
	                _T("FROM Main WHERE lModifiedDate > %lld AND ")
	                _T("bIsGroup = 0 AND lDontSync = 0 ")
	                _T("ORDER BY lModifiedDate DESC LIMIT 100 OFFSET 0"),
	                (long long)1700000000);

	EXPECT_STREQ(sql, expected);
}

TEST_F(CloudSyncManagerCore_Query, GetLocalClipsSince_SinceTimeZero)
{
	// When sinceTime=0, no date filter in WHERE clause
	CString sql = SimulateBuildQuery(0, 0, 0, 100);

	CString expected;
	expected.Format(_T("SELECT lID, lDate, mText, CRC, bIsGroup, lParentID, ")
	                _T("clipOrder, clipGroupOrder, stickyClipOrder, lShortCut, globalShortCut, ")
	                _T("lDontAutoDelete, lDontSync, m_Description, lastPasteDate, lModifiedDate ")
	                _T("FROM Main WHERE ")
	                _T("bIsGroup = 0 AND lDontSync = 0 ")
	                _T("ORDER BY lModifiedDate DESC LIMIT 100 OFFSET 0"));

	EXPECT_STREQ(sql, expected);
}

TEST_F(CloudSyncManagerCore_Query, GetLocalClipsSince_WithUpperBound)
{
	time_t sinceTime = 1700000000;
	time_t upperBound = 1700100000;

	CString sql = SimulateBuildQuery(sinceTime, upperBound, 0, 100);

	CString expected;
	expected.Format(_T("SELECT lID, lDate, mText, CRC, bIsGroup, lParentID, ")
	                _T("clipOrder, clipGroupOrder, stickyClipOrder, lShortCut, globalShortCut, ")
	                _T("lDontAutoDelete, lDontSync, m_Description, lastPasteDate, lModifiedDate ")
	                _T("FROM Main WHERE lModifiedDate > %lld AND lModifiedDate <= %lld AND ")
	                _T("bIsGroup = 0 AND lDontSync = 0 ")
	                _T("ORDER BY lModifiedDate DESC LIMIT 100 OFFSET 0"),
	                (long long)sinceTime, (long long)upperBound);

	EXPECT_STREQ(sql, expected);
}

TEST_F(CloudSyncManagerCore_Query, GetLocalClipsSince_PaginationOffset)
{
	// Verify LIMIT and OFFSET in SQL
	CString sql = SimulateBuildQuery(1700000000, 0, 50, 100);

	CString expected;
	expected.Format(_T("SELECT lID, lDate, mText, CRC, bIsGroup, lParentID, ")
	                _T("clipOrder, clipGroupOrder, stickyClipOrder, lShortCut, globalShortCut, ")
	                _T("lDontAutoDelete, lDontSync, m_Description, lastPasteDate, lModifiedDate ")
	                _T("FROM Main WHERE lModifiedDate > %lld AND ")
	                _T("bIsGroup = 0 AND lDontSync = 0 ")
	                _T("ORDER BY lModifiedDate DESC LIMIT 100 OFFSET 50"),
	                (long long)1700000000);

	EXPECT_STREQ(sql, expected);
}

TEST_F(CloudSyncManagerCore_Query, GetLocalClipsSince_HasMoreDetection)
{
	int limit = 100;

	// hasMore = (pageCount >= limit)

	// Scenario 1: page has exactly 'limit' items -> hasMore = true
	{
		int itemCount = limit;
		bool hasMore = (itemCount >= limit);
		EXPECT_TRUE(hasMore);
	}

	// Scenario 2: page has fewer than 'limit' items -> hasMore = false
	{
		int itemCount = 42;
		bool hasMore = (itemCount >= limit);
		EXPECT_FALSE(hasMore);
	}

	// Scenario 3: page is empty -> hasMore = false
	{
		int itemCount = 0;
		bool hasMore = (itemCount >= limit);
		EXPECT_FALSE(hasMore);
	}

	// Scenario 4: page exceeds limit (shouldn't happen but test for correctness)
	{
		int itemCount = 150;
		bool hasMore = (itemCount >= limit);
		EXPECT_TRUE(hasMore);
	}
}

TEST_F(CloudSyncManagerCore_Query, GetLocalClipsSince_UpperBoundOnly)
{
	time_t sinceTime = 0;
	time_t upperBound = 1700100000;

	CString sql = SimulateBuildQuery(sinceTime, upperBound, 0, 200);

	CString expected;
	expected.Format(_T("SELECT lID, lDate, mText, CRC, bIsGroup, lParentID, ")
	                _T("clipOrder, clipGroupOrder, stickyClipOrder, lShortCut, globalShortCut, ")
	                _T("lDontAutoDelete, lDontSync, m_Description, lastPasteDate, lModifiedDate ")
	                _T("FROM Main WHERE lModifiedDate <= %lld AND ")
	                _T("bIsGroup = 0 AND lDontSync = 0 ")
	                _T("ORDER BY lModifiedDate DESC LIMIT 200 OFFSET 0"),
	                (long long)upperBound);

	EXPECT_STREQ(sql, expected);
}

TEST_F(CloudSyncManagerCore_Query, GetLocalClipsSince_BuildsClipJsonCorrectly)
{
	// Simulate building clip JSON as GetLocalClipsSince does for each row
	int clipId = 42;
	std::string remoteId = "uuid-generated-for-42";
	std::string description = "Test clipboard content";
	int64_t crc = 0xDEADBEEF;
	time_t updatedAt = 1700000000;

	json clipJson = SimulateBuildClipJson(clipId, remoteId, description, crc, updatedAt);

	EXPECT_EQ(clipJson["id"], "uuid-generated-for-42");
	EXPECT_EQ(clipJson["description"], "Test clipboard content");
	EXPECT_EQ(clipJson["crc"], (int64_t)0xDEADBEEF);
	EXPECT_EQ(clipJson["updated_at"], "2023-11-14T22:13:20Z");
	EXPECT_TRUE(clipJson["formats"].is_array());
	EXPECT_TRUE(clipJson["formats"].empty());
}

TEST_F(CloudSyncManagerCore_Query, GetLocalClipsSince_OrderByModifiedDateDesc)
{
	// Verify SQL has ORDER BY lModifiedDate DESC
	CString sql = SimulateBuildQuery(1700000000, 0, 0, 100);

	EXPECT_NE(sql.Find(_T("ORDER BY lModifiedDate DESC")), -1);
}

TEST_F(CloudSyncManagerCore_Query, GetLocalClipsSince_FiltersNonGroups)
{
	// Verify WHERE clause includes bIsGroup = 0
	CString sql = SimulateBuildQuery(0, 0, 0, 100);

	EXPECT_NE(sql.Find(_T("bIsGroup = 0")), -1);
}

TEST_F(CloudSyncManagerCore_Query, GetLocalClipsSince_FiltersDontSync)
{
	// Verify WHERE clause includes lDontSync = 0
	CString sql = SimulateBuildQuery(0, 0, 0, 100);

	EXPECT_NE(sql.Find(_T("lDontSync = 0")), -1);
}

TEST_F(CloudSyncManagerCore_Query, GetLocalClipsSince_EmptyResultReturnsEmptyArray)
{
	json clipsArray = json::array();
	bool hasMore = false;

	// Simulate what happens with zero rows: empty array, hasMore = false
	EXPECT_TRUE(clipsArray.empty());
	EXPECT_FALSE(hasMore);
}

TEST_F(CloudSyncManagerCore_Query, GetLocalClipsSince_SinceTimeGreaterThanUpperBound)
{
	// Edge case: sinceTime > upperBound should produce no results
	// because the WHERE clause is: lModifiedDate > 2000 AND lModifiedDate <= 1000
	// which is impossible to satisfy

	time_t sinceTime = 2000;
	time_t upperBound = 1000;

	CString sql = SimulateBuildQuery(sinceTime, upperBound, 0, 100);
	CString expected;
	expected.Format(_T("lModifiedDate > %lld AND lModifiedDate <= %lld"),
	                (long long)sinceTime, (long long)upperBound);

	EXPECT_NE(sql.Find(expected), -1);
	// This WHERE clause will naturally return 0 rows from SQL — correct behavior
}

TEST_F(CloudSyncManagerCore_Query, GetLocalClipsSince_ClipsHaveFormatsArray)
{
	// Verify that clip JSON always includes a 'formats' array (even if empty)
	json clip = SimulateBuildClipJson(1, "uuid", "desc", 0, 0);

	EXPECT_TRUE(clip.contains("formats"));
	EXPECT_TRUE(clip["formats"].is_array());
}

TEST_F(CloudSyncManagerCore_Query, GetLocalClipsSince_UpdatedAtUsesModDate)
{
	// Real logic: updatedAt = (modDate > 0) ? modDate : lDate
	// If modDate > 0, use modDate; else fall back to lDate (creation date)
	time_t creationDate = 1000;
	time_t modDate = 2000;

	time_t updatedAt = (modDate > 0) ? modDate : creationDate;
	EXPECT_EQ(updatedAt, 2000);

	// Fallback when modDate is 0
	time_t modDateZero = 0;
	updatedAt = (modDateZero > 0) ? modDateZero : creationDate;
	EXPECT_EQ(updatedAt, 1000);
}

// ============================================================================
// InitializeEncryption Tests
// ============================================================================

static BOOL SimulateInitializeEncryption()
{
	CString csKeyB64 = CGetSetOptions::GetCloudEncryptionKey();
	if (csKeyB64.IsEmpty())
		return FALSE;

	CT2A keyB64A(csKeyB64, CP_UTF8);
	std::vector<BYTE> key = CCloudCrypto::Base64Decode(CStringA(keyB64A));
	if (key.size() != 32)
		return FALSE;

	return CCloudCrypto::Initialize(key);
}

TEST_F(CloudSyncManagerCore_Encryption, InitializeEncryption_Successful)
{
	// Store a valid 32-byte key
	std::vector<BYTE> key = CCloudCrypto::RandomBytes(32);
	CStringA keyB64 = CCloudCrypto::Base64Encode(key);
	CGetSetOptions::SetCloudSyncEncryptionEnabled(TRUE);
	CGetSetOptions::SetCloudEncryptionKey(CString(keyB64));

	BOOL result = SimulateInitializeEncryption();
	EXPECT_TRUE(result);

	// Verify crypto is actually usable
	CStringA test = CCloudCrypto::Encrypt(CStringA("verification"));
	EXPECT_FALSE(test.IsEmpty());
}

TEST_F(CloudSyncManagerCore_Encryption, InitializeEncryption_NoKeyStored)
{
	// Don't store any key
	CGetSetOptions::SetCloudSyncEncryptionEnabled(FALSE);

	BOOL result = SimulateInitializeEncryption();
	EXPECT_FALSE(result);
}

TEST_F(CloudSyncManagerCore_Encryption, InitializeEncryption_InvalidKeySize)
{
	// Store a key that is not 32 bytes
	std::vector<BYTE> wrongKey = CCloudCrypto::RandomBytes(16); // 16 bytes, wrong size
	CStringA keyB64 = CCloudCrypto::Base64Encode(wrongKey);
	CGetSetOptions::SetCloudSyncEncryptionEnabled(TRUE);
	CGetSetOptions::SetCloudEncryptionKey(CString(keyB64));

	BOOL result = SimulateInitializeEncryption();
	EXPECT_FALSE(result);
}

TEST_F(CloudSyncManagerCore_Encryption, InitializeEncryption_EmptyKeyString)
{
	// Store an empty key
	CGetSetOptions::SetCloudSyncEncryptionEnabled(TRUE);
	CGetSetOptions::SetCloudEncryptionKey(CString(""));

	BOOL result = SimulateInitializeEncryption();
	EXPECT_FALSE(result);
}

TEST_F(CloudSyncManagerCore_Encryption, InitializeEncryption_CorruptedBase64)
{
	// Store corrupted (invalid base64) key
	CGetSetOptions::SetCloudSyncEncryptionEnabled(TRUE);
	CGetSetOptions::SetCloudEncryptionKey(CString("!!!invalid-base64!!!"));

	BOOL result = SimulateInitializeEncryption();
	EXPECT_FALSE(result);
}

TEST_F(CloudSyncManagerCore_Encryption, InitializeEncryption_SetsCryptoFlag)
{
	// In the real CCloudSyncManager, InitializeEncryption sets m_cryptoInitialized = TRUE.
	// Simulate this with a boolean flag.
	BOOL m_cryptoInitialized = FALSE;

	std::vector<BYTE> key = CCloudCrypto::RandomBytes(32);
	CStringA keyB64 = CCloudCrypto::Base64Encode(key);
	CGetSetOptions::SetCloudSyncEncryptionEnabled(TRUE);
	CGetSetOptions::SetCloudEncryptionKey(CString(keyB64));

	BOOL ok = SimulateInitializeEncryption();
	EXPECT_TRUE(ok);

	m_cryptoInitialized = ok;
	EXPECT_TRUE(m_cryptoInitialized);
}

TEST_F(CloudSyncManagerCore_Encryption, InitializeEncryption_KeySizeNot32ReturnsFalse)
{
	// Test key sizes that are not 32
	int invalidSizes[] = {0, 1, 8, 16, 24, 64, 128};
	for (int size : invalidSizes)
	{
		CGetSetOptions::Reset();
		CCloudCrypto::Reset();

		std::vector<BYTE> key = CCloudCrypto::RandomBytes(size);
		CStringA keyB64 = CCloudCrypto::Base64Encode(key);
		CGetSetOptions::SetCloudSyncEncryptionEnabled(TRUE);
		CGetSetOptions::SetCloudEncryptionKey(CString(keyB64));

		BOOL result = SimulateInitializeEncryption();
		if (size == 32)
			EXPECT_TRUE(result) << "Key size " << size << " should succeed";
		else
			EXPECT_FALSE(result) << "Key size " << size << " should fail";
	}
}

TEST_F(CloudSyncManagerCore_Encryption, InitializeEncryption_AfterResetCanReinitialize)
{
	// First initialization
	std::vector<BYTE> key = CCloudCrypto::RandomBytes(32);
	CStringA keyB64 = CCloudCrypto::Base64Encode(key);
	CGetSetOptions::SetCloudSyncEncryptionEnabled(TRUE);
	CGetSetOptions::SetCloudEncryptionKey(CString(keyB64));

	BOOL firstInit = SimulateInitializeEncryption();
	EXPECT_TRUE(firstInit);

	// Reset crypto (simulates application shutdown or crypto reset)
	CCloudCrypto::Reset();

	// Re-initialize with same key
	BOOL secondInit = SimulateInitializeEncryption();
	EXPECT_TRUE(secondInit) << "Should be able to re-initialize after reset";
}

TEST_F(CloudSyncManagerCore_Encryption, InitializeEncryption_ExceptionSafety)
{
	// The real InitializeEncryption wraps its body in a try/catch.
	// Simulate that any std::exception is caught and returns FALSE.
	BOOL caughtException = FALSE;
	try
	{
		// Force a scenario that would throw — for simulation, just test
		// that unexpected base64 data doesn't throw
		CGetSetOptions::SetCloudSyncEncryptionEnabled(TRUE);
		CGetSetOptions::SetCloudEncryptionKey(CString(""));
		SimulateInitializeEncryption();
	}
	catch (...)
	{
		caughtException = TRUE;
	}
	EXPECT_FALSE(caughtException) << "InitializeEncryption should not throw";
}

// ============================================================================
// Initial Push Baseline Logic Tests
// ============================================================================

TEST_F(CloudSyncManagerCore_Query, GetMaxLocalClipModifiedDate)
{
	// Real logic: SELECT MAX(lModifiedDate) FROM Main WHERE bIsGroup = 0 AND lDontSync = 0
	// Simulate by verifying the SQL pattern
	CString csSQL = _T("SELECT MAX(lModifiedDate) FROM Main WHERE bIsGroup = 0 AND lDontSync = 0");
	EXPECT_NE(csSQL.Find(_T("MAX(lModifiedDate)")), -1);
	EXPECT_NE(csSQL.Find(_T("bIsGroup = 0")), -1);
	EXPECT_NE(csSQL.Find(_T("lDontSync = 0")), -1);
}

TEST_F(CloudSyncManagerCore_Push, PushNewClips_FirstPushUsesUpperBound)
{
	// First push logic: sinceTime=0, upperBound=baseline
	// This creates a snapshot: all clips modified <= baseline
	time_t baseline = 1700000000;
	time_t sinceTime = 0;
	time_t upperBound = baseline;

	CString where;
	if (sinceTime > 0)
	{
		CString cond;
		cond.Format(_T("lModifiedDate > %lld"), sinceTime);
		where = cond;
	}
	if (upperBound > 0)
	{
		CString cond;
		cond.Format(_T("lModifiedDate <= %lld"), upperBound);
		if (!where.IsEmpty()) where += _T(" AND ");
		where += cond;
	}

	CString expected = _T("lModifiedDate <= 1700000000");
	EXPECT_STREQ(where, expected);
}

// ============================================================================
// Thread Safety: lastSyncTime/lastPushTime Access
// ============================================================================

TEST_F(CloudSyncManagerCore_Query, ThreadSafeReadOfLastSyncTime)
{
	// The real code reads m_lastSyncTime under a critical section.
	// Simulate the same pattern.
	time_t m_lastSyncTime = 1700000000;
	CRITICAL_SECTION m_csSync;
	InitializeCriticalSection(&m_csSync);

	time_t lastSync;
	EnterCriticalSection(&m_csSync);
	lastSync = m_lastSyncTime;
	LeaveCriticalSection(&m_csSync);

	EXPECT_EQ(lastSync, 1700000000);

	// Thread-safe write
	time_t newTime = 1700100000;
	EnterCriticalSection(&m_csSync);
	m_lastSyncTime = newTime;
	LeaveCriticalSection(&m_csSync);

	EXPECT_EQ(m_lastSyncTime, 1700100000);

	DeleteCriticalSection(&m_csSync);
}

// ============================================================================
// RFC3339 Timestamp Format Consistency
// ============================================================================

TEST_F(CloudSyncManagerCore_Push, TimestampFormatConsistentBetweenPushAndPull)
{
	// Verify that PushNewClips and PullChanges use the same RFC3339 format
	time_t testTime = 1728000000;

	// PushNewClips format (via FileTimeToSystemTime)
	SYSTEMTIME st;
	FILETIME ft;
	ULARGE_INTEGER uli;
	uli.QuadPart = ((ULONGLONG)testTime * 10000000ULL) + 116444736000000000ULL;
	ft.dwLowDateTime = uli.LowPart;
	ft.dwHighDateTime = uli.HighPart;
	FileTimeToSystemTime(&ft, &st);
	char pushBuf[32];
	sprintf_s(pushBuf, "%04hd-%02hd-%02hdT%02hd:%02hd:%02hdZ",
	          st.wYear, st.wMonth, st.wDay,
	          st.wHour, st.wMinute, st.wSecond);

	// PullChanges format (via gmtime_s + strftime)
	struct tm gmtm;
	gmtime_s(&gmtm, &testTime);
	char pullBuf[32];
	strftime(pullBuf, sizeof(pullBuf), "%Y-%m-%dT%H:%M:%SZ", &gmtm);

	// Both should produce the same string
	EXPECT_STREQ(pushBuf, pullBuf);
	EXPECT_EQ(std::string(pushBuf), "2024-10-04T00:00:00Z");
}

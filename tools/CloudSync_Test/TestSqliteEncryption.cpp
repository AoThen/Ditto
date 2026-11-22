#include <gtest/gtest.h>
#include "../../src/sqlite/sqlite3mc_amalgamation.h"

namespace
{

class TempDbFile
{
public:
	TempDbFile()
	{
		char tempDir[MAX_PATH] = {};
		char tempFile[MAX_PATH] = {};
		GetTempPathA(MAX_PATH, tempDir);
		GetTempFileNameA(tempDir, "s3mc", 0, tempFile);
		// GetTempFileNameA creates the file; let SQLite own its lifecycle.
		DeleteFileA(tempFile);
		lstrcpyA(m_path, tempFile);
	}

	~TempDbFile()
	{
		DeleteFileA(m_path);
	}

	const char* Path() const { return m_path; }

private:
	char m_path[MAX_PATH] = {};
};

void ExecSql(sqlite3* db, const char* sql)
{
	char* error = nullptr;
	int rc = sqlite3_exec(db, sql, nullptr, nullptr, &error);
	if (error != nullptr)
	{
		std::string message = error;
		sqlite3_free(error);
		FAIL() << "sqlite3_exec failed (rc=" << rc << "): " << message;
	}
	ASSERT_EQ(SQLITE_OK, rc);
}

} // namespace

TEST(SqliteEncryption, EncryptedDbReadWriteWithKey)
{
	TempDbFile file;

	sqlite3* db = nullptr;
	ASSERT_EQ(SQLITE_OK, sqlite3_open(file.Path(), &db));
	ASSERT_EQ(SQLITE_OK, sqlite3_key(db, "correct-key", (int)strlen("correct-key")));
	ExecSql(db, "CREATE TABLE t (v TEXT)");
	ExecSql(db, "INSERT INTO t VALUES ('hello-encrypted')");
	sqlite3_close(db);

	db = nullptr;
	ASSERT_EQ(SQLITE_OK, sqlite3_open(file.Path(), &db));
	ASSERT_EQ(SQLITE_OK, sqlite3_key(db, "correct-key", (int)strlen("correct-key")));
	sqlite3_stmt* stmt = nullptr;
	ASSERT_EQ(SQLITE_OK, sqlite3_prepare_v2(db, "SELECT v FROM t", -1, &stmt, nullptr));
	ASSERT_EQ(SQLITE_ROW, sqlite3_step(stmt));
	EXPECT_STREQ("hello-encrypted", (const char*)sqlite3_column_text(stmt, 0));
	sqlite3_finalize(stmt);
	sqlite3_close(db);
}

TEST(SqliteEncryption, WrongKeyCannotReadData)
{
	TempDbFile file;

	sqlite3* db = nullptr;
	ASSERT_EQ(SQLITE_OK, sqlite3_open(file.Path(), &db));
	ASSERT_EQ(SQLITE_OK, sqlite3_key(db, "correct-key", (int)strlen("correct-key")));
	ExecSql(db, "CREATE TABLE t (v TEXT)");
	ExecSql(db, "INSERT INTO t VALUES ('secret')");
	sqlite3_close(db);

	db = nullptr;
	ASSERT_EQ(SQLITE_OK, sqlite3_open(file.Path(), &db));
	ASSERT_EQ(SQLITE_OK, sqlite3_key(db, "wrong-key", (int)strlen("wrong-key")));
	sqlite3_stmt* stmt = nullptr;
	int rc = sqlite3_prepare_v2(db, "SELECT v FROM t", -1, &stmt, nullptr);
	if (rc == SQLITE_OK)
		rc = sqlite3_step(stmt);
	EXPECT_TRUE(rc != SQLITE_ROW && rc != SQLITE_DONE)
		<< "decryption with a wrong key must not return plaintext (rc=" << rc << ")";
	if (stmt != nullptr)
		sqlite3_finalize(stmt);
	sqlite3_close(db);
}

TEST(SqliteEncryption, NoKeyCannotReadEncryptedDb)
{
	TempDbFile file;

	sqlite3* db = nullptr;
	ASSERT_EQ(SQLITE_OK, sqlite3_open(file.Path(), &db));
	ASSERT_EQ(SQLITE_OK, sqlite3_key(db, "correct-key", (int)strlen("correct-key")));
	ExecSql(db, "CREATE TABLE t (v TEXT)");
	ExecSql(db, "INSERT INTO t VALUES ('secret')");
	sqlite3_close(db);

	db = nullptr;
	ASSERT_EQ(SQLITE_OK, sqlite3_open(file.Path(), &db));
	sqlite3_stmt* stmt = nullptr;
	int rc = sqlite3_prepare_v2(db, "SELECT v FROM t", -1, &stmt, nullptr);
	if (rc == SQLITE_OK)
		rc = sqlite3_step(stmt);
	EXPECT_TRUE(rc != SQLITE_ROW && rc != SQLITE_DONE)
		<< "an encrypted db must not be readable without the key (rc=" << rc << ")";
	if (stmt != nullptr)
		sqlite3_finalize(stmt);
	sqlite3_close(db);
}

# Ditto Cloud Backend E2E Tests

## Quick Start

```bash
# Run all tests (with fast bcrypt for speed)
BCRYPT_COST=4 go test ./tests/... -v -count=1

# Run specific test file
BCRYPT_COST=4 go test ./tests/... -v -run TestWebSocket -count=1

# Run with coverage
BCRYPT_COST=4 go test ./tests/... -cover -count=1
```

## Test Results

| Module | Tests | Status |
|--------|-------|--------|
| Authentication | 9 | ✅ All pass |
| Clips | 6 | ✅ All pass |
| Sync | 3 | ✅ All pass |
| Encryption | 3 | ✅ All pass |
| Devices | 2 | ✅ All pass |
| Health | 3 | ✅ All pass |
| **WebSocket** | **5** | **✅ All pass** |
| **Total** | **29** | **✅ All pass** |

## Test Structure

```
tests/
├── testutil/
│   ├── setup.go          # httptest server, DB setup, HTTP helpers
│   └── factory.go        # Test data factories
├── auth_test.go          # 9 tests: register, login, rate limit, JWT expiry
├── clip_test.go          # 6 tests: CRUD, pagination, search, user isolation
├── sync_test.go          # 3 tests: push/pull, same device, empty push
├── encryption_test.go    # 3 tests: salt setup, retrieval, duplicate
├── device_test.go        # 2 tests: list, remove
├── health_test.go        # 3 tests: endpoint, stats, no auth
└── websocket_test.go     # 5 tests: connect, broadcast, isolation, invalid token, multi-conn
```

## Test Scenarios (29 total)

### Authentication (9)
| Test | Description |
|------|-------------|
| `TestRegister_Success` | Register new user, expect code=0 |
| `TestRegister_DuplicateUsername` | Same username twice, expect code=40001 |
| `TestRegister_DuplicateEmail` | Same email twice, expect code=40002 |
| `TestRegister_InvalidInput` | Empty fields, expect code=40000 |
| `TestLogin_Success` | Correct credentials, expect device_token |
| `TestLogin_WrongPassword` | Wrong password, expect code=40101 |
| `TestLogin_RateLimit_IP` | 5 failed logins → IP banned, expect code=42901 |
| `TestLogin_RateLimit_User` | 10 failed logins → user locked, expect code=42301 |
| `TestAuth_ExpiredToken` | Use expired JWT, expect code=40102 |

### Clips (6)
| Test | Description |
|------|-------------|
| `TestCreateClip_Success` | Create clip via sync push, expect code=0 |
| `TestListClips_Pagination` | 25 clips, page=1 per_page=20 → 20 items |
| `TestListClips_Search` | Search by description keyword |
| `TestGetClip_Detail` | Get single clip with full base64 format data |
| `TestDeleteClip_Success` | Delete clip, verify gone |
| `TestClip_UserIsolation` | User A cannot see user B's clips |

### Sync (3)
| Test | Description |
|------|-------------|
| `TestSync_PushAndPull` | Device A pushes, Device B pulls via sync |
| `TestSync_SameDevice` | Device should NOT receive its own clips back |
| `TestSync_EmptyPush` | Empty push should still return changes from other devices |

### Encryption (3)
| Test | Description |
|------|-------------|
| `TestEncryption_Setup` | Setup encryption, expect salt returned |
| `TestEncryption_GetSalt` | After setup, get same salt back |
| `TestEncryption_DuplicateSetup` | Setup twice, expect code=40901 |

### Devices (2)
| Test | Description |
|------|-------------|
| `TestDevice_ListAfterLogin` | After login, list devices, expect ≥1 |
| `TestDevice_Remove` | Remove device, verify it's gone |

### Health (3)
| Test | Description |
|------|-------------|
| `TestHealth_Endpoint` | GET /health → status "ok" |
| `TestHealth_WithStats` | Health response includes user count, clip count, uptime |
| `TestHealth_NoAuth` | Health endpoint works without authentication |

### WebSocket (5)
| Test | Description |
|------|-------------|
| `TestWebSocket_Connect` | Connect with valid JWT, verify "connected" message |
| `TestWebSocket_BroadcastOnSync` | Device A pushes clip → Device B receives "clips_added" via WS |
| `TestWebSocket_NoCrossUserBroadcast` | User B's WS does NOT receive User A's broadcasts |
| `TestWebSocket_InvalidToken` | Connect with invalid JWT → HTTP 401 |
| `TestWebSocket_MultipleConnections` | 3 WS clients for same user → all receive broadcast |

## Design

### Isolation
- Each test gets its own temp SQLite database file
- `os.CreateTemp("ditto_test_*.db")` + `t.Cleanup` for cleanup
- Tests are fully independent, can run in parallel

### HTTP Testing
- Uses `httptest.NewServer` (NOT `httptest.NewRecorder`)
- Tests the full HTTP stack (routing, middleware, handler, service)
- Real TCP connection, real JSON marshaling

### WebSocket Testing
- Uses `gorilla/websocket` Dialer to connect to httptest server
- Sends both `Authorization: Bearer` header AND `?token=` query param
- Read deadlines prevent hanging (5s normal, 1s for "no message" assertion)

### Rate Limit Testing
- `X-Forwarded-For` header simulates different client IPs
- In-memory rate limiter is reset per test (new server instance)

### bcrypt Cost
- Set `BCRYPT_COST=4` env var to speed up tests (default=12 in production)
- Tests run in ~3 seconds with cost=4

## Known Bugs Fixed

### 1. Rate Limiter Deadlock
**Location**: `internal/middleware/rate_limit.go`

**Symptom**: Login requests would hang under rate limit testing

**Root cause**: `LoginRateLimit` middleware held mutex while calling `c.Next()`, but the Login handler inside `c.Next()` called `IsUserLocked` which tried to acquire the same mutex → deadlock.

**Fix**: Release mutex before calling `c.Next()`.

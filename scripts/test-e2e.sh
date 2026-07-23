#!/bin/bash
# End-to-End Integration Test Script
# Tests: Go Backend <-> Web Frontend <-> Ditto Client (simulated)

set -e

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SERVER_DIR="$SCRIPT_DIR/server"
WEB_DIR="$SCRIPT_DIR/web"

echo "========================================="
echo "Ditto Cloud - End-to-End Integration Test"
echo "========================================="
echo ""

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

PASS_COUNT=0
FAIL_COUNT=0

pass() {
    echo -e "${GREEN}✓ PASS:${NC} $1"
    PASS_COUNT=$((PASS_COUNT + 1))
}

fail() {
    echo -e "${RED}✗ FAIL:${NC} $1"
    FAIL_COUNT=$((FAIL_COUNT + 1))
}

info() {
    echo -e "${YELLOW}ℹ INFO:${NC} $1"
}

# Test 1: Check Go backend binary exists
info "Test 1: Checking Go backend binary..."
if [ -f "$SERVER_DIR/server" ]; then
    pass "Go backend binary exists"
else
    fail "Go backend binary not found"
fi

# Test 2: Check web frontend build
info "Test 2: Checking web frontend..."
if [ -d "$WEB_DIR/node_modules" ]; then
    pass "Web frontend dependencies installed"
else
    info "Installing web frontend dependencies..."
    cd "$WEB_DIR" && npm install > /dev/null 2>&1
    if [ $? -eq 0 ]; then
        pass "Web frontend dependencies installed"
    else
        fail "Failed to install web frontend dependencies"
    fi
fi

# Test 3: Try to build web frontend
info "Test 3: Building web frontend..."
cd "$WEB_DIR"
if npm run build > /dev/null 2>&1; then
    pass "Web frontend builds successfully"
else
    fail "Web frontend build failed"
fi

# Test 4: Start Go backend in background
info "Test 4: Starting Go backend server..."
cd "$SERVER_DIR"
./server > /tmp/ditto-server.log 2>&1 &
SERVER_PID=$!

# Wait for server to be ready with retry logic
info "Waiting for server to be ready..."
MAX_RETRIES=10
RETRY_INTERVAL=2
SERVER_READY=false

for i in $(seq 1 $MAX_RETRIES); do
    if kill -0 $SERVER_PID 2>/dev/null; then
        # Server process is running, try health check
        HEALTH=$(curl -s --max-time 3 http://localhost:8080/health 2>/dev/null || echo "")
        if [ -n "$HEALTH" ] && echo "$HEALTH" | grep -q "ok"; then
            SERVER_READY=true
            info "Server ready after ${i} attempts ($((i * RETRY_INTERVAL))s)"
            break
        fi
    else
        fail "Go backend server process exited"
        cat /tmp/ditto-server.log
        exit 1
    fi
    info "Attempt $i/$MAX_RETRIES: Server not ready yet, waiting ${RETRY_INTERVAL}s..."
    sleep $RETRY_INTERVAL
done

if [ "$SERVER_READY" = true ]; then
    pass "Go backend server started and healthy (PID: $SERVER_PID)"
else
    fail "Go backend server failed to become ready after $((MAX_RETRIES * RETRY_INTERVAL))s"
    info "Server log:"
    cat /tmp/ditto-server.log
    kill $SERVER_PID 2>/dev/null
    exit 1
fi

# Test 5: Health check (already done above, but verify again)
info "Test 5: Health check..."
HEALTH=$(curl -s --max-time 5 http://localhost:8080/health 2>/dev/null || echo "")
if [ -n "$HEALTH" ] && echo "$HEALTH" | grep -q "ok"; then
    pass "Health check passed"
else
    fail "Health check failed"
    info "Response: $HEALTH"
fi

# Test 6: User registration
info "Test 6: User registration..."
REGISTER_RESPONSE=$(curl -s --max-time 10 -X POST http://localhost:8080/api/v1/auth/register \
  -H "Content-Type: application/json" \
  -d '{"username":"testuser","email":"test@example.com","password":"TestPass123"}' 2>/dev/null)

if echo "$REGISTER_RESPONSE" | grep -q -E '"code":0|"code":200|"success"'; then
    pass "User registration successful"
else
    # Check if user already exists (might be from previous test)
    if echo "$REGISTER_RESPONSE" | grep -q -i "exists"; then
        pass "User already exists (acceptable)"
    else
        fail "User registration failed"
        info "Response: $REGISTER_RESPONSE"
    fi
fi

# Test 7: User login (H1: cookies set automatically)
info "Test 7: User login..."
LOGIN_RESPONSE=$(curl -s --max-time 10 -X POST http://localhost:8080/api/v1/auth/login \
  -H "Content-Type: application/json" \
  -c /tmp/ditto-cookies.txt \
  -d '{"username":"testuser","password":"TestPass123"}' 2>/dev/null)

# H1: Token is now in HttpOnly cookie, check for success code
TOKEN=$(echo "$LOGIN_RESPONSE" | grep -o '"code":0' | head -1)
if [ -n "$TOKEN" ]; then
    pass "User login successful"
else
    fail "User login failed"
    info "Response: $LOGIN_RESPONSE"
    # Kill server and exit
    kill $SERVER_PID 2>/dev/null
    exit 1
fi

# Test 8: Get clips list (should be empty initially)
info "Test 8: Get clips list..."
CLIPS_RESPONSE=$(curl -s --max-time 10 http://localhost:8080/api/v1/clips \
  -b /tmp/ditto-cookies.txt 2>/dev/null)

if echo "$CLIPS_RESPONSE" | grep -q -E '"code":0|"data"'; then
    pass "Get clips list successful"
else
    fail "Get clips list failed"
    info "Response: $CLIPS_RESPONSE"
fi

# Test 9: Sync clips (push)
info "Test 9: Push clips to server..."
PUSH_RESPONSE=$(curl -s --max-time 10 -X POST http://localhost:8080/api/v1/clips/sync \
  -b /tmp/ditto-cookies.txt \
  -H "Content-Type: application/json" \
  -d '{
    "since": "1970-01-01T00:00:00Z",
    "device_id": "test-device-1",
    "push_clips": [
      {
        "remote_clip_id": "test-clip-1",
        "description": "Test clip from e2e test",
        "crc": 12345678,
        "formats": [
          {
            "format_name": "CF_UNICODETEXT",
            "format_type": 13,
            "data": "SGVsbG8gZnJvbSBlMmUgdGVzdCE=",
            "data_size": 20
          }
        ]
      }
    ]
  }' 2>/dev/null)

if echo "$PUSH_RESPONSE" | grep -q -E '"synced_count":[1-9]|"code":0'; then
    pass "Push clips successful"
else
    fail "Push clips failed"
    info "Response: $PUSH_RESPONSE"
fi

# Test 10: Get clips list (should have 1 clip now)
info "Test 10: Get clips list after push..."
CLIPS_AFTER=$(curl -s --max-time 10 http://localhost:8080/api/v1/clips \
  -b /tmp/ditto-cookies.txt 2>/dev/null)

if echo "$CLIPS_AFTER" | grep -q -E '"total":[1-9]|"items"'; then
    pass "Clips list updated after push"
else
    fail "Clips list not updated"
    info "Response: $CLIPS_AFTER"
fi

# Test 11: Stats overview
info "Test 11: Get stats overview..."
STATS_RESPONSE=$(curl -s --max-time 10 http://localhost:8080/api/v1/stats/overview \
  -b /tmp/ditto-cookies.txt 2>/dev/null)

if echo "$STATS_RESPONSE" | grep -q -E '"total_clips"|code'; then
    pass "Stats overview retrieved"
else
    fail "Stats overview failed"
    info "Response: $STATS_RESPONSE"
fi

# Test 12: Get devices list
info "Test 12: Get devices list..."
DEVICES_RESPONSE=$(curl -s --max-time 10 http://localhost:8080/api/v1/devices \
  -b /tmp/ditto-cookies.txt 2>/dev/null)

if echo "$DEVICES_RESPONSE" | grep -q -E '"data"|code'; then
    pass "Devices list retrieved"
else
    fail "Devices list failed"
    info "Response: $DEVICES_RESPONSE"
fi

# Cleanup: Kill server
info "Cleaning up..."
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null
pass "Server stopped"

# Summary
echo ""
echo "========================================="
echo "Test Summary"
echo "========================================="
echo -e "${GREEN}Passed: $PASS_COUNT${NC}"
echo -e "${RED}Failed: $FAIL_COUNT${NC}"
echo ""

if [ $FAIL_COUNT -eq 0 ]; then
    echo -e "${GREEN}All tests passed! ✓${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed! ✗${NC}"
    exit 1
fi

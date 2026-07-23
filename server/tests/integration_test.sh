#!/bin/bash
# Ditto Cloud E2E Integration Test Script
# Tests the full user journey: register → login → push clip → list → sync → encrypt
# Run against a running server (default: http://localhost:8080)

set -e

BASE_URL="${DITTO_SERVER_URL:-http://localhost:8080}"
PASS=0
FAIL=0

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

log() { echo -e "${YELLOW}[TEST]${NC} $1"; }
pass() { echo -e "${GREEN}[PASS]${NC} $1"; PASS=$((PASS+1)); }
fail() { echo -e "${RED}[FAIL]${NC} $1"; FAIL=$((FAIL+1)); }

check_server() {
    log "Checking server at $BASE_URL..."
    if ! curl -sf "$BASE_URL/health" > /dev/null 2>&1; then
        echo -e "${RED}Server not reachable at $BASE_URL${NC}"
        echo "Start it with: cd server && go run ./cmd/server/"
        exit 1
    fi
    pass "Server is running"
}

test_register() {
    log "Test: Register new user"
    RESP=$(curl -s -X POST "$BASE_URL/api/v1/auth/register" \
        -H "Content-Type: application/json" \
        -d '{"username":"e2e_user_'"$(date +%s)"'","email":"e2e_'"$(date +%s)"'@test.com","password":"testpass123"}')
    
    CODE=$(echo "$RESP" | python3 -c "import sys,json; print(json.load(sys.stdin)['code'])")
    if [ "$CODE" = "0" ]; then
        pass "User registered"
    else
        fail "Registration failed: $RESP"
    fi
}

test_login() {
    log "Test: Login"
    # Register first
    USERNAME="login_test_$(date +%s)"
    curl -s -X POST "$BASE_URL/api/v1/auth/register" \
        -H "Content-Type: application/json" \
        -d "{\"username\":\"$USERNAME\",\"email\":\"$USERNAME@test.com\",\"password\":\"testpass123\"}" > /dev/null

    RESP=$(curl -s -X POST "$BASE_URL/api/v1/auth/login" \
        -H "Content-Type: application/json" \
        -d "{\"username\":\"$USERNAME\",\"password\":\"testpass123\"}")
    
    TOKEN=$(echo "$RESP" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['device_token'])")
    DEVICE_ID=$(echo "$RESP" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['device_id'])")
    
    if [ -n "$TOKEN" ] && [ "$TOKEN" != "null" ]; then
        pass "Login successful, got token"
        export TOKEN DEVICE_ID USERNAME
    else
        fail "Login failed: $RESP"
    fi
}

test_push_clip() {
    log "Test: Push clip via sync"
    RESP=$(curl -s -X POST "$BASE_URL/api/v1/clips/sync" \
        -H "Authorization: Bearer $TOKEN" \
        -H "Content-Type: application/json" \
        -d "{
            \"since\": \"2000-01-01T00:00:00Z\",
            \"device_id\": \"$DEVICE_ID\",
            \"push_clips\": [
                {
                    \"id\": \"clip-e2e-$(date +%s)\",
                    \"description\": \"E2E Test Clip\",
                    \"crc\": 99999999,
                    \"group_id\": \"\",
                    \"short_cut\": 0,
                    \"formats\": [
                        {\"format_type\": 13, \"data\": \"RTJFIFRlc3QgQ2xpcA==\"}
                    ]
                }
            ]
        }")
    
    CODE=$(echo "$RESP" | python3 -c "import sys,json; print(json.load(sys.stdin)['code'])")
    if [ "$CODE" = "0" ]; then
        pass "Clip pushed successfully"
    else
        fail "Push clip failed: $RESP"
    fi
}

test_list_clips() {
    log "Test: List clips"
    RESP=$(curl -s "$BASE_URL/api/v1/clips?page=1&per_page=10" \
        -H "Authorization: Bearer $TOKEN")
    
    TOTAL=$(echo "$RESP" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['total'])")
    if [ "$TOTAL" -ge 1 ]; then
        pass "List clips: found $TOTAL clips"
    else
        fail "List clips failed: $RESP"
    fi
}

test_search_clips() {
    log "Test: Search clips"
    RESP=$(curl -s "$BASE_URL/api/v1/clips?page=1&per_page=10&search=E2E" \
        -H "Authorization: Bearer $TOKEN")
    
    TOTAL=$(echo "$RESP" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['total'])")
    if [ "$TOTAL" -ge 1 ]; then
        pass "Search clips: found $TOTAL clips matching 'E2E'"
    else
        fail "Search clips failed: $RESP"
    fi
}

test_get_clip() {
    log "Test: Get clip detail"
    CLIP_ID=$(curl -s "$BASE_URL/api/v1/clips?page=1&per_page=1" \
        -H "Authorization: Bearer $TOKEN" | \
        python3 -c "import sys,json; print(json.load(sys.stdin)['data']['items'][0]['id'])")
    
    RESP=$(curl -s "$BASE_URL/api/v1/clips/$CLIP_ID" \
        -H "Authorization: Bearer $TOKEN")
    
    DESC=$(echo "$RESP" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['description'])")
    if [ "$DESC" != "null" ] && [ -n "$DESC" ]; then
        pass "Get clip detail: $DESC"
    else
        fail "Get clip failed: $RESP"
    fi
}

test_encryption_setup() {
    log "Test: Setup encryption"
    RESP=$(curl -s -X POST "$BASE_URL/api/v1/encryption/setup" \
        -H "Authorization: Bearer $TOKEN" \
        -H "Content-Type: application/json" \
        -d '{"password_hint": "e2e test"}')
    
    CODE=$(echo "$RESP" | python3 -c "import sys,json; print(json.load(sys.stdin)['code'])")
    SALT=$(echo "$RESP" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['salt'])")
    
    if [ "$CODE" = "0" ] && [ -n "$SALT" ] && [ "$SALT" != "null" ]; then
        pass "Encryption setup, got salt"
    else
        fail "Encryption setup failed: $RESP"
    fi
}

test_get_salt() {
    log "Test: Get encryption salt"
    RESP=$(curl -s "$BASE_URL/api/v1/encryption/salt" \
        -H "Authorization: Bearer $TOKEN")
    
    CODE=$(echo "$RESP" | python3 -c "import sys,json; print(json.load(sys.stdin)['code'])")
    SALT=$(echo "$RESP" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['salt'])")
    
    if [ "$CODE" = "0" ] && [ -n "$SALT" ] && [ "$SALT" != "null" ]; then
        pass "Got encryption salt"
    else
        fail "Get salt failed: $RESP"
    fi
}

test_devices() {
    log "Test: List devices"
    RESP=$(curl -s "$BASE_URL/api/v1/devices" \
        -H "Authorization: Bearer $TOKEN")
    
    DEVICES=$(echo "$RESP" | python3 -c "import sys,json; print(len(json.load(sys.stdin)['data']))")
    if [ "$DEVICES" -ge 1 ]; then
        pass "List devices: found $DEVICES devices"
    else
        fail "List devices failed: $RESP"
    fi
}

test_invalid_token() {
    log "Test: Access with invalid token"
    HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" "$BASE_URL/api/v1/clips" \
        -H "Authorization: Bearer invalid_token_xyz")
    
    if [ "$HTTP_CODE" = "401" ]; then
        pass "Invalid token correctly rejected (401)"
    else
        fail "Invalid token not rejected: HTTP $HTTP_CODE"
    fi
}

test_rate_limit() {
    log "Test: Rate limit (5 failed logins from same IP)"
    for i in $(seq 1 6); do
        HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" -X POST "$BASE_URL/api/v1/auth/login" \
            -H "Content-Type: application/json" \
            -H "X-Forwarded-For: 10.0.0.99" \
            -d '{"username":"nonexistent","password":"wrong"}')
    done
    
    if [ "$HTTP_CODE" = "429" ]; then
        pass "Rate limit triggered (429 after 6 attempts)"
    else
        fail "Rate limit not triggered: HTTP $HTTP_CODE"
    fi
}

test_health() {
    log "Test: Health endpoint"
    RESP=$(curl -s "$BASE_URL/health")
    STATUS=$(echo "$RESP" | python3 -c "import sys,json; print(json.load(sys.stdin)['status'])")
    
    if [ "$STATUS" = "ok" ]; then
        pass "Health check: $RESP"
    else
        fail "Health check failed: $RESP"
    fi
}

# Run all tests
echo -e "\n${YELLOW}========================================${NC}"
echo -e "${YELLOW}  Ditto Cloud E2E Integration Tests${NC}"
echo -e "${YELLOW}========================================${NC}\n"

check_server
test_health
test_register
test_login
test_push_clip
test_list_clips
test_search_clips
test_get_clip
test_encryption_setup
test_get_salt
test_devices
test_invalid_token
test_rate_limit

echo -e "\n${YELLOW}========================================${NC}"
echo -e "${GREEN}Passed: $PASS${NC} | ${RED}Failed: $FAIL${NC}"
echo -e "${YELLOW}========================================${NC}\n"

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
exit 0

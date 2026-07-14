package tests

import (
	"encoding/json"
	"net/http"
	"testing"

	"ditto-cloud-server/tests/testutil"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// TestHealth_Endpoint — GET /health, expect status "ok" with stats
func TestHealth_Endpoint(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)

	// Initial health check (empty database)
	resp, err := http.Get(server.URL + "/health")
	require.NoError(t, err)
	defer resp.Body.Close()

	assert.Equal(t, http.StatusOK, resp.StatusCode)

	var health map[string]interface{}
	err = json.NewDecoder(resp.Body).Decode(&health)
	require.NoError(t, err)

	assert.Equal(t, "ok", health["status"])
	assert.NotNil(t, health["total_users"])
	assert.NotNil(t, health["total_clips"])
	assert.NotNil(t, health["uptime"])

	// Verify counts are 0 initially
	assert.Equal(t, float64(0), health["total_users"])
	assert.Equal(t, float64(0), health["total_clips"])
}

// TestHealth_WithStats — health endpoint reflects created users and clips
func TestHealth_WithStats(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)

	// Create first user (admin)
	admin := testutil.CreateFirstUser(t, server)

	// Create additional users via admin API
	testutil.CreateUserViaAdmin(t, server, admin.Token, "healthuser2", "health2@example.com", "password123")
	testutil.CreateUserViaAdmin(t, server, admin.Token, "healthuser3", "health3@example.com", "password123")

	// Check health after creating users
	resp, err := http.Get(server.URL + "/health")
	require.NoError(t, err)
	defer resp.Body.Close()

	var health map[string]interface{}
	err = json.NewDecoder(resp.Body).Decode(&health)
	require.NoError(t, err)

	assert.Equal(t, "ok", health["status"])
	assert.Equal(t, float64(3), health["total_users"])
}

// TestHealth_NoAuth — health endpoint does not require authentication
func TestHealth_NoAuth(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)

	// Access health without any auth token
	resp, err := http.Get(server.URL + "/health")
	require.NoError(t, err)
	defer resp.Body.Close()

	assert.Equal(t, http.StatusOK, resp.StatusCode)

	var health map[string]interface{}
	err = json.NewDecoder(resp.Body).Decode(&health)
	require.NoError(t, err)
	assert.Equal(t, "ok", health["status"])
}

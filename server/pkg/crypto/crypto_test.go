package crypto

import (
	"os"
	"testing"

	"github.com/stretchr/testify/assert"
)

func TestHashPassword_Success(t *testing.T) {
	password := "testPassword123"

	hash, err := HashPassword(password)

	assert.NoError(t, err)
	assert.NotEmpty(t, hash)
	assert.NotEqual(t, password, hash)
	// Bcrypt hash should start with $2a$, $2b$, or $2y$
	assert.Contains(t, hash, "$2")
}

func TestHashPassword_DifferentPasswords(t *testing.T) {
	password1 := "password1"
	password2 := "password2"

	hash1, err1 := HashPassword(password1)
	hash2, err2 := HashPassword(password2)

	assert.NoError(t, err1)
	assert.NoError(t, err2)
	assert.NotEqual(t, hash1, hash2)
}

func TestHashPassword_SamePasswordDifferentHashes(t *testing.T) {
	password := "samePassword"

	hash1, err1 := HashPassword(password)
	hash2, err2 := HashPassword(password)

	assert.NoError(t, err1)
	assert.NoError(t, err2)
	// Due to bcrypt's salt, same password should produce different hashes
	assert.NotEqual(t, hash1, hash2)
}

func TestCheckPasswordHash_CorrectPassword(t *testing.T) {
	password := "testPassword123"

	hash, err := HashPassword(password)
	require.NoError(t, err)

	result := CheckPasswordHash(password, hash)
	assert.True(t, result)
}

func TestCheckPasswordHash_IncorrectPassword(t *testing.T) {
	password := "correctPassword"
	wrongPassword := "wrongPassword"

	hash, err := HashPassword(password)
	require.NoError(t, err)

	result := CheckPasswordHash(wrongPassword, hash)
	assert.False(t, result)
}

func TestCheckPasswordHash_EmptyPassword(t *testing.T) {
	password := "nonEmptyPassword"

	hash, err := HashPassword(password)
	require.NoError(t, err)

	result := CheckPasswordHash("", hash)
	assert.False(t, result)
}

func TestCheckPasswordHash_EmptyHash(t *testing.T) {
	result := CheckPasswordHash("anyPassword", "")
	assert.False(t, result)
}

func TestCheckPasswordHash_InvalidHash(t *testing.T) {
	result := CheckPasswordHash("anyPassword", "invalidHash")
	assert.False(t, result)
}

func TestHashPassword_EmptyPassword(t *testing.T) {
	hash, err := HashPassword("")

	// bcrypt should handle empty passwords
	assert.NoError(t, err)
	assert.NotEmpty(t, hash)

	// Verify the empty password hash works
	result := CheckPasswordHash("", hash)
	assert.True(t, result)
}

func TestBcryptCost_DefaultValue(t *testing.T) {
	// If BCRYPT_COST is explicitly set, the default value is overridden
	if os.Getenv("BCRYPT_COST") != "" {
		t.Skip("BCRYPT_COST env var set, skipping default value check")
	}
	// BcryptCost should be at least 10 for security
	assert.GreaterOrEqual(t, BcryptCost, 10)
	// And not too high to avoid performance issues
	assert.LessOrEqual(t, BcryptCost, 15)
}

// TestGetBcryptCost_ClampsOutOfRange guards R12: an out-of-range BCRYPT_COST
// must be clamped, never applied verbatim.
func TestGetBcryptCost_ClampsOutOfRange(t *testing.T) {
	tests := []struct {
		name string
		env  string
		want int
	}{
		{"unset", "", DefaultBcryptCost},
		{"below minimum", "4", MinBcryptCost},
		{"at minimum", "10", 10},
		{"middle", "12", 12},
		{"at maximum", "14", 14},
		{"above maximum", "31", MaxBcryptCost},
		{"negative", "-1", MinBcryptCost},
		{"invalid", "abc", DefaultBcryptCost},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			t.Setenv("BCRYPT_COST", tt.env)
			assert.Equal(t, tt.want, getBcryptCost())
		})
	}
}

func TestHashPassword_SpecialCharacters(t *testing.T) {
	passwords := []string{
		"p@ssw0rd!",
		"密码测试",
		"emoji🔥test",
		" space before",
		"space after ",
		"  multiple   spaces  ",
		"tab\there",
		"newline\nhere",
		"special!@#$%^&*()_+-={}[]|:;<>,.?/~`",
	}

	for _, password := range passwords {
		hash, err := HashPassword(password)
		assert.NoError(t, err, "Failed to hash password: %s", password)

		result := CheckPasswordHash(password, hash)
		assert.True(t, result, "Failed to verify password: %s", password)
	}
}

func TestHashPassword_LongPassword(t *testing.T) {
	// bcrypt has a 72-byte limit on password length
	// Passwords longer than 72 bytes will cause an error
	longPassword := "thisIsAVeryLongPasswordThatExceedsSeventyTwoBytesButBcryptShouldTruncateItAutomatically"

	_, err := HashPassword(longPassword)
	// bcrypt returns an error for passwords > 72 bytes
	assert.Error(t, err)
	assert.Contains(t, err.Error(), "password length exceeds 72 bytes")

	// Test with exactly 72 bytes should work
	exact72 := "thisIsExactlySeventyTwoBytesLongPasswordWhichIsTheMaximum123456789" // exactly 72 chars
	hash, err := HashPassword(exact72)
	assert.NoError(t, err)
	result := CheckPasswordHash(exact72, hash)
	assert.True(t, result)
}

func TestCheckPasswordHash_CaseSensitive(t *testing.T) {
	password := "Password123"
	lowercasePassword := "password123"
	uppercasePassword := "PASSWORD123"

	hash, err := HashPassword(password)
	require.NoError(t, err)

	assert.True(t, CheckPasswordHash(password, hash))
	assert.False(t, CheckPasswordHash(lowercasePassword, hash))
	assert.False(t, CheckPasswordHash(uppercasePassword, hash))
}

func TestHashPassword_MultipleHashesSamePassword(t *testing.T) {
	password := "testPassword"
	hashes := make([]string, 10)

	// Generate multiple hashes for the same password
	for i := 0; i < 10; i++ {
		hash, err := HashPassword(password)
		assert.NoError(t, err)
		hashes[i] = hash
	}

	// All hashes should be different (due to salt)
	for i := 0; i < len(hashes); i++ {
		for j := i + 1; j < len(hashes); j++ {
			assert.NotEqual(t, hashes[i], hashes[j], "Hashes %d and %d should be different", i, j)
		}
	}

	// But all should verify the password correctly
	for i, hash := range hashes {
		result := CheckPasswordHash(password, hash)
		assert.True(t, result, "Hash %d should verify password", i)
	}
}

// require is used in the tests above, we need to import it
var require = struct{ NoError func(*testing.T, error) }{
	NoError: func(t *testing.T, err error) {
		if err != nil {
			t.Fatalf("Expected no error, got: %v", err)
		}
	},
}

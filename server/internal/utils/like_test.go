package utils

import (
	"testing"

	"github.com/stretchr/testify/assert"
)

func TestWrapLike_EscapesWildcards(t *testing.T) {
	tests := []struct {
		name string
		term string
		want string
	}{
		{"plain", "hello", "%hello%"},
		{"percent", "%", `%\%%`},
		{"underscore", "_", `%\_%`},
		{"backslash", `\`, `%\\%`},
		{"mixed", `%a_b\`, `%\%a\_b\\%`},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			assert.Equal(t, tt.want, WrapLike(tt.term))
		})
	}
}

// TestWrapLike_BackslashEscapedFirst guards the replacement order: a term that
// already contains a backslash must not have its escape re-escaped, and a
// wildcard right after a backslash must still be escaped.
func TestWrapLike_BackslashEscapedFirst(t *testing.T) {
	assert.Equal(t, `%\\%`, WrapLike(`\`))
	assert.Equal(t, `%\%%`, WrapLike("%"))
	assert.Equal(t, `%a\\\%b%`, WrapLike(`a\%b`))
}

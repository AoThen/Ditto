package utils

import (
	"testing"

	"github.com/stretchr/testify/assert"
)

func TestIsPinyinQuery(t *testing.T) {
	tests := []struct {
		name string
		s    string
		want bool
	}{
		{name: "纯英文字母", s: "hello", want: true},
		{name: "含中文", s: "你好", want: true},
		{name: "空字符串", s: "", want: false},
		{name: "含数字", s: "hello123", want: false},
		{name: "含空格", s: "hello world", want: false},
		{name: "含标点", s: "hello!", want: false},
		{name: "大写字母", s: "HELLO", want: true},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := IsPinyinQuery(tt.s)
			assert.Equal(t, tt.want, got)
		})
	}
}

func TestConvertToPinyin(t *testing.T) {
	tests := []struct {
		name string
		text string
		want string
	}{
		{name: "纯中文", text: "你好", want: "nihao|hao"},
		{name: "纯英文", text: "hello", want: "hello"},
		{name: "混合", text: "hello你好", want: "hellonihao|hao"},
		{name: "空字符串", text: "", want: ""},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := ConvertToPinyin(tt.text)
			assert.Equal(t, tt.want, got)
		})
	}
}
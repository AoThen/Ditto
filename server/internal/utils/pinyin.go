package utils

import (
	"strings"
	"unicode"

	"github.com/mozillazg/go-pinyin"
)

func IsPinyinQuery(s string) bool {
	if s == "" {
		return false
	}
	for _, r := range s {
		if !unicode.IsLetter(r) {
			return false
		}
	}
	return true
}

func ConvertToPinyin(text string) string {
	a := pinyin.NewArgs()
	a.Style = pinyin.Normal
	a.Heteronym = true

	runes := []rune(text)
	var parts []string
	for _, r := range runes {
		if unicode.Is(unicode.Han, r) {
			py := pinyin.SinglePinyin(r, a)
			if len(py) == 0 {
				parts = append(parts, string(r))
			} else {
				parts = append(parts, strings.Join(py, "|"))
			}
		} else {
			parts = append(parts, string(r))
		}
	}
	return strings.Join(parts, "")
}
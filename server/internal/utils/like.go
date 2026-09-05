package utils

import "strings"

// WrapLike returns a bounded LIKE pattern that matches `term` literally anywhere
// in the column value. `%`, `_` and the escape character `\` are escaped, so a
// term like `%` cannot turn the query into a full-table match.
//
// Callers must pair the pattern with an `ESCAPE '\'` clause.
func WrapLike(term string) string {
	return "%" + escapeLikePattern(term) + "%"
}

// escapeLikePattern escapes the SQL LIKE metacharacters. strings.Replacer makes
// a single pass and never rescans what it substituted, so an inserted escape
// character is not escaped again.
func escapeLikePattern(term string) string {
	return strings.NewReplacer(`\`, `\\`, `%`, `\%`, `_`, `\_`).Replace(term)
}

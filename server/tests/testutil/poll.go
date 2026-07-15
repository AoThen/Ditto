package testutil

import (
	"testing"
	"time"
)

// PollUntil blocks until the condition returns true or the timeout expires.
// Replaces brittle time.Sleep calls in tests with deterministic waits.
func PollUntil(t *testing.T, timeout time.Duration, condition func() bool) {
	t.Helper()
	timer := time.NewTimer(timeout)
	defer timer.Stop()
	tick := time.NewTicker(20 * time.Millisecond)
	defer tick.Stop()

	for {
		select {
		case <-timer.C:
			t.Fatalf("timeout %v waiting for condition", timeout)
			return
		case <-tick.C:
			if condition() {
				return
			}
		}
	}
}

package crypto

import (
	"log"
	"os"
	"strconv"

	"golang.org/x/crypto/bcrypt"
)

const (
	DefaultBcryptCost = 12
	MinBcryptCost     = 10
	MaxBcryptCost     = 14
)

// BcryptCost is the bcrypt work factor. Out-of-range values are clamped rather
// than applied: a cost of 4 hashes in microseconds (instantly crackable), while
// a cost of 31 would stall the server on every login.
var BcryptCost = getBcryptCost()

func getBcryptCost() int {
	if costStr := os.Getenv("BCRYPT_COST"); costStr != "" {
		if cost, err := strconv.Atoi(costStr); err == nil {
			switch {
			case cost < MinBcryptCost:
				log.Printf("[Crypto] BCRYPT_COST=%d is below %d, using %d", cost, MinBcryptCost, MinBcryptCost)
				return MinBcryptCost
			case cost > MaxBcryptCost:
				log.Printf("[Crypto] BCRYPT_COST=%d is above %d, using %d", cost, MaxBcryptCost, MaxBcryptCost)
				return MaxBcryptCost
			default:
				return cost
			}
		}
		log.Printf("[Crypto] Invalid BCRYPT_COST=%q, using %d", costStr, DefaultBcryptCost)
	}
	return DefaultBcryptCost
}

func HashPassword(password string) (string, error) {
	bytes, err := bcrypt.GenerateFromPassword([]byte(password), BcryptCost)
	return string(bytes), err
}

func CheckPasswordHash(password, hash string) bool {
	err := bcrypt.CompareHashAndPassword([]byte(hash), []byte(password))
	return err == nil
}

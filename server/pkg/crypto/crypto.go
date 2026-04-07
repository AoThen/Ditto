package crypto

import (
	"os"
	"strconv"

	"golang.org/x/crypto/bcrypt"
)

var BcryptCost = getBcryptCost()

func getBcryptCost() int {
	if costStr := os.Getenv("BCRYPT_COST"); costStr != "" {
		if cost, err := strconv.Atoi(costStr); err == nil {
			return cost
		}
	}
	return 12
}

func HashPassword(password string) (string, error) {
	bytes, err := bcrypt.GenerateFromPassword([]byte(password), BcryptCost)
	return string(bytes), err
}

func CheckPasswordHash(password, hash string) bool {
	err := bcrypt.CompareHashAndPassword([]byte(hash), []byte(password))
	return err == nil
}

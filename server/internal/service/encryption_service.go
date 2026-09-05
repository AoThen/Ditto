package service

import (
	cryptorand "crypto/rand"
	"crypto/subtle"
	"encoding/base64"
	"errors"
	"fmt"
	"regexp"
	"unicode/utf8"

	"ditto-cloud-server/internal/database"
	"ditto-cloud-server/internal/model"
	"ditto-cloud-server/pkg/crypto"

	"golang.org/x/crypto/bcrypt"
	"gorm.io/gorm"
)

type EncryptionService struct{}

func NewEncryptionService() *EncryptionService {
	return &EncryptionService{}
}

type SetupEncryptionRequest struct {
	WrappedDEK       string `json:"wrapped_dek" binding:"required"`
	VerificationHash string `json:"verification_hash" binding:"required"`
	PasswordHint     string `json:"password_hint"`
}

type DisableEncryptionRequest struct {
	VerificationHash string `json:"verification_hash" binding:"required"`
}

type ChangePasswordRequest struct {
	OldVerificationHash string `json:"old_verification_hash" binding:"required"`
	NewSalt             string `json:"new_salt" binding:"required"`
	NewWrappedDEK       string `json:"new_wrapped_dek" binding:"required"`
	NewVerificationHash string `json:"new_verification_hash" binding:"required"`
	NewPasswordHint     string `json:"new_password_hint"`
}

type EncryptionSetupResponse struct {
	Salt              string `json:"salt"`
	EncryptionEnabled bool   `json:"encryption_enabled"`
}

type EncryptionStatusResponse struct {
	Salt              string `json:"salt"`
	EncryptionEnabled bool   `json:"encryption_enabled"`
	PasswordHint      string `json:"password_hint"`
}

type KeyMaterialResponse struct {
	Salt       string `json:"salt"`
	WrappedDEK string `json:"wrapped_dek"`
}

const (
	maxEncSaltLen       = 64
	maxEncWrappedDEKLen = 4096
	// bcrypt truncates its input at 72 bytes, so a longer "hash" can never be
	// wrapped; reject it as invalid input rather than failing with 500 later.
	maxEncVerificationLen = 72
	maxEncPasswordHintLen = 200
)

var (
	ErrEncryptionAlreadyEnabled = errors.New("加密已启用，无需重复设置")
	ErrEncryptionNotSetup       = errors.New("请先设置端到端加密密码")
	ErrInvalidVerificationHash  = errors.New("旧密码验证失败")
	// ErrInvalidEncPayload marks client-supplied key material that is malformed
	// or oversized; handlers map it to HTTP 400 instead of 500.
	ErrInvalidEncPayload = errors.New("加密参数格式无效")
)

func validEncLen(field string, value []byte, max int) error {
	if len(value) == 0 || len(value) > max {
		return fmt.Errorf("%w: %s 无效或超出长度限制", ErrInvalidEncPayload, field)
	}
	return nil
}

// validateEncPayload bounds the client-supplied blobs so a compromised or buggy
// client cannot bloat the encryption_settings table.
func validateEncPayload(wrappedDEK, salt, verificationHash []byte, passwordHint string) error {
	if err := validEncLen("wrapped_dek", wrappedDEK, maxEncWrappedDEKLen); err != nil {
		return err
	}
	if salt != nil {
		if err := validEncLen("salt", salt, maxEncSaltLen); err != nil {
			return err
		}
	}
	if err := validEncLen("verification_hash", verificationHash, maxEncVerificationLen); err != nil {
		return err
	}
	if utf8.RuneCountInString(passwordHint) > maxEncPasswordHintLen {
		return fmt.Errorf("%w: password_hint 超长", ErrInvalidEncPayload)
	}
	if hasControlChar(passwordHint) {
		return fmt.Errorf("%w: password_hint 含控制字符", ErrInvalidEncPayload)
	}
	return nil
}

// hasControlChar rejects NUL / DEL and other control characters in the hint.
// The hint is displayed back to the user, so invisible characters could be
// used to hide or inject content.
func hasControlChar(s string) bool {
	for _, r := range s {
		if r < 0x20 || r == 0x7F {
			return true
		}
	}
	return false
}

// hashVerificationHash wraps the client-computed SHA-256 verification hash in
// bcrypt. Keeping only the raw digest would let anyone who reads the database
// call the disable / change-password endpoints with it (pass-the-hash).
func hashVerificationHash(raw []byte) ([]byte, error) {
	hashed, err := crypto.HashPassword(string(raw))
	if err != nil {
		return nil, err
	}
	return []byte(hashed), nil
}

// bcryptHashPattern is the full 60-byte shape of a bcrypt digest. Matching the
// whole form matters: a raw SHA-256 digest can happen to start with "$2", and
// misclassifying it would lock a legacy user out of disable/change-password.
var bcryptHashPattern = regexp.MustCompile(`^\$2[aby]?\$\d{2}\$[./A-Za-z0-9]{53}$`)

func isBcryptHash(value []byte) bool {
	return bcryptHashPattern.Match(value)
}

// checkVerificationHash compares a submitted verification hash with the stored
// one. The second return value reports whether the stored value is still a
// legacy raw digest that the caller should upgrade on the next write.
func checkVerificationHash(stored, submitted []byte) (matched bool, legacy bool) {
	if isBcryptHash(stored) {
		return bcrypt.CompareHashAndPassword(stored, submitted) == nil, false
	}
	return subtle.ConstantTimeCompare(stored, submitted) == 1, true
}

func (s *EncryptionService) ensureSaltRecord(userID uint) (*model.EncryptionSettings, error) {
	var settings model.EncryptionSettings

	database.DB.Where(model.EncryptionSettings{UserID: userID}).
		FirstOrInit(&settings)

	if settings.ID == 0 {
		// Only generate salt when creating a new record
		salt := make([]byte, 32)
		if _, err := cryptorand.Read(salt); err != nil {
			return nil, err
		}
		settings.UserID = userID
		settings.Salt = salt
		settings.WrappedDEK = make([]byte, 0)
		settings.VerificationHash = make([]byte, 0)

		if err := database.DB.Create(&settings).Error; err != nil {
			return nil, err
		}
	}

	return &settings, nil
}

func (s *EncryptionService) SetupEncryption(userID uint, req *SetupEncryptionRequest) (*EncryptionSetupResponse, error) {
	settings, err := s.ensureSaltRecord(userID)
	if err != nil {
		return nil, err
	}
	if settings.Enabled {
		return nil, ErrEncryptionAlreadyEnabled
	}

	wrappedDEK, err := base64.StdEncoding.DecodeString(req.WrappedDEK)
	if err != nil {
		return nil, fmt.Errorf("%w: wrapped_dek 格式无效", ErrInvalidEncPayload)
	}
	verificationHash, err := base64.StdEncoding.DecodeString(req.VerificationHash)
	if err != nil {
		return nil, fmt.Errorf("%w: verification_hash 格式无效", ErrInvalidEncPayload)
	}
	if err := validateEncPayload(wrappedDEK, nil, verificationHash, req.PasswordHint); err != nil {
		return nil, err
	}
	// Version 2 persists the bcrypt-wrapped hash, not the raw client digest.
	wrappedHash, err := hashVerificationHash(verificationHash)
	if err != nil {
		return nil, err
	}

	settings.WrappedDEK = wrappedDEK
	settings.VerificationHash = wrappedHash
	settings.PasswordHint = req.PasswordHint
	settings.Enabled = true
	settings.Version = 2

	if err := database.DB.Save(&settings).Error; err != nil {
		return nil, err
	}

	return &EncryptionSetupResponse{
		Salt:              base64.StdEncoding.EncodeToString(settings.Salt),
		EncryptionEnabled: true,
	}, nil
}

func (s *EncryptionService) GetEncryptionSalt(userID uint) (*EncryptionStatusResponse, error) {
	settings, err := s.ensureSaltRecord(userID)
	if err != nil {
		return nil, err
	}

	return &EncryptionStatusResponse{
		Salt:              base64.StdEncoding.EncodeToString(settings.Salt),
		EncryptionEnabled: settings.Enabled,
		PasswordHint:      settings.PasswordHint,
	}, nil
}

func (s *EncryptionService) GetKeyMaterial(userID uint) (*KeyMaterialResponse, error) {
	var settings model.EncryptionSettings
	if err := database.DB.Where("user_id = ?", userID).First(&settings).Error; err != nil {
		if errors.Is(err, gorm.ErrRecordNotFound) {
			return nil, ErrEncryptionNotSetup
		}
		return nil, err
	}
	if !settings.Enabled {
		return nil, ErrEncryptionNotSetup
	}

	return &KeyMaterialResponse{
		Salt:       base64.StdEncoding.EncodeToString(settings.Salt),
		WrappedDEK: base64.StdEncoding.EncodeToString(settings.WrappedDEK),
	}, nil
}

func (s *EncryptionService) DisableEncryption(userID uint, req *DisableEncryptionRequest) error {
	var settings model.EncryptionSettings
	if err := database.DB.Where("user_id = ?", userID).First(&settings).Error; err != nil {
		if errors.Is(err, gorm.ErrRecordNotFound) {
			return ErrEncryptionNotSetup
		}
		return err
	}
	if !settings.Enabled {
		return ErrEncryptionNotSetup
	}

	newHash, err := base64.StdEncoding.DecodeString(req.VerificationHash)
	if err != nil {
		return fmt.Errorf("%w: verification_hash 格式无效", ErrInvalidEncPayload)
	}
	if err := validEncLen("verification_hash", newHash, maxEncVerificationLen); err != nil {
		return err
	}
	matched, legacy := checkVerificationHash(settings.VerificationHash, newHash)
	if !matched {
		return ErrInvalidVerificationHash
	}
	// Legacy rows still hold the raw digest; upgrade them on the next write so a
	// database leak stops being pass-the-hash usable.
	if legacy {
		if wrapped, err := hashVerificationHash(newHash); err == nil {
			settings.VerificationHash = wrapped
		}
	}

	settings.Enabled = false
	return database.DB.Save(&settings).Error
}

func (s *EncryptionService) ChangeEncryptionPassword(userID uint, req *ChangePasswordRequest) (*EncryptionSetupResponse, error) {
	var settings model.EncryptionSettings
	if err := database.DB.Where("user_id = ?", userID).First(&settings).Error; err != nil {
		if errors.Is(err, gorm.ErrRecordNotFound) {
			return nil, ErrEncryptionNotSetup
		}
		return nil, err
	}
	if !settings.Enabled {
		return nil, ErrEncryptionNotSetup
	}

	oldHash, err := base64.StdEncoding.DecodeString(req.OldVerificationHash)
	if err != nil {
		return nil, fmt.Errorf("%w: old_verification_hash 格式无效", ErrInvalidEncPayload)
	}
	if err := validEncLen("old_verification_hash", oldHash, maxEncVerificationLen); err != nil {
		return nil, err
	}
	matched, _ := checkVerificationHash(settings.VerificationHash, oldHash)
	if !matched {
		return nil, ErrInvalidVerificationHash
	}

	newSalt, err := base64.StdEncoding.DecodeString(req.NewSalt)
	if err != nil {
		return nil, fmt.Errorf("%w: new_salt 格式无效", ErrInvalidEncPayload)
	}
	newWrappedDEK, err := base64.StdEncoding.DecodeString(req.NewWrappedDEK)
	if err != nil {
		return nil, fmt.Errorf("%w: new_wrapped_dek 格式无效", ErrInvalidEncPayload)
	}
	newVerificationHash, err := base64.StdEncoding.DecodeString(req.NewVerificationHash)
	if err != nil {
		return nil, fmt.Errorf("%w: new_verification_hash 格式无效", ErrInvalidEncPayload)
	}
	if err := validateEncPayload(newWrappedDEK, newSalt, newVerificationHash, req.NewPasswordHint); err != nil {
		return nil, err
	}
	wrappedHash, err := hashVerificationHash(newVerificationHash)
	if err != nil {
		return nil, err
	}

	settings.Salt = newSalt
	settings.WrappedDEK = newWrappedDEK
	settings.VerificationHash = wrappedHash
	settings.PasswordHint = req.NewPasswordHint

	if err := database.DB.Save(&settings).Error; err != nil {
		return nil, err
	}

	return &EncryptionSetupResponse{
		Salt:              base64.StdEncoding.EncodeToString(settings.Salt),
		EncryptionEnabled: true,
	}, nil
}
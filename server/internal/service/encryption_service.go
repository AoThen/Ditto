package service

import (
	"crypto/rand"
	"encoding/base64"
	"errors"

	"ditto-cloud-server/internal/database"
	"ditto-cloud-server/internal/model"

	"gorm.io/gorm"
)

type EncryptionService struct{}

func NewEncryptionService() *EncryptionService {
	return &EncryptionService{}
}

// SetupEncryptionRequest represents the encryption setup request
type SetupEncryptionRequest struct {
	PasswordHint string `json:"password_hint"`
}

// EncryptionSetupResponse represents the encryption setup response
type EncryptionSetupResponse struct {
	Salt                string `json:"salt"`
	EncryptionEnabled   bool   `json:"encryption_enabled"`
}

// EncryptionStatusResponse represents the encryption status response
type EncryptionStatusResponse struct {
	Salt              string `json:"salt"`
	EncryptionEnabled bool   `json:"encryption_enabled"`
	PasswordHint      string `json:"password_hint"`
}

var (
	ErrEncryptionAlreadyEnabled = errors.New("加密已启用，无需重复设置")
	ErrEncryptionNotSetup       = errors.New("请先设置端到端加密密码")
)

// SetupEncryption generates a random salt and enables encryption for the user
func (s *EncryptionService) SetupEncryption(userID uint, req *SetupEncryptionRequest) (*EncryptionSetupResponse, error) {
	// Check if encryption is already enabled
	var existing model.EncryptionSettings
	if err := database.DB.Where("user_id = ?", userID).First(&existing).Error; err == nil {
		if existing.Enabled {
			return nil, ErrEncryptionAlreadyEnabled
		}
		// Update existing disabled settings
		salt := make([]byte, 32)
		if _, err := rand.Read(salt); err != nil {
			return nil, err
		}

		existing.Salt = salt
		existing.PasswordHint = req.PasswordHint
		existing.Enabled = true
		if err := database.DB.Save(&existing).Error; err != nil {
			return nil, err
		}

		return &EncryptionSetupResponse{
			Salt:              base64.StdEncoding.EncodeToString(salt),
			EncryptionEnabled: true,
		}, nil
	}

	// Create new encryption settings
	salt := make([]byte, 32)
	if _, err := rand.Read(salt); err != nil {
		return nil, err
	}

	settings := model.EncryptionSettings{
		UserID:       userID,
		Salt:         salt,
		PasswordHint: req.PasswordHint,
		Enabled:      true,
	}

	if err := database.DB.Create(&settings).Error; err != nil {
		return nil, err
	}

	return &EncryptionSetupResponse{
		Salt:              base64.StdEncoding.EncodeToString(salt),
		EncryptionEnabled: true,
	}, nil
}

// GetEncryptionSalt returns the user's encryption salt
func (s *EncryptionService) GetEncryptionSalt(userID uint) (*EncryptionStatusResponse, error) {
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

	return &EncryptionStatusResponse{
		Salt:              base64.StdEncoding.EncodeToString(settings.Salt),
		EncryptionEnabled: settings.Enabled,
		PasswordHint:      settings.PasswordHint,
	}, nil
}

// DisableEncryption disables end-to-end encryption for the user
func (s *EncryptionService) DisableEncryption(userID uint) error {
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

	// Disable encryption but keep salt for potential re-enable
	settings.Enabled = false
	return database.DB.Save(&settings).Error
}

// ChangeEncryptionPassword changes the password hint for the user's encryption
// Note: The actual encryption key is derived client-side via PBKDF2(password, salt).
// The server only stores the salt and a hint. Changing the password means:
// 1. Client must re-encrypt all data with the new key (not handled here).
// 2. This endpoint only updates the hint. The actual password change requires
//    the client to re-derive the key and re-upload encrypted data.
//
// For a proper password change with data re-encryption, the client should:
// 1. Decrypt all data with old password
// 2. Call this endpoint to update hint
// 3. Re-encrypt with new password and re-push all clips
func (s *EncryptionService) ChangeEncryptionPassword(userID uint, newPasswordHint string) error {
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

	settings.PasswordHint = newPasswordHint
	return database.DB.Save(&settings).Error
}

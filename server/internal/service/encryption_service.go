package service

import (
	"crypto/rand"
	"crypto/subtle"
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

var (
	ErrEncryptionAlreadyEnabled = errors.New("加密已启用，无需重复设置")
	ErrEncryptionNotSetup       = errors.New("请先设置端到端加密密码")
	ErrInvalidVerificationHash  = errors.New("旧密码验证失败")
)

func (s *EncryptionService) ensureSaltRecord(userID uint) (*model.EncryptionSettings, error) {
	var settings model.EncryptionSettings
	if err := database.DB.Where("user_id = ?", userID).First(&settings).Error; err != nil {
		if errors.Is(err, gorm.ErrRecordNotFound) {
			salt := make([]byte, 32)
			if _, err := rand.Read(salt); err != nil {
				return nil, err
			}
			settings = model.EncryptionSettings{
				UserID:           userID,
				Salt:             salt,
				WrappedDEK:       make([]byte, 0),
				VerificationHash: make([]byte, 0),
			}
			if err := database.DB.Create(&settings).Error; err != nil {
				return nil, err
			}
		} else {
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
		return nil, errors.New("wrapped_dek 格式无效")
	}
	verificationHash, err := base64.StdEncoding.DecodeString(req.VerificationHash)
	if err != nil {
		return nil, errors.New("verification_hash 格式无效")
	}

	settings.WrappedDEK = wrappedDEK
	settings.VerificationHash = verificationHash
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
		return errors.New("verification_hash 格式无效")
	}
	if subtle.ConstantTimeCompare(newHash, settings.VerificationHash) != 1 {
		return ErrInvalidVerificationHash
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
		return nil, errors.New("old_verification_hash 格式无效")
	}
	if subtle.ConstantTimeCompare(oldHash, settings.VerificationHash) != 1 {
		return nil, ErrInvalidVerificationHash
	}

	newSalt, err := base64.StdEncoding.DecodeString(req.NewSalt)
	if err != nil {
		return nil, errors.New("new_salt 格式无效")
	}
	newWrappedDEK, err := base64.StdEncoding.DecodeString(req.NewWrappedDEK)
	if err != nil {
		return nil, errors.New("new_wrapped_dek 格式无效")
	}
	newVerificationHash, err := base64.StdEncoding.DecodeString(req.NewVerificationHash)
	if err != nil {
		return nil, errors.New("new_verification_hash 格式无效")
	}

	settings.Salt = newSalt
	settings.WrappedDEK = newWrappedDEK
	settings.VerificationHash = newVerificationHash
	settings.PasswordHint = req.NewPasswordHint

	if err := database.DB.Save(&settings).Error; err != nil {
		return nil, err
	}

	return &EncryptionSetupResponse{
		Salt:              base64.StdEncoding.EncodeToString(settings.Salt),
		EncryptionEnabled: true,
	}, nil
}
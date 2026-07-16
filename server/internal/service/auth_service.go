package service

import (
	"encoding/base64"
	"errors"
	"fmt"
	"regexp"
	"strings"
	"time"

	"ditto-cloud-server/internal/config"
	"ditto-cloud-server/internal/database"
	"ditto-cloud-server/internal/model"
	"ditto-cloud-server/pkg/crypto"

	"github.com/golang-jwt/jwt/v5"
	"gorm.io/gorm"
)

type AuthService struct {
	cfg *config.Config
}

func NewAuthService(cfg *config.Config) *AuthService {
	return &AuthService{cfg: cfg}
}

// GetTokenExpiryAccess returns the access token lifetime from config.
func (s *AuthService) GetTokenExpiryAccess() time.Duration {
	return s.cfg.TokenExpiryAccess
}

// GetTokenExpiryRefresh returns the refresh token lifetime from config.
func (s *AuthService) GetTokenExpiryRefresh() time.Duration {
	return s.cfg.TokenExpiryRefresh
}

// IsCookieSecure returns whether cookies should use the Secure flag.
func (s *AuthService) IsCookieSecure() bool {
	return s.cfg.CookieSecure
}

type RegisterRequest struct {
	Username string `json:"username" binding:"required,min=3,max=32"`
	Email    string `json:"email" binding:"required,email"`
	Password string `json:"password" binding:"required,min=6,max=64"`
}

type RegisterResponse struct {
	UserID uint `json:"user_id"`
}

type LoginRequest struct {
	Username string `json:"username" binding:"required"`
	Password string `json:"password" binding:"required"`
}

type LoginResponse struct {
	UserID       uint   `json:"user_id"`
	DeviceToken  string `json:"device_token"`
	RefreshToken string `json:"refresh_token"`
	DeviceID     string `json:"device_id"`
	Role         string `json:"role"`
}

var (
	ErrUsernameExists      = errors.New("用户名已存在")
	ErrEmailExists         = errors.New("邮箱已被注册")
	ErrInvalidCreds        = errors.New("用户名或密码错误")
	ErrUserLocked          = errors.New("账号已锁定")
	ErrTooManyAttempts     = errors.New("尝试次数过多")
	ErrRegistrationClosed  = errors.New("注册已关闭，请联系管理员")
)

func (s *AuthService) Register(req *RegisterRequest) (*RegisterResponse, error) {
	var resp *RegisterResponse

	err := database.DB.Transaction(func(tx *gorm.DB) error {
		var existing model.User

		if err := tx.Where("username = ?", req.Username).First(&existing).Error; err == nil {
			return ErrUsernameExists
		}
		if err := tx.Where("email = ?", req.Email).First(&existing).Error; err == nil {
			return ErrEmailExists
		}

		hashedPassword, err := crypto.HashPassword(req.Password)
		if err != nil {
			return err
		}

		var count int64
		tx.Model(&model.User{}).Count(&count)
		if count > 0 {
			return ErrRegistrationClosed
		}

		user := model.User{
			Username:     req.Username,
			Email:        req.Email,
			PasswordHash: hashedPassword,
			Role:         "admin",
			IsActive:     true,
		}
		if err := tx.Create(&user).Error; err != nil {
			return err
		}

		resp = &RegisterResponse{UserID: user.ID}
		return nil
	})
	if err != nil {
		return nil, err
	}
	return resp, nil
}

func (s *AuthService) Login(req *LoginRequest, deviceName string) (*LoginResponse, error) {
	var user model.User
	if err := database.DB.Where("username = ?", req.Username).First(&user).Error; err != nil {
		if errors.Is(err, gorm.ErrRecordNotFound) {
			return nil, ErrInvalidCreds
		}
		return nil, err
	}

	// Check password
	if !crypto.CheckPasswordHash(req.Password, user.PasswordHash) {
		return nil, ErrInvalidCreds
	}

	// Check if user is active
	if !user.IsActive {
		return nil, ErrInvalidCreds
	}

	// Generate device ID
	deviceID, err := generateDeviceID(user.ID, deviceName)
	if err != nil {
		return nil, err
	}

	// Register or update device
	device := model.Device{
		ID:         deviceID,
		UserID:     user.ID,
		DeviceName: deviceName,
		LastSeen:   time.Now(),
	}
	if err := database.DB.Where("id = ?", deviceID).
		Assign(model.Device{LastSeen: time.Now()}).
		FirstOrCreate(&device).Error; err != nil {
		return nil, err
	}

	// Generate JWT token (access token + refresh token)
	accessToken, err := s.generateToken(user.ID, deviceID, s.cfg.TokenExpiryAccess, "access")
	if err != nil {
		return nil, err
	}
	refreshToken, err := s.generateToken(user.ID, deviceID, s.cfg.TokenExpiryRefresh, "refresh")
	if err != nil {
		return nil, err
	}

	return &LoginResponse{
		UserID:       user.ID,
		DeviceToken:  accessToken,
		RefreshToken: refreshToken,
		DeviceID:     deviceID,
		Role:         user.Role,
	}, nil
}

func (s *AuthService) RefreshDeviceToken(userID uint, deviceID string) (string, string, error) {
	// C3 FIX: userID comes from the already-verified middleware context.
	// No need to re-parse the old token (avoids ParseUnverified security risk).
	// Device ID is also already verified by the middleware.

	// Verify user still exists
	var user model.User
	if err := database.DB.Where("id = ?", userID).First(&user).Error; err != nil {
		return "", "", errors.New("用户不存在")
	}

	var device model.Device
	if err := database.DB.Where("id = ?", deviceID).First(&device).Error; err != nil {
		return "", "", errors.New("设备不存在")
	}

	device.TokenVersion++
	if err := database.DB.Save(&device).Error; err != nil {
		return "", "", errors.New("刷新令牌失败")
	}

	// Generate new tokens
	accessToken, err := s.generateToken(userID, deviceID, s.cfg.TokenExpiryAccess, "access", device.TokenVersion)
	if err != nil {
		return "", "", err
	}
	refreshToken, err := s.generateToken(userID, deviceID, s.cfg.TokenExpiryRefresh, "refresh", device.TokenVersion)
	if err != nil {
		return "", "", err
	}

	return accessToken, refreshToken, nil
}

func (s *AuthService) Logout(userID uint, deviceID string) error {
	var device model.Device
	if err := database.DB.Where("id = ? AND user_id = ?", deviceID, userID).First(&device).Error; err != nil {
		return err
	}
	device.TokenVersion++
	return database.DB.Save(&device).Error
}

func (s *AuthService) generateToken(userID uint, deviceID string, expiry time.Duration, tokenType string, tokenVersion ...int) (string, error) {
	ver := 0
	if len(tokenVersion) > 0 {
		ver = tokenVersion[0]
	}
	token := jwt.NewWithClaims(jwt.SigningMethodHS256, jwt.MapClaims{
		"user_id":       userID,
		"device_id":     deviceID,
		"token_version": ver,
		"token_type":    tokenType,
		"exp":           time.Now().Add(expiry).Unix(),
		"iat":           time.Now().Unix(),
	})

	return token.SignedString([]byte(s.cfg.JWTSecret))
}

func sanitizeDeviceName(name string) (string, error) {
	name = strings.TrimSpace(name)
	if name == "" {
		return "", errors.New("设备名称不能为空")
	}
	reg := regexp.MustCompile(`[^\p{L}\p{N}_.-]`)
	safeName := reg.ReplaceAllString(name, "_")
	if len(safeName) > 64 {
		safeName = safeName[:64]
	}
	return safeName, nil
}

func generateDeviceID(userID uint, deviceName string) (string, error) {
	safeName, err := sanitizeDeviceName(deviceName)
	if err != nil {
		return "", err
	}
	encodedName := base64.RawURLEncoding.EncodeToString([]byte(safeName))
	return fmt.Sprintf("dev-%d-%s", userID, encodedName), nil
}

// IsFirstUser returns true if no users exist in the database.
func IsFirstUser() bool {
	var count int64
	database.DB.Model(&model.User{}).Count(&count)
	return count == 0
}

// RegisterAllowed returns true if new user registration is currently permitted.
// Registration is only allowed when there are no users yet (first user becomes admin).
func RegisterAllowed() bool {
	return IsFirstUser()
}

// ResetPasswordByUsername resets a user's password by username (admin operation).
func ResetPasswordByUsername(username, newPassword string) error {
	var user model.User
	if err := database.DB.Where("username = ?", username).First(&user).Error; err != nil {
		return fmt.Errorf("用户不存在")
	}
	hashedPassword, err := crypto.HashPassword(newPassword)
	if err != nil {
		return err
	}
	return database.DB.Model(&user).Update("password_hash", hashedPassword).Error
}

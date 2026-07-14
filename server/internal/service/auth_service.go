package service

import (
	"errors"
	"fmt"
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
}

var (
	ErrUsernameExists  = errors.New("用户名已存在")
	ErrEmailExists     = errors.New("邮箱已被注册")
	ErrInvalidCreds    = errors.New("用户名或密码错误")
	ErrUserLocked      = errors.New("账号已锁定")
	ErrTooManyAttempts = errors.New("尝试次数过多")
)

func (s *AuthService) Register(req *RegisterRequest) (*RegisterResponse, error) {
	// Check if username exists
	var existing model.User
	if err := database.DB.Where("username = ?", req.Username).First(&existing).Error; err == nil {
		return nil, ErrUsernameExists
	}

	// Check if email exists
	if err := database.DB.Where("email = ?", req.Email).First(&existing).Error; err == nil {
		return nil, ErrEmailExists
	}

	// Hash password
	hashedPassword, err := crypto.HashPassword(req.Password)
	if err != nil {
		return nil, err
	}

	user := model.User{
		Username:     req.Username,
		Email:        req.Email,
		PasswordHash: hashedPassword,
	}

	if err := database.DB.Create(&user).Error; err != nil {
		return nil, err
	}

	return &RegisterResponse{UserID: user.ID}, nil
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

	// Generate device ID
	deviceID := generateDeviceID(user.ID, deviceName)

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
	accessToken, err := s.generateToken(user.ID, deviceID, s.cfg.TokenExpiryAccess)
	if err != nil {
		return nil, err
	}
	refreshToken, err := s.generateToken(user.ID, deviceID, s.cfg.TokenExpiryRefresh)
	if err != nil {
		return nil, err
	}

	return &LoginResponse{
		UserID:       user.ID,
		DeviceToken:  accessToken,
		RefreshToken: refreshToken,
		DeviceID:     deviceID,
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
	accessToken, err := s.generateToken(userID, deviceID, s.cfg.TokenExpiryAccess, device.TokenVersion)
	if err != nil {
		return "", "", err
	}
	refreshToken, err := s.generateToken(userID, deviceID, s.cfg.TokenExpiryRefresh, device.TokenVersion)
	if err != nil {
		return "", "", err
	}

	return accessToken, refreshToken, nil
}

func (s *AuthService) generateToken(userID uint, deviceID string, expiry time.Duration, tokenVersion ...int) (string, error) {
	ver := 0
	if len(tokenVersion) > 0 {
		ver = tokenVersion[0]
	}
	token := jwt.NewWithClaims(jwt.SigningMethodHS256, jwt.MapClaims{
		"user_id":       userID,
		"device_id":     deviceID,
		"token_version": ver,
		"exp":           time.Now().Add(expiry).Unix(),
		"iat":           time.Now().Unix(),
	})

	return token.SignedString([]byte(s.cfg.JWTSecret))
}

func generateDeviceID(userID uint, deviceName string) string {
	// Simple deterministic device ID based on user and device name
	// In production, this should be generated by the client
	return fmt.Sprintf("dev-%d-%s", userID, deviceName)
}

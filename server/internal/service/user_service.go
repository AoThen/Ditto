package service

import (
	"errors"

	"ditto-cloud-server/internal/database"
	"ditto-cloud-server/internal/model"
	"ditto-cloud-server/pkg/crypto"
)

type UserService struct{}

func NewUserService() *UserService {
	return &UserService{}
}

func (s *UserService) CreateUser(username, email, password string) (*model.User, error) {
	var existing model.User
	if err := database.DB.Where("username = ?", username).First(&existing).Error; err == nil {
		return nil, errors.New("用户名已存在")
	}

	if err := database.DB.Where("email = ?", email).First(&existing).Error; err == nil {
		return nil, errors.New("邮箱已被注册")
	}

	hashedPassword, err := crypto.HashPassword(password)
	if err != nil {
		return nil, err
	}

	role := "user"

	user := model.User{
		Username:     username,
		Email:        email,
		PasswordHash: hashedPassword,
		Role:         role,
		IsActive:     true,
	}
	if err := database.DB.Create(&user).Error; err != nil {
		return nil, err
	}
	return &user, nil
}

func (s *UserService) ListUsers(search string, page, pageSize int) ([]model.User, int64, error) {
	var users []model.User
	var total int64

	query := database.DB.Model(&model.User{})
	if search != "" {
		like := "%" + search + "%"
		query = query.Where("username LIKE ? OR email LIKE ?", like, like)
	}
	query.Count(&total)

	offset := (page - 1) * pageSize
	dbQuery := database.DB.Model(&model.User{}).Select("id, username, email, role, is_active, created_at, updated_at")
	if search != "" {
		like := "%" + search + "%"
		dbQuery = dbQuery.Where("username LIKE ? OR email LIKE ?", like, like)
	}
	if err := dbQuery.Order("id DESC").Offset(offset).Limit(pageSize).Find(&users).Error; err != nil {
		return nil, 0, err
	}

	return users, total, nil
}

func (s *UserService) GetUser(id uint) (*model.User, error) {
	var user model.User
	if err := database.DB.First(&user, id).Error; err != nil {
		return nil, err
	}
	return &user, nil
}

func (s *UserService) GetDeviceCount(userID uint) (int64, error) {
	var count int64
	if err := database.DB.Model(&model.Device{}).Where("user_id = ?", userID).Count(&count).Error; err != nil {
		return 0, err
	}
	return count, nil
}

func (s *UserService) CountUsersByRole(role string) (int64, error) {
	var count int64
	if err := database.DB.Model(&model.User{}).Where("role = ?", role).Count(&count).Error; err != nil {
		return 0, err
	}
	return count, nil
}

func (s *UserService) CheckUserExists(id uint) error {
	var user model.User
	return database.DB.First(&user, id).Error
}

func (s *UserService) CheckEmailTakenByOther(email string, excludeUserID uint) error {
	var existing model.User
	if err := database.DB.Where("email = ? AND id != ?", email, excludeUserID).First(&existing).Error; err == nil {
		return errors.New("邮箱已被其他用户使用")
	}
	return nil
}

func (s *UserService) UpdateUser(id uint, updates map[string]interface{}) error {
	return database.DB.Model(&model.User{}).Where("id = ?", id).Updates(updates).Error
}

func (s *UserService) DeleteUser(id uint) error {
	var user model.User
	if err := database.DB.First(&user, id).Error; err != nil {
		return err
	}

	tx := database.DB.Begin()
	defer func() {
		if r := recover(); r != nil {
			tx.Rollback()
			panic(r)
		}
	}()

	// Re-query user inside transaction to avoid TOCTOU
	var txUser model.User
	if err := tx.First(&txUser, id).Error; err != nil {
		tx.Rollback()
		return err
	}
	var adminCount int64
	if err := tx.Model(&model.User{}).Where("role = ?", "admin").Count(&adminCount).Error; err != nil {
		tx.Rollback()
		return err
	}
	if txUser.Role == "admin" && adminCount <= 1 {
		tx.Rollback()
		return errors.New("无法删除最后一个管理员账号")
	}

	var clipIDs []string
	if err := tx.Model(&model.Clip{}).Where("user_id = ?", txUser.ID).Pluck("id", &clipIDs).Error; err != nil {
		tx.Rollback()
		return err
	}

	if len(clipIDs) > 0 {
		if err := tx.Where("clip_id IN ?", clipIDs).Delete(&model.ClipFormat{}).Error; err != nil {
			tx.Rollback()
			return err
		}
		if err := tx.Delete(&model.Clip{}, clipIDs).Error; err != nil {
			tx.Rollback()
			return err
		}
	}

	if err := tx.Where("user_id = ?", txUser.ID).Delete(&model.Device{}).Error; err != nil {
		tx.Rollback()
		return err
	}
	if err := tx.Where("user_id = ?", txUser.ID).Delete(&model.Group{}).Error; err != nil {
		tx.Rollback()
		return err
	}
	if err := tx.Where("user_id = ?", txUser.ID).Delete(&model.SyncLog{}).Error; err != nil {
		tx.Rollback()
		return err
	}
	if err := tx.Where("user_id = ?", txUser.ID).Delete(&model.EncryptionSettings{}).Error; err != nil {
		tx.Rollback()
		return err
	}
	if err := tx.Delete(&txUser).Error; err != nil {
		tx.Rollback()
		return err
	}

	if err := tx.Commit().Error; err != nil {
		tx.Rollback()
		return err
	}
	return nil
}

func (s *UserService) ResetPassword(userID uint, newPassword string) error {
	hashedPassword, err := crypto.HashPassword(newPassword)
	if err != nil {
		return err
	}
	return database.DB.Model(&model.User{}).Where("id = ?", userID).Update("password_hash", hashedPassword).Error
}
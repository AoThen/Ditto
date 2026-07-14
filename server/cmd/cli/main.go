package main

import (
	"bufio"
	"fmt"
	"log"
	"os"
	"strings"

	"ditto-cloud-server/internal/database"
	"ditto-cloud-server/internal/model"
	"ditto-cloud-server/internal/service"
	"ditto-cloud-server/pkg/crypto"

	"github.com/joho/godotenv"
)

func main() {
	_ = godotenv.Load()

	dbPath := os.Getenv("DATABASE_PATH")
	if dbPath == "" {
		dbPath = "./data/ditto_cloud.db"
	}

	if err := database.Init(dbPath); err != nil {
		log.Fatalf("Failed to initialize database: %v", err)
	}

	if len(os.Args) < 2 {
		printUsage()
		return
	}

	switch os.Args[1] {
	case "create-admin":
		createAdmin()
	case "reset-password":
		resetPassword()
	case "list-users":
		listUsers()
	default:
		printUsage()
	}
}

func printUsage() {
	fmt.Println("Ditto Cloud CLI 管理工具")
	fmt.Println("用法:")
	fmt.Println("  go run cmd/cli/main.go create-admin   创建管理员账号")
	fmt.Println("  go run cmd/cli/main.go reset-password  重置用户密码")
	fmt.Println("  go run cmd/cli/main.go list-users      列出所有用户")
}

func readInput(prompt string) string {
	reader := bufio.NewReader(os.Stdin)
	fmt.Print(prompt)
	text, _ := reader.ReadString('\n')
	return strings.TrimSpace(text)
}

func createAdmin() {
	fmt.Println("=== 创建管理员账号 ===")

	username := readInput("用户名: ")
	if username == "" {
		log.Fatal("用户名不能为空")
	}

	email := readInput("邮箱: ")
	if email == "" {
		log.Fatal("邮箱不能为空")
	}

	password := readInput("密码: ")
	if len(password) < 6 {
		log.Fatal("密码长度不能少于6位")
	}

	var existing model.User
	if err := database.DB.Where("username = ?", username).First(&existing).Error; err == nil {
		log.Fatalf("用户名 '%s' 已存在", username)
	}

	hashedPassword, err := crypto.HashPassword(password)
	if err != nil {
		log.Fatalf("密码加密失败: %v", err)
	}

	user := model.User{
		Username:     username,
		Email:        email,
		PasswordHash: hashedPassword,
		Role:         "admin",
		IsActive:     true,
	}

	if err := database.DB.Create(&user).Error; err != nil {
		log.Fatalf("创建管理员失败: %v", err)
	}

	fmt.Printf("管理员账号 '%s' 创建成功 (ID: %d)\n", username, user.ID)
}

func resetPassword() {
	fmt.Println("=== 重置用户密码 ===")

	username := readInput("用户名: ")
	if username == "" {
		log.Fatal("用户名不能为空")
	}

	if err := service.ResetPasswordByUsername(username, readInput("新密码: ")); err != nil {
		log.Fatalf("重置密码失败: %v", err)
	}

	fmt.Printf("用户 '%s' 密码已重置\n", username)
}

func listUsers() {
	fmt.Println("=== 用户列表 ===")

	var users []model.User
	if err := database.DB.Order("id ASC").Find(&users).Error; err != nil {
		log.Fatalf("查询用户失败: %v", err)
	}

	if len(users) == 0 {
		fmt.Println("暂无用户")
		return
	}

	fmt.Printf("%-4s %-20s %-30s %-10s %-8s %-20s\n", "ID", "用户名", "邮箱", "角色", "状态", "创建时间")
	fmt.Println(strings.Repeat("-", 100))
	for _, u := range users {
		status := "启用"
		if !u.IsActive {
			status = "禁用"
		}
		fmt.Printf("%-4d %-20s %-30s %-10s %-8s %-20s\n",
			u.ID, u.Username, u.Email, u.Role, status, u.CreatedAt.Format("2006-01-02 15:04:05"))
	}
}
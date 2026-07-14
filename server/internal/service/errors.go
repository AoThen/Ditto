package service

import "errors"

var (
	ErrClipNotFound         = errors.New("剪贴板不存在")
	ErrGroupNotFound        = errors.New("分组不存在")
	ErrFormatNotFound       = errors.New("指定格式不存在")
	ErrConflictClipNotFound = errors.New("冲突剪贴板不存在")
	ErrParentGroupNotFound  = errors.New("父分组不存在")
)

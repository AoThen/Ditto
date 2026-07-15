# 拼音智能搜索 — 设计方案

## 1. 概述

为 Ditto 的 **Web 管理面板 (Vue 3)** 和 **桌面客户端 (C++ MFC)** 增加拼音智能搜索功能，支持用户输入拼音字母匹配剪贴板中的中文内容。

### 核心需求

| 需求 | 描述 |
|------|------|
| 输入拼音匹配汉字 | 输入 `zhong` 能搜到包含「中国」「重要」等内容的条目 |
| 搜索范围 | 粘贴内容（`description` / `Main.mText` 文本字段） |
| 多音字处理 | 全部列举，任一读音匹配即命中 |
| 存储策略 | 实时转换，无需额外存储/索引 |
| 数据量级 | 个人小规模（数千条） |

---

## 2. 模块一：Web 管理面板 (Go 后端 + Vue 3 前端)

### 2.1 Go 后端改造

#### 涉及的代码文件

| 文件 | 操作 | 说明 |
|------|------|------|
| `server/go.mod` | 修改 | 新增依赖 `github.com/mozillazg/go-pinyin` |
| `server/internal/service/clip_service.go` | 修改 | 在 `ListClips` 中增加拼音搜索逻辑 |
| `server/internal/utils/pinyin.go` | 新增 | 拼音工具模块 |

#### 拼音工具模块 (`pinyin.go`)

```
package utils

import "github.com/mozillazg/go-pinyin"

// IsPinyinQuery 判断搜索词是否为拼音（纯 ASCII 字母）
func IsPinyinQuery(s string) bool

// ConvertToPinyin 将中文文本转为拼音字符串，多音字用 | 分隔
// 输入: "中国" → 输出: "zhongguo"
// 输入: "银行" → 输出: "yinhang|yinxing"
func ConvertToPinyin(text string) string
```

- 使用 `go-pinyin` 的 `pinyin.Args{Style: pinyin.Normal, Heteronym: true}` 获取全部读音
- 多音字用 `|` 拼接，搜索时匹配任意一个
- 非汉字字符（字母、数字、标点）保留原样

#### 搜索逻辑改造 (`clip_service.go`)

```
func (s *ClipService) ListClips(userID, page, perPage, search, groupID string) (...) {
    // 1. 原有 LIKE 搜索
    query := db.Model(&Clip{}).Where("user_id = ?", userID)
    if search != "" {
        query = query.Where("description LIKE ?", "%"+search+"%")
    }
    // ... groupID, pagination ...

    // 2. 拼音搜索增强（仅在 search 为纯字母时触发）
    if search != "" && utils.IsPinyinQuery(search) {
        lowSearch := strings.ToLower(search)
        allClips := []Clip{}
        db.Where("user_id = ?", userID).Find(&allClips)

        pinyinMatched := []Clip{}
        for _, clip := range allClips {
            pinyin := utils.ConvertToPinyin(clip.Description)
            if strings.Contains(pinyin, lowSearch) {
                pinyinMatched = append(pinyinMatched, clip)
            }
        }

        // 合并结果：LIKE 结果 + 拼音匹配结果（去重）
        // 排序后分页返回
    }
}
```

**注意点：**
- 拼音搜索仅在搜索词为纯 ASCII 字母时触发，避免影响原有中文搜索
- 小规模数据（< 10000 条），全量扫描 + 内存转换性能可接受
- 结果合并后按 `updated_at` 降序排列，再分页

### 2.2 Vue 3 前端改动

| 文件 | 操作 | 说明 |
|------|------|------|
| `web/src/views/Clips.vue` | 修改 | 搜索框 placeholder 增加提示 |

改动内容：

```html
<el-input
  v-model="searchQuery"
  placeholder="搜索剪贴板内容...（支持拼音搜索）"
  clearable
  @input="handleSearchDebounced"
  @keyup.enter="handleSearch"
/>
```

其余代码不做改动，搜索参数直接传给后端处理。

---

## 3. 模块二：桌面客户端 (C++ MFC)

### 3.1 涉及的文件

| 文件 | 操作 | 说明 |
|------|------|------|
| `src/Pinyin_Convert.h` | 新增 | 拼音转换头文件 |
| `src/Pinyin_Convert.cpp` | 新增 | 拼音转换实现 |
| `src/QPasteWnd.cpp` | 修改 | 搜索逻辑集成拼音匹配 |

### 3.2 拼音转换实现

利用已有 ICU 依赖（`icu.dll`）的 `Transliterator` API 实现汉字→拼音转换。

```cpp
// Pinyin_Convert.h
#pragma once
#include <string>
#include <vector>

std::string ConvertToPinyin(const std::string& utf8Text);
bool IsAlphaQuery(const std::string& s);
```

```cpp
// Pinyin_Convert.cpp
// 使用 ICU Transliterator: "Han-Latin/Names" 规则
// 将中文字符转为拉丁拼音
// 需要 ICU_Loader 确保 icu.dll 可用
```

**ICU 方案优势：**
- 项目已有 ICU 加载机制 (`ICU_Loader/`)
- 内置 `Han-Latin/Names` 音译规则，支持多音字（用 `|` 分隔不同读音）
- 无需额外数据文件
- 如果 ICU 不可用，拼音搜索自动降级为普通搜索

### 3.3 搜索集成

在 `QPasteWnd.cpp` 的搜索流程中增加拼音匹配分支：

```
// 在现有搜索逻辑中，当搜索词为纯字母且已启用搜索时：
// 1. 先做原有搜索（匹配原文）
// 2. 如果搜索词是纯字母，额外做拼音匹配：
//    - 将每条的 mText 转为拼音
//    - 检查拼音串是否包含搜索词
// 3. 合并结果
```

**注意点：**
- 拼音搜索仅在 `SearchDescription` 或 `SearchFullText` 等搜索模式开启时生效
- 不改变原有搜索模式的开关逻辑
- 搜索词为纯字母时才触发拼音匹配

---

## 4. 多音字与边界情况处理

| 场景 | 输入 | 预期匹配 | 说明 |
|------|------|----------|------|
| 纯拼音搜索 | `zhong` | 中国、重要、中心、钟表 | 多音字全部列举匹配 |
| 多音字搜索 | `hang` | 银行、行业、杭州 | 行读 hang/xing 均匹配 |
| 混合搜索 | `zhong国` | 中国 | 拼音部分转拼音匹配，中文部分 LIKE 匹配 |
| 纯中文搜索 | `中国` | 仅 LIKE 精确匹配 | 不做拼音转换，不影响原有行为 |
| 大小写混合 | `ZhongGuo` | 中国 | 全转小写后匹配 |
| 英文内容 | `hello` | 匹配英文内容 | 拼音搜索对英文无影响（正常 LIKE 匹配） |
| 空搜索 | `` | 返回全部 | 不做拼音处理 |
| 特殊字符 | `zhong-guo` | 中国 | 非字母字符保留原样 |

---

## 5. 非功能需求

### 5.1 性能

- 1000 条数据，纯拼音搜索耗时 < 500ms（Go 后端）
- 1000 条数据，C++ 客户端拼音搜索耗时 < 200ms
- 如果 ICU 初始化失败，C++ 客户端拼音搜索自动降级

### 5.2 兼容性

- 原有搜索功能完全不受影响
- 所有搜索配置、快捷键、搜索历史等保持不变
- 数据库结构不变

### 5.3 安全性

- 不引入新的网络请求
- 拼音转换纯本地运算，不涉及数据外传

---

## 6. 验证计划

| 验证项 | 方法 | 接受标准 |
|--------|------|----------|
| 拼音→汉字匹配 | 搜索 `zhong` | 结果包含「中国」「重要」等 |
| 多音字覆盖 | 搜索 `hang` | 结果包含「银行」「行业」 |
| 混合搜索 | 搜索 `zhong国` | 结果包含「中国」 |
| 原有搜索不受影响 | 搜索 `中国` | 结果与改动前一致 |
| C++ ICU 兼容 | 有无 icu.dll 两种场景 | 无 icu.dll 时降级正常 |
| 拼音搜索收敛 | 搜索 `zzz` | 无匹配结果，不报错 |
| 空/空白搜索 | 搜索 `` | 返回全部结果 |

---

## 7. 实现顺序

1. **Go 后端拼音工具** — `pinyin.go` 和 `clip_service.go` 改造
2. **Vue 前端** — placeholder 文字修改
3. **C++ 拼音转换** — `Pinyin_Convert.h/.cpp` 实现
4. **C++ 搜索集成** — `QPasteWnd.cpp` 改造
5. **验证** — 按验证计划逐项测试

---

## 8. 设计决策记录

| 决策 | 选项 | 选择理由 |
|------|------|----------|
| 拼音转换位置 | 前端/后端 | 后端统一处理，前端改动最小 |
| C++ 拼音方案 | ICU/内置数据表 | 利用已有 ICU 依赖，零额外维护成本 |
| 多音字策略 | 全部列举/常用优先 | 全部列举，覆盖更全，小规模数据可接受 |
| 搜索触发条件 | 纯字母/所有输入 | 仅纯字母触发，避免干扰中文搜索 |
| 存储策略 | 实时转换/拼音索引 | 实时转换，零存储开销，小规模数据够用 |
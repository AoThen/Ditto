# Ditto 测试覆盖缺口补充计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。

**目标：** 将 Ditto 测试覆盖率从 ~19% 提升至 ~35%，重点填补 C++ 桌面客户端和 Web 前端测试空白。

**架构：** 分层推进——P0（纯函数零依赖）→ P1（Web缺口+C++算法）→ P2（C++加密/工具库）→ P3（C++数据结构+Vue组件）。

**技术栈：**
- Go: `testing` + `github.com/stretchr/testify`
- Web: Vitest + `@vue/test-utils`
- C++: Google Test (`gtest`)，复用 `CloudSync_Test/` 基础设施

---

## 阶段一：P0 — 纯函数快速取胜（零依赖，2-4 小时）

### 任务 1：`server/internal/utils/pinyin.go` 单元测试

**文件：** 创建 `server/internal/utils/pinyin_test.go`

- [ ] **步骤 1：创建测试文件** — `IsPinyinQuery` 表驱动测试（7个用例：纯字母true、含汉字false、空串false）；`ConvertToPinyin` 表驱动测试（中文、英文、混合、空串）
- [ ] **步骤 2：运行** `go test -v ./server/internal/utils/`
- [ ] **步骤 3：Commit** `git add server/internal/utils/pinyin_test.go && git commit -m "test: add pinyin utils tests"`

### 任务 2：`web/src/composables/useFormatDate.js` 单元测试

**文件：** 创建 `web/src/composables/useFormatDate.spec.js`

- [ ] **步骤 1：创建测试文件** — `formatDate`: 正常日期、无效输入(null/undefined/字符串)、午夜；`formatShortDate`: 正常、无效、单位数月日
- [ ] **步骤 2：运行** `npm run test:unit -- --run --grep 'useFormatDate'`
- [ ] **步骤 3：Commit** `git add web/src/composables/useFormatDate.spec.js && git commit -m "test: add useFormatDate composable tests"`

### 任务 3：`EncryptDecrypt/rijndael.cpp` — AES 已知向量测试

**文件：** 创建 `CloudSync_Test/TestRijndael.cpp`

- [ ] **步骤 1：读取 `EncryptDecrypt/rijndael.h` 确认公开接口**
- [ ] **步骤 2：创建测试文件** — 4个测试：AES-128 ECB 加密/解密往返（NIST SP 800-38A向量）、AES-192 ECB、AES-256 ECB；每组包含 key/plaintext/expected 精确字节数组
- [ ] **步骤 3：根据实际 API 替换占位代码**
- [ ] **步骤 4：加入 CloudSync_Test.vcxproj**
- [ ] **步骤 5：MSBuild 构建运行**
- [ ] **步骤 6：Commit** `git add CloudSync_Test/TestRijndael.cpp && git commit -m "test: add AES Rijndael known-vector tests"`

### 任务 4：`EncryptDecrypt/sha2.cpp` — SHA-256/512 已知向量测试

**文件：** 创建 `CloudSync_Test/TestSHA2.cpp`

- [ ] **步骤 1：读取 `EncryptDecrypt/sha2.h` 确认公开接口**
- [ ] **步骤 2：创建测试文件** — SHA-256(空串)、SHA-256("hello")、SHA-256(10KB随机二进制回归测试)、SHA-512(空串)；使用 NIST FIPS 180-4 标准向量
- [ ] **步骤 3-6：同任务3模式（确认API→替换→加入vcxproj→构建→commit）**

---

## 阶段二：P1 — Web 前端缺口 + C++ 算法（4-6 小时）

### 任务 5：`web/src/stores/clip.js` Pinia Store 测试

**文件：** 创建 `web/src/stores/clip.spec.js`

- [ ] **步骤 1：读取 clip.js 确认 API**
- [ ] **步骤 2：创建测试文件** — 4个测试：默认值null、notifyClipAdded更新lastNotifiedClip、dispatchCustomEvent(ws-clip-added)、多次调用覆盖
- [ ] **步骤 3：运行** `npm run test:unit -- --run --grep 'clip store'`
- [ ] **步骤 4：Commit** `git add web/src/stores/clip.spec.js && git commit -m "test: add clip Pinia store tests"`

### 任务 6：`web/src/router/index.js` 路由守卫测试

**文件：** 创建 `web/src/router/index.spec.js`

- [ ] **步骤 1：读取 router/index.js 确认守卫逻辑**
- [ ] **步骤 2：创建测试文件** — 4个测试：未登录重定向/login、已登录从/login到/dashboard、非管理员/admin/users重定向、管理员允许访问/admin/users
- [ ] **步骤 3：运行** `npm run test:unit -- --run --grep 'router'`
- [ ] **步骤 4：Commit** `git add web/src/router/index.spec.js && git commit -m "test: add router guard tests"`

### 任务 7：`src/WildCardMatch.cpp` — 通配符匹配测试

**文件：** 创建 `CloudSync_Test/TestWildCardMatch.cpp`

- [ ] **步骤 1：读取 `src/WildCardMatch.h` 确认接口**
- [ ] **步骤 2：创建测试文件** — 6个测试：精确匹配、*匹配任意、*不匹配、?匹配单个字符、空模式、复杂模式（*.txt、foo*.baz等）
- [ ] **步骤 3：根据实际 API 调整调用签名**
- [ ] **步骤 4：加入 CloudSync_Test.vcxproj，添加 src/ 到 include path**
- [ ] **步骤 5：MSBuild 构建运行**
- [ ] **步骤 6：Commit** `git add CloudSync_Test/TestWildCardMatch.cpp && git commit -m "test: add WildCardMatch unit tests"`

### 任务 8：`src/Crc32Dynamic.cpp` — CRC32 测试

**文件：** 创建 `CloudSync_Test/TestCrc32.cpp`

- [ ] **步骤 1：读取 `src/Crc32Dynamic.h` 确认接口**
- [ ] **步骤 2：创建测试文件** — 3个测试：空数据(CRC32=0)、"hello"(CRC32=0x3610A686)、1024字节大数据(非零)
- [ ] **步骤 3-6：同任务7模式**

### 任务 9：`src/RegExFilterHelper.cpp` — 正则过滤测试

**文件：** 创建 `CloudSync_Test/TestRegExFilter.cpp`

- [ ] **步骤 1：读取 `src/RegExFilterHelper.h` 确认接口**
- [ ] **步骤 2：创建测试文件** — 4个测试：精确字符串匹配、部分匹配、特殊正则字符转义、空输入
- [ ] **步骤 3-6：同任务7模式**

### 任务 10：`Addins/DittoUtil/TextConvert.cpp` — 编码转换测试

**文件：** 创建 `CloudSync_Test/TestTextConvert.cpp`

- [ ] **步骤 1：读取 `Addins/DittoUtil/TextConvert.h` 确认接口**
- [ ] **步骤 2：创建测试文件** — 3个测试：UTF8→ANSI往返、中文字符UTF8往返、空字符串
- [ ] **步骤 3-6：同任务7模式**

---

## 阶段三：P2 — C++ 加密/工具库（4-6 小时）

### 任务 11：`src/Md5.cpp` — MD5 已知向量测试

**文件：** 创建 `CloudSync_Test/TestMd5.cpp`

- [ ] **步骤 1：读取 `src/Md5.h` 确认接口**
- [ ] **步骤 2：创建测试文件** — 3个测试：空串(d41d8cd98f00b204e9800998ecf8427e)、"hello"(5d41402abc4b2a76b9719d911017c592)、1024字节大数据(回归：非全零)
- [ ] **步骤 3-6：同任务7模式**

### 任务 12：`EncryptDecrypt/Encryption.cpp` — 旧版加密往返测试

**文件：** 创建 `CloudSync_Test/TestEncryption.cpp`

- [ ] **步骤 1：读取 `EncryptDecrypt/Encryption.h` 和 `IEncryption.h`**
- [ ] **步骤 2：创建测试文件** — 2个测试：Encrypt→Decrypt往返（"Hello"字节）、空输入不崩溃
- [ ] **步骤 3-6：同任务7模式**

### 任务 13：`Addins/DittoUtil/RemoveLineFeeds.cpp` — 换行符移除测试

**文件：** 创建 `CloudSync_Test/TestRemoveLineFeeds.cpp`

- [ ] **步骤 1：读取 `Addins/DittoUtil/RemoveLineFeeds.h`**
- [ ] **步骤 2：创建测试文件** — 4个测试：CRLF、Unix LF、无换行符不变、空串
- [ ] **步骤 3-6：同任务7模式**

---

## 阶段四：P3 — C++ 数据结构 + Vue 组件（6-8 小时）

### 任务 14：`web/src/components/ErrorFallback.vue` 组件测试

**文件：** 创建 `web/src/components/ErrorFallback.spec.js`

- [ ] **步骤 1：读取 ErrorFallback.vue 确认组件逻辑**
- [ ] **步骤 2：创建测试文件** — 3个测试：无错误时渲染slot、onErrorCaptured触发显示错误UI、handleRetry重新加载window.location
- [ ] **步骤 3：运行** `npm run test:unit -- --run --grep 'ErrorFallback'`
- [ ] **步骤 4：Commit** `git add web/src/components/ErrorFallback.spec.js && git commit -m "test: add ErrorFallback component tests"`

### 任务 15：`src/Slugify.h` — slugify 自由函数测试

**文件：** 创建 `CloudSync_Test/TestSlugify.cpp`

- [ ] **步骤 1：读取 `src/Slugify.h` 确认接口**
- [ ] **步骤 2：创建测试文件** — 4个测试：基本英文(Hello World→hello-world)、特殊字符(空格→-、--test--→test)、空输入、混合内容(Test 123→test-123)
- [ ] **步骤 3-6：同任务7模式**

### 任务 16：`src/ConvertRTFToText.cpp` — RTF 转文本测试

**文件：** 创建 `CloudSync_Test/TestConvertRTFToText.cpp`

- [ ] **步骤 1：读取接口**
- [ ] **步骤 2：创建测试文件** — 3个测试：简单RTF→纯文本、纯文本不变、空串
- [ ] **步骤 3-6：同任务7模式**

### 任务 17：`src/ClipCompare.cpp` — 剪贴板比较测试

**文件：** 创建 `CloudSync_Test/TestClipCompare.cpp`

- [ ] **步骤 1：读取 `src/ClipCompare.h`**
- [ ] **步骤 2：创建测试文件** — 比较逻辑测试：相同内容、不同内容、大小写敏感/不敏感（根据实现）
- [ ] **步骤 3-6：同任务7模式**

---

## 任务汇总

| 阶段 | 任务 | 文件 | 类型 | 预估时间 |
|------|------|------|------|----------|
| P0 | 1 | pinyin_test.go | Go | 15min |
| P0 | 2 | useFormatDate.spec.js | Web | 15min |
| P0 | 3 | TestRijndael.cpp | C++ | 45min |
| P0 | 4 | TestSHA2.cpp | C++ | 30min |
| P1 | 5 | clip.spec.js | Web | 30min |
| P1 | 6 | index.spec.js (router) | Web | 30min |
| P1 | 7 | TestWildCardMatch.cpp | C++ | 25min |
| P1 | 8 | TestCrc32.cpp | C++ | 15min |
| P1 | 9 | TestRegExFilter.cpp | C++ | 15min |
| P1 | 10 | TestTextConvert.cpp | C++ | 25min |
| P2 | 11 | TestMd5.cpp | C++ | 20min |
| P2 | 12 | TestEncryption.cpp | C++ | 20min |
| P2 | 13 | TestRemoveLineFeeds.cpp | C++ | 15min |
| P3 | 14 | ErrorFallback.spec.js | Web | 30min |
| P3 | 15 | TestSlugify.cpp | C++ | 15min |
| P3 | 16 | TestConvertRTFToText.cpp | C++ | 20min |
| P3 | 17 | TestClipCompare.cpp | C++ | 20min |

**总计：17个任务，新增约21个测试文件，预估总工时：5-8小时**

**预期覆盖率提升：** Go 85%→95%，Web 93%→98%，C++ 3%→12%，项目整体 19%→28%

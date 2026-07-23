# 项目目录结构说明

## 当前结构（保守整理后）

根目录保持 VS 项目文件不动，仅做了无冲突风险的整理：

- `docs/` — 文档（README 配图 ditto.gif 也在内）
- `docker/` — Docker Compose 文件（原根目录移入，内部路径已更新）
- `scripts/` — 辅助脚本（test-e2e.sh, generate-certs.sh）

## 未来计划中的目录重构

以下修改会影响上游（`sabrogden/Ditto`）合并，建议在合并上游最新代码后执行：

### 待执行项（按优先级）

1. `DittoSetup/` → `build/`（仅重命名，更新 BuildDitto.bld 中的路径）
2. `Addins/` → `addins/`（更新 CP_Main_10.sln 和 CloudSync_Test.vcxproj）
3. `Shared/` → `src/shared/`（更新 67 处 `#include` 路径）
4. 工具项目归集到 `tools/`：FocusHighlight, U3Stop, ICU_Loader, focusdll, EncryptDecrypt, CloudSync_Test（更新 .sln 和 CloudSync_Test.vcxproj）
5. `src/` 内部分组：将平铺的 ~130 个 .cpp/.h 按功能分入 `core/` `ui/` `clip/` `network/` `format/` `controls/` `util/` 子目录（需要更新 .vcxproj 中 440+ 文件路径，并加 AdditionalIncludeDirectories）
6. 清理已跟踪的构建工具：`DittoSetup/ProjectZip.exe`、`DittoSetup/rcedit-*.exe`

### 合并冲突处理策略

执行上述重构后，每次合并上游需：

1. **正常合并**：`git merge upstream/master`
2. **.vcxproj 冲突**：接受上游新增/删除的文件条目，手动将路径映射到新子目录
3. **.sln 冲突**：接受上游项目引用，更新路径为 `tools/` 或 `addins/` 前缀
4. **CloudSync_Test 冲突**：路径前缀从 `../` 改为 `../../tools/`
5. **源文件冲突**：文件物理位置变了，但逻辑内容可直接三方合并
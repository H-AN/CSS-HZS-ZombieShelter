# 来源、署名与许可

- **Ducheese**：原始HZS套件逆向、`RangeJA` / `PathLengthJA` 距离补丁及相关签名。上游仓库：[Ducheese/hzs-supporting-plugin](https://github.com/Ducheese/hzs-supporting-plugin)。
- **H-AN**：本仓库可选伴生插件维护，独立gamedata、字节校验、冲突拒绝与自有修改恢复流程整理。插件联合署名为 `Ducheese & H-AN`。

本插件从上游两处距离分支补丁派生，保留原生Idle，不采用上游的Idle detour；不包含僵尸技能、BOT修复或套件资源。安装后无需上游仓库或上游插件文件。

本独立子包的派生代码保留 **MIT** 许可。上游 `Copyright (c) 2025 Ducheese` 及许可全文逐字保存在 [LICENSE.upstream-MIT.txt](LICENSE.upstream-MIT.txt)，分发时一并保留。主仓库其他文件继续遵循根目录GPL许可证，根许可证未改动。

接入时核对的本地上游快照如下；SHA256用于标识来源文件，不是Git提交号。

| 上游文件 | SHA256 |
| --- | --- |
| `addons/sourcemod/scripting/HZSZombieSkill/detours.inc` | `473252162fb40d9df528430fedc48a5d2ea0f902c33109f962e71aed1ebf0b98` |
| `addons/sourcemod/gamedata/hzs_zombieskill.gamedata.txt` | `82e936e50a01283589714e9337423be573915e84c06f3dabd8ba92550faaa663` |
| `addons/sourcemod/scripting/hzs_zombieskill.sp` | `ecd88c4b19f71804dce0c8b0ca5af25155c01482a01f32a1f671e1fa4dcb8398` |
| `LICENSE.txt` | `d7aab03157aef2bdcf46a195e036606a2e16b707cb48d01954fc32ee409846cd` |

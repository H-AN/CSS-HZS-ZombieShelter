# 资源来源与依赖

本目录的分卷压缩包收录与默认地图和建筑配置配套的地图、模型、材质、声音及独立武器定义，保留素材原有文件内容和命名。下载与解压方法见[资源说明](README.md)。

## 发布范围

- 地图为 `zsh_desert_han_dn`、`zsh_deadcity_han_dn` 及其配套 NAV。
- 不包含废弃的 `zsh_desert_han.bsp/.nav`。
- 不分发游戏全局 `scripts/game_sounds_weapons.txt`，避免覆盖官方武器声音定义。
- 游戏内置资源由正常 CS:S／HL2 安装提供，官方 VPK 不纳入本包。
- 包内基础 NPC 文件不代表完整阵容；默认 NPC 以及外部武器、技能所需资源从对应上游项目取得。

`sound/weapons/tfa_cso/tomahawk/wall.wav` 与 `sound/weapons/hzsh_axe/hzsh_axe_hitwall1.wav` 为相同音效的两个兼容路径，分别用于 Shelter 维修配置和 HWS 斧头配置。

## 外部项目与资源入口

| 项目 | 配套内容 |
| --- | --- |
| [HZS 主体](https://github.com/H-AN/H-AN-CSS-ZombieScenario) | 普通 NPC、Boss 及其资源；默认模型使用该项目的[示例模型材质.7z](https://github.com/H-AN/H-AN-CSS-ZombieScenario/blob/main/%E7%A4%BA%E4%BE%8B%E6%A8%A1%E5%9E%8B%E6%9D%90%E8%B4%A8/%E7%A4%BA%E4%BE%8B%E6%A8%A1%E5%9E%8B%E6%9D%90%E8%B4%A8.7z) |
| [HanWeaponSystem](https://github.com/H-AN/H-AN-CSS-HanWeaponSystem) | HWS 武器功能及相应配套资源 |
| [DeltaForceKnife](https://github.com/H-AN/H-AN-CSS-Delta-Force-Knife) | 刀具功能及相应配套资源 |
| [HZS 技能配套插件](https://github.com/Ducheese/hzs-supporting-plugin) | 外部丧尸技能及相应配套资源 |

具体安装范围取决于启用的配置，见[安装与升级](../docs/安装与升级.md)。

## 署名与使用条款

素材在本项目中出现不改变其原作者和原使用条款；本项目不把第三方游戏素材声明为原创。仓库源码的许可证不替代第三方素材的授权。使用或再次分发素材时，应遵守原作者及上游项目的说明，并保留其署名和许可证。

外部资源需与所用插件和配置版本对应。若出现缺失材质或声音，应根据日志路径检查原资源及客户端下载，不能用空文件或无关素材冒名填补。

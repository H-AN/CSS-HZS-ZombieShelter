# 地图与建筑资源

本目录提供昼夜求生的基地、炮塔、建筑、资源节点、设施音效和配套地图，文件统一放在分卷压缩包中。

## 下载与安装

必须将以下两个分卷下载到同一目录，保持文件名不变：

| 文件 | 大小 |
| --- | --- |
| [resources.zip.001](resources.zip.001) | 94,371,840 字节 |
| [resources.zip.002](resources.zip.002) | 61,682,873 字节 |

用 7-Zip 打开或解压 **`resources.zip.001`**，程序会自动读取 `.002` 并解压整套资源。不要单独解压第二个分卷、改名或手动拼接文件。

解压后，将下面五个目录合并到服务端 `cstrike/` 下同名目录，并同步客户端资源下载。不要把外层解压目录再复制一层；例如模型应位于 `cstrike/models/`。

| 解压后的目录 | 内容 |
| --- | --- |
| `models/` | 建筑、炮塔、基地、资源节点等模型及侧件 |
| `materials/` | 模型材质、纹理和图标 |
| `sound/` | 采集、施工、维修、设施和生存提示音效 |
| `maps/` | 沙漠 DN、死城 DN 的 BSP／NAV |
| `scripts/` | 独立斧头的武器定义 |

模型的 `.mdl`、`.vvd`、`.vtx`、`.phy` 和对应材质应一起提供。两张地图须使用匹配的配置和 NAV，见[地图适配](../docs/地图适配.md)。旧 `zsh_desert_han` 不属于当前发行地图。

本包不分发 `scripts/game_sounds_weapons.txt`。这是游戏自带的全局武器声音表，不应通过此插件的资源包覆盖。

## 斧头声音的两个路径

压缩包内下面两个 WAV 的内容相同，但现有配置分别使用不同路径：

| 文件 | 用途 |
| --- | --- |
| `sound/weapons/tfa_cso/tomahawk/wall.wav` | Shelter 的 `sounds.cfg` 中 `repair` 事件 |
| `sound/weapons/hzsh_axe/hzsh_axe_hitwall1.wav` | HWS 的斧头声音前缀及下载清单 |

它们是同一音效的兼容文件名。直接删掉其中一个会使对应配置引用失效；如要统一文件名，需要同时调整使用该路径的配置。

## 丧尸、Boss 与武器资源

本包中的 `models/player/putonghe/` 只是基础丧尸资源的一部分，不是默认阵容的全集。使用默认 NPC 配置，还需从 [HZS 项目](https://github.com/H-AN/H-AN-CSS-ZombieScenario)下载[示例模型材质.7z](https://github.com/H-AN/H-AN-CSS-ZombieScenario/blob/main/%E7%A4%BA%E4%BE%8B%E6%A8%A1%E5%9E%8B%E6%9D%90%E8%B4%A8/%E7%A4%BA%E4%BE%8B%E6%A8%A1%E5%9E%8B%E6%9D%90%E8%B4%A8.7z)，其中提供默认配置对应的 23 种 NPC 模型及配套资源。

使用 HWS、刀具或外部丧尸技能时，按对应上游项目说明安装插件和资源，入口见[安装与升级](../docs/安装与升级.md)。游戏内置模型、材质和声音由正常 CS:S／HL2 安装提供，不重复分发官方 VPK。

[资源来源](SOURCES.md) · [安装与升级](../docs/安装与升级.md)

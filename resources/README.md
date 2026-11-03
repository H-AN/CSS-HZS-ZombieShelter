# 地图与建筑资源

本目录提供昼夜求生的基地、炮塔、建筑、资源节点、设施音效和配套地图文件。安装清单见 [manifest.json](manifest.json)。

## 安装

将以下目录的内容合并到服务端 `cstrike/` 下同名目录，并配置客户端资源下载；不要多复制一层外部 `resources/`。

| 目录 | 内容 |
| --- | --- |
| `models/` | 建筑、炮塔、基地、资源节点等模型及侧件 |
| `materials/` | 模型材质、纹理和图标 |
| `sound/` | 采集、施工、维修、设施和生存提示音效 |
| `maps/` | 沙漠 DN、死城 DN 的 BSP／NAV |
| `scripts/` | 独立斧头的武器定义 |

模型的 `.mdl`、`.vvd`、`.vtx`、`.phy` 和对应材质应一起提供。两张地图须使用匹配的配置和 NAV，见[地图适配](../docs/地图适配.md)。旧 `zsh_desert_han` 不属于当前发行地图。

本包不分发 `scripts/game_sounds_weapons.txt`。这是游戏自带的全局武器声音表，不应通过此插件的资源包覆盖。

## 斧头声音的两个路径

下面两个 WAV 的内容相同，但现有配置分别使用不同路径：

| 文件 | 用途 |
| --- | --- |
| `sound/weapons/tfa_cso/tomahawk/wall.wav` | Shelter 的 `sounds.cfg` 中 `repair` 事件 |
| `sound/weapons/hzsh_axe/hzsh_axe_hitwall1.wav` | HWS 的斧头声音前缀及下载清单 |

它们是同一音效的兼容文件名。直接删掉其中一个会使对应配置引用失效；如要统一文件名，需要同时调整使用该路径的配置。

## 丧尸、Boss 与武器资源

`models/player/putonghe/` 只是基础丧尸资源的一部分，不代表默认丧尸阵容的全集。完整安装所需的 HZS、NPC／Boss 与 HWS／刀具配套入口见[安装与升级](../docs/安装与升级.md)和[兼容组件](../optional/README.md)。

游戏内置模型、材质和声音由正常 CS:S／HL2 安装提供，不重复分发官方 VPK。资源模型及材质引用见 [dependencies.json](dependencies.json)；外部组件的已知缺项见其安装说明。

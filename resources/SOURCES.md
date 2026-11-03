# 资源来源与依赖

本目录收录与当前默认地图和建筑配置配套的既有地图、模型、材质、声音及独立武器定义，保留各素材的原文件内容和原有命名。文件级来源标签、大小及校验和见 [manifest.json](manifest.json)，模型、材质和下载引用见 [dependencies.json](dependencies.json)。

## 发布范围

- 地图为 `zsh_desert_han_dn`、`zsh_deadcity_han_dn` 及其配套 NAV。
- 不包含废弃的 `zsh_desert_han.bsp/.nav`。
- 不分发游戏全局 `scripts/game_sounds_weapons.txt`，避免覆盖官方武器声音定义。
- 游戏内置资源由正常 CS:S／HL2 安装提供，官方 VPK 不纳入本包。
- 额外丧尸、Boss 和武器资源按主项目安装说明获取；本目录中的基础 NPC 文件不代表完整阵容。

`weapons/tfa_cso/tomahawk/wall.wav` 与 `weapons/hzsh_axe/hzsh_axe_hitwall1.wav` 为相同音效的两个兼容路径，分别服务于现行 Shelter 维修配置和 HWS 斧头配置。

## 署名和已知缺项

素材在本项目中出现不改变其原作者和原使用条款；本项目不把第三方游戏素材声明为原创。外部插件来源及许可见配套兼容组件的 CREDITS.md 和 licenses 目录。

HZS Vox 和外部组件中尚未取得的材质、声音或声音脚本引用保留在相应组件的缺项清单中，不使用空文件或替代素材冒名填补。清单包含模型未必使用的皮肤和动画引用，不能将条目数量等同于实际游戏中的错误数量。

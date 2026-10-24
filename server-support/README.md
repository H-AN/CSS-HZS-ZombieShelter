# 服务端补充文件与可选插件

## 主体翻译补充

主体仓库的示例配置/模型包没有附带`HanZombieScenario.phrases.txt`。本目录提供匹配HZS2.3.2源码调用参数的中英兼容翻译，用于此次空Dedicated Server安装。已有完整主体安装请继续使用自己的翻译，不覆盖。

若确实缺文件，将`translations/HanZombieScenario.phrases.txt`复制到服务器`cstrike/addons/sourcemod/translations/`，重新载图。该文件只补文字，不修改主体SMX或源码。NPC模型/声音仍由主体资源安装提供，昼夜插件安装包不包含主体资源全集。

## 可选引擎伴生插件

[optional-plugins](optional-plugins/README.md) 中的插件由服主自行选择安装，主插件不依赖它们。请按各子目录说明复制所选插件的文件，不要将整个 `server-support` 合并到服务器。当前提供独立的 [NPC 追击距离修复](optional-plugins/npc-follow-range/README.md)；依赖签名和原始指令校验，缺失或不兼容时只停用伴生插件。BOT 修复仍暂缓。

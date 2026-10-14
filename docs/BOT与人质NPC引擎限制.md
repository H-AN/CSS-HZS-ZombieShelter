# BOT与人质NPC引擎限制：已阅读现成修复源码

2026-10-15，按用户要求直接阅读以下文件，记录给后续AI。这里只更新文档，没有安装修复、修改主体、加BOT或启动游戏测试。

- `D:\github\git\css\hzs-supporting-plugin\addons\sourcemod\scripting\hzs_botaddfix.sp`
- 同仓库 `addons/sourcemod/gamedata/hzs_botaddfix.gamedata.txt`
- 同仓库 `README.md` 的插件列表与逆向实现说明。
- 主体 `D:\github\git\css\H-AN-CSS-ZombieScenario\[H-AN-CSS]ZombieScenario.sp` 的 `Cmd_BlockAddBot` 及前置引擎BUG注释。

## 已明确的机制

HZS用人质实体作为丧尸NPC。用户已说明这与BOT共用的引擎内部处理存在冲突；配套仓库把具体问题定位为BOT添加/真实复活涉及的人质信息初始化：`CSGameState::Reset`遍历`g_Hostages`，`InitializeHostageInfo`使用硬编码数组维度，NPC人质数量会使该初始化发生buffer overflow并崩溃。

`hzs_botaddfix.sp`不负责创建机器人。它在`OnPluginStart`读取gamedata，定位`CSGameState_Reset`，将人质循环回边的`cmp reg, [g_Hostages.m_Size]`改成`cmp reg, 0`，后面填3个NOP。源码注释明确目的为使循环直接退出，跳过这些NPC人质的初始化。

实际写入6字节：`83 <CmpRegByte> 00 90 90 90`，写入地址为`resetAddr + LoopBackedge`。

| gamedata平台 | LoopBackedge十进制偏移 | CmpRegByte十进制值 | 写入字节 |
| --- | ---: | ---: | --- |
| windows（32位） | 350 | 251（0xFB） | 83 FB 00 90 90 90 |
| windows64（64位） | 382 | 254（0xFE） | 83 FE 00 90 90 90 |

地址定位与偏移来自对应server库签名。缺gamedata、找不到地址或缺偏移会`SetFailState`；成功日志为`[BotAddFix] patched back-edge ...`。当前gamedata给了Windows32/64两套，没有Linux项。配套README要求涉及逆向的插件在64位平台使用spcomp64构建。文件放进目录不等于补丁已成功生效，应核对插件加载结果与成功日志；本次没有进行安装验证。

源码没有卸载时还原原字节的逻辑，不能把卸载SMX当作引擎补丁已撤销；需要还原进程内补丁时按进程重启处理。

## 主体为什么禁止加BOT

主体已有明确注释：NPC生成后BOT复活会触发CS:S引擎BUG，NPC尚未创建的回合阶段才可加BOT。`Cmd_BlockAddBot`在`GameStart`为真时返回`Plugin_Handled`，禁止添加。

该禁止是已有保护，不能因空服休眠、人数未到目标或配置调参就尝试绕过。`hzs_botaddfix`只改引擎内存，不会自动删除主体的这条命令拦截；以后若任务确实要求BOT，还要区分“引擎修复已生效”和“主体命令是否仍拦截”，不能凭修复插件文件存在就发bot_add。

此次由服务器控制台执行加BOT，还出现主体保护提示对client=0调用`CPrintToChat`的错误。这是额外的日志现象，不能据此忘记已经明确的人质NPC/BOT引擎限制，也不能把加BOT当成继续定位changelevel问题的手段。

## 本项目以后执行的规则

1. **录制开发视频、展示昼夜玩法、调采集/建造/升级/丧尸数值，均不需要BOT。录制服保持bot_quota=0，使用真人客户端。**
2. 当前活动插件只有`HanZombieShelter.smx`和`UnmodifiedHZS.smx`，没有加载`hzs_botaddfix`。**NPC存在后禁止添加/补充/复活BOT，也不使用自动补BOT的启动配置或测试夹具。**
3. 不为解决空服休眠添加BOT；用户进入游戏后再观察实际人数。改配置不扩展为重复专服测试。
4. 后续任务确实涉及BOT时，先阅读本说明、修复源码和实际服务器加载状态，核对对应架构及gamedata。录制服仍保持无BOT；不能把隔离回归用的机器人步骤复制到录制服。
5. 当前用户只要求理解并记录，未要求安装补丁、修改主体保护或诊断引擎崩溃。无需再尝试复现这个已知BUG。

## 同仓库带来的索敌结论更正

此前只检查`Han_Think`，它选择最近可见存活玩家时确实没有最大距离条件；但这不能证明引擎已解除全图跟随距离限制。

配套仓库 `HZSZombieSkill/detours.inc` 的`PatchRangeJA`跳过直线距离>2000放弃跟随的分支，`PatchPathLengthJA`跳过路径长度>4000分支，`PrepIdleDetour`拦截`CHostage::Idle`。这些属于`hzs_zombieskill`的引擎补丁，**不是hzs_botaddfix的功能，也不是HanZombieScenarioConfig.cfg里的索敌字段**。

当前录制服没有加载`hzs_zombieskill`，因此准确结论为：主体选目标不设最大距离；配套仓库已有全图跟随修复实现，但当前服没有通过该插件应用它。先前“当前服已经无限距离”的笼统结论不成立。此次只纠正文档，没有改索敌或安装相关模块。

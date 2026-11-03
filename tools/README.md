# 打包

使用 Python 3.10 或更新版本，在仓库根目录运行：

```powershell
python tools/package_release.py
```

生成六个独立 ZIP：运行文件、纯源码、主资源、默认 NPC／武器兼容资源、可选追击插件及主体翻译补充。已有输出不覆盖；可用 `--output <新目录>` 指定位置。

运行文件和资源由各自 `manifest.json` 确定，逐文件核对大小和 SHA256；修改文件后需同步审阅清单。源码从 `src/` 收集，并检查它与 `plugin/addons/sourcemod/scripting/` 一致。源码 ZIP 不带运行配置或插件二进制。

本工具不安装文件、不控制服务器、不上传内容。ZIP 内含独立安装说明、许可和校验清单。

分包检查：

```powershell
python -B tests/test_package_release.py
```

编译方法见[开发与 API](../docs/开发与API.md)。

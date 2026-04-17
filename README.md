# v2ray_to_clash

把常见 `v2ray` 节点链接转换成 `Clash.Meta / Mihomo` 可用的 YAML 配置。

当前支持：

- `vmess://`
- `vless://`
- `trojan://`
- `ss://`
- `hysteria2://`
- `tuic://`

## 编译

```bash
cmake -S . -B build
cmake --build build
```

## 使用

```bash
./build/v2ray_to_clash -i nodes.txt -o config.yaml
```

直接拉取订阅链接：

```bash
./build/v2ray_to_clash -u 'https://example.com/subscription' -o config.yaml
```

如果订阅接口需要请求头：

```bash
./build/v2ray_to_clash -u 'https://example.com/subscription' \
  -H 'User-Agent: clash-converter/1.0' \
  -H 'Authorization: Bearer xxx' \
  -o config.yaml
```

或者：

```bash
cat nodes.txt | ./build/v2ray_to_clash > config.yaml
```

`nodes.txt` 可以是：

- 一行一个节点链接
- 订阅解码后的纯文本
- 整段 Base64 订阅文本
- 通过 `--url` 下载得到的订阅内容

## 说明

- 生成的是一个最小可用配置，包含 `proxies`、`proxy-groups`、`rules`
- `vless` 的输出面向 `Clash.Meta / Mihomo`
- `hysteria2`、`tuic` 同样面向 `Clash.Meta / Mihomo`
- 已兼容常见 `ss` 插件参数、`grpc` 额外参数，以及 `reality` 的 `spider-x`
- 遇到不支持或格式错误的节点时，程序会跳过并在标准错误输出提示

[![CI](https://github.com/hakoniwalab/hakoniwa-pdu-endpoint/actions/workflows/ci.yml/badge.svg)](https://github.com/hakoniwalab/hakoniwa-pdu-endpoint/actions/workflows/ci.yml)
[![Core Variants](https://github.com/hakoniwalab/hakoniwa-pdu-endpoint/actions/workflows/core-variants.yml/badge.svg)](https://github.com/hakoniwalab/hakoniwa-pdu-endpoint/actions/workflows/core-variants.yml)

# hakoniwa-pdu-endpoint

[English](README.md) | [日本語](README.ja.md)

`hakoniwa-pdu-endpoint` は、箱庭の分散シミュレーション向け `Endpoint` 基盤です。

単なるtransport wrapperではなく、次の意味論を分離して扱います。

- `cache`: データ寿命、上書き、queueing
- `comm`: 配送、永続化、transport
- `pdu_def`: 任意のPDU名、channel ID、sizeの意味づけ

この分離により、Endpoint APIを変えずにtransportや保存方式を差し替えられます。

設計意図は [docs/design_philosophy.md](docs/design_philosophy.md) を参照してください。

## 主な対応機能

- TCP / UDP / WebSocket
- Hakoniwa Shared Memory
- Storage
- Zenoh
- MQTT
- `latest` / `queue` cache
- PDU name resolution
- C facade
- Python/cffi binding
- C# binding

TCPとSHM(callback)の性能特性は [benchmarks/PERFORMANCE.ja.md](benchmarks/PERFORMANCE.ja.md) を参照してください。

## CI / 対応環境

CIは役割を分けています。

- `ci`: 通常build、tests、bindings、manifest resolution、transport combinations
- `core-variants`: Hakoniwa Core package統合と外部CMake consumerのlink契約

`core-variants` では Ubuntu x64、Linux ARM64 (`aarch64`)、macOS、Windows x64 で次を継続検証します。

```text
hakoniwa-core-pro をbuild/install
        ↓
Core有効のhakoniwa-pdu-endpointをbuild/install
        ↓
別CMake projectをconfigure
        ↓
find_package(hakoniwa_pdu_endpoint CONFIG REQUIRED)
        ↓
core_callback / core_polling consumerを実リンク
```

Linux ARM64はnative ARM64のGitHub Actions runnerでCI verifiedです。Endpointが利用するHakoniwa Coreのbuild/install経路、exportされた `assets` / `shakoc` package target、Endpoint Core variants、外部consumer linkまでARM64上で検証しています。

READMEや `docs/**` だけの変更では、重いbuild workflowは起動しません。

## 推奨ビルド: manifest駆動

Windows / macOS / Linuxで同じ開発者向けフローを使います。

`hakoniwa-build.yaml` に必要なcapabilityを宣言し、`tools/hako.py` がOS固有のCMake引数や前提条件を解決します。

デフォルトmanifest:

```yaml
bindings:
  python: true

features:
  hakoniwa_core: false
  zenoh: false
  mqtt: false
```

代表的な変更:

- C++のみ: `bindings.python: false`
- SHM / Hakoniwa time source: `features.hakoniwa_core: true`
- Zenoh: `features.zenoh: true`
- MQTT: `features.mqtt: true`

Python/cffiは言語bindingであり、Hakoniwa Coreとは独立です。TCP / UDP / WebSocket / Storage / Zenoh / MQTTだけならCoreは不要です。

設定確認とビルド:

```bash
python tools/hako.py doctor
python tools/hako.py configure --dry-run
python tools/hako.py build
python tools/hako.py test
```

resolverは次を生成します。

```text
.hako/resolved-build.yaml
.hako/cmake-args.txt
```

ビルド問題を報告するときは、これらを添えると解決済みfeature・path・CMake引数を再現できます。

詳細は [docs/build-architecture.md](docs/build-architecture.md) を参照してください。

## Hakoniwa Core有効時の生成物

`features.hakoniwa_core` は **callback / polling を選択する設定ではありません**。

manifestは「どのartifactを生成するか」を決め、consumer側のCMake targetが「どのCore frontendに依存するか」を決めます。

### Core OFF

```yaml
features:
  hakoniwa_core: false
```

主target:

```text
hakoniwa_pdu_endpoint
```

install後のCMake target:

```cmake
hakoniwa_pdu_endpoint::hakoniwa_pdu_endpoint
```

Core非依存のTCP / UDP / WebSocket / Storage / Zenoh / MQTT用途では、このtargetを使います。

### Core ON

```yaml
features:
  hakoniwa_core: true
```

3種類のnative targetを生成します。

| build target | install後のCMake target | Core依存 | 位置づけ |
|---|---|---|---|
| `hakoniwa_pdu_endpoint` | `hakoniwa_pdu_endpoint::hakoniwa_pdu_endpoint` | callback + polling | legacy互換 |
| `hakoniwa_pdu_endpoint_core_callback` | `hakoniwa_pdu_endpoint::core_callback` | `hakoniwa-core::assets` | 新規callback / asset統合で推奨 |
| `hakoniwa_pdu_endpoint_core_polling` | `hakoniwa_pdu_endpoint::core_polling` | `hakoniwa-core::shakoc` | 新規polling統合で推奨 |

従来の `hakoniwa_pdu_endpoint` targetは互換性のため残しています。Core ON時はcallbackとpollingの両SHM/time-source実装を含む「全部入り」のlegacy targetです。

新しいCore統合では、利用するAPIスタイルに合わせてexplicit variantを選びます。

```text
Coreなし
  -> hakoniwa_pdu_endpoint::hakoniwa_pdu_endpoint

Coreあり + callback/assets API
  -> hakoniwa_pdu_endpoint::core_callback

Coreあり + polling/shakoc API
  -> hakoniwa_pdu_endpoint::core_polling
```

重要な責務分担:

```text
manifest
  -> どのcapability / artifactを生成するか

consumerのCMake target
  -> callback / pollingのどちらに依存するか
```

このためmanifestにはcallback/polling選択フィールドを設けません。

## CMake packageとして利用する

install済みEndpointを利用するprojectでは、include/lib pathを手動で組み立てず、export済みCMake targetを使います。

callback/assets API:

```cmake
find_package(hakoniwa_pdu_endpoint CONFIG REQUIRED)

target_link_libraries(my_app PRIVATE
  hakoniwa_pdu_endpoint::core_callback
)
```

polling/shakoc API:

```cmake
find_package(hakoniwa_pdu_endpoint CONFIG REQUIRED)

target_link_libraries(my_app PRIVATE
  hakoniwa_pdu_endpoint::core_polling
)
```

Core対応Endpoint packageは `hakoniwa-core` をtransitive dependencyとして解決します。

consumerが `assets` / `shakoc` / `hako` を直接 `find_library()` したり、Coreのinclude pathをhard-codeする必要はありません。

この `install -> find_package -> external link` 契約は `core-variants` CIで検証します。

## Requirements

- C++20 compiler
- CMake 3.16以上
- Boost headers
- testsをbuildする場合はGoogleTest
- SHM / Hakoniwa time sourceを使う場合はHakoniwa Core
- `bindings.python: true` の場合はPython 3 + `cffi` / `setuptools`

WindowsではBoost.Asio / Boost.Beastをvcpkgで用意する構成を推奨します。

## 手動CMakeビルド

manifestを使わず直接CMakeを利用する経路も、互換・高度な開発者向けとして残しています。

```bash
cmake -S . -B build
cmake --build build
```

Coreを明示的に有効化する例:

```bash
cmake -S . -B build-core \
  -DHAKO_PDU_ENDPOINT_ENABLE_HAKONIWA_CORE=ON \
  -DHAKO_PDU_ENDPOINT_HAKONIWA_CORE_ROOT=/path/to/hakoniwa-core-install
cmake --build build-core
```

raw CMakeのlegacy defaultはmanifest defaultと異なる場合があります。再現性を重視する通常利用ではmanifest flowを推奨します。

## Endpoint設定

Endpoint設定は主に次の4要素で構成されます。

- Endpoint config
- Cache config
- Communication (`comm`) config
- 任意のPDU Definition (`pdu_def`) config

代表例:

```json
{
  "name": "my_endpoint",
  "pdu_def_path": "comm/hakoniwa/pdudef.json",
  "cache": "cache/queue.json",
  "comm": "comm/hakoniwa/shm_comm.json"
}
```

内部cacheだけを使うEndpointは `comm: null` にします。

```json
{
  "name": "my_internal_buffer",
  "cache": "cache/buffer.json",
  "comm": null
}
```

設定仕様とvalidation:

- [docs/tutorials/endpoint.md](docs/tutorials/endpoint.md)
- [docs/receive_semantics.md](docs/receive_semantics.md)
- [docs/tcp-runtime-contract-ja.md](docs/tcp-runtime-contract-ja.md)
- [docs/storage_comm.md](docs/storage_comm.md)
- `config/schema/`

## Transport概要

### TCP / TCP Mux

永続TCP sessionの既定値は`read_timeout_ms: 0`および`write_timeout_ms: 0`です。blocking I/Oに正のtimeoutを指定して期限に達した場合、そのconnectionは`HAKO_PDU_ERR_TIMEOUT`として終了し、socketは再利用されません。詳細は[docs/tcp-runtime-contract-ja.md](docs/tcp-runtime-contract-ja.md)を参照してください。

### Storage

- `latest`: keyごとの最新値。主read APIは `recv(key, ...)`
- `queue`: 到着順queue。主read APIは `recv_next(...)`

`hako_pdu_storage_debug` でstorage fileをinspectできます。

### Zenoh

```yaml
features:
  zenoh: true
```

Zenohはfirst-class pub/sub transportとして扱います。Zenoh native topologyはZenoh側configに置き、Hakoniwa固有の意味論はEndpoint comm configに残します。

### MQTT

```yaml
features:
  mqtt: true
```

MQTTはbroker-based pub/subとして扱いながら、同じEndpoint APIを維持します。

### Shared Memory

SHMにはHakoniwa Coreが必要です。legacy targetではruntime configによるcallback/polling選択も維持しますが、新規CMake consumerは上記のexplicit variantを選ぶことを推奨します。

## Language bindings

### Python

Python bindingはC facadeの上に `cffi` で構成します。

主なmodule:

- `python/hakoniwa_pdu_endpoint/c_endpoint.py`
- `python/hakoniwa_pdu_endpoint/endpoint_container.py`

詳細: [docs/python_binding.md](docs/python_binding.md)

### C#

C# bindingもC facadeをnative ABI boundaryとして利用します。

- [docs/csharp_binding.md](docs/csharp_binding.md)
- [docs/csharp_engine_integration.md](docs/csharp_engine_integration.md)

## Examples / tools

- [examples/README.md](examples/README.md)
- [benchmarks/README.md](benchmarks/README.md)
- [docs/diagrams/README.md](docs/diagrams/README.md)
- [FAQ.md](FAQ.md)

## Maintainer rule

新しいoptional transport / binding / runtime capabilityを追加するときは、

1. どの独立manifest axisが責務を持つか決める
2. OS固有の解決はconfiguratorより下に置く
3. dependencyは絶対include/lib pathではなくCMake targetで公開する
4. capability combinationのsmoke testを追加する
5. downstream CMake consumerに公開する契約ならinstall後のconsumer linkまで検証する

# hakoniwa-pdu-endpoint

[English](README.md) | [日本語](README.ja.md)

`hakoniwa-pdu-endpoint` は、箱庭の分散シミュレーション向け `Endpoint` 基盤です。単なるメッセージングライブラリではなく、シミュレーション参加者間の因果境界を定義し、`cache`、`comm`、`pdu_def` を分離して意味論を明示することを重視しています。

この日本語版は、英語版 README と対応づく章構成で要点を整理したものです。詳細な例や補足は必要に応じて英語版も参照してください。

## What This Is Good At

このプロジェクトが特に向いているのは、次を同時に満たしたい場合です。

- 明示的なシミュレーション意味論
- transport 非依存な API
- 監査・再現しやすい通信
- 設定駆動の組み立て

実務的には、

- `cache` で寿命・上書き規則を決める
- `comm` で配送・永続化・失敗モデルを決める
- `pdu_def` でバイト列の意味を決める

という分離が中核です。

## Why Endpoint?

箱庭系の分散シミュレーションでは、TCP/UDP/SHM/WebSocket など複数の通信手段が混在しがちです。`Endpoint` はそれらを統一 API と統一設定モデルで扱うための抽象です。

狙いは次です。

- protocol 差し替えをコード変更ではなく設定変更にする
- `cache` と `comm` の責務を分ける
- higher-level system から多数の通信リンクを同じ方法で扱えるようにする

`comm: null` を使えば、ネットワーク無しの内部 cache 専用 endpoint としてテストにも使えます。

## Features

主な機能:

- modular な `Endpoint` 構成
- PDU 定義による name-based API
- JSON ベースの設定
- `latest` / `queue` の cache 戦略
- TCP / UDP / SHM / WebSocket / Storage / Zenoh / MQTT
- C facade を介した Python / C# バインディング

## Requirements

主な要件:

- C++20 compiler
- CMake 3.16 以上
- Boost headers
- GoogleTest
- SHM / Hakoniwa time source を使う場合は Hakoniwa Core

詳細は英語版 README の Requirements を参照してください。

## How to Build

標準ビルド:

```bash
cmake -S . -B build
cmake --build build
```

FFI や C# 向けに shared library を作る場合:

```bash
cmake -S . -B build-shared -DBUILD_SHARED_LIBS=ON
cmake --build build-shared --target hakoniwa_pdu_endpoint
```

生成物の例:

- macOS: `build-shared/src/libhakoniwa_pdu_endpoint.dylib`
- Linux: `build-shared/src/libhakoniwa_pdu_endpoint.so`
- Windows: `build-win/src/Release/hakoniwa_pdu_endpoint.dll`

### Helper Scripts

root には補助スクリプトがあります。

Core C++:

- build: `bash build.bash`
- test: `bash test.bash`

Python:

- build native + `cffi`: `bash build-python.bash`
- smoke tests: `bash test-python.bash`
- Windows: `.\build-python-win.ps1`

C#:

- build shared native + managed projects: `bash build-csharp.bash`
- smoke tests: `bash test-csharp.bash`
- Windows:
  - `.\build-csharp-win.ps1`
  - `.\test-csharp-win.ps1`

## Quick Start For Storage

Storage を先に試したい場合:

1. project を build
2. `config/sample/comm/storage_latest_out_comm.json` または `storage_queue_out_comm.json` を使う
3. storage backend の endpoint で送信
4. `hako_pdu_storage_debug` で中身を確認

意味論:

- `latest`: key ごとの最新状態
- `queue`: 受信順ログ

詳細:

- [docs/storage_comm.md](docs/storage_comm.md)
- [examples/README.md](examples/README.md)

## Quick Start For Zenoh

Zenoh は pub/sub を `Endpoint` モデルのまま使いたい場合の選択肢です。

詳細な build/run 手順は英語版 README を参照してください。

## Quick Start For MQTT

MQTT は broker ベースの pub/sub を `Endpoint` モデルのまま使いたい場合の選択肢です。

詳細な build/run 手順は英語版 README を参照してください。

## Quick Start For Python

Python は `cffi` ベースの薄い binding と、その上の async wrapper で構成されています。

代表コマンド:

```bash
bash build-python.bash
bash test-python.bash
```

主なモジュール:

- `python/hakoniwa_pdu_endpoint/c_endpoint.py`
- `python/hakoniwa_pdu_endpoint/c_endpoint_async.py`
- `python/hakoniwa_pdu_endpoint/endpoint_container.py`

主な examples:

- `python/examples/endpoint_internal_cache.py`
- `python/examples/endpoint_async_callback.py`
- `python/examples/endpoint_recv_next.py`
- `python/examples/endpoint_container.py`

## Quick Start For C#

C# は C facade の上に薄い managed binding を載せています。

代表コマンド:

```bash
bash build-csharp.bash
bash test-csharp.bash
```

主なディレクトリ:

- `csharp/hakoniwa_pdu_endpoint/`
- `csharp/examples/`
- `csharp/tests/`

Unity / Godot への導入手順:

- [docs/csharp_engine_integration.md](docs/csharp_engine_integration.md)

## Install / Uninstall

インストール:

```bash
bash build.bash
sudo bash install.bash
```

アンインストール:

```bash
sudo bash uninstall.bash
```

## C Facade

C facade は foreign-language binding の ABI 境界です。

主な API:

- `create/destroy`
- `open/start/post_start/stop/close`
- `process_recv_events`
- `send`
- `recv`
- `recv_next`
- callback 登録
- PDU 名 / channel 取得

設計関連:

- [docs/python_binding.md](docs/python_binding.md)
- [docs/csharp_binding.md](docs/csharp_binding.md)
- [docs/receive_semantics.md](docs/receive_semantics.md)

## How to Run Tests

代表コマンド:

```bash
bash test.bash
bash test-python.bash
bash test-csharp.bash
```

## Configuration

設定は主に次の分割です。

- Endpoint config
- Cache config
- Comm config
- PDU Definition config

設計意図は「設定を小さな semantic decision ごとに分ける」ことです。

関連:

- `config/schema/`
- `docs/tutorials/endpoint.md`
- `docs/design_tradeoffs.md`

## Basic Usage

API は大きく 2 系統です。

- name-based API
  - `pdu_def_path` がある場合
- resolved-key API
  - 常に利用可能

runtime の受信モデルは transport 非依存になるよう整理されています。

- `latest`
  - 最新値のみ保持
  - `recv_next(...)` は pending key を到着順で返す
- `queue`
  - 複数イベント保持
  - `recv_next(...)` はグローバル到着順で返す

詳細:

- [docs/receive_semantics.md](docs/receive_semantics.md)

## Examples

例の入口:

- [examples/README.md](examples/README.md)
- [csharp/examples/README.md](csharp/examples/README.md)

## Config Generator

設定生成は次で行えます。

```bash
python -m hakoniwa_pdu_endpoint.gen_endpoint_config --protocol tcp --direction inout --role server --name demo --out-dir config/generated
```

generator は boilerplate を減らしますが、意味論を隠すことは目的にしていません。

## Endpoint Comm Multiplexer (TCP Mux)

TCP Mux は複数 client を 1 server endpoint 群として扱うための仕組みです。

詳細は英語版 README と `examples/endpoint_tcp_mux.cpp` を参照してください。

## Architectural Design

関連設計文書:

- [docs/design_philosophy.md](docs/design_philosophy.md)
- [docs/design_notes.md](docs/design_notes.md)
- [docs/design_tradeoffs.md](docs/design_tradeoffs.md)
- [docs/receive_semantics.md](docs/receive_semantics.md)

---

詳細な build オプション、各 transport の手順、長い背景説明は英語版 README を参照してください。

[English](README.md) | [日本語](README.ja.md)

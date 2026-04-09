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
- prefix 配下へ Python runtime 一式を配置: `bash install-python.bash`
- smoke tests: `bash test-python.bash`
- Windows: `.\build-python-win.ps1`
- Windows install: `.\install-python-win.ps1`

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

Python は `cffi` ベースの `Endpoint` binding と、その上の pure-Python container で構成されています。
`pyproject.toml` は Python 依存関係の定義に使いますが、native の
`hakoniwa_pdu_endpoint` 共有ライブラリは別途必要です。

リポジトリから prefix 配下へまとめて配置したい場合は、次を使えます。

```bash
bash install-python.bash
export PYTHONPATH="/usr/local/hakoniwa/share/hakoniwa-pdu-endpoint/python:$PYTHONPATH"
```

install 後の確認は次の順で行えます。

1. まず import できることを確認する:

```bash
python3 -c "from hakoniwa_pdu_endpoint import c_endpoint; print('import ok')"
```

2. smoke test を流す:

```bash
PYTHON_CMD=python3 BUILD_FIRST=OFF bash test-python.bash
```

最小確認だけでよければ、次の 2 本でも十分です。

```bash
python3 python/test/test_c_endpoint_smoke.py
python3 python/test/test_endpoint_container_smoke.py
```

代表コマンド:

```bash
python3 -m pip install -e .
bash build-python.bash
bash test-python.bash
```

Windows helper:

- `.\build-python-win.ps1`
- `.\test-python-win.ps1`

Python ローダーは native 共有ライブラリを次の順で探索します。

- `HAKO_PDU_ENDPOINT_SHARED_LIB`
- `HAKO_PDU_ENDPOINT_LIB_DIR`
- リポジトリ配下の `build*/src`
- OS 標準の探索パス

Windows では PowerShell helper を使う想定です。

```powershell
python -m pip install --upgrade pip setuptools wheel cffi
.\build-python-win.ps1 `
  -BuildNative `
  -BuildFfi `
  -BuildDirName build-win `
  -Configuration Release `
  -PythonCommand python `
  -ToolchainFile C:\project\vcpkg\scripts\buildsystems\vcpkg.cmake `
  -VcpkgTriplet x64-windows `
  -Platform x64
python .\python\test\test_c_endpoint_smoke.py
python .\python\test\test_endpoint_container_smoke.py
```

prefix 配下へまとめて配置したい場合:

```powershell
.\install-python-win.ps1 `
  -BuildFirst `
  -BuildDirName build-win `
  -Configuration Release `
  -PythonCommand python `
  -Prefix C:\hakoniwa `
  -ToolchainFile C:\project\vcpkg\scripts\buildsystems\vcpkg.cmake `
  -VcpkgTriplet x64-windows `
  -Platform x64
$env:PYTHONPATH="C:\hakoniwa\share\hakoniwa-pdu-endpoint\python;$env:PYTHONPATH"
```

Windows install 後の確認は次の順です。

1. import できることを確認する:

```powershell
python -c "from hakoniwa_pdu_endpoint import c_endpoint; print('import ok')"
```

2. 最小 smoke test を流す:

```powershell
python .\python\test\test_c_endpoint_smoke.py
python .\python\test\test_endpoint_container_smoke.py
```

Linux/macOS で詰まりやすい点:

- `cffi` と `_cffi_backend` の `Version mismatch`:
  virtualenv の Python を使っているのに、`PYTHONPATH` が system の `dist-packages` を指していて混在しています。build 時は `PYTHONPATH` を空にしてください。例:
  `PYTHONPATH= PYTHON_CMD=python3 BUILD_SHARED_LIBS=ON bash build-python.bash`
- `sudo bash install-python.bash` で `ModuleNotFoundError: No module named 'cffi'`:
  `sudo` により virtualenv ではない Python に切り替わっています。virtualenv の Python を明示してください。例:
  `sudo env "PYTHON_CMD=$VIRTUAL_ENV/bin/python" PYTHONPATH= bash install-python.bash`
- install 後に import できない:
  install 先を `PYTHONPATH` に追加してください。例:
  `export PYTHONPATH="/usr/local/hakoniwa/share/hakoniwa-pdu-endpoint/python:$PYTHONPATH"`

または Windows 用の smoke-test helper を使います。

```powershell
.\test-python-win.ps1 `
  -BuildFirst `
  -BuildDirName build-win `
  -Configuration Release `
  -PythonCommand python `
  -ToolchainFile C:\project\vcpkg\scripts\buildsystems\vcpkg.cmake `
  -VcpkgTriplet x64-windows `
  -Platform x64
```

当面の Windows Python 対応範囲:

- 対象: internal cache を使う基本 smoke test
- 非対象: SHM / Zenoh / MQTT

Windows で詰まりやすい点:

- `py` が無い:
  `python` を使うか、`-PythonCommand python` を指定する
- `BoostConfig.cmake` が見つからない:
  `vcpkg` で `boost-asio:x64-windows` と `boost-beast:x64-windows` を入れ、`-ToolchainFile`、`-VcpkgTriplet`、`-Platform x64` を渡す
- `generator platform: x64 ... used previously`:
  既存 build directory を削除するか、`-Clean` を使う
- Python 3.12 で `setuptools` が無い:
  `python -m pip install --upgrade setuptools wheel cffi`
- `cffi` と `_cffi_backend` の `Version mismatch`:
  build 時に別の Python 環境の `PYTHONPATH` を混ぜない
- 実行時に `hakoniwa_pdu_endpoint.dll` が見つからない:
  `HAKO_PDU_ENDPOINT_SHARED_LIB` と `HAKO_PDU_ENDPOINT_LIB_DIR` を設定する

主なモジュール:

- `python/hakoniwa_pdu_endpoint/c_endpoint.py`
- `python/hakoniwa_pdu_endpoint/endpoint_container.py`

主な examples:

- `python/examples/endpoint_internal_cache.py`
- `python/examples/endpoint_callback.py`
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

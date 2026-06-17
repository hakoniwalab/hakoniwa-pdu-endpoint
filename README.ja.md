# hakoniwa-pdu-endpoint

[English](README.md) | [日本語](README.ja.md)

`hakoniwa-pdu-endpoint` は、箱庭の分散シミュレーション向け `Endpoint` 基盤です。単なるメッセージングライブラリではなく、シミュレーション参加者間の因果境界を定義し、`cache`、`comm`、`pdu_def` を分離して意味論を明示することを重視しています。

この日本語版は、英語版 README と対応づく章構成で要点を整理したものです。詳細な例や補足は必要に応じて英語版も参照してください。

## Performance and Benchmarks

TCP と SHM(callback) の使い分けを知りたい場合は、まず性能概要を参照してください。

- [性能特性と設計意図](benchmarks/PERFORMANCE.ja.md)
- [詳細 benchmark report](benchmarks/report.md)
- [benchmark runner documentation](benchmarks/README.md)

benchmark 結果は、macOS、Ubuntu/WSL2、native Windows における endpoint-level の
TCP / SHM(callback) 挙動を比較しています。これは raw transport bandwidth ではなく、
Hakoniwa PDU endpoint path 全体の測定です。

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
  - TCP / SHM(callback) の性能上の使い分けは [benchmarks/PERFORMANCE.ja.md](benchmarks/PERFORMANCE.ja.md) を参照してください。
- C facade を介した Python / C# バインディング

## Requirements

主な要件:

- C++20 compiler
- CMake 3.16 以上
- Boost headers
- GoogleTest
- SHM / Hakoniwa time source を使う場合は Hakoniwa Core

詳細は英語版 README の Requirements を参照してください。

## クイックスタート: ビルドとインストール

このガイドは、C++ライブラリとPythonバインディングをビルドし、お使いのシステムにインストールするための最短手順です。

### 前提条件

- C++20互換コンパイラ (例: GCC, Clang, MSVC)
- CMake (3.16以上)
- Python 3
- Python FFI (C言語連携) に必要な `cffi` パッケージ。仮想環境の利用を推奨します。

    ```bash
    # 仮想環境の作成と有効化 (任意ですが推奨)
    python3 -m venv .venv
    source .venv/bin/activate

    # 必要なPythonパッケージのインストール
    python -m pip install --upgrade pip setuptools wheel cffi
    ```

### ビルドとインストール

リポジトリに含まれるヘルパースクリプトが、ビルドとインストールのプロセスを自動化します。

1.  **リポジトリをクローンし、ディレクトリに移動します:**
    ```bash
    git clone https://github.com/hakoniwalab/hakoniwa-pdu-endpoint.git
    cd hakoniwa-pdu-endpoint
    ```

2.  **ビルドスクリプトを実行します:**
    このスクリプトは、C++共有ライブラリ (`libhakoniwa_pdu_endpoint.dylib` on macOS) とPython FFI拡張モジュールの両方をビルドします。
    ```bash
    bash build.bash
    ```

3.  **インストールスクリプトを実行します:**
    このスクリプトは、全てのコンポーネント (ライブラリ、ヘッダ、Pythonパッケージ) をデフォルトで `/usr/local/hakoniwa` にインストールします。管理者権限が必要な場合があります。
    ```bash
    sudo bash install.bash
    ```

### Python連携の確認

インストール後、`PYTHONPATH` を設定し、簡単なインポートテストを実行してPythonバインディングが正しく動作するかを確認します。

```bash
# インストールされたパッケージをPYTHONPATHに追加
export PYTHONPATH=/usr/local/hakoniwa/share/hakoniwa-pdu-endpoint/python:$PYTHONPATH

# インポートテストを実行
python3 -c "from hakoniwa_pdu_endpoint.c_endpoint import Endpoint; print(Endpoint)"
```

成功すると、`<class 'hakoniwa_pdu_endpoint.c_endpoint.Endpoint'>` と表示されます。これで、インストールは完了です。
注意: このPythonパッケージは、ネイティブの共有ライブラリとCFFI拡張を必要とするため、`pip`だけでインストールすることはできません。

## 開発者向けガイド

このセクションは、プロジェクトへの貢献、テストの実行、または高度なビルド設定を利用したい方向けの情報です。

### 開発環境 (インストールしない場合)

システムディレクトリにインストールせずにローカルでの変更をテストしたい場合は、一時的に環境変数を設定してPythonスクリプトを実行できます。

**macOSの場合:**
```bash
# ビルド成果物を指すように環境変数を設定
export PYTHONPATH=$(pwd)/python:$(pwd)/build/python
export DYLD_LIBRARY_PATH=$(pwd)/build/src

# これでスクリプトを直接実行可能
python3 python/test/test_c_endpoint_smoke.py
```

**Linuxの場合:**
```bash
# ビルド成果物を指すように環境変数を設定
export PYTHONPATH=$(pwd)/python:$(pwd)/build/python
export LD_LIBRARY_PATH=$(pwd)/build/src

# これでスクリプトを直接実行可能
python3 python/test/test_c_endpoint_smoke.py
```

### テストの実行

`bash build.bash` でビルドした後、`build`ディレクトリからC++のテストスイートを実行できます。

```bash
ctest --test-dir build --output-on-failure
```
PythonとC#のテストについては、`test-python.bash` と `test-csharp.bash` スクリプトを参照してください。

### 手動でのCMakeビルド

ビルドプロセスを完全に制御したい場合は、CMakeを直接実行することもできます。

```bash
# プロジェクトの設定
cmake -S . -B build

# 全てのターゲットをビルド
cmake --build build
```
特定のオプションを渡すには `-D` フラグを使用します (例: `-DHAKO_PDU_ENDPOINT_ENABLE_ZENOH=OFF`)。

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

## Pythonからの利用

Pythonバインディングを利用する推奨方法は、このドキュメントの冒頭にある「クイックスタート」に従って、必要なネイティブコンポーネントをビルド・インストールすることです。Pythonパッケージは、デフォルトで `/usr/local/hakoniwa/share/hakoniwa-pdu-endpoint/python` にインストールされます。

自分のプロジェクトで利用するには、このパスを `PYTHONPATH` に追加してください。
```bash
export PYTHONPATH=/usr/local/hakoniwa/share/hakoniwa-pdu-endpoint/python:$PYTHONPATH
```

主なエントリーポイント:
- `hakoniwa_pdu_endpoint.c_endpoint`: CFFIベースの薄いラッパー。
- `hakoniwa_pdu_endpoint.endpoint_container`: 複数のエンドポイントを管理するpure-Pythonコンテナ。

実行可能なサンプルは `python/examples/` ディレクトリにあります。

## Quick Start For C#

C# は C facade の上に薄い managed binding を載せています。

代表コマンド:

```bash
bash build.bash
bash build-csharp.bash
bash test-csharp.bash
```

詳細は英語版READMEを参照してください。

## インストールとアンインストール

「クイックスタート」で説明した `install.bash` スクリプトは、プロジェクトのコンポーネントを指定されたプレフィックス（デフォルト: `/usr/local/hakoniwa`）にインストールします。

**インストールされるコンポーネント:**
- **ヘッダファイル**: C++ヘッダ (`<prefix>/include`)
- **共有ライブラリ**: コアC++ライブラリ (`libhakoniwa_pdu_endpoint.dylib` or `.so`) (`<prefix>/lib`)
- **CMake設定ファイル**: CMakeパッケージファイル (`<prefix>/lib/cmake`)
- **Pythonパッケージ**: CFFI拡張とそれに必要なネイティブライブラリを含む完全なPythonパッケージ (`<prefix>/share/hakoniwa-pdu-endpoint/python`)

**アンインストール:**
`uninstall.bash`スクリプトで、インストールされたファイルを削除できます。
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
 / Zenoh / MQTT

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

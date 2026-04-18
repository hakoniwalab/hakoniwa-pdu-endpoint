# hakoniwa-pdu-endpoint Python 導入ガイド

このドキュメントは、PyPI から `hakoniwa-pdu-endpoint` を導入する Python ユーザー向けの最短手順です。

現時点の配布モデルは次の通りです。

- PyPI: Python package と依存関係を配布
- GitHub Releases: native shared library (`.dll`, `.so`, `.dylib`) を配布

そのため、`pip install hakoniwa-pdu-endpoint` だけでは動作しません。
Python package に加えて、OS に対応した native shared library が必要です。

## 1. Python package をインストールする

```bash
pip install hakoniwa-pdu-endpoint
```

または、このリポジトリを clone 済みなら利用者向け installer を使えます。

### POSIX (Linux/macOS)

```bash
bash install-python.bash
bash install-python-runtime.bash
```

### Windows

```powershell
.\install-python-win.ps1
```

## 2. release bundle を取得する

GitHub Releases から、自分の OS / CPU / Python ABI に対応した zip bundle を取得してください。

想定 asset 名:

- Linux: `hakoniwa-pdu-endpoint-linux-x86_64-cp312.zip`
- macOS:
  - `hakoniwa-pdu-endpoint-macos-x86_64-cp312.zip`
  - `hakoniwa-pdu-endpoint-macos-arm64-cp312.zip`
- Windows:
  - `hakoniwa-pdu-endpoint-windows-x64-cp312.zip`

zip を展開したディレクトリには次のものが入っています。

- native shared library
- `cffi` extension module
- `hakoniwa_pdu_endpoint` pure-Python runtime files
- `README.txt`

展開先は、たとえば次のようなディレクトリを使います。

- Linux/macOS: `$HOME/.local/lib/hakoniwa-pdu-endpoint/`
- Windows: `C:\hakoniwa\hakoniwa-pdu-endpoint\bin\`

POSIX では installer を 2 段に分けています。

- `install-python.bash`
  - package install と bundle download
- `install-python-runtime.bash`
  - installed package directory への runtime overlay

copy 権限だけが必要な環境では、後者だけ `sudo` 実行できます。

## 3. 環境変数を設定する

Python ローダーは次の環境変数を参照します。

- `HAKO_PDU_ENDPOINT_SHARED_LIB`
- `HAKO_PDU_ENDPOINT_LIB_DIR`

### Linux の例

```bash
export HAKO_PDU_ENDPOINT_SHARED_LIB=$HOME/.local/lib/hakoniwa-pdu-endpoint/hakoniwa_pdu_endpoint-linux-x86_64.so
export HAKO_PDU_ENDPOINT_LIB_DIR=$HOME/.local/lib/hakoniwa-pdu-endpoint
```

### macOS の例

```bash
export HAKO_PDU_ENDPOINT_SHARED_LIB=$HOME/.local/lib/hakoniwa-pdu-endpoint/hakoniwa_pdu_endpoint-macos-arm64.dylib
export HAKO_PDU_ENDPOINT_LIB_DIR=$HOME/.local/lib/hakoniwa-pdu-endpoint
```

### Windows PowerShell の例

```powershell
$env:HAKO_PDU_ENDPOINT_SHARED_LIB="C:\hakoniwa\hakoniwa-pdu-endpoint\bin\hakoniwa_pdu_endpoint-windows-x64.dll"
$env:HAKO_PDU_ENDPOINT_LIB_DIR="C:\hakoniwa\hakoniwa-pdu-endpoint\bin"
```

## 4. 動作確認

まずは import できることを確認します。

```bash
python -c "from hakoniwa_pdu_endpoint import c_endpoint; print('import ok')"
```

## 5. WebSocket での疎通確認 (2 プロセス)

このリポジトリには、`hakoniwa-pdu-endpoint` 単体で完結する WebSocket 疎通確認用の
Python script を用意しています。

- server 側: `python/examples/endpoint_websocket_server.py`
- client 側: `python/examples/endpoint_websocket_client.py`

想定シナリオ:

- Windows 側で server を起動
- WSL2 / Ubuntu 側で client を起動
- client から送った payload を server 側で受信する

### 5.1 Windows 側で server を起動

```powershell
python .\python\examples\endpoint_websocket_server.py
```

起動後、次のような表示になります。

```text
server started: waiting for payload on ws://0.0.0.0:54003/ws
```

### 5.2 WSL2 / Ubuntu 側で client を起動

```bash
python python/examples/endpoint_websocket_client.py
```

成功すると client 側では次のように表示されます。

```text
client sent: hakoniwa websocket demo
```

server 側では次のように表示されます。

```text
server callback: robot=py_ws_demo_robot channel_id=1 payload=b'hakoniwa websocket demo'
server received: hakoniwa websocket demo
```

補足:

- 使用する config は `config/sample/endpoint_websocket_server.json` と `config/sample/endpoint_websocket_client.json` です
- payload は raw bytes を使っています
- client 側の sample config は `127.0.0.1:54003` を使います
- WSL2 から Windows localhost に到達できる標準的な構成を前提にしています
- まずは「2 プロセス間で WebSocket 経由の bytes が往復する」ことを最小確認にしています

`hakoniwa-pdu` は companion package として別途利用できます。
将来的に Hakoniwa PDU メッセージ型 (`geometry_msgs/Twist` など) を使う確認手順を
追加する場合は、`hakoniwa-pdu` 連携の節を別に設けるのがよいです。

## 6. できること / まだ対象外のもの

現時点で最初のサポート対象にしているのは、internal cache ベースの Python minimum support です。

- 対象:
  - `Endpoint`
  - `EndpointContainer`
  - internal cache を使う基本フロー
- まだ対象外:
  - SHM
  - Zenoh
  - MQTT

## 7. よくある詰まりどころ

### `hakoniwa_pdu_endpoint` の import で失敗する

まず package が入っているか確認してください。

```bash
pip show hakoniwa-pdu-endpoint
```

### native shared library が見つからない

`HAKO_PDU_ENDPOINT_SHARED_LIB` と `HAKO_PDU_ENDPOINT_LIB_DIR` を設定してください。

### Windows で `py` コマンドが無い

`py` の代わりに `python` を使ってください。

### Python 3.12 で `setuptools` が無いと言われる

```bash
python -m pip install --upgrade setuptools wheel cffi
```

## 8. 関連情報

- PyPI: `pip install hakoniwa-pdu-endpoint`
- GitHub Releases: native binary 配布元
- Repository README: 開発者向けの build / test / release 手順

# Python Zenoh サンプル

Mac と Raspberry Pi の間で、Hakoniwa PDU endpoint の Zenoh 通信を確認する
Python サンプルです。

Mac 側で `command` を送信し、Raspberry Pi 側で受信します。Raspberry Pi 側は
受け取った値に `1000` を足して `debuginfo` として返します。

## ファイル構成

```text
python/examples/zenoh/
  command_pub.py
  command_sub.py
  config/
    pdudef.json
    pdutypes.json
    endpoint_mac.json
    endpoint_raspberry_pi.json
    comm/
      mac_zenoh_comm.json
      raspberry_pi_zenoh_comm.json
      zenoh/
        router.json5
        mac_client.json5
        raspberry_pi_client.json5
```

- `command_pub.py`: Mac側。`command` を送信し、`debuginfo` を受信する
- `command_sub.py`: Raspberry Pi側。`command` を受信し、`debuginfo` を送信する

Mac 1台だけでも動作確認できます。その場合、どちらのアプリも `127.0.0.1` の
Zenoh router に接続します。

## 通信PDU

通信する値は `std_msgs/UInt16` の `data` フィールドです。

| PDU | channel_id | type | 想定方向 |
| --- | ---: | --- | --- |
| `command` | 0 | `std_msgs/UInt16` | Mac -> Raspberry Pi |
| `debuginfo` | 1 | `std_msgs/UInt16` | Raspberry Pi -> Mac |

ロボット名は `demo` です。
PDUサイズは endpoint が読み込んだPDU定義から取得します。

## Python パッケージ

PDU converter は `hakoniwa-pdu` package の生成済みmoduleを使います。

```bash
pip install hakoniwa-pdu
```

サンプルでは次のmoduleを使います。

```python
from hakoniwa_pdu.pdu_msgs.std_msgs.pdu_conv_UInt16 import py_to_pdu_UInt16, pdu_to_py_UInt16
from hakoniwa_pdu.pdu_msgs.std_msgs.pdu_pytype_UInt16 import UInt16
```

## zenohd のインストール

Mac側では Zenoh router として `zenohd` を起動します。

まずPATH上にあるか確認します。

```bash
command -v zenohd
```

見つからない場合は、macOS では Homebrew でインストールします。

```bash
brew install zenohd
```

インストール後、バージョンを確認します。

```bash
zenohd --version
```

このサンプルでは `zenohd` は Mac 側だけで起動します。Raspberry Pi 側は
Pythonアプリが client として Mac の router に接続するため、Raspberry Pi 側に
`zenohd` を入れる必要はありません。

## ビルド

サンプル実行前に、native shared library と Python CFFI module を build します。

```bash
cmake -S . -B build-zenoh-shared \
  -DBUILD_SHARED_LIBS=ON \
  -DHAKO_PDU_ENDPOINT_ENABLE_ZENOH=ON \
  -DHAKO_PDU_ENDPOINT_ENABLE_HAKONIWA_CORE=OFF \
  -DHAKO_PDU_ENDPOINT_BUILD_EXAMPLES=OFF \
  -DHAKO_PDU_ENDPOINT_BUILD_BENCHMARKS=OFF \
  -DHAKO_PDU_ENDPOINT_BUILD_TOOLS=OFF \
  -DHAKO_PDU_ENDPOINT_INSTALL=OFF

cmake --build build-zenoh-shared -j4

BUILD_DIR="$(pwd)/build-zenoh-shared" bash build-python.bash
```

## 環境変数

macOS:

```bash
export PYTHONPATH="$(pwd)/python:$(pwd)/build-zenoh-shared/python"
export HAKO_PDU_ENDPOINT_LIB_DIR="$(pwd)/build-zenoh-shared/src"
export HAKO_PDU_ENDPOINT_SHARED_LIB="$(pwd)/build-zenoh-shared/src/libhakoniwa_pdu_endpoint.dylib"
```

Linux / Raspberry Pi:

```bash
export PYTHONPATH="$(pwd)/python:$(pwd)/build-zenoh-shared/python"
export HAKO_PDU_ENDPOINT_LIB_DIR="$(pwd)/build-zenoh-shared/src"
export HAKO_PDU_ENDPOINT_SHARED_LIB="$(pwd)/build-zenoh-shared/src/libhakoniwa_pdu_endpoint.so"
export LD_LIBRARY_PATH="$(pwd)/build-zenoh-shared/src:${LD_LIBRARY_PATH:-}"
```

import できることを確認します。

```bash
python3 -c "from hakoniwa_pdu_endpoint.c_endpoint import Endpoint; print('import ok')"
```

## ネットワーク設定

Mac側で `zenohd` router を起動し、Mac側アプリとRaspberry Pi側アプリは
どちらも client として接続します。

Mac側 router:

```text
mode: router
listen: tcp/0.0.0.0:7447
```

Mac側 `command_pub.py`:

```text
mode: client
connect: tcp/127.0.0.1:7447
```

Raspberry Pi側 `command_sub.py`:

```text
mode: client
connect: tcp/127.0.0.1:7447
```

Mac 1台でテストしやすいように、初期設定では
`config/comm/zenoh/raspberry_pi_client.json5` の接続先も
`tcp/127.0.0.1:7447` にしています。

Mac と Raspberry Pi の2台で試す場合は、Raspberry Pi 側で使う
`config/comm/zenoh/raspberry_pi_client.json5` の `127.0.0.1` を
Mac の実IPアドレスに変更してください。

## Mac 1台で試す

3つのterminalで次の順に起動します。

Terminal 1:

```bash
zenohd -c python/examples/zenoh/config/comm/zenoh/router.json5
```

Terminal 2:

```bash
python3 python/examples/zenoh/command_sub.py
```

Terminal 3:

```bash
python3 python/examples/zenoh/command_pub.py
```

`command_pub.py` は `command=1, 2, 3...` を送信します。
`command_sub.py` は受信した値に `1000` を足して `debuginfo` として返します。

期待する表示例:

```text
# command_pub.py
send command=1
recv debuginfo=1001
send command=2
recv debuginfo=1002
```

```text
# command_sub.py
waiting for command samples...
recv command=1
send debuginfo=1001
recv command=2
send debuginfo=1002
```

## Mac と Raspberry Pi で試す

1. Mac側で `zenohd` router を起動する。
2. Raspberry Pi側の `raspberry_pi_client.json5` の接続先を Mac の実IPアドレスに変更する。
3. Raspberry Pi側で `command_sub.py` を起動する。
4. Mac側で `command_pub.py` を起動する。

Mac側 Terminal 1:

```bash
zenohd -c python/examples/zenoh/config/comm/zenoh/router.json5
```

Mac側 Terminal 2:

```bash
python3 python/examples/zenoh/command_pub.py
```

Raspberry Pi側:

```bash
python3 python/examples/zenoh/command_sub.py
```

起動直後の接続待ちを入れたい場合は、Mac側で次のように指定します。

```bash
python3 python/examples/zenoh/command_pub.py --initial-delay 3
```

## オプション

`command_pub.py`:

```bash
python3 python/examples/zenoh/command_pub.py --count 20 --interval 0.1 --initial-delay 0
```

- `--count`: 送信回数
- `--interval`: 送信間隔
- `--initial-delay`: 送信開始前の待ち時間。デフォルトは `0`。

`command_sub.py`:

```bash
python3 python/examples/zenoh/command_sub.py --duration 120 --offset 1000
```

- `--duration`: 実行時間
- `--offset`: `debuginfo` として返すときに加算する値

## トラブルシュート

`open failed: err=3 (HAKO_PDU_ERR_IO_ERROR)` が出た場合は、Zenoh session の
open に失敗しています。エラーメッセージに表示される
`zenoh.config(mode=...; endpoints=...)` を確認してください。

Raspberry Pi から Mac 側の router に接続する場合、`endpoints` には Mac の実IPアドレスを
指定します。存在しないIPアドレスや、Raspberry Pi 自身の `127.0.0.1` を指定すると
接続できません。

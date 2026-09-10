# SHM callback の module boundary 設計

この文書は、Hakoniwa Shared Memory (SHM) callback Endpoint の実行時メモリモデルと、Windows / Linux / macOS のリンク構成差を整理する設計資料です。

中心となる原則は次の1点です。

> OSが管理するShared Memoryの実体と、そのShared Memoryを参照するmodule-localなCore/Asset stateは別物である。

したがって、HakoniwaのShared Memory自体が正常に存在していても、あるDLL内のCore instanceでは `recv_event_table_ == nullptr` のまま、という状態が成立します。

## 1. 状態は2層ある

SHM callback統合では、次の2層を分けて考えます。

```text
Process-local / module-local state

  HakoProData instance
    ├─ master_data_ptr
    ├─ pro_data_ptr
    ├─ asset_ptr
    └─ recv_event_table_ ───────────────┐
                                         │
                                         v
                              +-----------------------+
                              | OS Shared Memory      |
                              |                       |
                              | master data           |
                              | PDU data              |
                              | recv-event table      |
                              | service table         |
                              +-----------------------+
```

Shared Memory領域はOSが管理し、複数module / processからmapできます。

一方、`asset_ptr`、`pro_data_ptr`、`recv_event_table_` などは、そのShared Memoryを利用するための通常のprocess/module内stateです。実際にSHM callback APIを呼ぶmodule側で初期化されている必要があります。

## 2. Windowsの構成

Windowsではcallback `assets` と `conductor` がstatic libraryとしてbuildされます。Coreの `hako` もstaticです。そのため、consumer executableとEndpoint DLLが、それぞれCore/Asset実装とmodule-local stateを持つ構成になり得ます。

代表的な構成は次のとおりです。

```text
Windows process
│
├─ robot-arm-hakoniwa-asset.exe
│   ├─ assets.lib      ─┐
│   ├─ conductor.lib   │ static link
│   └─ hako.lib        ┘
│
│   Core / Asset module-local state A
│     ├─ asset_ptr
│     ├─ pro_data_ptr
│     ├─ master_data_ptr
│     └─ recv_event_table_ ─────────────┐
│                                       │
├─ hakoniwa_pdu_endpoint_core_callback.dll
│   ├─ assets.lib      ─┐
│   └─ hako.lib        ┘ static/link dependency
│
│   Core / Asset module-local state B
│     ├─ asset_ptr
│     ├─ pro_data_ptr
│     ├─ master_data_ptr
│     └─ recv_event_table_ ─────────────┤
│                                       │
└───────────────────────────────────────┼─────────
                                        v
                             +-----------------------+
                             | Windows Shared Memory |
                             | master / PDU / events |
                             +-----------------------+
```

重要なのは次です。

```text
同じprocess
!=
同じCore static state
```

Shared Memoryがすでに作成済みでも、Endpoint DLL側の `asset_ptr`、`pro_data_ptr`、`recv_event_table_` は別途未初期化であり得ます。

そのmoduleがSHM callback I/Oやreceive-event registrationを行う前に、そのmodule自身のCore/Asset stateを既存Shared Memoryへattachする必要があります。

## 3. Linux / macOSの構成

非Windowsではcallback `assets` と `conductor` はshared libraryとしてbuildされます。そのため、consumerごとに `assets.lib` を埋め込むWindows構成と比べ、callback frontend実装はshared object側に置かれ、downstream consumerから共有して参照される構成になります。

```text
Linux / macOS process
│
├─ application executable
│       │
│       ├──────────────┐
│       v              v
│  libassets.so    libconductor.so
│  (or .dylib)     (or .dylib)
│       │
│       │ callback/Core frontend state
│       │
├─ libhakoniwa_pdu_endpoint_core_callback.so
│  (or .dylib)
│       │
│       └── callback/assets frontendへ依存
│
└───────────────────────────────┐
                                v
                     +-----------------------+
                     | OS shared memory/mmap |
                     | master / PDU / events |
                     +-----------------------+
```

このlink構成では、Windowsのように各binary moduleへcallback frontendをstaticに埋め込む場合より、frontend stateの複製が起きにくくなります。

ただし「Linuxではstatic stateがすべて共有される」という意味ではありません。Core内部にはstatic libraryもあり、将来link構成が変わる可能性もあります。

portableな原則は常に次です。

> processが同じだから初期化済み、と仮定しない。SHM callback操作を行うmoduleは、自身の有効なattach contextを持つ必要がある。

Windowsでは、この原則がlink構成上より表面化しやすい、という違いです。

## 4. Shared Memoryが存在しても `recv_event_table_` がnullになり得る理由

Shared Memoryの実体と、それを指すpointerはライフサイクルが別です。

```text
Module A

  HakoProData A
    recv_event_table_ ───────────────┐
                                      │
                                      v
                           +---------------------+
                           | shared event table  |
                           +---------------------+
                                      ^
                                      │
Module B                              │
                                     │
  HakoProData B                       │
    recv_event_table_ = nullptr       │
```

Module AがすでにShared Memoryをmapしていても、それだけでModule Bはreadyになりません。Module B側でも、自身のpointer stateを初期化するattach/load経路を通る必要があります。

したがって、

```text
ERROR: recv_event_table_ is null
```

というエラーは、まず「OS Shared Memoryが存在しない」と考えるのではなく、「このmoduleのCore/Asset attach stateが完成していない」と解釈するべきです。

## 5. Endpointのasset context

`Endpoint::open()` には2つの明確な意味があります。

### External endpoint

```cpp
endpoint.open(config_path);
```

これはexternal-use contextです。SHM callback backendは必要になった時点で、

```text
hako_asset_attach_core()
```

を使ってattachします。

Hakoniwa Assetに所属しない外部peerとしてEndpointを使う場合はこちらです。

### Hakoniwa Assetが所有するEndpoint

```cpp
endpoint.open(config_path, asset_name);
```

こちらはasset identityをcommunication layerへ渡します。SHM callback backendはcontextを保持し、SHM accessやreceive-event registrationが必要になった時点で遅延attachします。

```text
hako_asset_attach_core_with_name(asset_name, pdu_config_path)
```

`asset_name` はAssetをregisterするための引数ではありません。

> どの既存Hakoniwa Assetの文脈で、このEndpointがPDU I/Oを行うかを指定するための情報です。

たとえばruntime自身が `Nova5` としてAsset登録されるなら、callback SHM Endpointも同じasset名でopenします。

```cpp
endpoint.open(endpoint_config_path, "Nova5");
```

この状況で1引数版を使うと、Windowsのようなmodule分離環境でcallback backendが必要とするasset contextを失います。

## 6. 初期化シーケンス

asset-owned callback Endpointの代表的な順序です。

```text
Endpoint::open(config, asset_name)
    |
    | asset contextを保持
    v
applicationがHakoniwa Assetをregister / initialize
    |
    v
Endpoint::start()
    |
    v
Endpoint::post_start()
    |
    v
SHM callback operation / recv-event registration
    |
    v
ensure_attached()
    |
    v
hako_asset_attach_core_with_name(asset_name, pdu_config_path)
    |
    v
このmodule自身のCore/Asset stateを既存SHMへattach
```

attachはlazyです。`open()` に `asset_name` を渡すこと自体はAsset登録ではなく、その場で即座にSHM accessを行うものでもありません。

## 7. Build時の前提

asset contextとSHM capabilityは別問題です。

まずEndpoint package自体をHakoniwa Core有効でbuildする必要があります。

```yaml
features:
  hakoniwa_core: true
```

その上で、consumerはcallback frontendを明示的に選びます。

```cmake
find_package(hakoniwa_pdu_endpoint CONFIG REQUIRED)

target_link_libraries(my_app PRIVATE
  hakoniwa_pdu_endpoint::core_callback
)
```

asset-owned SHM callback Endpointでは、次の2条件が両方必要です。

```text
build-time:
  Hakoniwa Core / SHM capabilityが存在する

runtime:
  Endpointが正しいasset contextでopenされる
```

## 8. この設計が必要になった経緯

Windowsのmodule boundaryで起きるstate分離に対応するため、明示的なasset-context attach経路が導入されました。

関連する履歴:

- `hakoniwalab/hakoniwa-core-pro#64` — external / DLL consumer向けの明示attach初期化
- `hakoniwalab/hakoniwa-pdu-endpoint#31` — `Endpoint::open(config_path, asset_name)` / `open_with_asset`
- `hakoniwalab/hakoniwa-core-pro#90` — asset runtime内でexternal-context overloadを使っていたことが表面化した後続のWindows callback事例
- `hakoniwalab/hakoniwa-robot-runtime#7` — runtime側でasset名を `Endpoint::open` へ渡す修正

ここから得られる設計上の教訓は、個別Issueより一般的です。

> Shared Memoryは共有ストレージである。Core/Asset runtime stateはbinary module境界を越えて自動共有されるものではない。Endpoint境界でownership contextを明示する。

## 9. SHM callback consumerのレビュー項目

新しいSHM callback consumerを追加するときは、次を確認します。

- Endpoint buildでHakoniwa Core supportが有効になっている。
- Core libraryを手作業で再構成せず、`hakoniwa_pdu_endpoint::core_callback` をlinkしている。
- external peerなら `Endpoint::open(config_path)` を意図して使っている。
- registered Hakoniwa Assetなら `Endpoint::open(config_path, asset_name)` を使い、Asset runtimeと同じidentityを渡している。
- asset-context attachが必要な場合、Endpoint configに解決可能な `pdu_def_path` がある。
- WindowsではEndpoint construction/openだけでなく、`post_start()` とreceive-event registrationまでテストしている。
- 別moduleでSHM生成に成功したことを、現在moduleのcallback state初期化済みの証拠にしない。

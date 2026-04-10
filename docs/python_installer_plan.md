# Python Installer Plan

## 目的

利用者向けに `hakoniwa-pdu-endpoint` を一気通貫で導入できる installer を用意する。

現状は次の分離になっている。

- PyPI: Python package
- GitHub Releases: native shared library + `cffi` extension module
- repo scripts: 開発者向け build / install

このため、利用者は

1. `pip install hakoniwa-pdu-endpoint`
2. release bundle を別途取得
3. 環境変数を設定

を理解する必要がある。

これを installer で吸収する。


## 既存スクリプトの役割

### 開発者向け

- `build-python.bash`
  - native build
  - `cffi` build
- `build-python-win.ps1`
  - Windows での native build
  - `cffi` build
- `install.bash`
  - CMake install
  - prefix 配下へ Python source 配置

これらは user bootstrap ではなく developer workflow。


## 新規にほしいもの

### 利用者向け installer

- POSIX 向け:
  - `install-python.bash`
- Windows 向け:
  - `install-python-win.ps1`

必要なら WSL から呼ぶ wrapper を別で足す。


## 共通仕様

### モード

- `bootstrap`
  - `pip install`
  - release bundle download
  - runtime 配置
  - 必要な環境変数設定案内
  - smoke test
- `use-existing`
  - 既存の Python / runtime 配置を利用
  - 検証を中心に行う

既定は `bootstrap`。

### install 対象

- `hakoniwa-pdu-endpoint`
- companion package `hakoniwa-pdu`
- release bundle

### validate

最低限:

```bash
python -c "from hakoniwa_pdu_endpoint import c_endpoint; print('import ok')"
```

必要なら:

- `python/test/test_c_endpoint_smoke.py`
- `python/test/test_endpoint_container_smoke.py`


## OS ごとの差分

### POSIX

- release bundle:
  - `.so` / `.dylib`
- 配置候補:
  - `$HOME/.local/lib/hakoniwa-pdu-endpoint`
- 環境変数:
  - `HAKO_PDU_ENDPOINT_SHARED_LIB`
  - `HAKO_PDU_ENDPOINT_LIB_DIR`

### Windows

- release bundle:
  - `.dll`
  - `_c_endpoint_ffi*.pyd`
- 配置候補:
  - `%LOCALAPPDATA%\Hakoniwa\hakoniwa-pdu-endpoint`
- 実装上の注意:
  - package import と `_c_endpoint_ffi` の相対 import の整合をとる必要がある
  - launcher / helper で環境変数設定だけで吸収するか
  - package directory へ `_c_endpoint_ffi*.pyd` を補完配置するか


## 実装方針

### phase 1

- `install-python.bash` を追加
- `install-python-win.ps1` を追加
- README / README.ja / python/README.ja を実体に合わせる

### phase 2

- `use-existing` モード追加
- smoke test を installer に統合

### phase 3

- Windows の runtime 補完戦略を固定化
- WSL helper を追加


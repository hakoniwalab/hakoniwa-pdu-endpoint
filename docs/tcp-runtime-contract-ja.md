# TCPランタイム契約

[English](tcp-runtime-contract.md) | [日本語](tcp-runtime-contract-ja.md)

この文書は、`tcp`および`tcp_mux`通信プロバイダのランタイム契約を定義します。

## Timeout設定

`options.read_timeout_ms`と`options.write_timeout_ms`には、0以上のミリ秒値を指定します。

| 値 | 意味 |
|---:|---|
| `0` | blocking I/Oをtimeoutさせません。既定値であり、永続接続での推奨値です。 |
| 正数 | blocking read/writeが期限に達すると`HAKO_PDU_ERR_TIMEOUT`になります。そのconnectionはcloseされ、再利用されません。 |

timeoutはconnection errorの境界です。Endpointの停止状態を定期確認するためのwake-upには使用しません。`stop()`は、受信threadをjoinする前にsocketを`shutdown`してcloseすることで、timeoutなしのblocking receiveを解除します。

Windowsでは、この区別が特に重要です。Winsockは、`SO_RCVTIMEO`または`SO_SNDTIMEO`によるblocking I/O timeout後のconnectionを不定状態とし、closeするよう規定しています。そのため、`WSAETIMEDOUT`をretry可能な`WSAEWOULDBLOCK`として扱いません。

根拠:

- [Microsoft Learn: SOL_SOCKET Socket Options](https://learn.microsoft.com/en-us/windows/win32/winsock/sol-socket-socket-options)

POSIXでは、blocking socketのtimeoutが`EAGAIN`/`EWOULDBLOCK`として通知されることがあります。blocking modeかつ正のtimeout値を指定した場合、Endpointはこれも同じ終端的な`HAKO_PDU_ERR_TIMEOUT`へ変換します。non-blocking socketの通常のwould-blockはretry可能です。

## 切断と再接続の責務

次の事象は現在のTCP connectionを終了させます。

- peer EOF
- read/write timeout
- connection resetなどのnative socket error
- 不正なpacket framing
- 明示的なEndpoint stop

通常の`tcp`プロバイダは、start状態を維持している間、次のように動作します。

- serverは失敗したconnectionをcloseして、次の`accept`へ戻る
- clientは失敗したconnectionをcloseして、connect loopへ戻る

accept/connectが成功するたびに、新しいconnection IDが割り当てられます。失敗したsocketを後続connectionで再利用することはありません。

`tcp_mux`では、失敗したsessionは終了します。listenerは新しいsessionをacceptし続けますが、上位レイヤは失敗したclient用Endpointを、新しくacceptされたsessionで置き換える必要があります。

`stop()`は期待されたライフサイクル遷移です。`shutdown + close`によって発生したsocket errorを異常切断として通知しません。

## 診断ログ

TCPエラーは、native resultを`HakoPduErrorType`へ変換する前に、失敗を検出したoperationで記録します。診断情報には次を含みます。

- monotonic timestamp（ミリ秒）
- transport（`tcp`または`tcp_mux`）
- comm設定名
- role
- connection ID
- 取得可能な場合はpeerの数値address/port
- operation（`accept`、`recv_header`、`recv_body`、`send_packet`など）
- native socket error codeとOS message
- Endpoint errorへの変換結果
- 設定されたtimeout値

peer EOF、native socket error、packet framing errorは別の事象として出力されます。

```text
TCP transport error timestamp_msec=... transport=tcp comm=my-client role=client connection_id=4 peer=127.0.0.1:50051 operation=recv_header native_error=10060 native_message="A connection attempt failed ..." mapped_error=TIMEOUT timeout_ms=1000
```

monotonic timestampは、同一process内での事象順序を確認するための値です。Host間で直接比較できるwall-clock時刻ではありません。

## 運用上の推奨

inactivity deadlineが明示的な要件でない長時間simulation接続では、`read_timeout_ms: 0`と`write_timeout_ms: 0`を使用してください。正のtimeoutを選択した場合、期限到達時に現在のconnectionが終了することを前提にしてください。

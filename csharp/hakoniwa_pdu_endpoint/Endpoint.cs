using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

namespace Hakoniwa.PduEndpoint;

public sealed class Endpoint : IDisposable
{
    private readonly NativeMethods.SafeEndpointHandle _handle;
    private readonly List<CallbackSubscription> _subscriptions = new();
    private bool _disposed;

    public Endpoint(string name, EndpointDirection direction)
    {
        try
        {
            _handle = NativeMethods.hako_pdu_endpoint_create(name, direction);
        }
        catch (DllNotFoundException ex)
        {
            throw new NativeLibraryNotAvailableException(
                "Native library 'hakoniwa_pdu_endpoint' was not found. A shared library build is required for the C# binding.",
                ex);
        }

        if (_handle.IsInvalid)
        {
            _handle.Dispose();
            throw new EndpointException(HakoPduError.OutOfMemory, "hako_pdu_endpoint_create");
        }
    }

    public void Open(string configPath, string? assetName = null)
    {
        ThrowIfDisposed();
        if (assetName is null)
        {
            Check(NativeMethods.hako_pdu_endpoint_open(_handle, configPath), "open");
            return;
        }
        if (assetName.Length == 0)
        {
            throw new ArgumentException("assetName must not be empty.", nameof(assetName));
        }
        Check(NativeMethods.hako_pdu_endpoint_open_with_asset(_handle, configPath, assetName), "open_with_asset");
    }

    public void CreatePduLchannels(string configPath)
    {
        ThrowIfDisposed();
        Check(NativeMethods.hako_pdu_endpoint_create_pdu_lchannels(_handle, configPath), "create_pdu_lchannels");
    }

    public void Close()
    {
        ThrowIfDisposed();
        Check(NativeMethods.hako_pdu_endpoint_close(_handle), "close");
        ReleaseSubscriptions();
    }

    public void Start()
    {
        ThrowIfDisposed();
        Check(NativeMethods.hako_pdu_endpoint_start(_handle), "start");
    }

    public void PostStart()
    {
        ThrowIfDisposed();
        Check(NativeMethods.hako_pdu_endpoint_post_start(_handle), "post_start");
    }

    public void Stop()
    {
        ThrowIfDisposed();
        Check(NativeMethods.hako_pdu_endpoint_stop(_handle), "stop");
    }

    public bool IsRunning()
    {
        ThrowIfDisposed();
        Check(NativeMethods.hako_pdu_endpoint_is_running(_handle, out var running), "is_running");
        return running != 0;
    }

    public void ProcessRecvEvents()
    {
        ThrowIfDisposed();
        NativeMethods.hako_pdu_endpoint_process_recv_events(_handle);
    }

    public void Send(PduResolvedKey key, byte[] payload)
    {
        ThrowIfDisposed();
        var nativeKey = ToNative(key);
        Check(
            NativeMethods.hako_pdu_endpoint_send(_handle, ref nativeKey, payload ?? Array.Empty<byte>(), ToUIntPtr(payload?.Length ?? 0)),
            "send");
    }

    public void SendByName(PduKey key, byte[] payload)
    {
        ThrowIfDisposed();
        var nativeKey = ToNative(key);
        Check(
            NativeMethods.hako_pdu_endpoint_send_by_name(_handle, ref nativeKey, payload ?? Array.Empty<byte>(), ToUIntPtr(payload?.Length ?? 0)),
            "send_by_name");
    }

    public void SubscribeOnRecvCallback(PduResolvedKey key, Action<PduResolvedKey, byte[]> callback)
    {
        ThrowIfDisposed();
        if (callback is null)
        {
            throw new ArgumentNullException(nameof(callback));
        }

        var subscription = new CallbackSubscription(callback);
        var nativeKey = ToNative(key);
        Check(
            NativeMethods.hako_pdu_endpoint_subscribe_on_recv_callback(_handle, ref nativeKey, subscription.NativeCallback, subscription.UserData),
            "subscribe_on_recv_callback");
        _subscriptions.Add(subscription);
    }

    public void SubscribeOnRecvCallbackByName(PduKey key, Action<PduResolvedKey, byte[]> callback)
    {
        ThrowIfDisposed();
        if (callback is null)
        {
            throw new ArgumentNullException(nameof(callback));
        }

        var subscription = new CallbackSubscription(callback);
        var nativeKey = ToNative(key);
        Check(
            NativeMethods.hako_pdu_endpoint_subscribe_on_recv_callback_by_name(_handle, ref nativeKey, subscription.NativeCallback, subscription.UserData),
            "subscribe_on_recv_callback_by_name");
        _subscriptions.Add(subscription);
    }

    public byte[] Recv(PduResolvedKey key, int bufferSize)
    {
        ThrowIfDisposed();
        var nativeKey = ToNative(key);
        var buffer = new byte[bufferSize];
        Check(
            NativeMethods.hako_pdu_endpoint_recv(_handle, ref nativeKey, buffer, ToUIntPtr(buffer.Length), out var receivedSize),
            "recv");
        return Slice(buffer, receivedSize);
    }

    public void SetRecvEvent(PduResolvedKey key)
    {
        ThrowIfDisposed();
        var nativeKey = ToNative(key);
        Check(
            NativeMethods.hako_pdu_endpoint_set_recv_event(_handle, ref nativeKey),
            "set_recv_event");
    }

    public int GetPendingCount()
    {
        ThrowIfDisposed();
        Check(
            NativeMethods.hako_pdu_endpoint_get_pending_count(_handle, out var outCount),
            "get_pending_count");
        return checked((int)outCount.ToUInt64());
    }

    public byte[] RecvByName(PduKey key, int bufferSize)
    {
        ThrowIfDisposed();
        var nativeKey = ToNative(key);
        var buffer = new byte[bufferSize];
        Check(
            NativeMethods.hako_pdu_endpoint_recv_by_name(_handle, ref nativeKey, buffer, ToUIntPtr(buffer.Length), out var receivedSize),
            "recv_by_name");
        return Slice(buffer, receivedSize);
    }

    public PduRecord RecvNext(int bufferSize)
    {
        ThrowIfDisposed();
        var buffer = new byte[bufferSize];
        Check(
            NativeMethods.hako_pdu_endpoint_recv_next(_handle, buffer, ToUIntPtr(buffer.Length), out var outKey, out var timestampNs, out var receivedSize),
            "recv_next");
        return new PduRecord(FromNative(outKey), timestampNs, Slice(buffer, receivedSize));
    }

    public int GetPduSize(PduKey key)
    {
        ThrowIfDisposed();
        var nativeKey = ToNative(key);
        return checked((int)NativeMethods.hako_pdu_endpoint_get_pdu_size(_handle, ref nativeKey).ToUInt64());
    }

    public int GetPduChannelId(PduKey key)
    {
        ThrowIfDisposed();
        var nativeKey = ToNative(key);
        return NativeMethods.hako_pdu_endpoint_get_pdu_channel_id(_handle, ref nativeKey);
    }

    public string GetPduName(PduResolvedKey key, int bufferSize = NativeMethods.PduNameMax)
    {
        ThrowIfDisposed();
        var nativeKey = ToNative(key);
        var buffer = new byte[bufferSize];
        Check(
            NativeMethods.hako_pdu_endpoint_get_pdu_name(_handle, ref nativeKey, buffer, ToUIntPtr(buffer.Length)),
            "get_pdu_name");
        return DecodeFixedString(buffer);
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        ReleaseSubscriptions();
        _handle.Dispose();
        _disposed = true;
        GC.SuppressFinalize(this);
    }

    private void ReleaseSubscriptions()
    {
        foreach (var subscription in _subscriptions)
        {
            subscription.Dispose();
        }
        _subscriptions.Clear();
    }

    private static void Check(HakoPduError error, string operation)
    {
        if (error != HakoPduError.Ok)
        {
            throw new EndpointException(error, operation);
        }
    }

    private void ThrowIfDisposed()
    {
        if (_disposed)
        {
            throw new ObjectDisposedException(nameof(Endpoint));
        }
    }

    private static NativeMethods.NativeResolvedKey ToNative(PduResolvedKey key)
    {
        return new NativeMethods.NativeResolvedKey
        {
            Robot = EncodeFixedString(key.Robot, NativeMethods.RobotNameMax),
            ChannelId = key.ChannelId,
        };
    }

    private static NativeMethods.NativePduKey ToNative(PduKey key)
    {
        return new NativeMethods.NativePduKey
        {
            Robot = EncodeFixedString(key.Robot, NativeMethods.RobotNameMax),
            Pdu = EncodeFixedString(key.Pdu, NativeMethods.PduNameMax),
        };
    }

    private static PduResolvedKey FromNative(NativeMethods.NativeResolvedKey key)
    {
        return new PduResolvedKey(DecodeFixedString(key.Robot), key.ChannelId);
    }

    private static byte[] EncodeFixedString(string value, int maxSize)
    {
        if (value is null)
        {
            throw new ArgumentNullException(nameof(value));
        }

        var bytes = Encoding.UTF8.GetBytes(value);
        if (bytes.Length >= maxSize)
        {
            throw new ArgumentException($"String is too long for fixed buffer size {maxSize}.", nameof(value));
        }

        var buffer = new byte[maxSize];
        Buffer.BlockCopy(bytes, 0, buffer, 0, bytes.Length);
        return buffer;
    }

    private static string DecodeFixedString(byte[] bytes)
    {
        var end = Array.IndexOf(bytes, (byte)0);
        if (end < 0)
        {
            end = bytes.Length;
        }
        return Encoding.UTF8.GetString(bytes, 0, end);
    }

    private static byte[] Slice(byte[] buffer, UIntPtr size)
    {
        var length = checked((int)size.ToUInt64());
        if (length == buffer.Length)
        {
            return buffer;
        }

        var output = new byte[length];
        Buffer.BlockCopy(buffer, 0, output, 0, length);
        return output;
    }

    private static UIntPtr ToUIntPtr(int value)
    {
        return new UIntPtr(checked((uint)value));
    }

    private sealed class CallbackSubscription : IDisposable
    {
        private readonly Action<PduResolvedKey, byte[]> _callback;
        private readonly GCHandle _gcHandle;
        private bool _disposed;

        public CallbackSubscription(Action<PduResolvedKey, byte[]> callback)
        {
            _callback = callback;
            _gcHandle = GCHandle.Alloc(this);
            NativeCallback = StaticOnRecv;
        }

        public NativeMethods.HakoPduOnRecvCb NativeCallback { get; }
        public IntPtr UserData => GCHandle.ToIntPtr(_gcHandle);

        public void Dispose()
        {
            if (_disposed)
            {
                return;
            }
            _gcHandle.Free();
            _disposed = true;
        }

        private static void StaticOnRecv(IntPtr userData, IntPtr keyPtr, IntPtr dataPtr, UIntPtr size)
        {
            var handle = GCHandle.FromIntPtr(userData);
            if (handle.Target is not CallbackSubscription subscription)
            {
                return;
            }
            subscription.Invoke(keyPtr, dataPtr, size);
        }

        private void Invoke(IntPtr keyPtr, IntPtr dataPtr, UIntPtr size)
        {
            var nativeKey = Marshal.PtrToStructure<NativeMethods.NativeResolvedKey>(keyPtr);
            var managedKey = FromNative(nativeKey);
            var payloadLength = checked((int)size.ToUInt64());
            var payload = new byte[payloadLength];
            if (payloadLength > 0 && dataPtr != IntPtr.Zero)
            {
                Marshal.Copy(dataPtr, payload, 0, payloadLength);
            }
            _callback(managedKey, payload);
        }
    }
}

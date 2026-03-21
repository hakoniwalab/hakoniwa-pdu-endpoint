using System;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace Hakoniwa.PduEndpoint;

internal static class NativeMethods
{
    internal const int RobotNameMax = 128;
    internal const int PduNameMax = 128;
    private const string NativeLibraryName = "hakoniwa_pdu_endpoint";

    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeResolvedKey
    {
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = RobotNameMax)]
        public byte[] Robot;
        public uint ChannelId;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct NativePduKey
    {
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = RobotNameMax)]
        public byte[] Robot;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = PduNameMax)]
        public byte[] Pdu;
    }

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void HakoPduOnRecvCb(
        IntPtr userData,
        IntPtr key,
        IntPtr data,
        UIntPtr size);

    internal sealed class SafeEndpointHandle : SafeHandleZeroOrMinusOneIsInvalid
    {
        private SafeEndpointHandle()
            : base(true)
        {
        }

        protected override bool ReleaseHandle()
        {
            hako_pdu_endpoint_destroy(handle);
            return true;
        }
    }

    [DllImport(NativeLibraryName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    internal static extern SafeEndpointHandle hako_pdu_endpoint_create(string name, EndpointDirection direction);

    [DllImport(NativeLibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void hako_pdu_endpoint_destroy(IntPtr endpoint);

    [DllImport(NativeLibraryName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    internal static extern HakoPduError hako_pdu_endpoint_open(SafeEndpointHandle endpoint, string configPath);

    [DllImport(NativeLibraryName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    internal static extern HakoPduError hako_pdu_endpoint_create_pdu_lchannels(SafeEndpointHandle endpoint, string configPath);

    [DllImport(NativeLibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern HakoPduError hako_pdu_endpoint_close(SafeEndpointHandle endpoint);

    [DllImport(NativeLibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern HakoPduError hako_pdu_endpoint_start(SafeEndpointHandle endpoint);

    [DllImport(NativeLibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern HakoPduError hako_pdu_endpoint_post_start(SafeEndpointHandle endpoint);

    [DllImport(NativeLibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern HakoPduError hako_pdu_endpoint_stop(SafeEndpointHandle endpoint);

    [DllImport(NativeLibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern HakoPduError hako_pdu_endpoint_is_running(SafeEndpointHandle endpoint, out byte running);

    [DllImport(NativeLibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void hako_pdu_endpoint_process_recv_events(SafeEndpointHandle endpoint);

    [DllImport(NativeLibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern HakoPduError hako_pdu_endpoint_send(
        SafeEndpointHandle endpoint,
        ref NativeResolvedKey key,
        byte[] data,
        UIntPtr size);

    [DllImport(NativeLibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern HakoPduError hako_pdu_endpoint_subscribe_on_recv_callback(
        SafeEndpointHandle endpoint,
        ref NativeResolvedKey key,
        HakoPduOnRecvCb callback,
        IntPtr userData);

    [DllImport(NativeLibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern HakoPduError hako_pdu_endpoint_send_by_name(
        SafeEndpointHandle endpoint,
        ref NativePduKey key,
        byte[] data,
        UIntPtr size);

    [DllImport(NativeLibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern HakoPduError hako_pdu_endpoint_subscribe_on_recv_callback_by_name(
        SafeEndpointHandle endpoint,
        ref NativePduKey key,
        HakoPduOnRecvCb callback,
        IntPtr userData);

    [DllImport(NativeLibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern HakoPduError hako_pdu_endpoint_recv(
        SafeEndpointHandle endpoint,
        ref NativeResolvedKey key,
        byte[] buffer,
        UIntPtr bufferSize,
        out UIntPtr receivedSize);

    [DllImport(NativeLibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern HakoPduError hako_pdu_endpoint_recv_by_name(
        SafeEndpointHandle endpoint,
        ref NativePduKey key,
        byte[] buffer,
        UIntPtr bufferSize,
        out UIntPtr receivedSize);

    [DllImport(NativeLibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern HakoPduError hako_pdu_endpoint_recv_next(
        SafeEndpointHandle endpoint,
        byte[] buffer,
        UIntPtr bufferSize,
        out NativeResolvedKey outKey,
        out ulong timestampNs,
        out UIntPtr receivedSize);

    [DllImport(NativeLibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern UIntPtr hako_pdu_endpoint_get_pdu_size(
        SafeEndpointHandle endpoint,
        ref NativePduKey key);

    [DllImport(NativeLibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int hako_pdu_endpoint_get_pdu_channel_id(
        SafeEndpointHandle endpoint,
        ref NativePduKey key);

    [DllImport(NativeLibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern HakoPduError hako_pdu_endpoint_get_pdu_name(
        SafeEndpointHandle endpoint,
        ref NativeResolvedKey key,
        byte[] outName,
        UIntPtr outNameSize);
}

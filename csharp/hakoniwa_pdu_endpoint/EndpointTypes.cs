using System;

namespace Hakoniwa.PduEndpoint;

public enum EndpointDirection
{
    In = 0,
    Out = 1,
    InOut = 2,
}

public enum HakoPduError
{
    Ok = 0,
    InvalidArgument = 1,
    OutOfMemory = 2,
    IoError = 3,
    NoSpace = 4,
    Busy = 5,
    Timeout = 6,
    NoEntry = 7,
    FileNotFound = 8,
    InvalidJson = 9,
    InvalidConfig = 10,
    NotRunning = 11,
    Unsupported = 12,
    InvalidPduKey = 13,
    NotOwner = 14,
}

public sealed class EndpointException : InvalidOperationException
{
    public EndpointException(HakoPduError error, string operation)
        : base($"{operation} failed: err={(int)error} ({error})")
    {
        Error = error;
        Operation = operation;
    }

    public HakoPduError Error { get; }
    public string Operation { get; }
}

public sealed class NativeLibraryNotAvailableException : InvalidOperationException
{
    public NativeLibraryNotAvailableException(string message, Exception innerException)
        : base(message, innerException)
    {
    }
}

public readonly record struct PduResolvedKey(string Robot, uint ChannelId);

public readonly record struct PduKey(string Robot, string Pdu);

public readonly record struct PduRecord(PduResolvedKey Key, ulong TimestampNs, byte[] Payload);

public readonly record struct PduEvent(PduResolvedKey Key, byte[] Payload);

using System;
using Hakoniwa.PduEndpoint;

namespace RecvByKeyExample;

internal static class Program
{
    private static void Main(string[] args)
    {
        var configPath = args.Length > 0
            ? args[0]
            : "config/sample/endpoint_internal_cache.json";

        using var endpoint = new Endpoint("csharp_recv_by_key", EndpointDirection.InOut);
        endpoint.Open(configPath);
        endpoint.Start();

        var key = new PduResolvedKey("robot1", 1);
        var payload = new byte[] { 0xAA, 0xBB, 0xCC };

        endpoint.Send(key, payload);

        // This example intentionally does not call SetRecvEvent(...).
        // It demonstrates the simpler "read by key" usage.
        var recv = endpoint.Recv(key, 16);
        Console.WriteLine($"recv robot={key.Robot} channel={key.ChannelId} bytes={BitConverter.ToString(recv)}");

        endpoint.Stop();
        endpoint.Close();
    }
}

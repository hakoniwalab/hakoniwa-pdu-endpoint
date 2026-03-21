using System;
using Hakoniwa.PduEndpoint;

namespace MinimalExample;

internal static class Program
{
    private static void Main(string[] args)
    {
        var configPath = args.Length > 0
            ? args[0]
            : "config/sample/endpoint_internal_cache.json";

        using var endpoint = new Endpoint("csharp_minimal", EndpointDirection.InOut);
        endpoint.Open(configPath);
        endpoint.Start();

        var key = new PduResolvedKey("robot1", 1);
        endpoint.SetRecvEvent(key);

        endpoint.Send(key, new byte[] { 0x01, 0x02, 0x03 });

        endpoint.ProcessRecvEvents();
        var pending = endpoint.GetPendingCount();
        for (var i = 0; i < pending; i++)
        {
            var record = endpoint.RecvNext(16);
            Console.WriteLine($"received robot={record.Key.Robot} channel={record.Key.ChannelId} size={record.Payload.Length}");
        }
        Console.WriteLine($"pending_processed={pending}");

        endpoint.Stop();
        endpoint.Close();
    }
}

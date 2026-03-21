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
        var endpointAsync = new EndpointAsync(endpoint);

        endpointAsync.Open(configPath);
        endpointAsync.Start();

        var key = new PduResolvedKey("robot1", 1);
        endpointAsync.OnRecv(key, ev =>
        {
            Console.WriteLine($"received robot={ev.Key.Robot} channel={ev.Key.ChannelId} size={ev.Payload.Length}");
        });

        endpointAsync.Send(key, new byte[] { 0x01, 0x02, 0x03 });

        endpointAsync.ProcessRecvEvents();
        var dispatched = endpointAsync.DrainPending();
        Console.WriteLine($"dispatched={dispatched}");

        endpointAsync.Stop();
        endpointAsync.Close();
    }
}

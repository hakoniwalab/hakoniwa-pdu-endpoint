using System;
using System.Threading;
using Hakoniwa.PduEndpoint;

namespace ManualPumpExample;

internal static class Program
{
    private static void Main(string[] args)
    {
        var configPath = args.Length > 0
            ? args[0]
            : "config/sample/endpoint_internal_cache.json";

        using var endpoint = new Endpoint("csharp_manual_pump", EndpointDirection.InOut);
        var endpointAsync = new EndpointAsync(endpoint);

        endpointAsync.Open(configPath);
        endpointAsync.Start();

        var key = new PduResolvedKey("robot1", 1);
        endpointAsync.OnRecv(key, ev =>
        {
            Console.WriteLine(
                $"main-thread handler robot={ev.Key.Robot} channel={ev.Key.ChannelId} bytes={BitConverter.ToString(ev.Payload)}");
        });

        Console.WriteLine("sending one payload and running a manual pump loop");
        endpointAsync.Send(key, new byte[] { 0x10, 0x20, 0x30, 0x40 });

        for (var frame = 0; frame < 5; frame++)
        {
            endpointAsync.ProcessRecvEvents();
            var dispatched = endpointAsync.DrainPending();
            Console.WriteLine($"frame={frame} dispatched={dispatched}");
            Thread.Sleep(16);
        }

        endpointAsync.Stop();
        endpointAsync.Close();
    }
}

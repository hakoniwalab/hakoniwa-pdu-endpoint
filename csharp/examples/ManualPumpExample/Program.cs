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
        endpoint.Open(configPath);
        endpoint.Start();

        var key = new PduResolvedKey("robot1", 1);
        endpoint.SetRecvEvent(key);

        Console.WriteLine("sending one payload and running a manual pump loop");
        endpoint.Send(key, new byte[] { 0x10, 0x20, 0x30, 0x40 });

        for (var frame = 0; frame < 5; frame++)
        {
            endpoint.ProcessRecvEvents();
            var pending = endpoint.GetPendingCount();
            for (var i = 0; i < pending; i++)
            {
                var record = endpoint.RecvNext(16);
                Console.WriteLine(
                    $"frame={frame} robot={record.Key.Robot} channel={record.Key.ChannelId} bytes={BitConverter.ToString(record.Payload)}");
            }
            Console.WriteLine($"frame={frame} pending_processed={pending}");
            Thread.Sleep(16);
        }

        endpoint.Stop();
        endpoint.Close();
    }
}

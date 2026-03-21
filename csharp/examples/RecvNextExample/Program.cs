using System;
using Hakoniwa.PduEndpoint;

namespace RecvNextExample;

internal static class Program
{
    private static void Main()
    {
        RunLatestExample();
        RunQueueExample();
    }

    private static void RunLatestExample()
    {
        Console.WriteLine("latest runtime receive semantics");

        using var endpoint = new Endpoint("csharp_recv_next_latest", EndpointDirection.InOut);
        endpoint.Open("test/test_endpoint_buffer.json");
        endpoint.Start();

        var keyA = new PduResolvedKey("csharp_latest_a", 31);
        var keyB = new PduResolvedKey("csharp_latest_b", 32);

        endpoint.Send(keyA, new byte[] { 0x01 });
        endpoint.Send(keyB, new byte[] { 0x02 });
        endpoint.Send(keyA, new byte[] { 0x03 });

        while (true)
        {
            try
            {
                var record = endpoint.RecvNext(16);
                Console.WriteLine($"latest recv_next: {record.Key.Robot} {record.Key.ChannelId} [{BitConverter.ToString(record.Payload)}]");
            }
            catch (EndpointException ex) when (ex.Error == HakoPduError.NoEntry)
            {
                break;
            }
        }

        endpoint.Stop();
        endpoint.Close();
    }

    private static void RunQueueExample()
    {
        Console.WriteLine("queue runtime receive semantics");

        using var endpoint = new Endpoint("csharp_recv_next_queue", EndpointDirection.InOut);
        endpoint.Open("test/test_endpoint_queue.json");
        endpoint.Start();

        var keyA = new PduResolvedKey("csharp_queue_a", 41);
        var keyB = new PduResolvedKey("csharp_queue_b", 42);

        endpoint.Send(keyA, new byte[] { 0x0A });
        endpoint.Send(keyB, new byte[] { 0x0B });
        endpoint.Send(keyA, new byte[] { 0x0C });

        while (true)
        {
            try
            {
                var record = endpoint.RecvNext(16);
                Console.WriteLine($"queue recv_next: {record.Key.Robot} {record.Key.ChannelId} [{BitConverter.ToString(record.Payload)}]");
            }
            catch (EndpointException ex) when (ex.Error == HakoPduError.NoEntry)
            {
                break;
            }
        }

        endpoint.Stop();
        endpoint.Close();
    }
}

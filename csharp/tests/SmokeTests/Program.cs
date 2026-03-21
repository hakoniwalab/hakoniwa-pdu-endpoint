using System;
using System.Threading;
using Hakoniwa.PduEndpoint;

namespace SmokeTests;

internal static class Program
{
    private sealed class SkipTestException : Exception
    {
        public SkipTestException(string message)
            : base(message)
        {
        }
    }

    private readonly record struct TestCase(string Name, Action Body);

    private static int Main()
    {
        var tests = new[]
        {
            new TestCase("InternalLatestSendRecvWorks", InternalLatestSendRecvWorks),
            new TestCase("RecvByKeyWithoutSetRecvEventWorks", RecvByKeyWithoutSetRecvEventWorks),
            new TestCase("InternalLatestRecvNextWorks", InternalLatestRecvNextWorks),
            new TestCase("InternalQueueRecvNextWorks", InternalQueueRecvNextWorks),
            new TestCase("SetRecvEventPendingCountWorks", SetRecvEventPendingCountWorks),
            new TestCase("TcpSendRecvWorks", TcpSendRecvWorks),
        };

        var passed = 0;
        var skipped = 0;
        var failed = 0;

        foreach (var test in tests)
        {
            Console.WriteLine($"[ RUN      ] {test.Name}");
            try
            {
                test.Body();
                Console.WriteLine($"[       OK ] {test.Name}");
                passed++;
            }
            catch (SkipTestException ex)
            {
                Console.WriteLine($"[  SKIPPED ] {test.Name}: {ex.Message}");
                skipped++;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[  FAILED  ] {test.Name}: {ex}");
                failed++;
            }
        }

        Console.WriteLine($"passed={passed} skipped={skipped} failed={failed}");
        return failed == 0 ? 0 : 1;
    }

    private static void InternalLatestSendRecvWorks()
    {
        using var endpoint = new Endpoint("csharp_test_latest", EndpointDirection.InOut);
        endpoint.Open("config/sample/endpoint_internal_cache.json");
        endpoint.Start();

        var key = new PduResolvedKey("csharp_test_latest_robot", 501);
        var payload = new byte[] { 0x01, 0x02, 0x03, 0x04 };

        endpoint.Send(key, payload);
        var recv = endpoint.Recv(key, 16);
        AssertBytes(recv, payload, "latest recv payload mismatch");

        endpoint.Stop();
        endpoint.Close();
    }

    private static void InternalLatestRecvNextWorks()
    {
        using var endpoint = new Endpoint("csharp_test_latest_next", EndpointDirection.InOut);
        endpoint.Open("test/test_endpoint_buffer.json");
        endpoint.Start();

        var keyA = new PduResolvedKey("csharp_test_latest_a", 511);
        var keyB = new PduResolvedKey("csharp_test_latest_b", 512);

        endpoint.Send(keyA, new byte[] { 0x01 });
        endpoint.Send(keyB, new byte[] { 0x02 });
        endpoint.Send(keyA, new byte[] { 0x03 });

        var record = endpoint.RecvNext(16);
        Assert(record.Key.Equals(keyA), "latest recv_next key A mismatch");
        AssertBytes(record.Payload, new byte[] { 0x03 }, "latest recv_next latest payload mismatch");

        record = endpoint.RecvNext(16);
        Assert(record.Key.Equals(keyB), "latest recv_next key B mismatch");
        AssertBytes(record.Payload, new byte[] { 0x02 }, "latest recv_next payload B mismatch");

        ExpectNoEntry(() => endpoint.RecvNext(16));

        endpoint.Stop();
        endpoint.Close();
    }

    private static void RecvByKeyWithoutSetRecvEventWorks()
    {
        using var endpoint = new Endpoint("csharp_test_recv_by_key", EndpointDirection.InOut);
        endpoint.Open("config/sample/endpoint_internal_cache.json");
        endpoint.Start();

        var key = new PduResolvedKey("csharp_test_recv_by_key_robot", 505);
        var payload = new byte[] { 0x51, 0x52, 0x53 };

        endpoint.Send(key, payload);
        var recv = endpoint.Recv(key, 16);
        AssertBytes(recv, payload, "recv-by-key payload mismatch");

        endpoint.Stop();
        endpoint.Close();
    }

    private static void InternalQueueRecvNextWorks()
    {
        using var endpoint = new Endpoint("csharp_test_queue_next", EndpointDirection.InOut);
        endpoint.Open("test/test_endpoint_queue.json");
        endpoint.Start();

        var keyA = new PduResolvedKey("csharp_test_queue_a", 521);
        var keyB = new PduResolvedKey("csharp_test_queue_b", 522);

        endpoint.Send(keyA, new byte[] { 0x0A });
        endpoint.Send(keyB, new byte[] { 0x0B });
        endpoint.Send(keyA, new byte[] { 0x0C });

        var record = endpoint.RecvNext(16);
        Assert(record.Key.Equals(keyA), "queue recv_next first key mismatch");
        AssertBytes(record.Payload, new byte[] { 0x0A }, "queue recv_next first payload mismatch");

        record = endpoint.RecvNext(16);
        Assert(record.Key.Equals(keyB), "queue recv_next second key mismatch");
        AssertBytes(record.Payload, new byte[] { 0x0B }, "queue recv_next second payload mismatch");

        record = endpoint.RecvNext(16);
        Assert(record.Key.Equals(keyA), "queue recv_next third key mismatch");
        AssertBytes(record.Payload, new byte[] { 0x0C }, "queue recv_next third payload mismatch");

        ExpectNoEntry(() => endpoint.RecvNext(16));

        endpoint.Stop();
        endpoint.Close();
    }

    private static void SetRecvEventPendingCountWorks()
    {
        using var endpoint = new Endpoint("csharp_test_pending_count", EndpointDirection.InOut);
        endpoint.Open("test/test_endpoint_buffer.json");
        endpoint.Start();

        var keyA = new PduResolvedKey("csharp_test_pending_a", 523);
        var keyB = new PduResolvedKey("csharp_test_pending_b", 524);

        endpoint.SetRecvEvent(keyA);
        endpoint.SetRecvEvent(keyB);

        endpoint.Send(keyA, new byte[] { 0x11 });
        endpoint.Send(keyB, new byte[] { 0x12 });
        endpoint.Send(keyA, new byte[] { 0x13 });

        var pending = endpoint.GetPendingCount();
        Assert(pending == 2, $"expected pending=2 but got {pending}");

        var record = endpoint.RecvNext(16);
        Assert(record.Key.Equals(keyA), "pending recv_next first key mismatch");
        AssertBytes(record.Payload, new byte[] { 0x13 }, "pending recv_next first payload mismatch");

        record = endpoint.RecvNext(16);
        Assert(record.Key.Equals(keyB), "pending recv_next second key mismatch");
        AssertBytes(record.Payload, new byte[] { 0x12 }, "pending recv_next second payload mismatch");

        endpoint.Stop();
        endpoint.Close();
    }

    private static void TcpSendRecvWorks()
    {
        using var server = new Endpoint("csharp_test_tcp_server", EndpointDirection.InOut);
        using var client = new Endpoint("csharp_test_tcp_client", EndpointDirection.InOut);

        try
        {
            server.Open("test/test_endpoint_tcp_server.json");
        }
        catch (EndpointException ex) when (ex.Error == HakoPduError.IoError)
        {
            throw new SkipTestException($"tcp server open unavailable in this environment: {ex.Message}");
        }

        client.Open("test/test_endpoint_tcp_client.json");
        server.Start();
        client.Start();

        Thread.Sleep(100);

        var key = new PduResolvedKey("csharp_test_tcp_robot", 541);
        var payload = new byte[] { (byte)'p', (byte)'i', (byte)'n', (byte)'g' };
        client.Send(key, payload);

        Thread.Sleep(100);

        var recv = server.Recv(key, 16);
        AssertBytes(recv, payload, "tcp recv payload mismatch");

        server.Stop();
        client.Stop();
        server.Close();
        client.Close();
    }

    private static void ExpectNoEntry(Action action)
    {
        try
        {
            action();
            throw new Exception("expected no-entry failure");
        }
        catch (EndpointException ex) when (ex.Error == HakoPduError.NoEntry)
        {
        }
    }

    private static void Assert(bool condition, string message)
    {
        if (!condition)
        {
            throw new Exception(message);
        }
    }

    private static void AssertBytes(byte[] actual, byte[] expected, string message)
    {
        if (actual.Length != expected.Length)
        {
            throw new Exception($"{message}: length {actual.Length} != {expected.Length}");
        }
        for (var i = 0; i < actual.Length; i++)
        {
            if (actual[i] != expected[i])
            {
                throw new Exception($"{message}: byte[{i}] {actual[i]} != {expected[i]}");
            }
        }
    }
}

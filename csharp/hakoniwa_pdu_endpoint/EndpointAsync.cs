using System;
using System.Collections.Concurrent;
using System.Collections.Generic;

namespace Hakoniwa.PduEndpoint;

public sealed class EndpointAsync
{
    private readonly Endpoint _endpoint;
    private readonly ConcurrentQueue<PduEvent> _queue = new();
    private readonly Dictionary<PduResolvedKey, List<Action<PduEvent>>> _handlers = new();
    private readonly object _gate = new();

    public EndpointAsync(Endpoint endpoint)
    {
        _endpoint = endpoint ?? throw new ArgumentNullException(nameof(endpoint));
    }

    public void OnRecv(PduResolvedKey key, Action<PduEvent> handler)
    {
        if (handler is null)
        {
            throw new ArgumentNullException(nameof(handler));
        }

        lock (_gate)
        {
            if (!_handlers.TryGetValue(key, out var handlers))
            {
                handlers = new List<Action<PduEvent>>();
                _handlers[key] = handlers;
                _endpoint.SubscribeOnRecvCallback(key, EnqueueEvent);
            }
            handlers.Add(handler);
        }
    }

    public void OnRecvByName(PduKey key, Action<PduEvent> handler)
    {
        var channelId = _endpoint.GetPduChannelId(key);
        if (channelId < 0)
        {
            throw new EndpointException(HakoPduError.InvalidPduKey, "get_pdu_channel_id");
        }

        OnRecv(new PduResolvedKey(key.Robot, checked((uint)channelId)), handler);
    }

    public int DrainPending()
    {
        var dispatched = 0;
        while (_queue.TryDequeue(out var ev))
        {
            List<Action<PduEvent>> handlers;
            lock (_gate)
            {
                if (!_handlers.TryGetValue(ev.Key, out handlers!))
                {
                    continue;
                }
                handlers = new List<Action<PduEvent>>(handlers);
            }

            foreach (var handler in handlers)
            {
                handler(ev);
            }
            dispatched++;
        }
        return dispatched;
    }

    public bool TryDequeue(out PduEvent ev)
    {
        return _queue.TryDequeue(out ev);
    }

    public void ProcessRecvEvents()
    {
        _endpoint.ProcessRecvEvents();
    }

    public void Open(string configPath)
    {
        _endpoint.Open(configPath);
    }

    public void CreatePduLchannels(string configPath)
    {
        _endpoint.CreatePduLchannels(configPath);
    }

    public void Start()
    {
        _endpoint.Start();
    }

    public void PostStart()
    {
        _endpoint.PostStart();
    }

    public void Stop()
    {
        _endpoint.Stop();
    }

    public void Close()
    {
        _endpoint.Close();
    }

    public bool IsRunning()
    {
        return _endpoint.IsRunning();
    }

    public void Send(PduResolvedKey key, byte[] payload)
    {
        _endpoint.Send(key, payload);
    }

    public void SendByName(PduKey key, byte[] payload)
    {
        _endpoint.SendByName(key, payload);
    }

    public byte[] Recv(PduResolvedKey key, int bufferSize)
    {
        return _endpoint.Recv(key, bufferSize);
    }

    public byte[] RecvByName(PduKey key, int bufferSize)
    {
        return _endpoint.RecvByName(key, bufferSize);
    }

    public PduRecord RecvNext(int bufferSize)
    {
        return _endpoint.RecvNext(bufferSize);
    }

    private void EnqueueEvent(PduResolvedKey key, byte[] payload)
    {
        _queue.Enqueue(new PduEvent(key, payload));
    }
}

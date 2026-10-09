function R = sb_ioport(cfg, R)
%SB_IOPORT  serial_bench parts for Psychtoolbox's IOPort.
%   Clock: GetSecs (QueryPerformanceCounter on Windows), in seconds, scaled to
%   us here. IOPort('Read') returns a receive stamp 'when' in the same clock:
%   for a direct read, GetSecs when the read completed; for the background
%   reader with ReadFilterFlags=4, GetSecs when the reader thread got the
%   first byte of the line. Writes use blocking = 0 in the round trips (the
%   low-latency choice); the call-cost part also times the default blocking
%   = 1, which waits until the driver reports the transmit queue empty.
    A = 'ioport';
    clk = @() GetSecs * 1e6;

    if sb_want(cfg, 'clock')
        n = cfg.n_clock;
        d = zeros(n, 1);
        for i = 1:n
            a = GetSecs;
            b = GetSecs;
            d(i) = (b - a) * 1e6;
        end
        R = sb_add(R, A, 'clock', 'clock_pair', d, 'two back-to-back GetSecs calls');
        R = sb_add(R, A, 'clock', 'clock_step', min(d(d > 0)), 'smallest nonzero difference');
    end

    if sb_want(cfg, 'noport')
        n = cfg.n_floor;
        a = GetSecs;
        for i = 1:n
            IOPort('Verbosity');
        end
        b = GetSecs;
        R = sb_add(R, A, 'noport', 'floor_batch_mean', (b - a) * 1e6 / n, ...
                   sprintf('mean of %d IOPort(''Verbosity'') calls: the cheapest call into the MEX', n));
        d = zeros(cfg.n_open_absent, 1);
        old = IOPort('Verbosity', 0);
        for i = 1:cfg.n_open_absent
            a = GetSecs;
            try
                [h, err] = IOPort('OpenSerialPort', cfg.absent_port); %#ok<ASGLU>
                if h >= 0, IOPort('Close', h); end
            catch
            end
            b = GetSecs;
            d(i) = (b - a) * 1e6;
        end
        IOPort('Verbosity', old);
        R = sb_add(R, A, 'noport', 'open_absent', d, ...
                   sprintf('open %s, which does not exist: the error path, with try/catch', cfg.absent_port));
        R = sb_add(R, A, 'noport', 'list_ports', NaN, 'IOPort has no port enumeration');
    end

    if isempty(cfg.port), return; end
    conf = sprintf('BaudRate=%d DataBits=8 Parity=None StopBits=1 FlowControl=None DTR=1 RTS=1 ReceiveTimeout=1.0', cfg.baud);

    if sb_want(cfg, 'calls')
        dop = zeros(cfg.n_open, 1); dcl = dop;
        for i = 1:cfg.n_open
            a = GetSecs;
            h = sb_ioport_open(cfg.port, conf);
            b = GetSecs;
            IOPort('Close', h);
            c = GetSecs;
            dop(i) = (b - a) * 1e6; dcl(i) = (c - b) * 1e6;
        end
        R = sb_add(R, A, 'calls', 'open', dop, ['OpenSerialPort ''' conf '''']);
        R = sb_add(R, A, 'calls', 'close', dcl, '');
    end

    h = sb_ioport_open(cfg.port, conf);
    guard = onCleanup(@() sb_ioport_close(h));
    IOPort('Purge', h);

    if sb_want(cfg, 'calls')
        for blocking = [0 1]
            for sz = cfg.write_sizes
                data = uint8(repmat('z', 1, sz));
                [n, pace] = sb_pace(cfg, sz);
                if blocking, n = min(n, cfg.n_blocking); end
                d = zeros(n, 1);
                for i = 1:n
                    a = GetSecs;
                    IOPort('Write', h, data, blocking);
                    b = GetSecs;
                    d(i) = (b - a) * 1e6;
                    IOPort('Read', h);
                    while GetSecs < b + pace / 1e6, end
                end
                if blocking
                    R = sb_add(R, A, 'calls', sprintf('write_%d_blocking', sz), d, ...
                               sprintf('Write(h, data, 1), the default; gap %.0f us', pace));
                else
                    R = sb_add(R, A, 'calls', sprintf('write_%d', sz), d, ...
                               sprintf('Write(h, data, 0); gap %.0f us', pace));
                end
            end
        end
        IOPort('Write', h, uint8(10), 0);
        pause(0.05);
        IOPort('Purge', h);
        n = cfg.n_calls;
        d = zeros(n, 1); e = d;
        for i = 1:n
            a = GetSecs;
            IOPort('Read', h);
            b = GetSecs;
            IOPort('BytesAvailable', h);
            c = GetSecs;
            d(i) = (b - a) * 1e6; e(i) = (c - b) * 1e6;
        end
        R = sb_add(R, A, 'calls', 'read_empty', d, 'Read(h), non-blocking, with nothing waiting');
        R = sb_add(R, A, 'calls', 'available', e, 'BytesAvailable(h)');
        if ~strcmp(cfg.device, 'none')
            m = cfg.n_read_waiting;
            d = nan(m, 1);
            for i = 1:m
                [cmd, L] = sb_cmd(i, cfg.device);
                IOPort('Write', h, cmd, 0);
                t = GetSecs;
                while IOPort('BytesAvailable', h) < L && GetSecs - t < 0.2, end
                a = GetSecs;
                r = IOPort('Read', h, 0, L);
                b = GetSecs;
                if numel(r) == L, d(i) = (b - a) * 1e6; end
            end
            R = sb_add(R, A, 'calls', 'read_waiting', d, sprintf('Read(h, 0, %d) with the bytes waiting', L));
        end
    end

    if sb_want(cfg, 'rt') && ~strcmp(cfg.device, 'none')
        for strat = {'block', 'poll'}
            X = sb_ioport_rt(h, cfg, cfg.n_rt, 0, strat{1}, clk);
            R = sb_rt_rows(R, A, strat{1}, X, cfg);
        end
    end

    if sb_want(cfg, 'stream') && strcmp(cfg.device, 'echo')
        L = 26;
        nlines = round(cfg.stream_hz * cfg.stream_s);
        for mode = {'line', 'poll', 'async'}
            P1 = sb_ioport_rt(h, cfg, cfg.n_probe, 1e6, 'block', clk);
            reads = cell(nlines + 100, 1); host = nan(nlines + 100, 1); stamp = host; nr = 0; got = 0;
            cmd = uint8(sprintf('s %d %d\n', cfg.stream_hz, nlines));
            if strcmp(mode{1}, 'async')
                % One line per quantum, stamped at its first byte by the
                % reader thread; the buffer must hold a whole number of them.
                IOPort('ConfigureSerialPort', h, sprintf(['InputBufferSize=%d Terminator=10 ' ...
                    'BlockingBackgroundRead=1 ReadFilterFlags=4 StartBackgroundRead=%d'], L * 4096, L));
                c0 = cputime; w0 = GetSecs;
                IOPort('Write', h, cmd, 0);
                while IOPort('BytesAvailable', h) < nlines * L && GetSecs - w0 < cfg.stream_s + 2
                    pause(0.1);
                end
                w1 = GetSecs; c1 = cputime;
                while true
                    [r, when] = IOPort('Read', h, 0, L);
                    if isempty(r), break; end
                    % A reader-thread read that timed out with no byte stores a
                    % quantum of zeros; it is not data.
                    if all(r == 0), continue; end
                    nr = nr + 1;
                    reads{nr} = uint8(r(:)');
                    stamp(nr) = when * 1e6;
                end
                IOPort('ConfigureSerialPort', h, 'StopBackgroundRead');
                note = 'background reader thread, BlockingBackgroundRead=1, ReadFilterFlags=4; main thread in pause(0.1)';
            else
                c0 = cputime; w0 = GetSecs;
                IOPort('Write', h, cmd, 0);
                while got < nlines * L && GetSecs - w0 < cfg.stream_s + 2
                    if strcmp(mode{1}, 'line')
                        [r, when] = IOPort('Read', h, 1, L);
                    else
                        [r, when] = IOPort('Read', h);
                    end
                    if ~isempty(r)
                        t = GetSecs;
                        nr = nr + 1;
                        reads{nr} = uint8(r(:)');
                        host(nr) = t * 1e6;
                        stamp(nr) = when * 1e6;
                        got = got + numel(r);
                    end
                end
                w1 = GetSecs; c1 = cputime;
                if strcmp(mode{1}, 'line')
                    note = 'Read(h, 1, 26) per line (blocking)';
                else
                    note = 'tight loop of Read(h) (non-blocking)';
                end
            end
            IOPort('Write', h, uint8(sprintf('x\n')), 0);
            pause(0.05);
            IOPort('Purge', h);
            P2 = sb_ioport_rt(h, cfg, cfg.n_probe, 2e6, 'block', clk);
            S.reads = reads(1:nr); S.host = host(1:nr); S.stamp = stamp(1:nr);
            S.probe = sb_cat_probes(P1, P2);
            S.nlines = nlines; S.cpu_s = c1 - c0; S.wall_s = w1 - w0;
            S.note = note;
            R = sb_stream_rows(R, A, mode{1}, S, cfg);
        end
    end
end

function h = sb_ioport_open(port, conf)
    [h, err] = IOPort('OpenSerialPort', port, conf);
    if h < 0
        error('serial_bench:ioport', 'IOPort could not open %s: %s', port, err);
    end
end

function X = sb_ioport_rt(h, cfg, n, seq0, strat, clk)
    IOPort('Write', h, uint8(10), 0);
    pause(0.05);
    IOPort('Purge', h);
    X.seq = seq0 + (1:n)';
    X.t0 = nan(n, 1); X.t1 = X.t0; X.t2 = X.t0; X.when = X.t0; X.rx = X.t0; X.tx = X.t0;
    X.ok = false(n, 1);
    for i = 1:n
        [cmd, L] = sb_cmd(X.seq(i), cfg.device);
        tw = clk() + rand * 1000;
        while clk() < tw, end
        t0 = GetSecs;
        IOPort('Write', h, cmd, 0);
        t1 = GetSecs;
        if strcmp(strat, 'block')
            [r, when] = IOPort('Read', h, 1, L);
            t2 = GetSecs;
        else
            r = [];
            while numel(r) < L
                [q, when] = IOPort('Read', h, 0, L - numel(r));
                r = [r, q(:)']; %#ok<AGROW>
                t2 = GetSecs;
                if t2 - t0 > 1, break; end
            end
        end
        [X.ok(i), X.rx(i), X.tx(i)] = sb_reply(uint8(r), X.seq(i), cfg.device);
        X.t0(i) = t0 * 1e6; X.t1(i) = t1 * 1e6; X.t2(i) = t2 * 1e6; X.when(i) = when * 1e6;
        if ~X.ok(i)
            pause(0.02);
            IOPort('Purge', h);
        end
    end
end

function sb_ioport_close(h)
    try
        IOPort('Close', h);
    catch
    end
end

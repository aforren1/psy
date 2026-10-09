function R = sb_serialport(cfg, R)
%SB_SERIALPORT  serial_bench parts for MATLAB's serialport (base MATLAB since
%   R2019b; no toolbox needed).
%   Clock: tic/toc, one tic at the start, toc in us. serialport gives no
%   receive stamp from a read; the stamp is toc after the read returns. Its
%   callbacks (configureCallback) carry an event time, evt.AbsTime, a
%   datetime; the stream part converts it to the toc clock with an offset
%   measured from datetime('now') and toc pairs, and reports it as the API
%   stamp of the 'async' mode. serialport has no non-blocking read: a poll
%   is a NumBytesAvailable query, then a read of that many bytes.
    A = 'serialport';
    T0 = tic;

    % Shared with the callback (nested function) of the async stream mode.
    cb_reads = {}; cb_host = []; cb_stamp = []; cb_n = 0; cb_got = 0;

    if sb_want(cfg, 'clock')
        n = cfg.n_clock;
        d = zeros(n, 1);
        for i = 1:n
            a = toc(T0);
            b = toc(T0);
            d(i) = (b - a) * 1e6;
        end
        R = sb_add(R, A, 'clock', 'clock_pair', d, 'two back-to-back toc(t0) calls');
        R = sb_add(R, A, 'clock', 'clock_step', min(d(d > 0)), 'smallest nonzero difference');
    end

    if sb_want(cfg, 'noport')
        R = sb_add(R, A, 'noport', 'floor_batch_mean', NaN, 'no call into serialport is possible without a port');
        d = zeros(cfg.n_enum, 1);
        for i = 1:cfg.n_enum
            a = toc(T0);
            p = serialportlist("all");
            b = toc(T0);
            d(i) = (b - a) * 1e6;
        end
        R = sb_add(R, A, 'noport', 'list_ports', d, sprintf('serialportlist("all"), %d ports found', numel(p)));
        d = zeros(cfg.n_open_absent, 1);
        for i = 1:cfg.n_open_absent
            a = toc(T0);
            try
                s = serialport(cfg.absent_port, cfg.baud);
                delete(s);
            catch
            end
            b = toc(T0);
            d(i) = (b - a) * 1e6;
        end
        R = sb_add(R, A, 'noport', 'open_absent', d, ...
                   sprintf('serialport("%s", baud), which does not exist: the error path, with try/catch', cfg.absent_port));
    end

    if isempty(cfg.port), return; end

    if sb_want(cfg, 'calls')
        dop = zeros(cfg.n_open, 1); dcl = dop;
        for i = 1:cfg.n_open
            a = toc(T0);
            s = sb_sp_open(cfg);
            b = toc(T0);
            delete(s);
            c = toc(T0);
            dop(i) = (b - a) * 1e6; dcl(i) = (c - b) * 1e6;
        end
        R = sb_add(R, A, 'calls', 'open', dop, 'serialport(port, baud) plus setDTR, setRTS, configureTerminator, Timeout');
        R = sb_add(R, A, 'calls', 'close', dcl, 'delete(s)');
    end

    s = sb_sp_open(cfg);
    guard = onCleanup(@() delete(s));
    flush(s, "input");

    if sb_want(cfg, 'calls')
        for sz = cfg.write_sizes
            data = uint8(repmat('z', 1, sz));
            [n, pace] = sb_pace(cfg, sz);
            d = zeros(n, 1);
            for i = 1:n
                a = toc(T0);
                write(s, data, "uint8");
                b = toc(T0);
                d(i) = (b - a) * 1e6;
                if s.NumBytesAvailable > 0, flush(s, "input"); end
                while toc(T0) < b + pace / 1e6, end
            end
            R = sb_add(R, A, 'calls', sprintf('write_%d', sz), d, sprintf('write(s, data, "uint8"); gap %.0f us', pace));
        end
        write(s, uint8(10), "uint8");
        pause(0.05);
        flush(s, "input");
        n = cfg.n_calls;
        e = zeros(n, 1);
        for i = 1:n
            a = toc(T0);
            s.NumBytesAvailable;
            b = toc(T0);
            e(i) = (b - a) * 1e6;
        end
        R = sb_add(R, A, 'calls', 'read_empty', NaN, 'no non-blocking read: a poll is the NumBytesAvailable query');
        R = sb_add(R, A, 'calls', 'available', e, 's.NumBytesAvailable');
        if ~strcmp(cfg.device, 'none')
            m = cfg.n_read_waiting;
            d = nan(m, 1);
            for i = 1:m
                [cmd, L] = sb_cmd(i, cfg.device);
                write(s, cmd, "uint8");
                t = toc(T0);
                while s.NumBytesAvailable < L && toc(T0) - t < 0.2, end
                a = toc(T0);
                r = read(s, L, "uint8");
                b = toc(T0);
                if numel(r) == L, d(i) = (b - a) * 1e6; end
            end
            R = sb_add(R, A, 'calls', 'read_waiting', d, sprintf('read(s, %d, "uint8") with the bytes waiting', L));
        end
    end

    if sb_want(cfg, 'rt') && ~strcmp(cfg.device, 'none')
        for strat = {'block', 'poll'}
            X = sb_sp_rt(s, T0, cfg, cfg.n_rt, 0, strat{1});
            R = sb_rt_rows(R, A, strat{1}, X, cfg);
        end
    end

    if sb_want(cfg, 'stream') && strcmp(cfg.device, 'echo')
        L = 26;
        nlines = round(cfg.stream_hz * cfg.stream_s);
        for mode = {'line', 'poll', 'async'}
            P1 = sb_sp_rt(s, T0, cfg, cfg.n_probe, 1e6, 'block');
            reads = cell(nlines + 100, 1); host = nan(nlines + 100, 1); stamp = host; nr = 0; got = 0;
            cmd = uint8(sprintf('s %d %d\n', cfg.stream_hz, nlines));
            if strcmp(mode{1}, 'async')
                off1 = sb_abs_offset(T0);
                cb_reads = cell(nlines + 100, 1); cb_host = nan(nlines + 100, 1); cb_stamp = cb_host;
                cb_n = 0; cb_got = 0;
                configureCallback(s, "terminator", @on_line);
                c0 = cputime; w0 = toc(T0);
                write(s, cmd, "uint8");
                while cb_got < nlines * L && toc(T0) - w0 < cfg.stream_s + 2
                    pause(0.1);
                end
                w1 = toc(T0); c1 = cputime;
                configureCallback(s, "off");
                off2 = sb_abs_offset(T0);
                reads = cb_reads(1:cb_n); host = cb_host(1:cb_n);
                stamp = cb_stamp(1:cb_n) - (off1.us + off2.us) / 2;
                nr = cb_n;
                note = sprintf(['configureCallback "terminator", the callback reads all bytes waiting; ' ...
                    'stamp = evt.AbsTime (datetime to toc offset spread %.0f us)'], max(off1.spread, off2.spread));
            else
                c0 = cputime; w0 = toc(T0);
                write(s, cmd, "uint8");
                while got < nlines * L && toc(T0) - w0 < cfg.stream_s + 2
                    if strcmp(mode{1}, 'line')
                        r = read(s, L, "uint8");
                    else
                        nb = s.NumBytesAvailable;
                        if nb > 0, r = read(s, nb, "uint8"); else, r = []; end
                    end
                    if ~isempty(r)
                        t = toc(T0);
                        nr = nr + 1;
                        reads{nr} = uint8(r(:)');
                        host(nr) = t * 1e6;
                        got = got + numel(r);
                    end
                end
                w1 = toc(T0); c1 = cputime;
                if strcmp(mode{1}, 'line')
                    note = 'read(s, 26, "uint8") per line (blocking)';
                else
                    note = 'tight loop of NumBytesAvailable and read';
                end
            end
            write(s, uint8(sprintf('x\n')), "uint8");
            pause(0.05);
            flush(s, "input");
            P2 = sb_sp_rt(s, T0, cfg, cfg.n_probe, 2e6, 'block');
            S.reads = reads(1:nr); S.host = host(1:nr); S.stamp = stamp(1:nr);
            S.probe = sb_cat_probes(P1, P2);
            S.nlines = nlines; S.cpu_s = c1 - c0; S.wall_s = w1 - w0;
            S.note = note;
            R = sb_stream_rows(R, A, mode{1}, S, cfg);
        end
    end

    function on_line(src, evt)
        % Nested so it can fill the arrays above; every name it assigns
        % starts with cb_, because a nested function shares any variable
        % name it has in common with the parent.
        cb_t = toc(T0) * 1e6;
        cb_nb = src.NumBytesAvailable;
        if cb_nb <= 0, return; end
        cb_r = read(src, cb_nb, "uint8");
        cb_n = cb_n + 1;
        cb_reads{cb_n} = uint8(cb_r(:)');
        cb_host(cb_n) = cb_t;
        try
            cb_stamp(cb_n) = posixtime(evt.AbsTime) * 1e6;
        catch
            cb_stamp(cb_n) = NaN;
        end
        cb_got = cb_got + numel(cb_r);
    end
end

function X = sb_sp_rt(s, T0, cfg, n, seq0, strat)
    write(s, uint8(10), "uint8");
    pause(0.05);
    flush(s, "input");
    X.seq = seq0 + (1:n)';
    X.t0 = nan(n, 1); X.t1 = X.t0; X.t2 = X.t0; X.when = X.t0; X.rx = X.t0; X.tx = X.t0;
    X.ok = false(n, 1);
    for i = 1:n
        [cmd, L] = sb_cmd(X.seq(i), cfg.device);
        tw = toc(T0) + rand * 1e-3;            % leave the USB frame phase
        while toc(T0) < tw, end
        t0 = toc(T0);
        write(s, cmd, "uint8");
        t1 = toc(T0);
        if strcmp(strat, 'block')
            r = read(s, L, "uint8");
            t2 = toc(T0);
        else
            r = [];
            while numel(r) < L
                nb = s.NumBytesAvailable;
                if nb > 0
                    q = read(s, min(nb, L - numel(r)), "uint8");
                    r = [r, q(:)']; %#ok<AGROW>
                end
                t2 = toc(T0);
                if t2 - t0 > 1, break; end
            end
        end
        [X.ok(i), X.rx(i), X.tx(i)] = sb_reply(uint8(r), X.seq(i), cfg.device);
        X.t0(i) = t0 * 1e6; X.t1(i) = t1 * 1e6; X.t2(i) = t2 * 1e6;
        if ~X.ok(i)
            pause(0.02);
            flush(s, "input");
        end
    end
end

function o = sb_abs_offset(T0)
    % datetime('now') in us minus toc in us, over 200 pairs: the median
    % converts evt.AbsTime to the toc clock; the spread is p99 - min.
    k = 200;
    v = zeros(k, 1);
    for j = 1:k
        a = toc(T0);
        w = posixtime(datetime('now')) * 1e6;
        b = toc(T0);
        v(j) = w - (a + b) / 2 * 1e6;
    end
    st = sb_stats(v);
    o.us = st.median;
    o.spread = st.p99 - st.min;
end

function s = sb_sp_open(cfg)
    s = serialport(cfg.port, cfg.baud, 'DataBits', 8, 'Parity', 'none', 'StopBits', 1, 'FlowControl', 'none');
    setDTR(s, true);
    setRTS(s, true);
    configureTerminator(s, "LF");
    s.Timeout = 1;
end
